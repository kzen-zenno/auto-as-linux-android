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

#ifndef VEHICLE_STATE_IDS_H
#define VEHICLE_STATE_IDS_H

#include <array>
#include <cstdint>

// VehicleStateService SOME/IP contract constants.
// Source: specs/sys/SYS-002-someip.md, specs/swarch/SWARCH-002-someip-server.md

namespace sdv {
namespace someip {
namespace vehicle_state {

constexpr uint16_t kServiceId       = 0x1770;
constexpr uint16_t kInstanceId      = 0x0001;
constexpr uint8_t  kServiceMajor    = 0x01;
constexpr uint8_t  kServiceMinor    = 0x00;

constexpr uint16_t kEventId         = 0x8001;  // EvVehicleState
constexpr uint16_t kEventGroupId    = 0x0001;  // EventGroupVehicleState
constexpr uint32_t kEventCycleMs    = 100;

constexpr uint16_t kMethodSnapshot  = 0x0001;  // GetVehicleStateSnapshot
constexpr uint16_t kMethodWrite     = 0x0002;  // WriteVehicleState

constexpr uint16_t kSchemaVersion   = 0x0001;

// LkvEntry.value_type (SYS-002 5.2)
enum class ValueType : uint8_t {
    kBool    = 0x01,
    kUint8   = 0x02,
    kInt8    = 0x03,
    kFloat32 = 0x04,
    kString  = 0x05,
};

// LkvEntry.quality (SYS-002 5.2)
enum class Quality : uint8_t {
    kOk      = 0x00,
    kInvalid = 0x01,
    kStale   = 0x02,
};

// LkvEntry.confidence_level (SYS-002 5.2 / SYS-004 CDC-F-004)
enum class Confidence : uint8_t {
    kUnknown = 0,
    kLow     = 1,
    kMedium  = 2,
    kHigh    = 3,
};

// WriteResponse.result_code (SYS-002 5.2)
enum class WriteResultCode : uint8_t {
    kOk          = 0x00,
    kInvalid     = 0x01,
    kUnsupported = 0x02,
};

constexpr uint8_t kFailedEntryIndexNone = 0xFF;  // all entries valid

constexpr size_t kMaxEntries      = 16;
constexpr size_t kMaxSourceLength = 32;

constexpr const char* kDefaultSource = "Gateway ECU";

// Full key table (SYS-002 4.1). Keys 0x1006..0x100A are writable via
// WriteVehicleState (SYS-002 5.2 / SWARCH-002 4).
struct KeyDescriptor {
    uint16_t   key_id;
    ValueType  type;
    const char *name;
};

constexpr uint16_t kKeySpeed              = 0x1001;
constexpr uint16_t kKeyTacoMeter          = 0x1002;
constexpr uint16_t kKeyReverse            = 0x1004;
constexpr uint16_t kKeyParkingBrake       = 0x1005;
constexpr uint16_t kKeyAirconOnOff        = 0x1006;
constexpr uint16_t kKeyAirconTemperature  = 0x1007;
constexpr uint16_t kKeyAirconFan          = 0x1008;
constexpr uint16_t kKeyAirconDefrost      = 0x1009;
constexpr uint16_t kKeyAirconMode         = 0x100A;
constexpr uint16_t kKeyEscIsError         = 0x100C;
constexpr uint16_t kKeyAbsIsError         = 0x100D;
constexpr uint16_t kKeySeatIsBelted       = 0x100E;
constexpr uint16_t kKeyDoorIsOpen         = 0x100F;
constexpr uint16_t kKeyAirbagIsDeployed   = 0x1010;
constexpr uint16_t kKeyFuelRelativeLevel  = 0x1011;

// Writable range for WriteVehicleState (SYS-002 5.2).
constexpr uint16_t kWritableKeyMin = kKeyAirconOnOff;
constexpr uint16_t kWritableKeyMax = kKeyAirconMode;

constexpr std::array<KeyDescriptor, 15> kKeyTable{{
    {kKeySpeed,             ValueType::kFloat32, "Speed"},
    {kKeyTacoMeter,         ValueType::kFloat32, "TacoMeter"},
    {kKeyReverse,           ValueType::kBool,    "Reverse"},
    {kKeyParkingBrake,      ValueType::kBool,    "ParkingBrake"},
    {kKeyAirconOnOff,       ValueType::kBool,    "AirconOnOff"},
    {kKeyAirconTemperature, ValueType::kFloat32, "AirconTemperature"},
    {kKeyAirconFan,         ValueType::kUint8,   "AirconFan"},
    {kKeyAirconDefrost,     ValueType::kBool,    "AirconDefrost"},
    {kKeyAirconMode,        ValueType::kUint8,   "AirconMode"},
    {kKeyEscIsError,        ValueType::kBool,    "EscIsError"},
    {kKeyAbsIsError,        ValueType::kBool,    "AbsIsError"},
    {kKeySeatIsBelted,      ValueType::kBool,    "SeatIsBelted"},
    {kKeyDoorIsOpen,        ValueType::kBool,    "DoorIsOpen"},
    {kKeyAirbagIsDeployed,  ValueType::kBool,    "AirbagIsDeployed"},
    {kKeyFuelRelativeLevel, ValueType::kUint8,   "FuelRelativeLevel"},
}};

inline const KeyDescriptor* FindKey(uint16_t key_id) {
    for (const auto &k : kKeyTable) {
        if (k.key_id == key_id) return &k;
    }
    return nullptr;
}

inline bool IsWritableKey(uint16_t key_id) {
    return key_id >= kWritableKeyMin && key_id <= kWritableKeyMax
        && FindKey(key_id) != nullptr;
}

}  // namespace vehicle_state
}  // namespace someip
}  // namespace sdv

#endif  // VEHICLE_STATE_IDS_H
