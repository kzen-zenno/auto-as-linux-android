# SWRS-Gateway-ECU: AUTOSAR-CP Gateway ECU Requirements Template

## 1. Purpose
Defines the requirements for the AUTOSAR-CP Gateway ECU to receive CAN signals and relay them to the Linux side via SOME/IP.

## 2. Reference Relationships
- Source SYS: [SYS-001-can-signal.md](../sys/SYS-001-can-signal.md), [SYS-002-someip.md](../sys/SYS-002-someip.md)
- Downstream SWRS: [SWRS-Linux-VehicleService.md](SWRS-Linux-VehicleService.md)

## 3. Scope of Responsibility
- Receiving CAN signals and converting them to physical values
- Mapping to SOME/IP events / methods
- Handling of invalid values, timeouts, and retransmission failures
- Conditions for returning command ACKs

## 4. SHALL Requirements Template

| Requirement ID | SHALL Statement | Acceptance Criteria | Reference |
|---|---|---|---|
| SWRS-GW-001 | The gateway ECU shall receive the CAN signal and publish the corresponding SOME/IP event. | Define normal case, abnormal case, and timeout. | SYS-001 / SYS-002 |
| SWRS-GW-002 | The gateway ECU shall mark the output as stale when the source signal exceeds the timeout threshold. | Define the quality and notification behavior on reception loss. | SYS-001 / SYS-002 |
| SWRS-GW-003 | The gateway ECU shall return an ACK or error state for command-oriented signals. | Define the 3 viewpoints of ACK success / failure / non-delivery. | SYS-001 / SYS-002 |
| SWRS-GW-004 | The gateway ECU shall map ESC/ABS/Seatbelt/DoorOpen/Airbag/LowFuel CAN frames to SOME/IP key IDs 0x100C..0x1011 without loss of semantic meaning. | Confirm key ID and value type agreement over 100 cases, with 0 mismatches. | SYS-001 / SYS-002 |
| SWRS-GW-005 | The gateway ECU shall apply timeout thresholds defined in SYS-001 per signal and propagate quality as OK/STALE/INVALID to SOME/IP payloads. | State transition must occur within threshold + allowed delay. | SYS-001 / SYS-002 |
| SWRS-GW-006 | The gateway ECU shall include quality, confidence, timestamp, and source for all published SOME/IP entries. | 0 missing mandatory metadata over 100 cases. | SYS-001 / SYS-002 |

## 5. Verification Viewpoints
- Normal case: does the path from CAN reception to SOME/IP transmission behave as expected?
- Abnormal case: is the handling of invalid values, communication loss, and payload anomalies defined?
- Timeout: does the state transition to stale / fallback occur on reception loss?

## 6. Update Notes
- This version assumes Service ID=0x1770, Event ID=0x8001, Method ID=0x0001/0x0002.
- Timeout/quality transitions use SYS-001's per-signal definitions as the canonical reference.

## 7. Implementation

`autoas-as/src/app/fmw` implements this template (see its `README.md`).
Functional verification performed so far: build succeeds; service discovery,
`EvVehicleState` (100 ms), and `GetVehicleStateSnapshot` round-tripped end to
end against a throwaway test client (`autoas-as/src/app/fmw/test_client`).
`WriteVehicleState` was verified with a 12-case matrix covering all 3
SWRS-GW-003 viewpoints (4 valid HVAC writes: ACK `result_code=0x00` with the
value confirmed actually applied on the CAN bus by decoding raw
`0x526 AirconCommand` traffic; 6 out-of-range/non-integral values and 2
non-HVAC keys: rejected with `result_code=0x01`/`0x02` and, per the raw CAN
capture, no bus traffic at all -- confirming the all-or-nothing rule leaves
the model/CAN untouched). This covers SWRS-GW-001, -GW-003, -GW-004, and
-GW-006. SWRS-GW-002/-GW-005 (timeout-threshold timing accuracy) and the full
SWRS-CDC-AllSignals threshold/timing matrix have not been separately
re-verified against this SOME/IP path (the underlying CAN-side quality/
confidence state machine in `fmw_signals.c` was already verified per
`docs/fmwapp.md`; only its relay into SOME/IP is new). Uses the fixed-length
LKV schema (SYS-002 §5.2 migration note), not the original TLV.
