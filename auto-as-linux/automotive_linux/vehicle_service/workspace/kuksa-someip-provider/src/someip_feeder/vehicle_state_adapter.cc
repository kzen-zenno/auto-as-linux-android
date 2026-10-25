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

#include <chrono>
#include <cmath>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <vector>
#include <unistd.h>  // access()

#include "vehicle_state_adapter.h"
#include "vehicle_state_ids.h"
#include "fixed_lkv_codec.h"

using namespace sdv::someip::vehicle_state;

namespace sdv {
namespace adapter {

/*** LOG helpers ***/
#define LEVEL_TRC   3
#define LEVEL_DBG   2
#define LEVEL_INF   1
#define LEVEL_ERR   0
#define MODULE_PREFIX   "# VehicleStateAdapter::"

#define LOG_TRACE   if (log_level_ >= LEVEL_TRC) std::cout << MODULE_PREFIX << __func__ << ": [trace] "
#define LOG_DEBUG   if (log_level_ >= LEVEL_DBG) std::cout << MODULE_PREFIX << __func__ << ": [debug] "
#define LOG_INFO    if (log_level_ >= LEVEL_INF) std::cout << MODULE_PREFIX << __func__ << ": [info] "
#define LOG_ERROR   if (log_level_ >= LEVEL_ERR) std::cerr << MODULE_PREFIX << __func__ << ": [error] "

namespace {

/*** VSS paths (SYS-003 / SWARCH-001 2.3; Seat/Door/Airbag instance fixed to
 * Row1.DriverSide earlier this session). 0x1009's overlay path name is this
 * plan's own choice -- SYS-002/SYS-003 only say "custom overlay", no
 * literal path is specified anywhere upstream.
 *
 * VSS_GEAR and VSS_AC_ACTIVE were verified against the real COVESA VSS 6.0
 * catalog loaded by this deployment's kuksa-databroker (data/vss-core/
 * vss_release_6.0.json) and corrected here -- SYS-002/SYS-003 name
 * "Vehicle.Powertrain.Transmission.Gear" and
 * "Vehicle.Cabin.HVAC.Station.Row1.Driver.IsAirConditioningActive", neither
 * of which exists in that catalog (confirmed via a live `Set` against the
 * running databroker returning "not found"); the real paths are
 * `CurrentGear` (not `Gear`) and a vehicle-wide (not per-station)
 * `IsAirConditioningActive`. This is a spec inaccuracy worth fixing in
 * SYS-002/SYS-003, not something this adapter should silently diverge on
 * without flagging. ***/
const std::string VSS_SPEED               = "Vehicle.Speed";
const std::string VSS_ENGINE_SPEED        = "Vehicle.Powertrain.CombustionEngine.Speed";
const std::string VSS_GEAR                = "Vehicle.Powertrain.Transmission.CurrentGear";
const std::string VSS_PARKING_BRAKE       = "Vehicle.Chassis.ParkingBrake.IsEngaged";
const std::string VSS_AC_ACTIVE           = "Vehicle.Cabin.HVAC.IsAirConditioningActive";
const std::string VSS_AC_TEMPERATURE      = "Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature";
const std::string VSS_AC_FAN_SPEED        = "Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed";
const std::string VSS_AC_DEFROST          = "Vehicle.Cabin.HVAC.Station.Row1.Driver.IsDefrosting";
const std::string VSS_AC_AIR_DISTRIBUTION = "Vehicle.Cabin.HVAC.Station.Row1.Driver.AirDistribution";
const std::string VSS_ESC_IS_ERROR        = "Vehicle.ADAS.ESC.IsError";
const std::string VSS_ABS_IS_ERROR        = "Vehicle.ADAS.ABS.IsError";
const std::string VSS_SEAT_IS_BELTED      = "Vehicle.Cabin.Seat.Row1.DriverSide.IsBelted";
const std::string VSS_DOOR_IS_OPEN        = "Vehicle.Cabin.Door.Row1.DriverSide.IsOpen";
const std::string VSS_AIRBAG_IS_DEPLOYED  = "Vehicle.Cabin.Seat.Row1.DriverSide.Airbag.IsDeployed";
const std::string VSS_FUEL_LEVEL          = "Vehicle.Powertrain.FuelSystem.RelativeLevel";

// Actuator (write) paths, all 5 writable HVAC keys of SYS-002 4.1
// (0x1006..0x100A).
//
// VSS_AC_DEFROST used to be excluded here because it has no standard VSS
// path (SYS-005 5) and kuksa.val.v1's Subscribe fails its *entire* request
// -- not just the one path -- when any requested path is unknown, so
// including it broke actuation for the other 4 HVAC keys as well. It is
// included again now that the databroker is started with the FMW overlay
// that registers the path (vehicle_service/databroker/vss_fmw_overlay.json,
// passed via the comma-separated --vss list). Running against a databroker
// *without* that overlay reintroduces the all-or-nothing Subscribe failure.
const std::array<std::string, 5> SUBSCRIBE_ACTUATORS = {
    VSS_AC_ACTIVE, VSS_AC_TEMPERATURE, VSS_AC_FAN_SPEED, VSS_AC_AIR_DISTRIBUTION,
    VSS_AC_DEFROST
};

/*** SYS-001 3 / SWRS-CDC-AllSignals 3 per-signal STALE/INVALID thresholds,
 * keyed by SOME/IP key id. ***/
struct Thresholds { uint32_t stale_ms; uint32_t invalid_ms; };

Thresholds ThresholdsFor(uint16_t key_id) {
    switch (key_id) {
        case kKeySpeed: case kKeyTacoMeter: case kKeyReverse:               // high frequency
            return {300, 1000};
        case kKeyParkingBrake:                                              // medium frequency
            return {500, 3000};
        case kKeyAirconOnOff: case kKeyAirconTemperature: case kKeyAirconFan:
        case kKeyAirconDefrost: case kKeyAirconMode: case kKeyFuelRelativeLevel:  // low frequency
            return {1500, 3000};
        case kKeyEscIsError: case kKeyAbsIsError: case kKeySeatIsBelted:
        case kKeyDoorIsOpen: case kKeyAirbagIsDeployed:                     // warning-related
            return {300, 1000};
        default:
            return {1500, 3000};
    }
}

// SYS-002 4.1 Range/Valid Values, for the receive-side validation this
// adapter performs independently of the server's own quality tag
// (SWARCH-001 4 "Type and range violations result in INVALID").
bool ValueInRange(uint16_t key_id, const LkvValue &value) {
    switch (key_id) {
        case kKeySpeed: {
            float v = 0.0f;
            return value.AsFloat32(v) && v >= 0.0f && v <= 255.0f;
        }
        case kKeyTacoMeter: {
            float v = 0.0f;
            return value.AsFloat32(v) && v >= 0.0f && v <= 8000.0f;
        }
        case kKeyReverse: case kKeyParkingBrake: case kKeyAirconOnOff: case kKeyAirconDefrost:
        case kKeyEscIsError: case kKeyAbsIsError: case kKeySeatIsBelted:
        case kKeyDoorIsOpen: case kKeyAirbagIsDeployed: {
            bool v = false;
            return value.AsBool(v);
        }
        case kKeyAirconTemperature: {
            float v = 0.0f;
            return value.AsFloat32(v) && v >= 16.0f && v <= 30.0f;
        }
        case kKeyAirconFan: {
            uint8_t v = 0;
            return value.AsUint8(v) && v <= 7;
        }
        case kKeyAirconMode: {
            uint8_t v = 0;
            return value.AsUint8(v) && v <= 6;
        }
        case kKeyFuelRelativeLevel: {
            uint8_t v = 0;
            return value.AsUint8(v) && v <= 100;
        }
        default:
            return false;
    }
}

uint64_t NowMsWall() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

void SetTimestamp(::kuksa::val::v1::Datapoint &dp, uint64_t timestamp_ms) {
    auto *ts = dp.mutable_timestamp();
    ts->set_seconds(static_cast<int64_t>(timestamp_ms / 1000));
    ts->set_nanos(static_cast<int32_t>((timestamp_ms % 1000) * 1000000));
}

}  // namespace

VehicleStateAdapter::VehicleStateAdapter() :
    feeder_active_(false),
    databroker_addr_(),
    collector_client_(nullptr),
    databroker_feeder_(nullptr),
    feeder_thread_(nullptr),
    actuator_subscriber_(nullptr),
    subscriber_thread_(nullptr),
    someip_client_(nullptr),
    someip_thread_(nullptr),
    someip_active_(false),
    quality_thread_(nullptr),
    quality_thread_running_(false),
    shutdown_requested_(false)
{
    log_level_ = sdv::someip::getEnvironmentInt("SOMEIP_CLI_DEBUG", 1, false);
    for (const auto &desc : kKeyTable) {
        key_states_[desc.key_id] = KeyState{};
    }
}

VehicleStateAdapter::~VehicleStateAdapter() {
    LOG_TRACE << "called." << std::endl;
    Shutdown();
    LOG_TRACE << "done." << std::endl;
}

bool VehicleStateAdapter::InitDataBrokerFeeder(const std::string &databroker_addr, const std::string &auth_token) {
    sdv::broker_feeder::DatapointConfiguration metadata = {
        {VSS_SPEED, ::kuksa::val::v1::Datapoint{},
            "Vehicle speed, from the VehicleState SOME/IP contract."},
        {VSS_ENGINE_SPEED, ::kuksa::val::v1::Datapoint{},
            "Engine speed (rpm)."},
        {VSS_GEAR, ::kuksa::val::v1::Datapoint{},
            "Current gear; -1 represents Reverse (SYS-003 5)."},
        {VSS_PARKING_BRAKE, ::kuksa::val::v1::Datapoint{},
            "True if the parking brake is engaged."},
        {VSS_AC_ACTIVE, ::kuksa::val::v1::Datapoint{},
            "True if the vehicle's air conditioning is active (VSS 6.0: vehicle-wide, not per-station)."},
        {VSS_AC_TEMPERATURE, ::kuksa::val::v1::Datapoint{},
            "Driver-station target temperature (degC)."},
        {VSS_AC_FAN_SPEED, ::kuksa::val::v1::Datapoint{},
            "Driver-station fan speed (percent), converted from the 8 SOME/IP levels."},
        {VSS_AC_DEFROST, ::kuksa::val::v1::Datapoint{},
            "Driver-station defrost state (custom overlay, no standard VSS path)."},
        {VSS_AC_AIR_DISTRIBUTION, ::kuksa::val::v1::Datapoint{},
            "UP/MIDDLE/DOWN, mapped from Aircon Mode Face/Foot/Face+Foot only."},
        {VSS_ESC_IS_ERROR, ::kuksa::val::v1::Datapoint{},
            "True if the ESC system reports an error."},
        {VSS_ABS_IS_ERROR, ::kuksa::val::v1::Datapoint{},
            "True if the ABS system reports an error."},
        {VSS_SEAT_IS_BELTED, ::kuksa::val::v1::Datapoint{},
            "Row1.DriverSide seatbelt state."},
        {VSS_DOOR_IS_OPEN, ::kuksa::val::v1::Datapoint{},
            "Row1.DriverSide door open state."},
        {VSS_AIRBAG_IS_DEPLOYED, ::kuksa::val::v1::Datapoint{},
            "Row1.DriverSide airbag deployment state."},
        {VSS_FUEL_LEVEL, ::kuksa::val::v1::Datapoint{},
            "Remaining fuel level (percent)."},
    };

    LOG_INFO << "Connecting to " << databroker_addr << std::endl;

    collector_client_ = sdv::broker_feeder::CollectorClient::createInstance(databroker_addr, auth_token);
    databroker_feeder_ = sdv::broker_feeder::DataBrokerFeeder::createInstance(collector_client_, std::move(metadata));
    actuator_subscriber_ = sdv::broker_feeder::kuksa::ActuatorSubscriber::createInstance(collector_client_);
    actuator_subscriber_->Init(
        std::vector<std::string>(SUBSCRIBE_ACTUATORS.begin(), SUBSCRIBE_ACTUATORS.end()),
        std::bind(&VehicleStateAdapter::on_actuator_change, this, std::placeholders::_1)
    );

    return true;
}

bool VehicleStateAdapter::InitSomeipClient() {
    auto someip_app = ::getenv("VSOMEIP_APPLICATION_NAME");
    auto someip_config = ::getenv("VSOMEIP_CONFIGURATION");

    bool someip_ok = true;
    if (!someip_app) {
        LOG_ERROR << "VSOMEIP_APPLICATION_NAME not set in environment, someip disabled!" << std::endl;
        someip_ok = false;
    }
    if (!someip_config) {
        LOG_ERROR << "VSOMEIP_CONFIGURATION not set in environment, someip disabled!" << std::endl;
        someip_ok = false;
    } else if (::access(someip_config, F_OK) == -1) {
        LOG_ERROR << "env VSOMEIP_CONFIGURATION file is missing: " << someip_config << std::endl;
        someip_ok = false;
    }

    if (someip_ok) {
        LOG_INFO << "VSOMEIP_APPLICATION_NAME=" << someip_app
                  << ", VSOMEIP_CONFIGURATION=" << someip_config << std::endl;

        // VehicleStateService uses one service (0x1770) for both the event
        // and the methods, so the IDs come straight from vehicle_state_ids.h
        // rather than SOMEIP_CLI_* env-var overrides.
        sdv::someip::SomeIPConfig config = sdv::someip::SomeIPClient::createEnvConfig();
        config.use_tcp = true;
        config.service = kServiceId;
        config.instance = kInstanceId;
        config.event_group = kEventGroupId;
        config.event = kEventId;
        config.service_major = kServiceMajor;
        config.service_minor = kServiceMinor;
        config.req.use_req = true;
        config.req.service = kServiceId;
        config.req.instance = kInstanceId;
        config.req.service_major = kServiceMajor;
        config.req.service_minor = kServiceMinor;

        someip_client_ = sdv::someip::SomeIPClient::createInstance(
            config,
            std::bind(&VehicleStateAdapter::on_someip_message, this,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                    std::placeholders::_4, std::placeholders::_5));
    }
    someip_active_ = someip_ok;
    return someip_ok;
}

void VehicleStateAdapter::on_actuator_change(sdv::broker_feeder::kuksa::ActuatorValues target_values) {
    LOG_INFO << "updated target_values: " << target_values.size() << std::endl;

    LkvMessage request;
    const uint64_t now = NowMsWall();

    auto add_entry = [&](uint16_t key_id, LkvValue value) {
        LkvEntry e;
        e.key_id = key_id;
        e.value = std::move(value);
        e.quality = Quality::kOk;
        e.confidence = Confidence::kHigh;
        e.timestamp_ms = now;
        e.age_ms = 0;
        e.source = "kuksa-provider";
        request.entries.push_back(std::move(e));
    };

    if (auto it = target_values.find(VSS_AC_ACTIVE); it != target_values.end()) {
        if (it->second.value_case() == ::kuksa::val::v1::Datapoint::ValueCase::kBool) {
            add_entry(kKeyAirconOnOff, LkvValue::Bool(it->second.bool_()));
        } else {
            LOG_ERROR << "wrong value type for " << VSS_AC_ACTIVE << std::endl;
        }
    }
    if (auto it = target_values.find(VSS_AC_TEMPERATURE); it != target_values.end()) {
        if (it->second.value_case() == ::kuksa::val::v1::Datapoint::ValueCase::kFloat) {
            add_entry(kKeyAirconTemperature, LkvValue::Float32(it->second.float_()));
        } else {
            LOG_ERROR << "wrong value type for " << VSS_AC_TEMPERATURE << std::endl;
        }
    }
    if (auto it = target_values.find(VSS_AC_FAN_SPEED); it != target_values.end()) {
        bool ok = false;
        double percent = 0.0;
        switch (it->second.value_case()) {
            case ::kuksa::val::v1::Datapoint::ValueCase::kUint32:
                percent = it->second.uint32(); ok = true; break;
            case ::kuksa::val::v1::Datapoint::ValueCase::kInt32:
                percent = it->second.int32(); ok = true; break;
            default: break;
        }
        if (ok) {
            long level = std::lround(percent * 7.0 / 100.0);
            if (level < 0) level = 0;
            if (level > 7) level = 7;
            add_entry(kKeyAirconFan, LkvValue::Uint8(static_cast<uint8_t>(level)));
        } else {
            LOG_ERROR << "wrong value type for " << VSS_AC_FAN_SPEED << std::endl;
        }
    }
    if (auto it = target_values.find(VSS_AC_DEFROST); it != target_values.end()) {
        if (it->second.value_case() == ::kuksa::val::v1::Datapoint::ValueCase::kBool) {
            add_entry(kKeyAirconDefrost, LkvValue::Bool(it->second.bool_()));
        } else {
            LOG_ERROR << "wrong value type for " << VSS_AC_DEFROST << std::endl;
        }
    }
    if (auto it = target_values.find(VSS_AC_AIR_DISTRIBUTION); it != target_values.end()) {
        if (it->second.value_case() == ::kuksa::val::v1::Datapoint::ValueCase::kString) {
            const std::string &dist = it->second.string();
            uint8_t mode;
            bool ok = true;
            if (dist == "UP") mode = 4;
            else if (dist == "DOWN") mode = 5;
            else if (dist == "MIDDLE") mode = 6;
            else { ok = false; mode = 0; }
            if (ok) {
                add_entry(kKeyAirconMode, LkvValue::Uint8(mode));
            } else {
                LOG_ERROR << "unrecognized AirDistribution value: " << dist << std::endl;
            }
        } else {
            LOG_ERROR << "wrong value type for " << VSS_AC_AIR_DISTRIBUTION << std::endl;
        }
    }

    if (request.entries.empty()) {
        LOG_ERROR << "No convertible target values in this update; nothing to send." << std::endl;
        return;
    }

    std::vector<uint8_t> bytes;
    if (!EncodeFixedWriteRequest(request, bytes)) {
        LOG_ERROR << "Failed to encode WriteVehicleState request!" << std::endl;
        return;
    }
    if (someip_client_) {
        LOG_INFO << "Sending WriteVehicleState with " << request.entries.size() << " entries" << std::endl;
        someip_client_->SendRequest(kServiceId, kInstanceId, kMethodWrite, bytes);
    }
}

void VehicleStateAdapter::handle_write_response(const uint8_t *payload, size_t payload_length) {
    WriteResult result;
    LkvMessage applied;
    if (!DecodeFixedWriteResponse(payload, payload_length, result, applied)) {
        LOG_ERROR << "Failed to decode WriteResponse!" << std::endl;
        return;
    }
    LOG_INFO << "WriteResponse result_code=" << static_cast<int>(result.result_code)
              << " failed_entry_index=" << static_cast<int>(result.failed_entry_index)
              << std::endl;
    // The applied values are re-published to Databroker via the next
    // periodic EvVehicleState event (handle_event); no separate feed here.
}

void VehicleStateAdapter::handle_event(const LkvMessage &msg) {
    struct PendingFeed { uint16_t key_id; LkvValue value; uint64_t timestamp_ms; };
    std::vector<PendingFeed> pending;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const auto &entry : msg.entries) {
            if (FindKey(entry.key_id) == nullptr) {
                LOG_DEBUG << "Ignoring unknown key 0x" << std::hex << entry.key_id << std::dec << std::endl;
                continue;
            }
            auto &state = key_states_[entry.key_id];

            if (state.last_lkv_timestamp_ms != 0 && entry.timestamp_ms <= state.last_lkv_timestamp_ms) {
                LOG_ERROR << "Out-of-order entry for key 0x" << std::hex << entry.key_id << std::dec
                          << " (ts=" << entry.timestamp_ms << " <= last=" << state.last_lkv_timestamp_ms
                          << "); dropped." << std::endl;
                continue;
            }

            const bool in_range = FindKey(entry.key_id)->type == entry.value.type
                                && ValueInRange(entry.key_id, entry.value);

            state.last_lkv_timestamp_ms = entry.timestamp_ms;
            state.last_receive_ms = NowMsWall();

            if (!in_range || entry.quality != Quality::kOk) {
                // No Databroker feed: the vendored kuksa.val.v1.Datapoint
                // proto has no failure/invalid representation (only
                // timestamp + a value oneof), so INVALID -- like STALE --
                // means "stop feeding"; the last good value/timestamp is
                // left in place for consumers to judge freshness from.
                if (state.published_quality != Quality::kInvalid) {
                    LOG_ERROR << "key 0x" << std::hex << entry.key_id << std::dec
                              << " -> INVALID (type/range violation or server-tagged non-OK)" << std::endl;
                }
                state.consecutive_valid = 0;
                state.published_quality = Quality::kInvalid;
                continue;
            }

            state.consecutive_valid++;
            const bool was_ok = (state.published_quality == Quality::kOk);
            if (was_ok || state.consecutive_valid >= 2) {
                if (!was_ok) {
                    LOG_INFO << "key 0x" << std::hex << entry.key_id << std::dec
                             << " -> OK (recovered after 2 consecutive valid samples)" << std::endl;
                }
                state.published_quality = Quality::kOk;
                pending.push_back({entry.key_id, entry.value, entry.timestamp_ms});
            }
            // else: still recovering (1st valid sample after a gap) -- SYS-004
            // CDC-F-003 requires 2 consecutive valid samples before resuming.
        }
    }

