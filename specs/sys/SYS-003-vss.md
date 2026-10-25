# SYS-003: VSS Property Specification (Draft v1)

## 1. Purpose
This specification defines the properties published from the Linux Vehicle Service to the VSS Server.

## 2. Scope
- Sender: Linux Vehicle Service
- Publish target: VSS Server (KUKSA)
- Consumers: Android VM / MET App / MM App

## 3. Property Definitions

> Standard COVESA VSS paths are preferred; items not present in the standard are handled as custom overlays within this specification.

| VSS Path | Type | Unit | Valid Range | Update Mode | Owner | Source | COVESA Compliance |
|---|---|---|---|---|---|---|---|
| Vehicle.Speed | float | km/h | 0.0..255.0 | 100 ms cycle | Shared | SYS-002 / SYS-001 Speed | Standard |
| Vehicle.Powertrain.CombustionEngine.Speed | float | rpm | 0..8000 | 50 ms cycle | Shared | SYS-002 / SYS-001 TacoMeter | Standard |
| Vehicle.Chassis.ParkingBrake.IsEngaged | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 ParkingBrake | Standard |
| Vehicle.Cabin.HVAC.IsAirConditioningActive | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 Aircon | Standard (vehicle-wide, not per-station; verified per SYS-005) |
| Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature | float | degC | 16.0..30.0 | 100 ms cycle | Shared | SYS-002 / SYS-001 Aircon | Standard |
| Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed | uint8 | percent | 0..100 (8 SOME/IP levels) | 100 ms cycle | Shared | SYS-002 / SYS-001 Aircon | Standard |
| Vehicle.Cabin.HVAC.Station.Row1.Driver.AirDistribution | string | - | UP / MIDDLE / DOWN | 100 ms cycle | Shared | SYS-002 / SYS-001 Aircon | Standard (Mode is mapped) |
| Vehicle.ADAS.ESC.IsError | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 ESC | Standard |
| Vehicle.ADAS.ABS.IsError | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 ABS | Standard |
| Vehicle.Cabin.Seat.*.*.IsBelted | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 Seatbelt | Standard (initial instance: `Row1.DriverSide`) |
| Vehicle.Cabin.Door.*.*.IsOpen | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 DoorOpen | Standard (initial instance: `Row1.DriverSide`) |
| Vehicle.Cabin.Seat.*.*.Airbag.IsDeployed | boolean | - | true/false | 100 ms cycle | Shared | SYS-002 / SYS-001 Airbag | Standard (initial instance: `Row1.DriverSide`) |
| Vehicle.Powertrain.FuelSystem.RelativeLevel | uint8 | percent | 0..100 | 500 ms cycle | Shared | SYS-002 / SYS-001 LowFuel | Standard |
| Vehicle.Powertrain.Transmission.CurrentGear | int8 | - | -1..+5, 0=Neutral, 126=Park, 127=Drive | 50 ms cycle | Shared | SYS-002 / SYS-001 Reverse / Shift state | Standard (verified path name per SYS-005) |

## 4. AirDistribution Specification
- `Vehicle.Cabin.HVAC.Station.Row1.Driver.AirDistribution` is a standard COVESA VSS signal representing airflow direction.
- The accepted values are `UP / MIDDLE / DOWN`, which map the Aircon mode's `Face / Foot / Face+Foot` to their closest semantic equivalents.
- The specific mapping is as follows:
  - `Face` -> `UP`
  - `Foot` -> `DOWN`
  - `Face+Foot` -> `MIDDLE`
- Note that since `AirDistribution` is represented as `UP/MIDDLE/DOWN` in the implementation, the UI interprets this value as auxiliary information for the Aircon mode.

## 5. Reverse Substitute Specification
- `Vehicle.Powertrain.Transmission.CurrentGear` is published as the standard shift information and used as a substitute for `Reverse`.
- Here, `CurrentGear = -1` is interpreted as equivalent to Reverse, and the UI uses it as a substitute display for the `Reverse` state.
- This avoids adding a proprietary `Vehicle.Powertrain.Transmission.ReverseEngaged` and instead expresses it in a form aligned with the COVESA standard.

## 6. Gear Value to UI Display Mapping

| CurrentGear Value | UI Display / Interpretation |
| --- | --- |
| -1 | Reverse |
| 0 | Neutral |
| 1 | 1st |
| 2 | 2nd |
| 3 | 3rd |
| 4 | 4th |
| 5 | 5th |
| 6 | 6th |
| 126 | Park |
| 127 | Drive |

- By default, `Vehicle.Powertrain.Transmission.CurrentGear` is published as the standard shift information.
- The UI displays this value as-is, interpreting `-1` as `Reverse` when needed.
- This conveys vehicle state using a standard gear representation instead of a proprietary `ReverseEngaged`.

