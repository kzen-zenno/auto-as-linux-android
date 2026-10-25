# SYS-001: FMW CAN Signal Specification (Draft v3)

## 1. Overview
This specification defines the CAN signals for the fictional learning-purpose OEM "FMW".
It uses 11-bit standard IDs and transmits vehicle status and MM commands over 12 independent frames (each DLC=8).

Signals in scope:
- Speed
- TacoMeter
- Aircon
- AirconCommand (MM)
- Reverse
- ParkingBrake
- ESC
- ABS
- Seatbelt
- DoorOpen
- Airbag
- LowFuel

## 2. CAN Frame Definitions

| CAN ID (hex) | Signal Name | DLC | Cycle | Byte/Bit Layout | Raw Range / Physical Conversion | Initial Value | Timeout Behavior |
|---|---|---:|---|---|---|---|---|
| 0x120 | Speed | 8 | 100 ms | Byte0-1: VehicleSpeedRaw (uint16, LE), Byte2-7: Reserved=0x00 | Raw: 0..65534, 0xFFFF=Invalid. Phys[km/h] = Raw x 0.01. Operational valid range: 0.00..255.00 km/h | 0x0000 (0.00 km/h) | Stale if not received for 300 ms. Display holds the last value, then falls back to 0 after 2 seconds |
| 0x121 | TacoMeter | 8 | 50 ms | Byte0-1: EngineSpeedRaw (uint16, LE), Byte2-7: Reserved=0x00 | Raw: 0..32000, 0xFFFF=Invalid. Phys[rpm] = Raw x 0.25. Valid range: 0..8000 rpm | 0x0000 (0 rpm) | Stale if not received for 150 ms. The cluster needle returns to 0 and a fault flag is raised |
| 0x323 | Aircon | 8 | 100 ms | Byte0 bit0: AC_OnOff, bit1: Defrost, bit2-4: Mode(0=Auto,1=Cool,2=Heat,3=Dry,4=Face,5=Foot,6=FaceFoot,7=Reserved), bit5-7: Reserved. Byte1: FanLevel(0..7). Byte2: TargetTempRaw. Byte3-7: Reserved=0x00 | TargetTemp[degC] = 16.0 + (Raw x 0.5), Raw 0..28 -> 16.0..30.0 degC. 0xFF=Invalid | AC=0, Defrost=0, Mode=0(Auto), Fan=0, TempRaw=8 (20.0 degC) | Aircon display is greyed out if not received for 300 ms. State is treated as Unknown |
| 0x526 | AirconCommand (MM) | 8 | 100 ms | Byte0 bit0: MM_AC_Cmd, bit1: MM_Temp_Cmd, bit2: MM_Fan_Cmd, bit3: MM_Def_Cmd, bit4-7: Reserved. Byte1: CmdFanLevel(0..7). Byte2: CmdTargetTempRaw. Byte3 bit0: CmdAC_OnOff, bit1: CmdDefrost, bit2-4: CmdMode(0=Auto,1=Cool,2=Heat,3=Dry,4=Face,5=Foot,6=FaceFoot,7=Reserved), bit5-7: Reserved. Byte4-7: Reserved=0x00 | CmdTargetTemp[degC] = 16.0 + (Raw x 0.5), Raw 0..28 -> 16.0..30.0 degC. 0xFF=NoUpdate | MM_AC_Cmd=0, MM_Temp_Cmd=0, MM_Fan_Cmd=0, MM_Def_Cmd=0 | New commands are invalidated if not received for 300 ms. The Aircon status frame (0x323) is treated as authoritative |
| 0x224 | Reverse | 8 | 50 ms | Byte0 bit0: ReverseState(0=Not engaged,1=Engaged), Byte0 bit1-7: Reserved. Byte1-7: Reserved=0x00 | Raw: {0,1}, any other value is Invalid | 0 (Not engaged) | Unknown if not received for 150 ms. Reverse-linked features (camera/guide lines) are hidden fail-safe |
| 0x225 | ParkingBrake | 8 | 100 ms | Byte0 bit0: ParkingBrakeState(0=Released,1=Applied), bit1: AutoHoldActive, bit2-7: Reserved. Byte1-7: Reserved=0x00 | ParkingBrakeState Raw: {0,1}. AutoHoldActive Raw: {0,1}. Any other value is Invalid | 0 (Released), AutoHold=0 | Unknown if not received for 300 ms. Launch-inhibit logic transitions to the conservative side (equivalent to Brake Applied) |
| 0x226 | ESC | 8 | 100 ms | Byte0 bit0: ESC_IsError(0=NoError,1=Error), bit1-7: Reserved. Byte1-7: Reserved=0x00 | Raw: {0,1}, any other value is Invalid | 0 (NoError) | Unknown if not received for 300 ms. The stability-control fault indicator is forced ON (fail-safe) |
| 0x227 | ABS | 8 | 100 ms | Byte0 bit0: ABS_IsError(0=NoError,1=Error), bit1-7: Reserved. Byte1-7: Reserved=0x00 | Raw: {0,1}, any other value is Invalid | 0 (NoError) | Unknown if not received for 300 ms. The braking fault indicator is forced ON (fail-safe) |
| 0x228 | Seatbelt | 8 | 100 ms | Byte0 bit0: Row1.DriverSide.IsBelted, bit1: Row1.PassengerSide.IsBelted, bit2: Row2.DriverSide.IsBelted, bit3: Row2.PassengerSide.IsBelted, bit4-7: Reserved. Byte1-7: Reserved=0x00 | Raw bit: 0=Unbelted, 1=Belted | 0x00 (all seats Unbelted) | Unknown if not received for 300 ms. The UI shows per-seat state as indeterminate |
| 0x229 | DoorOpen | 8 | 100 ms | Byte0 bit0: Row1.DriverSide.IsOpen, bit1: Row1.PassengerSide.IsOpen, bit2: Row2.DriverSide.IsOpen, bit3: Row2.PassengerSide.IsOpen, bit4-7: Reserved. Byte1-7: Reserved=0x00 | Raw bit: 0=Closed, 1=Open | 0x00 (all doors Closed) | Unknown if not received for 300 ms. The UI shows door-open state as indeterminate |
| 0x22A | Airbag | 8 | 100 ms | Byte0 bit0: Row1.DriverSide.Airbag.IsDeployed, bit1: Row1.PassengerSide.Airbag.IsDeployed, bit2: Row2.DriverSide.Airbag.IsDeployed, bit3: Row2.PassengerSide.Airbag.IsDeployed, bit4-7: Reserved. Byte1-7: Reserved=0x00 | Raw bit: 0=NotDeployed, 1=Deployed | 0x00 (all seats NotDeployed) | Unknown if not received for 300 ms. Safety-related display is forced ON (fail-safe) |
| 0x22B | LowFuel | 8 | 500 ms | Byte0: FuelRelativeLevel(0..100), Byte1-7: Reserved=0x00 | Raw: 0..100 [%], 0xFF=Invalid | 100 | Stale if not received for 1500 ms. The fuel-level display holds the last value, then shows indeterminate |

