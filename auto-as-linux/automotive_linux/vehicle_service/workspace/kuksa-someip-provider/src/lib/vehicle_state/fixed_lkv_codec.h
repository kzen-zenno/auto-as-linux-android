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

#ifndef FIXED_LKV_CODEC_H
#define FIXED_LKV_CODEC_H

#include <cstdint>
#include <vector>

#include "lkv_codec.h"

// Codec for the SYS-002 5.2 *fixed-length* LKV schema (the migration note
// added when the real, non-simulator AUTOSAR-CP Gateway ECU
// (autoas-as/src/app/fmw) was implemented -- see SWARCH-000-R-005). This is
// a wire format DISTINCT from lkv_codec.h's original variable-length TLV,
// which src/examples/vehicle_state_service (the someip-server simulator)
// and vehicle_state_probe still use unchanged; the two are not
// interchangeable on the wire. Only someip_feeder (the real Gateway ECU
// consumer) uses this codec.
//
// Wire format: AUTOSAR SomeIpXf (big-endian TLV), matching what
// autoas-as's native SomeIp/SomeIpXf generator produces for
// autoas-as/src/app/fmw/config/SomeIp/Network.json's LkvEntry /
// VehicleStateMessage / WriteRequest / WriteResponse structs:
//
//   LkvEntry (fixed 20 bytes, no struct-length prefix as an array element)
//     uint16 BE  keyId
//     uint8      quality           // 0=OK, 1=INVALID, 2=STALE
//     uint8      confidence        // 0..3
//     uint64 BE  timestampMs
//     uint32 BE  ageMs
//     uint32 BE  value             // IEEE 754 binary32 bit pattern, in
//                                  // network byte order (bool/uint8/int8
//                                  // keys carry their value cast to
//                                  // float32; see SYS-002 5.2)
//
//   VehicleStateMessage (EvVehicleState / GetVehicleStateSnapshot)
//     uint16 BE  structLength      // byte length of everything below
//     uint16 BE  schemaVersion
//     uint16 BE  entriesByteLength // N * 20
//     LkvEntry   entries[N]        // N = entriesByteLength / 20, N <= 16
//
//   WriteRequest (WriteVehicleState request)
//     uint8      structLength      // byte length of everything below
//     uint8      entriesByteLength // N * 20
//     LkvEntry   entries[N]        // N <= 5
//
//   WriteResponse (WriteVehicleState response)
//     uint8      structLength      // byte length of everything below
//     uint8      resultCode        // 0x00=OK, 0x01=INVALID, 0x02=UNSUPPORTED
//     uint8      failedEntryIndex  // 0xFF when all entries are valid
//     uint8      entriesByteLength // N * 20
//     LkvEntry   entries[N]        // applied_state, N <= 5, always present
//                                  // (possibly empty) regardless of resultCode
//
// key_id alone determines each entry's semantic value type (SYS-002 4.1 KEY
// table via FindKey()); the wire no longer carries value_type. There is
// also no per-entry `source` on the wire -- decoded entries are stamped
// with the fixed source "Gateway ECU" for informational parity with the
// old codec's LkvEntry::source field.

namespace sdv {
namespace someip {
namespace vehicle_state {

constexpr size_t kFixedMaxEntries = 16;       // VehicleStateMessage
constexpr size_t kFixedMaxWriteEntries = 5;   // WriteRequest / WriteResponse
constexpr size_t kFixedEntrySize = 20;

/**
 * @brief Decodes a VehicleStateMessage (EvVehicleState event payload).
 * Strict: a malformed length or an entry count beyond kFixedMaxEntries
 * rejects the whole message.
 */
bool DecodeFixedVehicleStateMessage(const uint8_t *data, size_t len, LkvMessage &out);

/**
 * @brief Encodes a WriteRequest (WriteVehicleState request). Fails if
 * `msg.entries.size()` exceeds kFixedMaxWriteEntries or contains a
 * ValueType::kString entry (unsupported on this wire format).
 */
bool EncodeFixedWriteRequest(const LkvMessage &msg, std::vector<uint8_t> &out);

/**
 * @brief Decodes a WriteResponse (WriteVehicleState response).
 * `applied_state.entries` is always populated from the wire (possibly
 * empty), regardless of `result.result_code`.
 */
bool DecodeFixedWriteResponse(const uint8_t *data, size_t len, WriteResult &result,
                               LkvMessage &applied_state);

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv

#endif  // FIXED_LKV_CODEC_H
