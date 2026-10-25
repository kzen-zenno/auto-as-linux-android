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

#include "vehicle_state_model.h"

#include <algorithm>
#include <cmath>

namespace sdv {
namespace someip {
namespace vehicle_state {

namespace {

// Deterministic, RNG-free triangle wave in [0, amplitude], one full
// up/down cycle every `period_ticks` calls.
double TriangleWave(uint64_t tick_count, uint64_t period_ticks, double amplitude) {
    const uint64_t half = period_ticks / 2;
    const uint64_t phase = tick_count % period_ticks;
    const double ratio = (phase < half)
        ? static_cast<double>(phase) / static_cast<double>(half)
        : static_cast<double>(period_ticks - phase) / static_cast<double>(half);
    return amplitude * ratio;
}

constexpr uint64_t kSpeedPeriodTicks = 300;  // 30 s at 100 ms/tick
constexpr double kSpeedAmplitudeKph  = 120.0;
constexpr double kTacoMaxRpm         = 6000.0;
constexpr double kFuelDecrementPct   = 0.01;  // per 100 ms tick

}  // namespace

VehicleStateModel::VehicleStateModel() {
    values_[kKeySpeed]             = LkvValue::Float32(0.0f);
    values_[kKeyTacoMeter]         = LkvValue::Float32(0.0f);
    values_[kKeyReverse]           = LkvValue::Bool(false);
    values_[kKeyParkingBrake]      = LkvValue::Bool(false);
    values_[kKeyAirconOnOff]       = LkvValue::Bool(false);
    values_[kKeyAirconTemperature] = LkvValue::Float32(20.0f);
    values_[kKeyAirconFan]         = LkvValue::Uint8(0);
    values_[kKeyAirconDefrost]     = LkvValue::Bool(false);
    values_[kKeyAirconMode]        = LkvValue::Uint8(0);  // Auto
    values_[kKeyEscIsError]        = LkvValue::Bool(false);
    values_[kKeyAbsIsError]        = LkvValue::Bool(false);
    values_[kKeySeatIsBelted]      = LkvValue::Bool(false);
    values_[kKeyDoorIsOpen]        = LkvValue::Bool(false);
    values_[kKeyAirbagIsDeployed]  = LkvValue::Bool(false);
    values_[kKeyFuelRelativeLevel] = LkvValue::Uint8(100);
}

void VehicleStateModel::Tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++tick_count_;

    const double speed_kph = TriangleWave(tick_count_, kSpeedPeriodTicks, kSpeedAmplitudeKph);
    const double taco_rpm = std::min(kTacoMaxRpm, (speed_kph / kSpeedAmplitudeKph) * kTacoMaxRpm);
    values_[kKeySpeed] = LkvValue::Float32(static_cast<float>(speed_kph));
    values_[kKeyTacoMeter] = LkvValue::Float32(static_cast<float>(taco_rpm));

    uint8_t fuel_pct = 100;
    values_[kKeyFuelRelativeLevel].AsUint8(fuel_pct);
    double fuel = static_cast<double>(fuel_pct) - kFuelDecrementPct;
    if (fuel < 0.0) fuel = 100.0;
    values_[kKeyFuelRelativeLevel] = LkvValue::Uint8(
        static_cast<uint8_t>(std::round(fuel)));
}

std::vector<LkvEntry> VehicleStateModel::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t now = NowMs();

    std::vector<LkvEntry> entries;
    entries.reserve(kKeyTable.size());
    for (const auto &desc : kKeyTable) {
        LkvEntry e;
        e.key_id = desc.key_id;
        e.value = values_.at(desc.key_id);
        e.quality = Quality::kOk;
        e.confidence = Confidence::kHigh;
        e.timestamp_ms = now;
        e.age_ms = 0;
        e.source = kDefaultSource;
        entries.push_back(std::move(e));
    }
    return entries;
}

void VehicleStateModel::ApplyWrite(const std::vector<LkvEntry> &validated_entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &e : validated_entries) {
        if (!IsWritableKey(e.key_id)) continue;
        values_[e.key_id] = e.value;
    }
}

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv
