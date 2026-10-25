/********************************************************************************
* Copyright (c) 2026 Contributors to the Eclipse Foundation
*
* See the NOTICE file(s) distributed with this work for additional
* information regarding copyright ownership.
*
* This program and the accompanying materials are made available under the
* terms of the Apache License 2.0 which is available at
* http://www.apache.org/licenses/LICENSE-2.0
*
* SPDX-License-Identifier: Apache-2.0
********************************************************************************/

#include "fixed_lkv_codec.h"

#include <cmath>
#include <cstring>

// Big-endian helpers matching autoas-as's SomeIpXf.c encode/decode
// primitives exactly (src/infras/communication/SomeIpXf/SomeIpXf.c
// SomeIpXf_{Encode,Decode}{Byte,Short,Long,LongLong}); see fixed_lkv_codec.h
// for the resulting struct layouts this file must produce/consume.

namespace sdv {
namespace someip {
namespace vehicle_state {

namespace {

void PutU8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

void PutU16BE(std::vector<uint8_t> &buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void PutU32BE(std::vector<uint8_t> &buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void PutU64BE(std::vector<uint8_t> &buf, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        buf.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
    }
}

// float -> its raw IEEE 754 bit pattern, written big-endian (matches
// autoas-as encoding the C `float value` field via SomeIpXf_EncodeLong,
// which type-puns the field's bytes to uint32_t -- see fixed_lkv_codec.h).
void PutFloatBitsBE(std::vector<uint8_t> &buf, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    PutU32BE(buf, bits);
}

/**
 * @brief Bounds-checked big-endian cursor reader.
 */
class Reader {
public:
    Reader(const uint8_t *data, size_t len) : data_(data), len_(len), pos_(0) {}

    bool GetU8(uint8_t &v) {
        if (pos_ + 1 > len_) return false;
        v = data_[pos_];
        pos_ += 1;
        return true;
    }

    bool GetU16(uint16_t &v) {
        if (pos_ + 2 > len_) return false;
        v = (static_cast<uint16_t>(data_[pos_]) << 8) | data_[pos_ + 1];
        pos_ += 2;
        return true;
    }

    bool GetU32(uint32_t &v) {
        if (pos_ + 4 > len_) return false;
        v = (static_cast<uint32_t>(data_[pos_]) << 24) | (static_cast<uint32_t>(data_[pos_ + 1]) << 16) |
            (static_cast<uint32_t>(data_[pos_ + 2]) << 8) | data_[pos_ + 3];
        pos_ += 4;
        return true;
    }

    bool GetU64(uint64_t &v) {
        if (pos_ + 8 > len_) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | data_[pos_ + i];
        }
        pos_ += 8;
        return true;
    }

    bool GetFloatBits(float &v) {
        uint32_t bits = 0;
        if (!GetU32(bits)) return false;
        std::memcpy(&v, &bits, 4);
        return true;
    }

    size_t pos() const { return pos_; }
    size_t remaining() const { return len_ - pos_; }

private:
    const uint8_t *data_;
    size_t len_;
    size_t pos_;
};

// Converts a decoded float32 (the wire's only value representation, see
// fixed_lkv_codec.h) back into the key's proper LkvValue type. Unknown keys
// (FindKey() == nullptr) default to Float32 -- the caller (someip_feeder's
// handle_event) already ignores entries with an unrecognized key_id.
LkvValue MakeValueForKey(uint16_t key_id, float raw) {
    const KeyDescriptor *desc = FindKey(key_id);
    ValueType type = (desc != nullptr) ? desc->type : ValueType::kFloat32;
    switch (type) {
        case ValueType::kBool:
            return LkvValue::Bool(raw != 0.0f);
        case ValueType::kUint8:
            return LkvValue::Uint8(static_cast<uint8_t>(std::lround(raw)));
        case ValueType::kInt8:
            return LkvValue::Int8(static_cast<int8_t>(std::lround(raw)));
        case ValueType::kFloat32:
        default:
            return LkvValue::Float32(raw);
        case ValueType::kString:
            // No writable/published key uses kString; treat defensively as
            // float32 rather than crash.
            return LkvValue::Float32(raw);
    }
}

// Converts an LkvValue to the single float32 wire representation. Returns
// false for ValueType::kString (unsupported on this wire format).
bool ValueToFloatBits(const LkvValue &value, float &out) {
    switch (value.type) {
        case ValueType::kBool: {
            bool v = false;
            value.AsBool(v);
            out = v ? 1.0f : 0.0f;
            return true;
        }
        case ValueType::kUint8: {
            uint8_t v = 0;
            value.AsUint8(v);
            out = static_cast<float>(v);
            return true;
        }
        case ValueType::kInt8: {
            int8_t v = 0;
            value.AsInt8(v);
            out = static_cast<float>(v);
            return true;
        }
        case ValueType::kFloat32:
            return value.AsFloat32(out);
        case ValueType::kString:
        default:
            return false;
    }
}

void EncodeFixedEntry(const LkvEntry &e, std::vector<uint8_t> &out) {
    PutU16BE(out, e.key_id);
    PutU8(out, static_cast<uint8_t>(e.quality));
    PutU8(out, static_cast<uint8_t>(e.confidence));
    PutU64BE(out, e.timestamp_ms);
    PutU32BE(out, e.age_ms);
    float bits = 0.0f;
    (void)ValueToFloatBits(e.value, bits);  // caller (EncodeFixedWriteRequest) pre-validates
    PutFloatBitsBE(out, bits);
}

bool DecodeFixedEntry(Reader &r, LkvEntry &e) {
    uint16_t key_id = 0;
    uint8_t quality_raw = 0, confidence_raw = 0;
    uint64_t timestamp_ms = 0;
    uint32_t age_ms = 0;
    float raw_value = 0.0f;

    if (!r.GetU16(key_id)) return false;
    if (!r.GetU8(quality_raw)) return false;
    if (!r.GetU8(confidence_raw)) return false;
    if (quality_raw > static_cast<uint8_t>(Quality::kStale)) return false;
    if (confidence_raw > static_cast<uint8_t>(Confidence::kHigh)) return false;
    if (!r.GetU64(timestamp_ms)) return false;
    if (!r.GetU32(age_ms)) return false;
    if (!r.GetFloatBits(raw_value)) return false;

    e.key_id = key_id;
    e.quality = static_cast<Quality>(quality_raw);
    e.confidence = static_cast<Confidence>(confidence_raw);
    e.timestamp_ms = timestamp_ms;
    e.age_ms = age_ms;
    e.value = MakeValueForKey(key_id, raw_value);
    e.source = "Gateway ECU";  // fixed on this wire format, see fixed_lkv_codec.h
    return true;
}

// Decodes a bounded LkvEntry array: a `lengthFieldBytes`-byte big-endian
// byte-length field followed by that many bytes of back-to-back 20-byte
// entries (AUTOSAR SomeIpXf STRUCT_ARRAY encoding, see fixed_lkv_codec.h).
bool DecodeFixedEntryArray(Reader &r, size_t lengthFieldBytes, size_t maxEntries,
                            std::vector<LkvEntry> &out) {
    uint32_t byteLength = 0;
    if (1 == lengthFieldBytes) {
        uint8_t v = 0;
        if (!r.GetU8(v)) return false;
        byteLength = v;
    } else if (2 == lengthFieldBytes) {
        uint16_t v = 0;
        if (!r.GetU16(v)) return false;
        byteLength = v;
    } else {
        return false;
    }
    if ((byteLength % kFixedEntrySize) != 0) return false;
    const size_t count = byteLength / kFixedEntrySize;
    if (count > maxEntries) return false;
    if (r.remaining() < byteLength) return false;

    out.clear();
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        LkvEntry e;
        if (!DecodeFixedEntry(r, e)) return false;
        out.push_back(std::move(e));
    }
    return true;
}

void PutEntryArrayLength(std::vector<uint8_t> &buf, size_t lengthFieldBytes, uint32_t byteLength) {
    if (1 == lengthFieldBytes) {
        PutU8(buf, static_cast<uint8_t>(byteLength));
    } else {
        PutU16BE(buf, static_cast<uint16_t>(byteLength));
    }
}

}  // namespace

bool DecodeFixedVehicleStateMessage(const uint8_t *data, size_t len, LkvMessage &out) {
    Reader r(data, len);
    uint16_t struct_length = 0;
    if (!r.GetU16(struct_length)) return false;
    if (struct_length > len - 2) return false;

    uint16_t schema_version = 0;
    if (!r.GetU16(schema_version)) return false;

    std::vector<LkvEntry> entries;
    if (!DecodeFixedEntryArray(r, 2, kFixedMaxEntries, entries)) return false;

    out.schema_version = schema_version;
    out.entries = std::move(entries);
    return true;
}

bool EncodeFixedWriteRequest(const LkvMessage &msg, std::vector<uint8_t> &out) {
    if (msg.entries.size() > kFixedMaxWriteEntries) return false;
    for (const auto &e : msg.entries) {
        float dummy;
        if (!ValueToFloatBits(e.value, dummy)) return false;  // reject kString up front
    }

    std::vector<uint8_t> body;
    const uint32_t byteLength = static_cast<uint32_t>(msg.entries.size() * kFixedEntrySize);
    PutEntryArrayLength(body, 1, byteLength);
    for (const auto &e : msg.entries) {
        EncodeFixedEntry(e, body);
    }

    out.clear();
    PutU8(out, static_cast<uint8_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

bool DecodeFixedWriteResponse(const uint8_t *data, size_t len, WriteResult &result,
                               LkvMessage &applied_state) {
    Reader r(data, len);
    uint8_t struct_length = 0;
    if (!r.GetU8(struct_length)) return false;
    if (struct_length > len - 1) return false;

    uint8_t result_code_raw = 0, failed_entry_index = 0;
    if (!r.GetU8(result_code_raw)) return false;
    if (!r.GetU8(failed_entry_index)) return false;
    if (result_code_raw > static_cast<uint8_t>(WriteResultCode::kUnsupported)) return false;

    std::vector<LkvEntry> entries;
    if (!DecodeFixedEntryArray(r, 1, kFixedMaxWriteEntries, entries)) return false;

    result.result_code = static_cast<WriteResultCode>(result_code_raw);
    result.failed_entry_index = failed_entry_index;
    applied_state.schema_version = kSchemaVersion;
    applied_state.entries = std::move(entries);
    return true;
}

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv
