# SYS-002: AUTOSAR-CP Gateway ECU SOME/IP Specification (Draft v1)

## 1. Purpose
This specification defines the SOME/IP interface sent from the AUTOSAR-CP Gateway ECU to the Linux Vehicle Service.

## 2. Scope
- Sender: AUTOSAR-CP Gateway ECU
- Receiver: Linux Vehicle Service
- Target signals: the vehicle status signals defined in SYS-001

## 3. Service Definition

### 3.1 Revision: Common LKV / Multi-list / Bidirectional Communication Form
- In the previous definition, state distribution and control requests were handled together, blurring the responsibility separation for SOME/IP, so Event and Method are now clearly separated.
- State distribution is handled as an Event; write requests are handled as a Method.
- The sender sends multiple data items at once in a payload with an `entries[]` array, and the receiver receives them in the same form.
- Each entry has `keyId`, `value`, `quality`, `confidence`, `timestamp`, and `source`.
- The MET / MM distinction is abolished; all signals are handled in a common LKV (Last Known Value) space.
- Directions are as follows:
  - Gateway ECU -> Linux: vehicle status is distributed as an Event.
  - Linux -> Gateway ECU: a generic write request is sent as a Method.
  - Gateway ECU -> Linux: the result is returned via the Method's Response.

| Item | Value | Notes |
|---|---|---|
| Service Name | VehicleStateService | Logical name for learning purposes |
| Service ID | 0x1770 | Hexadecimal notation |
| Instance ID | 0x0001 | Single instance |
| Major Version | 1 | For compatibility management |
| Minor Version | 0 | For compatibility management |
| Transport | TCP | All Events and Methods use reliable transport |

## 4. Event/Method Definitions

### 4.1 KEY / VALUE Definition List

| Signal | Key ID | Value Type | Unit | Range/Valid Values | Usage |
|---|---:|---|---|---|---|
| Speed | 0x1001 | float | km/h | 0.0..255.0 | State distribution / snapshot |
| TacoMeter | 0x1002 | float | rpm | 0..8000 | State distribution / snapshot |
| Reverse | 0x1004 | bool | - | true/false | State distribution / snapshot |
| ParkingBrake | 0x1005 | bool | - | true/false | State distribution / snapshot |
| Aircon ON/OFF | 0x1006 | bool | - | true/false | State distribution / write request / response notification |
| Aircon Temperature | 0x1007 | float | degC | 16.0..30.0 | State distribution / write request / response notification |
| Aircon Fan | 0x1008 | uint8 | - | 0..7 | State distribution / write request / response notification |
| Aircon Defrost | 0x1009 | bool | - | true/false | State distribution / write request / response notification |
| Aircon Mode | 0x100A | uint8 | - | 0..6 (0=Auto, 1=Cool, 2=Heat, 3=Dry, 4=Face, 5=Foot, 6=Face+Foot) | State distribution / write request / response notification |
| ESC IsError | 0x100C | bool | - | true/false | State distribution / snapshot |
| ABS IsError | 0x100D | bool | - | true/false | State distribution / snapshot |
| Seat IsBelted | 0x100E | bool | - | true/false | State distribution / snapshot |
| Door IsOpen | 0x100F | bool | - | true/false | State distribution / snapshot |
| Airbag IsDeployed | 0x1010 | bool | - | true/false | State distribution / snapshot |
| Fuel RelativeLevel | 0x1011 | uint8 | percent | 0..100 | State distribution / snapshot |

### 4.1.1 VSS Mapping Paths

| Signal | VSS Path |
|---|---|
| Speed | Vehicle.Speed |
| TacoMeter | Vehicle.Powertrain.CombustionEngine.Speed |
| Reverse | Vehicle.Powertrain.Transmission.CurrentGear (interpret `-1` as equivalent to Reverse; verified path name per SYS-005) |
| ParkingBrake | Vehicle.Chassis.ParkingBrake.IsEngaged |
| Aircon ON/OFF | Vehicle.Cabin.HVAC.IsAirConditioningActive (vehicle-wide, not per-station; verified path name per SYS-005) |
| Aircon Temperature | Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature |
| Aircon Fan | Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed |
| Aircon Defrost | `Vehicle.Cabin.HVAC.Station.Row1.Driver.IsDefrosting` (no standard equivalent; registered by the FMW databroker overlay, see SYS-005 section 5) |
| Aircon Mode | Vehicle.Cabin.HVAC.Station.Row1.Driver.AirDistribution (Face/Foot/Face+Foot mapped approximately) |
| ESC IsError | Vehicle.ADAS.ESC.IsError |
| ABS IsError | Vehicle.ADAS.ABS.IsError |
| Seat IsBelted | Vehicle.Cabin.Seat.*.*.IsBelted (initial instance: `Row1.DriverSide`) |
| Door IsOpen | Vehicle.Cabin.Door.*.*.IsOpen (initial instance: `Row1.DriverSide`) |
| Airbag IsDeployed | Vehicle.Cabin.Seat.*.*.Airbag.IsDeployed (initial instance: `Row1.DriverSide`) |
| Fuel RelativeLevel | Vehicle.Powertrain.FuelSystem.RelativeLevel |