## 3. CDC-F Mapping Policy
- The quality state of the 6 signals is unified as `OK / INVALID / STALE`, handled as quality metadata via `quality`, `freshness`, `confidence`, `timestamp`, and `source`.
- `confidence` has four levels: Unknown/Not-assessed=0, Low=40, Medium=60,
	and High=100. The encoding is defined by SYS-002.
- Recovery determination requires "2 consecutive valid samples" as a mandatory condition; a single successful reception is not treated as recovery.
- The threshold policy is, as a baseline: high-frequency signals 300 ms / 1000 ms, medium-frequency signals 500 ms / 3000 ms, low-frequency control reports 1500 ms / 3000 ms.

## 3.1 VSS Mapping Paths (for learning purposes)
- ESC -> `Vehicle.ADAS.ESC.IsError`
- ABS -> `Vehicle.ADAS.ABS.IsError`
- Seatbelt -> `Vehicle.Cabin.Seat.*.*.IsBelted`
- DoorOpen -> `Vehicle.Cabin.Door.*.*.IsOpen`
- Airbag -> `Vehicle.Cabin.Seat.*.*.Airbag.IsDeployed`
- LowFuel -> `Vehicle.Powertrain.FuelSystem.RelativeLevel`

## 3. Implementation Notes

### 3.1 Endianness Assumption
- Multi-byte values assume Intel (Little Endian).
- Bit numbering within each byte is LSB=bit0, MSB=bit7.

### 3.2 Invalid Value Handling Convention
- uint16-family signals reserve `0xFFFF` as Invalid.
- For enum/boolean/bitmask signals, a value outside the defined set is reported upward as `Invalid`.
- If a Reserved bit is 1, it is recorded as a protocol anomaly and treated as a DTC candidate.

### 3.3 Cycle Time Rationale
- Speed (100 ms): a good balance between meter responsiveness and bus load.
- TacoMeter (50 ms): engine speed changes quickly, so a high frequency is used to minimize perceived lag.
- Aircon (100 ms): unified at 100 ms to prioritize UI responsiveness and consistency with MM coordination.
- AirconCommand (100 ms): sent at 100 ms to minimize control latency from the MM.
- Reverse (50 ms): high frequency to minimize delay in reverse-linked display/camera activation.
- ParkingBrake (100 ms): safety-related but changes infrequently, so medium frequency is appropriate.

## 4. Decode Pseudocode Example (Speed: 0x120)

```c
// Input: can_id (11-bit), dlc, data[8]
if (can_id == 0x120 && dlc == 8) {
	uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8); // little endian

	if (raw == 0xFFFF) {
		speed_status = SIGNAL_INVALID;
	} else {
		float speed_kph = (float)raw * 0.01f;

		if (speed_kph < 0.0f || speed_kph > 255.0f) {
			speed_status = SIGNAL_INVALID;
		} else {
			vehicle_speed_kph = speed_kph;
			speed_status = SIGNAL_OK;
			last_speed_rx_ms = now_ms;
		}
	}
}

// timeout supervision (called periodically)
if ((now_ms - last_speed_rx_ms) > 300) {
	speed_status = SIGNAL_STALE;
}
```

## 5. Finalized Items (This Version)
- The Speed upper bound is clamped to 255 km/h, common across OEMs.
- Aircon extends Mode so that AC/Temp/Fan/Defrost can be controlled independently.
- The Reverse/ParkingBrake fail-safe policy is formally traced to the vehicle functional safety requirements.

## 6. Functional Safety Traceability Policy (Reverse/ParkingBrake)
- On Reverse (0x224) timeout, treat as Unknown, and disable reverse-linked features on the safe side.
- On ParkingBrake (0x225) timeout, treat as Unknown, and transition launch inhibit to the conservative state.
- These 2 items are managed with trace IDs assigned to the SWRS and safety requirements (Safety Goals / TSR).
- At implementation time, record Timeout/Invalid occurrences as diagnostic events, subject to DTC management.
