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

#include "write_policy.h"

namespace sdv {
namespace someip {
namespace vehicle_state {

namespace {

// SYS-002 4.1 valid ranges for the 5 writable keys.
bool InRange(uint16_t key_id, const LkvValue &value) {
    switch (key_id) {
        case kKeyAirconOnOff:
        case kKeyAirconDefrost: {
            bool v = false;
            return value.AsBool(v);
        }
        case kKeyAirconTemperature: {
            float v = 0.0f;
            if (!value.AsFloat32(v)) return false;
            return v >= 16.0f && v <= 30.0f;
        }
        case kKeyAirconFan: {
            uint8_t v = 0;
            if (!value.AsUint8(v)) return false;
            return v <= 7;
        }
        case kKeyAirconMode: {
            uint8_t v = 0;
            if (!value.AsUint8(v)) return false;
            return v <= 6;
        }
        default:
            return false;
    }
}

}  // namespace

ValidatedWrite ValidateWrite(const LkvMessage &request) {
    ValidatedWrite out;

    for (size_t i = 0; i < request.entries.size(); ++i) {
        const auto &e = request.entries[i];

        if (!IsWritableKey(e.key_id)) {
            out.result.result_code = WriteResultCode::kUnsupported;
            out.result.failed_entry_index = static_cast<uint8_t>(i);
            out.applied_entries.clear();
            return out;
        }

        // InRange() rejects a type mismatch itself (LkvValue::As*() checks
        // the active variant alternative), so no separate type check here.
        if (FindKey(e.key_id) == nullptr || !InRange(e.key_id, e.value)) {
            out.result.result_code = WriteResultCode::kInvalid;
            out.result.failed_entry_index = static_cast<uint8_t>(i);
            out.applied_entries.clear();
            return out;
        }
    }

    // All entries valid: re-stamp with a fresh timestamp/quality/confidence
    // for the model and for WriteResponse.applied_state.
    out.result.result_code = WriteResultCode::kOk;
    out.result.failed_entry_index = kFailedEntryIndexNone;
    const uint64_t now = NowMs();
    out.applied_entries.reserve(request.entries.size());
    for (const auto &e : request.entries) {
        LkvEntry applied = e;
        applied.quality = Quality::kOk;
        applied.confidence = Confidence::kHigh;
        applied.timestamp_ms = now;
        applied.age_ms = 0;
        applied.source = kDefaultSource;
        out.applied_entries.push_back(std::move(applied));
    }
    return out;
}

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv
