# SWRS-CDC-AllSignals: Requirements Specification for All C-DC Signals

## 1. Purpose
This specification defines SWRS SHALL statements and acceptance criteria for all C-DC MET/MM signals, from the 3 viewpoints of normal, abnormal, and timeout.

## 2. Scope
- Signals: Speed, TacoMeter, Aircon, Reverse, ParkingBrake, ESC, ABS, Seatbelt, DoorOpen, Airbag, LowFuel
- Constraint: minimal implementation for learning purposes

## 3. Common Assumptions
- quality states: OK / STALE / INVALID
- Recovery-confirmed condition: 2 consecutive valid samples received
- Quality thresholds:
  - High frequency (Speed, TacoMeter, Reverse): STALE 300ms, INVALID 1000ms
  - Medium frequency (ParkingBrake): STALE 500ms, INVALID 3000ms
  - Low frequency (Aircon, LowFuel): STALE 1500ms, INVALID 3000ms
  - Warning-related (ESC, ABS, Seatbelt, DoorOpen, Airbag): STALE 300ms, INVALID 1000ms

- Reverse publication canonical form:
  - The published canonical form is `Vehicle.Powertrain.Transmission.CurrentGear`, treating `CurrentGear=-1` as equivalent to Reverse (path verified per SYS-005).
  - Reverse(bool) is a derived interpretation for compatibility display.

## 4. SHALL List (11 signals x 3 viewpoints)

| SHALL ID | Signal | Viewpoint | SHALL Statement | Acceptance Criteria |
|---|---|---|---|---|
| SWRS-CDC-001 | Speed | Normal | The system shall update Speed within a 100ms cycle and maintain quality=OK. | Update-cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-002 | Speed | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an out-of-range Speed value. | INVALID transition within 100ms of abnormal-value injection |
| SWRS-CDC-003 | Speed | Timeout | The system shall transition to STALE after Speed is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-004 | TacoMeter | Normal | The system shall update TacoMeter within a 50ms cycle and maintain quality=OK. | Update-cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-005 | TacoMeter | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an out-of-range TacoMeter value. | INVALID transition within 50ms of abnormal-value injection |
| SWRS-CDC-006 | TacoMeter | Timeout | The system shall transition to STALE after TacoMeter is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-010 | Aircon | Normal | The system shall update Aircon state and maintain quality=OK while MM is operating. | Update-cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-011 | Aircon | Abnormal | While MM is stopped, the system shall degrade only Aircon and shall not propagate the effect to other signals. | 0 update stoppages among the other 5 signals during MM-stop testing |
| SWRS-CDC-012 | Aircon | Timeout | The system shall transition to STALE after Aircon is not updated for over 1500ms, and to INVALID after over 3000ms. | State transition within threshold+100ms |
| SWRS-CDC-013 | Reverse | Normal | The system shall update Reverse on a 50ms cycle or on state change, and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-014 | Reverse | Abnormal | The system shall transition to quality=INVALID upon receiving an invalid Reverse value. | INVALID transition within 50ms of invalid-value injection |
| SWRS-CDC-015 | Reverse | Timeout | The system shall transition to STALE after Reverse is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-016 | ParkingBrake | Normal | The system shall update ParkingBrake on a 100ms cycle or on state change, and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-017 | ParkingBrake | Abnormal | The system shall transition to quality=INVALID upon receiving an invalid ParkingBrake value. | INVALID transition within 100ms of invalid-value injection |
| SWRS-CDC-018 | ParkingBrake | Timeout | The system shall transition to STALE after ParkingBrake is not updated for over 500ms, and to INVALID after over 3000ms. | State transition within threshold+100ms |
| SWRS-CDC-019 | ESC | Normal | The system shall update ESC IsError within a 100ms cycle and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-020 | ESC | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an invalid ESC value. | INVALID within 100ms of abnormal-value injection |
| SWRS-CDC-021 | ESC | Timeout | The system shall transition to STALE after ESC is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-022 | ABS | Normal | The system shall update ABS IsError within a 100ms cycle and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-023 | ABS | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an invalid ABS value. | INVALID within 100ms of abnormal-value injection |
| SWRS-CDC-024 | ABS | Timeout | The system shall transition to STALE after ABS is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-025 | Seatbelt | Normal | The system shall update Seatbelt within a 100ms cycle and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-026 | Seatbelt | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an invalid Seatbelt value. | INVALID within 100ms of abnormal-value injection |
| SWRS-CDC-027 | Seatbelt | Timeout | The system shall transition to STALE after Seatbelt is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-028 | DoorOpen | Normal | The system shall update DoorOpen within a 100ms cycle and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-029 | DoorOpen | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an invalid DoorOpen value. | INVALID within 100ms of abnormal-value injection |
| SWRS-CDC-030 | DoorOpen | Timeout | The system shall transition to STALE after DoorOpen is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-031 | Airbag | Normal | The system shall update Airbag IsDeployed within a 100ms cycle and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-032 | Airbag | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an invalid Airbag value. | INVALID within 100ms of abnormal-value injection |
| SWRS-CDC-033 | Airbag | Timeout | The system shall transition to STALE after Airbag is not updated for over 300ms, and to INVALID after over 1000ms. | State transition within threshold+50ms |
| SWRS-CDC-034 | LowFuel | Normal | The system shall update LowFuel within a 500ms cycle and maintain quality=OK. | Cycle deviation rate under 1% over a 10-minute measurement |
| SWRS-CDC-035 | LowFuel | Abnormal | The system shall transition to quality=INVALID within 1 cycle upon receiving an invalid LowFuel value. | INVALID within 500ms of abnormal-value injection |
| SWRS-CDC-036 | LowFuel | Timeout | The system shall transition to STALE after LowFuel is not updated for over 1500ms, and to INVALID after over 3000ms. | State transition within threshold+100ms |

## 5. Cross-Cutting SHALL Statements

| SHALL ID | Viewpoint | SHALL Statement | Acceptance Criteria |
|---|---|---|---|
| SWRS-CDC-X01 | Availability | The system shall continue distributing MET-facing signals (Speed/TacoMeter/Gear/ParkingBrake/ESC/ABS/Seatbelt/DoorOpen/Airbag/LowFuel) even while MM is stopped. | MET update success rate >= 99.0% over a 30-minute MM-stop test |
| SWRS-CDC-X02 | Recovery | The system shall return to normal display within 300ms after receiving 2 consecutive valid samples upon recovery. | Recovery time within 300ms in a brief-dropout recovery test |
| SWRS-CDC-X03 | Auditability | The system shall record the reason for each quality transition (dropped/out-of-order/reconnection) in a log. | 0 missing log entries over a 100-case test |

## 6. Traceability
- Upstream specifications: SYS-001 / SYS-002 / SYS-003
- Ruling basis: docs/reviews/20260802-cdc-met-mm-all-signals-review.md
- Finding IDs: CDC-F-001, CDC-F-003..008
