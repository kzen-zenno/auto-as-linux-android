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

#ifndef VEHICLE_STATE_MODEL_H
#define VEHICLE_STATE_MODEL_H

#include <map>
#include <mutex>
#include <vector>

#include "lkv_codec.h"

// Thread-safe VehicleState model (SWARCH-002 section 3 "vehicle_state_model").
//
// Holds the current value for all 15 SYS-002 keys and runs a deterministic,
// RNG-free simulation of the status signals (Speed/TacoMeter/Fuel). HVAC
// keys (0x1006..0x100A) never change except via ApplyWrite(), per
// SWARCH-002 section 3. Every value is always emitted with
// quality=OK/confidence=High/source="Gateway ECU" -- this simulator never
// generates INVALID/STALE data about itself; that is left as a future
// extension (see plan "Out of scope").

namespace sdv {
namespace someip {
namespace vehicle_state {

class VehicleStateModel {
public:
    VehicleStateModel();

    /**
     * @brief Advances the deterministic status-signal simulation by one
     * cycle. Does not touch HVAC keys.
     */
    void Tick();

    /**
     * @brief Returns a consistent copy of all 15 entries, freshly stamped
     * with the current time.
     */
    std::vector<LkvEntry> Snapshot() const;

    /**
     * @brief Atomically applies already-validated HVAC entries (see
     * write_policy.h). Entries not in the writable key range are ignored.
     */
    void ApplyWrite(const std::vector<LkvEntry> &validated_entries);

private:
    mutable std::mutex mutex_;
    std::map<uint16_t, LkvValue> values_;
    uint64_t tick_count_ = 0;
};

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv

#endif  // VEHICLE_STATE_MODEL_H
