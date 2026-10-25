/********************************************************************************
* Copyright (c) 2022 Contributors to the Eclipse Foundation
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

#include "lkv_codec.h"

#include <chrono>
#include <cstring>
#include <sstream>

// Multi-byte fields are encoded little-endian by direct memcpy -- the host
// (x86_64 Linux container) is little-endian, so no byte-swapping is
// performed.

namespace sdv {
namespace someip {
namespace vehicle_state {

namespace {

void PutU8(std::vector<uint8_t> &buf, uint8_t v) { buf.push_back(v); }

void PutU16(std::vector<uint8_t> &buf, uint16_t v) {
    uint8_t tmp[2];
    std::memcpy(tmp, &v, 2);
    buf.insert(buf.end(), tmp, tmp + 2);
}

void PutU32(std::vector<uint8_t> &buf, uint32_t v) {
    uint8_t tmp[4];
    std::memcpy(tmp, &v, 4);
    buf.insert(buf.end(), tmp, tmp + 4);
}

void PutU64(std::vector<uint8_t> &buf, uint64_t v) {
    uint8_t tmp[8];
    std::memcpy(tmp, &v, 8);
    buf.insert(buf.end(), tmp, tmp + 8);
}

void PutF32(std::vector<uint8_t> &buf, float v) {
    uint8_t tmp[4];
    std::memcpy(tmp, &v, 4);
    buf.insert(buf.end(), tmp, tmp + 4);
}

void PutBytes(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

/**
 * @brief Bounds-checked little-endian cursor reader. Every Get* call fails
 * (returns false) instead of reading past `len_`, so a single `if (!ok)
 * return false;` chain in DecodeMessage rejects any truncated/malformed
 * field.
 */
class Reader {
public:
    Reader(const uint8_t *data, size_t len) : data_(data), len_(len), pos_(0) {}

    bool GetU8(uint8_t &v) { return Get(&v, 1); }
    bool GetU16(uint16_t &v) { return Get(&v, 2); }
    bool GetU32(uint32_t &v) { return Get(&v, 4); }
    bool GetU64(uint64_t &v) { return Get(&v, 8); }
    bool GetF32(float &v) { return Get(&v, 4); }

