# SWRS-000: FMW SWRS Overall Structure

## 1. Purpose
This file shows the overall structure of the SWRS document set in the FMW learning platform and manages open items.

## 2. SWRS Structure

| File | Role | Primary Reference |
|---|---|---|
| [SWRS-Gateway-ECU.md](SWRS-Gateway-ECU.md) | Requirements template for the AUTOSAR-CP Gateway ECU | SYS-001 / SYS-002 |
| [SWRS-Linux-VehicleService.md](SWRS-Linux-VehicleService.md) | Requirements template for the Linux Vehicle Service | SYS-002 / SYS-003 |
| [SWRS-Android-VehicleApp.md](SWRS-Android-VehicleApp.md) | Requirements template for the Android Vehicle App | SYS-003 |
| [SWRS-MET-UI.md](SWRS-MET-UI.md) | Finalized requirements for the Linux/PyQt MET UI app (`metapp`, implemented) | SYS-001 / SYS-003 |
| [SWRS-CDC-AllSignals.md](SWRS-CDC-AllSignals.md) | Finalized SHALL statements and acceptance criteria for all C-DC signals | SYS-001 / SYS-002 / SYS-003 / SYS-004 |

## 3. Coverage
- Defines requirements that receive the CAN signals from SYS-001.
- Defines requirements that receive the SOME/IP contract from SYS-002.
- Defines requirements that receive the VSS property publication from SYS-003.
- Defines requirements corresponding to the CDC-F diffs from SYS-004.

## 4. Template Usage
- Requirements for the Gateway ECU start from [SWRS-Gateway-ECU.md](SWRS-Gateway-ECU.md).
- Requirements for the Linux side start from [SWRS-Linux-VehicleService.md](SWRS-Linux-VehicleService.md).
- Requirements for the Android side start from [SWRS-Android-VehicleApp.md](SWRS-Android-VehicleApp.md).
- Requirements for the Linux MET UI client (`metapp`) start from [SWRS-MET-UI.md](SWRS-MET-UI.md).

## 5. Verification Policy
- The 3 viewpoints of normal case, abnormal case, and timeout are mandatory.
- Acceptance criteria are managed tied to SHALL statements.
- If verification granularity is insufficient, it is consolidated as an open item in this file.

## 6. Open Item Tracking

| ID | Target | Open Item | Priority | Status |
|---|---|---|---|---|
| SWRS-000-R-001 | SWRS-Gateway-ECU | Fill in requirements for the Gateway ECU template | High | In progress (implemented in `autoas-as/src/app/fmw`; basic functional verification done, full SWRS-CDC-AllSignals timing matrix not yet re-verified against SOME/IP -- see SWRS-Gateway-ECU §7) |
| SWRS-000-R-002 | SWRS-Linux-VehicleService | Fill in requirements for the Linux template | High | In progress |
| SWRS-000-R-003 | SWRS-Android-VehicleApp | Fill in requirements for the Android template | High | In progress |
| SWRS-000-R-005 | SWRS-CDC-AllSignals | Reflect implementation and collect test evidence for 6 signals x 3 viewpoints | High | Done |
| SWRS-000-R-006 | SWRS-MET-UI | Fill in requirements for the MET UI client | High | Done (implemented in `metapp/`; fault-injection viewpoint against the full stack not yet formally exercised, see SWRS-MET-UI §6) |

## 7. Update Rules
- The body of the requirements specification is consolidated in each ECU/module template.
- Organization of the specification structure and open items is consolidated in this file.
- When adding a new requirement, first update the open-item list in this file.
