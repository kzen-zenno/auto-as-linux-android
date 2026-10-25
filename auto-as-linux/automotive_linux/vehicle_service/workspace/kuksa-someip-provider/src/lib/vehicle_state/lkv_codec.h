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

#ifndef LKV_CODEC_H
#define LKV_CODEC_H

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "vehicle_state_ids.h"

// Codec for the little-endian LKV binary payload defined in
// specs/sys/SYS-002-someip.md section 5.2. This is the only module that
// knows the wire layout (SWARCH-001 section 3 / SWARCH-002 section 3):
//
//   Message      { uint16 schema_version; uint16 entry_count; LkvEntry[] }
//   LkvEntry     { uint16 key_id; uint8 value_type; uint8 quality;
//                  uint8 confidence_level; uint8 reserved(=0);
//                  uint64 timestamp_ms; uint32 age_ms; uint16 value_length;
//                  uint8 value[value_length]; uint8 source_length;
//                  uint8 source[source_length] }
//   WriteResponse{ uint8 result_code; uint8 failed_entry_index;
//                  uint16 reserved(=0); Message applied_state (only if
//                  result_code == 0x00) }
//
// Decode is strict: entry_count > 16, source_length > 32, a non-zero
// reserved byte, a value_length that does not match the declared
// value_type, or any declared length that overruns the buffer all cause
// the *entire* message to be rejected (no partial result), per SYS-002 5.2
// "the complete message is rejected when any entry is malformed".

namespace sdv {
namespace someip {
namespace vehicle_state {

/**
 * @brief Tagged value carried by one LkvEntry. The active variant
 * alternative always matches `type`.
 */
struct LkvValue {
    ValueType type = ValueType::kBool;
    std::variant<bool, uint8_t, int8_t, float, std::string> data = false;

    static LkvValue Bool(bool v);
    static LkvValue Uint8(uint8_t v);
    static LkvValue Int8(int8_t v);
    static LkvValue Float32(float v);
    static LkvValue String(std::string v);

    bool AsBool(bool &out) const;
    bool AsUint8(uint8_t &out) const;
    bool AsInt8(int8_t &out) const;
    bool AsFloat32(float &out) const;
    bool AsString(std::string &out) const;

    // Encoded byte length of `data` on the wire (1 for bool/uint8/int8,
    // 4 for float32, string length for string).
    size_t WireLength() const;

    std::string ToString() const;
};

struct LkvEntry {
    uint16_t key_id = 0;
    LkvValue value;
    Quality quality = Quality::kOk;
    Confidence confidence = Confidence::kUnknown;
    uint64_t timestamp_ms = 0;
    uint32_t age_ms = 0;
    std::string source;
};

struct LkvMessage {
    uint16_t schema_version = kSchemaVersion;
    std::vector<LkvEntry> entries;
};

struct WriteResult {
    WriteResultCode result_code = WriteResultCode::kOk;
    uint8_t failed_entry_index = kFailedEntryIndexNone;
};

/**
 * @brief Encode a Message (event payload / snapshot response / write
 * request / applied_state). Fails (returns false) only if `entries.size()`
 * exceeds kMaxEntries or a `source` exceeds kMaxSourceLength -- the caller
 * is expected to build well-formed messages.
 */
bool EncodeMessage(const LkvMessage &msg, std::vector<uint8_t> &out);

/**
 * @brief Decode a Message. Strict: any malformed field rejects the whole
 * message and leaves `out` unspecified.
 */
bool DecodeMessage(const uint8_t *data, size_t len, LkvMessage &out);

/**
 * @brief Encode a WriteResponse. `applied_state` must be non-null iff
 * `result.result_code == WriteResultCode::kOk`.
 */
bool EncodeWriteResponse(const WriteResult &result,
                          const LkvMessage *applied_state,
                          std::vector<uint8_t> &out);

/**
 * @brief Decode a WriteResponse. `has_applied_state` reports whether
 * `applied_state` was populated (result_code == kOk).
 */
bool DecodeWriteResponse(const uint8_t *data, size_t len, WriteResult &result,
                          LkvMessage &applied_state, bool &has_applied_state);

/**
 * @brief Current wall-clock time in epoch milliseconds.
 */
uint64_t NowMs();

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv

#endif  // LKV_CODEC_H