### 4.2 Send/Receive Interfaces

| Type | Name | ID | Cycle/Trigger Condition | Payload | Transport |
|---|---|---|---|---|---|
| Event | EvVehicleState | 0x8001 | 100 ms cycle | List<LKV> of vehicle state entries | TCP |
| Method | GetVehicleStateSnapshot | 0x0001 | On-demand | Request/Response-style List<LKV> snapshot | TCP |
| Method | WriteVehicleState | 0x0002 | On-demand | List<LKV>-style generic write request | TCP |
| EventGroup | EventGroupVehicleState | 0x0001 | - | EvVehicleState | - |

## 5. Payload Conversion Convention
- When converting CAN -> SOME/IP, store the value with SYS-001's physical-unit conversion applied into each element of `entries[]`.
- One message carries multiple signals as a single List<LKV> for send/receive; each entry has a `keyId` and `value`.
- All signals use the common `0x10xx`-series fixed IDs, sharing the same key space across state distribution, snapshot, write request, and response notification.
- Invalid values are tagged with `quality=INVALID`; the value itself may retain the last valid value.
- On timeout detection, include `quality=STALE` and `age_ms`.
- All Events / Methods must propagate `quality`, `confidence`, `timestamp`, and `source`. `freshness` is not mandatory since the receiver can compute it from `timestamp`.
- TTL is based on twice the event cycle by default; re-discovery/re-subscription resumption conditions are based on the number of missed events and the recovery condition. The SOME/IP-SD TTL field is a whole-seconds count, so this rounds up to the nearest whole second; for the 100 ms `EvVehicleState` cycle the configured value is 1 second (see SWARCH-002 §2, §7).
- On success of WriteVehicleState, return the result via Response, then issue an MM state-update event afterward to notify the applied state.

### 5.1 Transport

All interfaces in this specification use TCP. This applies to
`EvVehicleState`, `GetVehicleStateSnapshot`, and `WriteVehicleState`.
The event remains a periodic notification, but it is delivered through the
reliable TCP connection established by SOME/IP Service Discovery. No UDP or
multicast event endpoint is required.

### 5.1.1 Version fields (normative)

The Major Version of §3 (= 1) is not merely documentation; both peers must
put it on the wire, in two distinct places:

1. **SD OFFER / SUBSCRIBE entries.** The offering side must set the entry's
   major version to 1. A stack that offers 0 while the consumer requests 1
   is not interoperable: vsomeip only wires up a remote service when the
   offered major equals the requested one, equals its `DEFAULT_MAJOR`, or
   the consumer asked for `ANY_MAJOR` — and vsomeip ships with
   `DEFAULT_MAJOR` patched to `0x01` ("works better with autosar"), so an
   offered 0 matches nothing and the consumer's availability callback never
   fires, silently. `Sd.c` on the Gateway ECU side applies the mirror-image
   check to incoming SUBSCRIBE entries.
