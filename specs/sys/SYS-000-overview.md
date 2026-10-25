# SYS-000: FMW SYS Overall Structure

## 1. Purpose
This file shows the overall structure and responsibility split of the SYS document set in the FMW learning platform.

## 2. SYS Structure

| File | Role | Primary Owner |
|---|---|---|
| [SYS-001-can-signal.md](SYS-001-can-signal.md) | CAN signal physical definitions, cycle times, invalid values, timeouts | Vehicle signal foundation |
| [SYS-002-someip.md](SYS-002-someip.md) | SOME/IP transmission spec for the AUTOSAR-CP Gateway ECU | Gateway relay |
| [SYS-003-vss.md](SYS-003-vss.md) | Linux/VSS property spec | Vehicle state exposure |
| [SYS-004-cdcf-diff.md](SYS-004-cdcf-diff.md) | CDC-F diff table and finalized mapping to the 3 categories | Review integration |
| [SYS-005-vss-catalog-reference.md](SYS-005-vss-catalog-reference.md) | Verified real COVESA VSS catalog data (version, date, source) backing SYS-002/SYS-003 path names | Ground-truth reference |

## 3. Responsibility Split
- SYS-001 holds the primary definition of the signals.
- SYS-002 holds the relay contract from AUTOSAR-CP to Linux.
- SYS-003 holds the physical properties exposed from Linux to VSS.
- SYS-005 holds the verified real-world VSS catalog data SYS-003's path names must match.

## 4. Traceability Policy
- CAN signals originate from SYS-001.
- SOME/IP relay contracts are consolidated in SYS-002.
- VSS exposure contracts are consolidated in SYS-003.
- Finalization management of CDC-F diffs is consolidated in SYS-004, with mapping destinations integrated into SYS-001 / SYS-002 / SYS-003.
- VSS path names in SYS-002 / SYS-003 must be checked against SYS-005 before being finalized.
- Open items are consolidated and listed in this file rather than in each detail file.

## 5. Open Item Tracking

| ID | Target | Open Item | Priority | Status |
|---|---|---|---|---|
| SYS-000-R-002 | SYS overall | Final consistency check of SYS-002 / SYS-003 / SYS-004 references | High | Done |
| SYS-000-R-003 | SYS-002 / SYS-003 | Correct `Transmission.Gear` and per-station `IsAirConditioningActive` against SYS-005 | High | Done |
| SYS-001-R-001 | SYS-001 | Finalize the Reserved future-extension policy | Medium | In progress |
| SYS-004-R-001 | SYS-004 | Completion check for CDC-F diff implementation reflection | High | Done |

## 6. Update Rules
- SYS-001 is limited to the foundational signal information.
- SOME/IP changes are reflected in SYS-002.
- VSS changes are reflected in SYS-003.
- If a structural change is needed, update this file first.
