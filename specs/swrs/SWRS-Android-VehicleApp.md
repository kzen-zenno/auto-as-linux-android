# SWRS-Android-VehicleApp: Android Vehicle App Requirements Template

## 1. Purpose
Defines the requirements for the Android Vehicle App to subscribe to VSS properties and perform display, operation, and error notification.

## 2. Reference Relationships
- Source SYS: [SYS-003-vss.md](../sys/SYS-003-vss.md)
- Upstream SWRS: [SWRS-Linux-VehicleService.md](SWRS-Linux-VehicleService.md)

## 3. Scope of Responsibility
- Subscribing to VSS properties
- Updating screen display and missing-data UI behavior
- Sending write requests for write-capable properties such as HVAC
- Notifying the user on insufficient permissions or ACK failure

## 4. SHALL Requirements Template

| Requirement ID | SHALL Statement | Acceptance Criteria | Reference |
|---|---|---|---|
| SWRS-APP-001 | The Android app shall display subscribed VSS values within the allowed latency budget. | Define normal case, abnormal case, and timeout. | SYS-003 |
| SWRS-APP-002 | The Android app shall show a degraded state when VSS quality becomes INVALID or STALE. | Define the conditions for missing-data UI display. | SYS-003 |
| SWRS-APP-003 | The Android app shall handle write requests with explicit success or failure feedback. | Define ACK success / failure / non-delivery. | SYS-003 |

### 4.1 AAOS VHAL path (SWARCH-003)

When the app reads values through the AAOS VHAL (`CarPropertyManager`) instead
of subscribing to KUKSA directly:

- SWRS-APP-001 is satisfied by the VHAL property update; the mapping is in SWARCH-003 §3.
- SWRS-APP-002 is **not satisfied** through the VHAL: `STALE` is ignored and
  `INVALID` keeps the last valid value (SWARCH-003 §4.3). An app that needs the
  degraded display must subscribe to KUKSA directly.
- SWRS-APP-003: a rejected or unconfirmed HVAC write is reported as a property
  set error (`TRY_AGAIN` after 3 s without the state following).
- ESC/ABS `IsError` are not available through the VHAL.

## 5. Verification Viewpoints
- Normal case: does the UI update within the expected latency?
- Abnormal case: are insufficient permissions, subscription failure, and UI update breakdowns handled with defined behavior?
- Timeout: does the UI avoid showing an overly stale value while in the stale state?

## 6. Update Notes
- Fill in the screen items, subscription cycle, allowed display latency, and write-operation failure display.