2. **The SOME/IP message header's Interface Version field.** It must equal
   the Major Version (1) on every request, response, and notification.
   The Gateway ECU dispatches a method only when the request's interface
   version matches the configured one, so a consumer that leaves the field
   at its library default (vsomeip's `create_request()` leaves it 0) has its
   requests dropped without a response.

Both of these were real defects found during bring-up; see
`autoas-as/src/app/fmw/README.md` §11.3.1 and §11.3.2.

### 5.2 LKV payload (fixed-length schema)

**Revision note**: this section originally specified a self-describing
variable-length TLV (a `value_type` tag plus a `value_length`-prefixed value
and a `source_length`-prefixed string per entry). The AUTOSAR-CP Gateway ECU
implementation (`autoas-as`) uses this project's native `SomeIp`/`SomeIpXf`
code generator, which requires every struct field to have a fixed wire size;
a per-entry variable-length tagged union does not fit that generator. The
payload was therefore revised to a **fixed-length `LkvEntry` struct inside a
bounded array** (a runtime entry count plus a fixed maximum capacity), which
the generator natively supports. This is a breaking wire-format change; see
the migration note at the end of this section.

All multi-byte integers use little-endian byte order. Floating-point values
use IEEE 754 binary32. The same `LkvEntry` layout is used for the event,
snapshot response, write request, and applied-state response entries.

```text
LkvEntry (fixed, 20 bytes)
  uint16  key_id
  uint8   quality              // 0=OK, 1=INVALID, 2=STALE
  uint8   confidence_level     // 0..3; 0=unknown, 1=low, 2=medium, 3=high
  uint64  timestamp_ms
  uint32  age_ms
  float32 value                // IEEE 754 binary32; see "Value representation" below

VehicleStateMessage
  uint16   schema_version      // 0x0001
  LkvEntry entries[..16]       // runtime count 0..16 (bounded array)

WriteRequest
  LkvEntry entries[..5]        // runtime count 0..5 (bounded array); keys 0x1006..0x100A only

WriteResponse
  uint8    result_code         // 0x00=OK, 0x01=INVALID, 0x02=UNSUPPORTED
  uint8    failed_entry_index  // 0xFF when all entries are valid
  LkvEntry entries[..5]        // applied_state; populated only when result_code is 0x00
```

Compared to the original TLV:

- `value_type` is removed from the wire. `key_id` alone determines the value's
  semantic type via the KEY table in §4.1; a value that is conceptually
  `bool` or an integer is still carried in the `float32 value` field (see
  "Value representation" below).
- `source` (a per-entry UTF-8 string) is removed from the wire. This
  specification still requires a source to be attributable per SWRS-GW-006;
  the Gateway ECU implementation uses one fixed, implicit value
  (`"Gateway ECU"`) for every entry it produces instead of repeating it on
  the wire.
- `entry_count` is no longer a separate field placed before the array; each
  bounded array carries its own runtime count (0..16 for `VehicleStateMessage`,
  0..5 for `WriteRequest`/`WriteResponse`), capped at the fixed capacity shown
  above. A count exceeding the capacity is rejected, and the complete message
  is rejected when any entry is malformed -- a write request must not
  partially update the model.

**Value representation**: `bool` is encoded as `0.0` (false) or `1.0` (true);
`uint8`/`int8` values (SYS-002 §4.1: `Aircon Fan`, `Aircon Mode`,
`Fuel RelativeLevel`) are encoded as their integer value cast to `float32`,
which is exact for every value these keys can take (0..255). `float32` keys
(`Speed`, `TacoMeter`, `Aircon Temperature`) are carried as-is.

`quality` is `0x00=OK`, `0x01=INVALID`, and `0x02=STALE`.
`confidence_level` is an integer from 0 to 3. The corresponding VSS confidence
percentage is `0=Unknown/Not-assessed`, `1=Low (40%)`, `2=Medium (60%)`, and
`3=High (100%)`. These four values are the confidence levels used by all
related specifications.

`WriteVehicleState` accepts a `WriteRequest` containing only keys `0x1006`
through `0x100A`. `GetVehicleStateSnapshot` returns a `VehicleStateMessage`
directly; the request is empty (no arguments).

**Migration note (open item)**: this revision applies to the `autoas-as`
Gateway ECU implementation. The Linux-side implementation
(`kuksa-someip-provider`'s `lkv_codec`/`vehicle_state_service`/`someip-server`
mock, SWARCH-001/SWARCH-002) still implements the original variable-length
TLV described above and has not been updated to match; the two are currently
wire-incompatible. See SYS-000 §5 / SWARCH-000 §9 for open-item tracking.

### 5.3 VSS conversion supplement

Each `LkvEntry.value` must be interpreted using the type given for its
`key_id` in the §4.1 KEY table (the wire no longer carries a `value_type`
tag; see §5.2's migration note) and must match the VSS type in SYS-003. The
provider applies these conversions before publication:

- `0x1004 Reverse` is represented by VSS `CurrentGear=-1`; the reverse
  boolean is derived from that value and is not published as a proprietary
  VSS signal.
- `0x100A Aircon Mode` maps `Face` to `UP`, `Foot` to `DOWN`, and `Face+Foot`
  to `MIDDLE` for VSS `AirDistribution`.
- `0x1008 Aircon Fan` retains the eight SOME/IP levels `0..7`. For the VSS
  percentage representation, level `n` is published as
  `round(n * 100 / 7)`, yielding `0%, 14%, 29%, 43%, 57%, 71%, 86%, 100%`.
  VSS percentage writes are rounded to the nearest SOME/IP level; level 7
  represents 100%.
- `0x1009 Aircon Defrost` is a custom overlay because SYS-003 has no standard
  VSS path for it.
- Seat, door, and airbag wildcard paths require a configured VSS instance;
  `key_id` alone does not identify an instance. The initial profile configures
  a single instance, `Row1.DriverSide`, for all three; multi-instance
  publication is a future extension.

The provider copies `timestamp_ms` to VSS `timestamp`, calculates freshness
from receive time, retains the last value for `STALE`, and does not overwrite a
newer value with an older timestamp. VSS publication follows SYS-003 update
periods; a single SOME/IP event may update the latest cache while each VSS
signal is emitted at its own configured period.

## 6. QoS / Error Handling
- The receiver re-subscribes after 3 consecutive missed EvVehicleState cycles.
- On re-subscription failure, transition to a degraded state and notify the UI of the last confirmed valid value.
- If Service Discovery fails to establish, re-discover at a 1-second cycle.
- Recovery requires "2 consecutive valid samples received" as the condition; a single successful reception does not count as recovery.
- The audit log records drops, out-of-order delivery, delays, and recovery failures to ensure traceability during operation.

## 7. Trace
- SYS-001: source of the corresponding CAN signal definitions
- SYS-000: owner of the overall SYS structure and open-item tracking
- SYS-003: conversion target into VSS property definitions
- SYS-005: verified real VSS catalog data backing the path names in §4.1.1 and §5.3
- SWRS-Gateway-ECU / SWRS-Linux-VehicleService: conversion target into implementation and verification requirements