    // Perform the (potentially blocking) Databroker feed calls outside the
    // lock, per SWARCH-002's "don't hold the lock during the network call"
    // convention already used in vehicle_state_service.
    for (const auto &p : pending) {
        publish_valid(p.key_id, p.value, p.timestamp_ms);
    }
}

int VehicleStateAdapter::on_someip_message(
            vsomeip::service_t service, vsomeip::instance_t instance, vsomeip::method_t method,
            const uint8_t *payload, size_t payload_length)
{
    if (service != kServiceId || instance != kInstanceId) {
        LOG_DEBUG << "Ignoring message from unrelated service/instance" << std::endl;
        return -1;
    }

    if (method == kEventId) {
        LkvMessage msg;
        if (!DecodeFixedVehicleStateMessage(payload, payload_length, msg)) {
            LOG_ERROR << "Failed to decode EvVehicleState payload!" << std::endl;
            return -2;
        }
        handle_event(msg);
        return 0;
    }
    if (method == kMethodWrite) {
        handle_write_response(payload, payload_length);
        return 0;
    }
    LOG_DEBUG << "Ignoring method 0x" << std::hex << method << std::dec << std::endl;
    return 0;
}

void VehicleStateAdapter::publish_valid(uint16_t key_id, const LkvValue &value, uint64_t timestamp_ms) {
    if (!databroker_feeder_) return;

    ::kuksa::val::v1::Datapoint dp;
    SetTimestamp(dp, timestamp_ms);

    switch (key_id) {
        case kKeySpeed: {
            float v = 0.0f; value.AsFloat32(v);
            dp.set_float_(v);
            databroker_feeder_->FeedValue(VSS_SPEED, dp);
            break;
        }
        case kKeyTacoMeter: {
            float v = 0.0f; value.AsFloat32(v);
            dp.set_float_(v);
            databroker_feeder_->FeedValue(VSS_ENGINE_SPEED, dp);
            break;
        }
        case kKeyReverse: {
            bool v = false; value.AsBool(v);
            // Gear is only ever set to -1 (Reverse) here; there is no
            // defined "not reverse" gear value to publish when false
            // (SYS-003 5 avoids a proprietary ReverseEngaged path).
            if (v) {
                dp.set_int32(-1);
                databroker_feeder_->FeedValue(VSS_GEAR, dp);
            }
            break;
        }
        case kKeyParkingBrake: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_PARKING_BRAKE, dp);
            break;
        }
        case kKeyAirconOnOff: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_AC_ACTIVE, dp);
            break;
        }
        case kKeyAirconTemperature: {
            float v = 0.0f; value.AsFloat32(v);
            dp.set_float_(v);
            databroker_feeder_->FeedValue(VSS_AC_TEMPERATURE, dp);
            break;
        }
        case kKeyAirconFan: {
            uint8_t v = 0; value.AsUint8(v);
            dp.set_uint32(static_cast<uint32_t>(std::lround(v * 100.0 / 7.0)));
            databroker_feeder_->FeedValue(VSS_AC_FAN_SPEED, dp);
            break;
        }
        case kKeyAirconDefrost: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_AC_DEFROST, dp);
            break;
        }
        case kKeyAirconMode: {
            uint8_t v = 0; value.AsUint8(v);
            std::string dist;
            if (v == 4) dist = "UP";
            else if (v == 5) dist = "DOWN";
            else if (v == 6) dist = "MIDDLE";
            if (!dist.empty()) {
                dp.set_string(dist);
                databroker_feeder_->FeedValue(VSS_AC_AIR_DISTRIBUTION, dp);
            } else {
                LOG_DEBUG << "AirconMode=" << static_cast<int>(v)
                          << " has no AirDistribution mapping (SYS-002 5.3); not publishing." << std::endl;
            }
            break;
        }
        case kKeyEscIsError: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_ESC_IS_ERROR, dp);
            break;
        }
        case kKeyAbsIsError: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_ABS_IS_ERROR, dp);
            break;
        }
        case kKeySeatIsBelted: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_SEAT_IS_BELTED, dp);
            break;
        }
        case kKeyDoorIsOpen: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_DOOR_IS_OPEN, dp);
            break;
        }
        case kKeyAirbagIsDeployed: {
            bool v = false; value.AsBool(v);
            dp.set_bool_(v);
            databroker_feeder_->FeedValue(VSS_AIRBAG_IS_DEPLOYED, dp);
            break;
        }
        case kKeyFuelRelativeLevel: {
            uint8_t v = 0; value.AsUint8(v);
            dp.set_uint32(v);
            databroker_feeder_->FeedValue(VSS_FUEL_LEVEL, dp);
            break;
        }
        default:
            break;
    }
}