## 7. Quality / Missing-Data Convention
- All properties have a `quality` attribute, taking values `OK / INVALID / STALE`.
- All published values carry `quality`, `freshness`, `confidence`, `timestamp`, and `source` as mandatory quality metadata.
- `confidence` has four levels: High=100, Medium=60, Low=40, and
  Unknown/Not-assessed=0. On the SOME/IP wire these are encoded as levels 3,
  2, 1, and 0 respectively.
- `freshness` indicates elapsed time since reception; `timestamp` holds the send time.
- `source` explicitly names the upstream source, e.g. `Gateway ECU`.
- Timeout follows the per-signal thresholds defined in SYS-001, transitioning to STALE.
- While Stale, the value is retained and `quality=STALE` is notified.
- Recovery requires "2 consecutive valid samples received" as the condition.

### 7.1 SOME/IP metadata and conversion supplement

The provider receives all values through the little-endian LKV payload defined
in SYS-002. `value_type` is checked against the VSS type before publication;
invalid type, range, or length data is marked `INVALID` and does not replace the
last valid value.

- `timestamp` is copied from LKV `timestamp_ms` and `freshness` is computed
  from receive time. `age_ms` is retained as upstream diagnostic data.
- `source` is copied from the LKV source field, with `Gateway ECU` as the
  default source for the server simulator.
- `Vehicle.Powertrain.Transmission.CurrentGear = -1` is the VSS
  representation of Reverse. No separate proprietary Reverse path is
  required.
- Aircon mode is converted to `AirDistribution` as `Face=UP`, `Foot=DOWN`,
  and `Face+Foot=MIDDLE`. Other numeric modes remain available to the UI.
- SOME/IP Fan has eight levels `0..7`. The VSS representation uses
  `round(level * 100 / 7)`, yielding `0%, 14%, 29%, 43%, 57%, 71%, 86%,
  100%` after rounding. VSS percentage writes map to the nearest SOME/IP
  level, with level 7 representing 100%.
- Aircon Defrost is a custom overlay and must have a configured path before it
  is exposed through KUKSA. The configured path is
  `Vehicle.Cabin.HVAC.Station.Row1.Driver.IsDefrosting` (boolean, actuator),
  registered by `vehicle_service/databroker/vss_fmw_overlay.json` which the
  databroker loads via its comma-separated `--vss` list (SYS-005 section 5).
- Wildcard Seat, Door, and Airbag paths require explicit configured instances;
  the provider must not infer an instance from the key alone. The initial
  profile configures a single instance, `Row1.DriverSide`, for all three;
  multi-instance publication is a future extension.

The SOME/IP event arrives every 100 ms over TCP. The provider keeps a latest
value cache and publishes each VSS signal at its configured period: 50 ms for
engine speed and gear, 100 ms for the standard body/HVAC/ADAS signals, and
500 ms for fuel level. A VSS publication cannot invent a newer timestamp; it
uses the latest received sample.

## 8. Warning Signal Comparison Against the VSS Standard
- `ESC` maps to the standard `Vehicle.ADAS.ESC.IsError`.
- `ABS` maps to the standard `Vehicle.ADAS.ABS.IsError`.
- `Seatbelt` is handled as `Vehicle.Cabin.Seat.*.*.IsBelted` to include the standard seat instance.
- `DoorOpen` is handled as `Vehicle.Cabin.Door.*.*.IsOpen` to include the standard door instance.
- `Airbag` is handled as `Vehicle.Cabin.Seat.*.*.Airbag.IsDeployed` to include the standard seat instance.
- `LowFuel` does not exist in the standard, so `Vehicle.Powertrain.FuelSystem.RelativeLevel` (remaining-level percentage) is adopted.

## 9. Update Contract
- Cyclically-updated properties are published at their defined cycle.
- Event-updated properties (future extension) are published only on value change.
- The result of a write request is reflected into the state properties after the ACK is received.

## 10. MET App, MM App Usage Assumptions
- MET-related displays always subscribe to `Vehicle.Speed`, `Vehicle.ADAS.ESC.IsError`, `Vehicle.ADAS.ABS.IsError`, `Vehicle.Cabin.Seat.*.*.IsBelted`, `Vehicle.Cabin.Door.*.*.IsOpen`, `Vehicle.Cabin.Seat.*.*.Airbag.IsDeployed`, and `Vehicle.Powertrain.FuelSystem.RelativeLevel`.
- HVAC / Display UIs read and write their corresponding properties; on failure, they notify the UI of the failure rather than retrying.

## 11. Trace
- SYS-001: CAN signal definitions (origin of the values)
- SYS-002: SOME/IP specification (relay contract)
- SYS-000: owner of the overall SYS structure and open-item tracking
- SYS-005: verified real VSS catalog data backing the path names in §3
- SWARCH-003: mapping of these VSS paths to AAOS VHAL properties
- SWRS-Linux-VehicleService / SWRS-Android-VehicleApp / SWRS-MET-UI: implementation requirements for VSS updates and quality management
