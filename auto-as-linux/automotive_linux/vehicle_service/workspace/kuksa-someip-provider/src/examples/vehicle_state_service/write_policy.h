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

#ifndef WRITE_POLICY_H
#define WRITE_POLICY_H

#include "lkv_codec.h"

// HVAC-only write validation (SWARCH-002 section 4 "WriteVehicleState" /
// section 3 "write_policy"). Validates a decoded WriteVehicleState request
// against SYS-002's key range/type/value rules, all-or-nothing: the first
// invalid entry aborts validation for the whole request and the model must
// not be touched.

namespace sdv {
namespace someip {
namespace vehicle_state {

struct ValidatedWrite {
    WriteResult result;
    // Populated iff result.result_code == WriteResultCode::kOk: the
    // request's entries, re-stamped with a fresh timestamp/quality/
    // confidence/source for application to the model and for
    // WriteResponse.applied_state.
    std::vector<LkvEntry> applied_entries;
};

/**
 * @brief Validates a decoded WriteVehicleState request.
 *
 * - A key outside kWritableKeyMin..kWritableKeyMax yields kUnsupported.
 * - A value_type mismatch or an out-of-range value (SYS-002 4.1 ranges)
 *   yields kInvalid.
 * - `failed_entry_index` is the index of the first offending entry, or
 *   kFailedEntryIndexNone if every entry validated.
 */
ValidatedWrite ValidateWrite(const LkvMessage &request);

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv

#endif  // WRITE_POLICY_H