void VehicleStateAdapter::quality_checker_th() {
    // Neither STALE nor INVALID has a Databroker-visible representation
    // (see handle_event) -- this thread only updates internal state and
    // logs the transition; the vendored proto's only observable signal is
    // that `timestamp` stops advancing, which is the SYS-002 5 / SYS-003 7
    // "consumer computes freshness from timestamp" mechanism already used
    // elsewhere in this spec family.
    while (quality_thread_running_) {
        std::vector<uint16_t> newly_invalid;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            const uint64_t now = NowMsWall();
            for (auto &kv : key_states_) {
                auto &state = kv.second;
                if (state.last_receive_ms == 0) continue;  // never received yet
                const auto thresholds = ThresholdsFor(kv.first);
                const uint64_t age = now - state.last_receive_ms;
                if (age >= thresholds.invalid_ms && state.published_quality != Quality::kInvalid) {
                    state.published_quality = Quality::kInvalid;
                    state.consecutive_valid = 0;
                    newly_invalid.push_back(kv.first);
                } else if (age >= thresholds.stale_ms && state.published_quality == Quality::kOk) {
                    state.published_quality = Quality::kStale;
                    LOG_INFO << "key 0x" << std::hex << kv.first << std::dec << " -> STALE" << std::endl;
                }
            }
        }
        for (uint16_t key_id : newly_invalid) {
            LOG_ERROR << "key 0x" << std::hex << key_id << std::dec << " -> INVALID (receive timeout)" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void VehicleStateAdapter::Start() {
    LOG_INFO << "Starting adapter..." << std::endl;
    if (databroker_feeder_) {
        feeder_thread_ = std::make_shared<std::thread>(&sdv::broker_feeder::DataBrokerFeeder::Run, databroker_feeder_);
        pthread_setname_np(feeder_thread_->native_handle(), "vs_broker_feed");
    }
    if (actuator_subscriber_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        subscriber_thread_ = std::make_shared<std::thread>(
            &sdv::broker_feeder::kuksa::ActuatorSubscriber::Run, actuator_subscriber_);
        pthread_setname_np(subscriber_thread_->native_handle(), "vs_target_sub");
    }
    if (someip_active_ && someip_client_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        someip_thread_ = std::make_shared<std::thread>(&sdv::someip::SomeIPClient::Run, someip_client_);
        pthread_setname_np(someip_thread_->native_handle(), "vs_someip_main");
    }
    quality_thread_running_ = true;
    quality_thread_ = std::make_shared<std::thread>(&VehicleStateAdapter::quality_checker_th, this);
    pthread_setname_np(quality_thread_->native_handle(), "vs_quality");

    feeder_active_ = true;
}

void VehicleStateAdapter::Shutdown() {
    std::lock_guard<std::mutex> its_lock(shutdown_mutex_);
    if (shutdown_requested_) return;
    shutdown_requested_ = true;
    feeder_active_ = false;

    quality_thread_running_ = false;
    if (quality_thread_ && quality_thread_->joinable()) {
        quality_thread_->join();
    }
    if (subscriber_thread_ && actuator_subscriber_) {
        actuator_subscriber_->Shutdown();
    }
    if (feeder_thread_ && databroker_feeder_) {
        databroker_feeder_->Shutdown();
    }
    if (someip_client_) {
        someip_client_->Shutdown();
        if (someip_thread_ && someip_thread_->joinable()) {
            if (someip_thread_->get_id() != std::this_thread::get_id()) {
                someip_thread_->join();
            } else {
                someip_thread_->detach();
            }
        }
    }
    if (feeder_thread_ && feeder_thread_->joinable()) {
        feeder_thread_->join();
    }
    if (subscriber_thread_ && subscriber_thread_->joinable()) {
        subscriber_thread_->join();
    }
}

}  // namespace adapter
}  // namespace sdv