    bool GetBytes(std::vector<uint8_t> &out, size_t n) {
        if (pos_ + n > len_) return false;
        out.assign(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return true;
    }

    bool GetString(std::string &out, size_t n) {
        if (pos_ + n > len_) return false;
        out.assign(reinterpret_cast<const char *>(data_ + pos_), n);
        pos_ += n;
        return true;
    }

    size_t pos() const { return pos_; }
    size_t remaining() const { return len_ - pos_; }
    bool AtEnd() const { return pos_ == len_; }

private:
    bool Get(void *out, size_t n) {
        if (pos_ + n > len_) return false;
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    const uint8_t *data_;
    size_t len_;
    size_t pos_;
};

size_t ValueWireLength(ValueType type, size_t string_len) {
    switch (type) {
        case ValueType::kBool:
        case ValueType::kUint8:
        case ValueType::kInt8:
            return 1;
        case ValueType::kFloat32:
            return 4;
        case ValueType::kString:
            return string_len;
    }
    return 0;
}

bool IsKnownValueType(uint8_t raw) {
    switch (static_cast<ValueType>(raw)) {
        case ValueType::kBool:
        case ValueType::kUint8:
        case ValueType::kInt8:
        case ValueType::kFloat32:
        case ValueType::kString:
            return true;
    }
    return false;
}

}  // namespace

// ---- LkvValue ----------------------------------------------------------

LkvValue LkvValue::Bool(bool v) {
    LkvValue r; r.type = ValueType::kBool; r.data = v; return r;
}
LkvValue LkvValue::Uint8(uint8_t v) {
    LkvValue r; r.type = ValueType::kUint8; r.data = v; return r;
}
LkvValue LkvValue::Int8(int8_t v) {
    LkvValue r; r.type = ValueType::kInt8; r.data = v; return r;
}
LkvValue LkvValue::Float32(float v) {
    LkvValue r; r.type = ValueType::kFloat32; r.data = v; return r;
}
LkvValue LkvValue::String(std::string v) {
    LkvValue r; r.type = ValueType::kString; r.data = std::move(v); return r;
}

bool LkvValue::AsBool(bool &out) const {
    if (type != ValueType::kBool) return false;
    out = std::get<bool>(data);
    return true;
}
bool LkvValue::AsUint8(uint8_t &out) const {
    if (type != ValueType::kUint8) return false;
    out = std::get<uint8_t>(data);
    return true;
}
bool LkvValue::AsInt8(int8_t &out) const {
    if (type != ValueType::kInt8) return false;
    out = std::get<int8_t>(data);
    return true;
}
bool LkvValue::AsFloat32(float &out) const {
    if (type != ValueType::kFloat32) return false;
    out = std::get<float>(data);
    return true;
}
bool LkvValue::AsString(std::string &out) const {
    if (type != ValueType::kString) return false;
    out = std::get<std::string>(data);
    return true;
}

size_t LkvValue::WireLength() const {
    if (type == ValueType::kString) {
        return std::get<std::string>(data).size();
    }
    return ValueWireLength(type, 0);
}

std::string LkvValue::ToString() const {
    std::ostringstream os;
    switch (type) {
        case ValueType::kBool:    os << (std::get<bool>(data) ? "true" : "false"); break;
        case ValueType::kUint8:   os << static_cast<int>(std::get<uint8_t>(data)); break;
        case ValueType::kInt8:    os << static_cast<int>(std::get<int8_t>(data)); break;
        case ValueType::kFloat32: os << std::get<float>(data); break;
        case ValueType::kString:  os << std::get<std::string>(data); break;
    }
    return os.str();
}

// ---- Encode -------------------------------------------------------------

namespace {

bool EncodeEntry(const LkvEntry &e, std::vector<uint8_t> &out) {
    if (e.source.size() > kMaxSourceLength) return false;
    const size_t value_len = e.value.WireLength();
    if (e.value.type == ValueType::kString && (value_len < 1 || value_len > 32)) return false;

    PutU16(out, e.key_id);
    PutU8(out, static_cast<uint8_t>(e.value.type));
    PutU8(out, static_cast<uint8_t>(e.quality));
    PutU8(out, static_cast<uint8_t>(e.confidence));
    PutU8(out, 0);  // reserved
    PutU64(out, e.timestamp_ms);
    PutU32(out, e.age_ms);
    PutU16(out, static_cast<uint16_t>(value_len));

    switch (e.value.type) {
        case ValueType::kBool: {
            bool v = false; e.value.AsBool(v);
            PutU8(out, v ? 0x01 : 0x00);
            break;
        }
        case ValueType::kUint8: {
            uint8_t v = 0; e.value.AsUint8(v);
            PutU8(out, v);
            break;
        }
        case ValueType::kInt8: {
            int8_t v = 0; e.value.AsInt8(v);
            PutU8(out, static_cast<uint8_t>(v));
            break;
        }
        case ValueType::kFloat32: {
            float v = 0.0f; e.value.AsFloat32(v);
            PutF32(out, v);
            break;
        }
        case ValueType::kString: {
            std::string v; e.value.AsString(v);
            PutBytes(out, reinterpret_cast<const uint8_t *>(v.data()), v.size());
            break;
        }
    }

    PutU8(out, static_cast<uint8_t>(e.source.size()));
    PutBytes(out, reinterpret_cast<const uint8_t *>(e.source.data()), e.source.size());
    return true;
}

bool DecodeEntry(Reader &r, LkvEntry &e) {
    uint16_t key_id = 0;
    uint8_t value_type_raw = 0, quality_raw = 0, confidence_raw = 0, reserved = 0;
    uint64_t timestamp_ms = 0;
    uint32_t age_ms = 0;
    uint16_t value_length = 0;

    if (!r.GetU16(key_id)) return false;
    if (!r.GetU8(value_type_raw)) return false;
    if (!r.GetU8(quality_raw)) return false;
    if (!r.GetU8(confidence_raw)) return false;
    if (!r.GetU8(reserved)) return false;
    if (reserved != 0) return false;
    if (!r.GetU64(timestamp_ms)) return false;
    if (!r.GetU32(age_ms)) return false;
    if (!r.GetU16(value_length)) return false;

    if (!IsKnownValueType(value_type_raw)) return false;
    const ValueType type = static_cast<ValueType>(value_type_raw);

    if (quality_raw > static_cast<uint8_t>(Quality::kStale)) return false;
    if (confidence_raw > static_cast<uint8_t>(Confidence::kHigh)) return false;

    switch (type) {
        case ValueType::kBool:
        case ValueType::kUint8:
        case ValueType::kInt8:
            if (value_length != 1) return false;
            break;
        case ValueType::kFloat32:
            if (value_length != 4) return false;
            break;
        case ValueType::kString:
            if (value_length < 1 || value_length > 32) return false;
            break;
    }

    switch (type) {
        case ValueType::kBool: {
            uint8_t v = 0;
            if (!r.GetU8(v)) return false;
            if (v != 0x00 && v != 0x01) return false;
            e.value = LkvValue::Bool(v != 0);
            break;
        }
        case ValueType::kUint8: {
            uint8_t v = 0;
            if (!r.GetU8(v)) return false;
            e.value = LkvValue::Uint8(v);
            break;
        }
        case ValueType::kInt8: {
            uint8_t raw = 0;
            if (!r.GetU8(raw)) return false;
            e.value = LkvValue::Int8(static_cast<int8_t>(raw));
            break;
        }
        case ValueType::kFloat32: {
            float v = 0.0f;
            if (!r.GetF32(v)) return false;
            e.value = LkvValue::Float32(v);
            break;
        }
        case ValueType::kString: {
            std::string v;
            if (!r.GetString(v, value_length)) return false;
            e.value = LkvValue::String(std::move(v));
            break;
        }
    }

    uint8_t source_length = 0;
    if (!r.GetU8(source_length)) return false;
    if (source_length > kMaxSourceLength) return false;
    std::string source;
    if (!r.GetString(source, source_length)) return false;

    e.key_id = key_id;
    e.quality = static_cast<Quality>(quality_raw);
    e.confidence = static_cast<Confidence>(confidence_raw);
    e.timestamp_ms = timestamp_ms;
    e.age_ms = age_ms;
    e.source = std::move(source);
    return true;
}

}  // namespace

bool EncodeMessage(const LkvMessage &msg, std::vector<uint8_t> &out) {
    if (msg.entries.size() > kMaxEntries) return false;
    out.clear();
    PutU16(out, msg.schema_version);
    PutU16(out, static_cast<uint16_t>(msg.entries.size()));
    for (const auto &e : msg.entries) {
        if (!EncodeEntry(e, out)) return false;
    }
    return true;
}

bool DecodeMessage(const uint8_t *data, size_t len, LkvMessage &out) {
    Reader r(data, len);
    uint16_t schema_version = 0, entry_count = 0;
    if (!r.GetU16(schema_version)) return false;
    if (!r.GetU16(entry_count)) return false;
    if (entry_count > kMaxEntries) return false;

    LkvMessage msg;
    msg.schema_version = schema_version;
    msg.entries.reserve(entry_count);
    for (uint16_t i = 0; i < entry_count; ++i) {
        LkvEntry e;
        if (!DecodeEntry(r, e)) return false;
        msg.entries.push_back(std::move(e));
    }
    // Reject trailing bytes: a length mismatch indicates a malformed message.
    if (!r.AtEnd()) return false;

    out = std::move(msg);
    return true;
}

// ---- WriteResponse --------------------------------------------------------

bool EncodeWriteResponse(const WriteResult &result,
                          const LkvMessage *applied_state,
                          std::vector<uint8_t> &out) {
    const bool ok = (result.result_code == WriteResultCode::kOk);
    if (ok != (applied_state != nullptr)) return false;

    out.clear();
    PutU8(out, static_cast<uint8_t>(result.result_code));
    PutU8(out, result.failed_entry_index);
    PutU16(out, 0);  // reserved

    if (ok) {
        std::vector<uint8_t> applied_bytes;
        if (!EncodeMessage(*applied_state, applied_bytes)) return false;
        PutBytes(out, applied_bytes.data(), applied_bytes.size());
    }
    return true;
}

bool DecodeWriteResponse(const uint8_t *data, size_t len, WriteResult &result,
                          LkvMessage &applied_state, bool &has_applied_state) {
    Reader r(data, len);
    uint8_t result_code_raw = 0, failed_entry_index = 0;
    uint16_t reserved = 0;
    if (!r.GetU8(result_code_raw)) return false;
    if (!r.GetU8(failed_entry_index)) return false;
    if (!r.GetU16(reserved)) return false;
    if (reserved != 0) return false;
    if (result_code_raw > static_cast<uint8_t>(WriteResultCode::kUnsupported)) return false;

    result.result_code = static_cast<WriteResultCode>(result_code_raw);
    result.failed_entry_index = failed_entry_index;

    has_applied_state = (result.result_code == WriteResultCode::kOk);
    if (has_applied_state) {
        if (!DecodeMessage(data + r.pos(), r.remaining(), applied_state)) return false;
    } else if (!r.AtEnd()) {
        return false;
    }
    return true;
}

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv
