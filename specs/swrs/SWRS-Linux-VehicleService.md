# SWRS-Linux-VehicleService: Linux Vehicle Service Requirements Template

## 1. Purpose
Defines the requirements for the Linux Vehicle Service to receive SOME/IP and publish it as VSS properties.

## 2. Reference Relationships
- Source SYS: [SYS-002-someip.md](../sys/SYS-002-someip.md), [SYS-003-vss.md](../sys/SYS-003-vss.md)
- Upstream SWRS: [SWRS-Gateway-ECU.md](SWRS-Gateway-ECU.md)
- Downstream SWRS: [SWRS-Android-VehicleApp.md](SWRS-Android-VehicleApp.md)

## 3. Scope of Responsibility
- Receiving SOME/IP and maintaining state
- Conversion to VSS, publication, and quality propagation
- Resynchronization on reconnect
- Notification contract for UI-facing state changes

## 4. SHALL Requirements Template

| Requirement ID | SHALL Statement | Acceptance Criteria | Reference |
|---|---|---|---|
| SWRS-LV-001 | The Linux vehicle service shall receive SOME/IP updates and publish them to the corresponding VSS properties. | Define normal case, abnormal case, and timeout. | SYS-002 / SYS-003 |
| SWRS-LV-002 | The Linux vehicle service shall propagate quality information such as OK, INVALID, and STALE. | Define quality carry-over and handling on missing data. | SYS-002 / SYS-003 |
| SWRS-LV-003 | The Linux vehicle service shall support resynchronization after reconnect. | Define initial-value and redistribution handling after reconnect. | SYS-002 / SYS-003 |
| SWRS-LV-004 | The Linux vehicle service shall publish ESC/ABS/Seatbelt/DoorOpen/Airbag/LowFuel to their corresponding VSS paths defined by SYS-003. | Confirm VSS path and type agreement over 100 cases, with 0 mismatches. | SYS-002 / SYS-003 |
| SWRS-LV-005 | The Linux vehicle service shall preserve and publish quality, freshness, confidence, timestamp, and source for each VSS update. | 0 missing metadata over 100 cases. | SYS-002 / SYS-003 |
| SWRS-LV-006 | The Linux vehicle service shall continue MET-related publish paths when MM control paths are stopped, and shall localize MM failures to MM-related signals only. | MET update success rate >= 99.0% over a 30-minute MM-stop test, with 0 propagation to non-MM signals. | SYS-002 / SYS-003 |
| SWRS-LV-007 | The Linux vehicle service shall recover to normal quality only after two consecutive valid samples per signal after reconnect. | 0 false recoveries from a single valid reception in a brief-dropout recovery test. | SYS-002 / SYS-003 |

## 5. Verification Viewpoints
- Normal case: are received SOME/IP values correctly reflected in VSS?
- Abnormal case: is the handling of reception corruption, out-of-order delivery, and reconnect failure defined?
- Timeout: is the stale state correctly propagated to VSS?

## 6. Update Notes
- VSS paths and update cycles use SYS-003's definitions as the canonical reference.
- Recovery determination after reconnect applies the "2 consecutive valid samples received" rule.
