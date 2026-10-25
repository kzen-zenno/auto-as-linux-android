# SYS-004: CDC-F Diff Table (Finalized)

## 1. Purpose
This specification manages the ruling results finalized in the C-DC MET/MM (all signals) review in CDC-F format, and clarifies the mapping destination into the 3 categories (signal specification / communication contract / requirements specification).

## 2. Scope
- Target feature: C-DC MET/MM
- Target signals: Speed, TacoMeter, Aircon (including 8-level Fan), Reverse, ParkingBrake
- Input source: docs/reviews/20260802-cdc-met-mm-all-signals-review.md

## 3. CDC-F Diff Table

| Finding ID | Ruling | Target Category | Change Type | Priority | Verification Method |
|---|---|---|---|---|---|
| CDC-F-001 | Unify the stale threshold per signal type (high-frequency 300ms/1000ms, medium-frequency 500ms/3000ms, low-frequency 1500ms/3000ms). | Requirements specification | update | High | Measure transition time via threshold-boundary testing |
| CDC-F-003 | Make "2 consecutive valid samples received" a mandatory condition for recovery determination. | Communication contract | update | High | Measure false-recovery rate via brief-dropout recovery testing |
| CDC-F-004 | confidence uses four levels (100/60/40/0), where 0 means Unknown/Not-assessed. | Signal specification | add | Medium | Confirm quality state transitions match value agreement rate |
| CDC-F-005 | Continue MET signal distribution even while MM is stopped; localize Aircon faults. | Requirements specification | add | High | Confirm no propagation via MM-stop injection testing |
| CDC-F-006 | Explicitly document Service/Instance/Method/Event, TCP transport, cycle, TTL, timeout, re-discovery, re-subscription, recovery conditions, and the little-endian LKV binary payload in the SOME/IP contract. | Communication contract | update | High | E2E communication test, payload codec test, reconnection test |
| CDC-F-007 | Require quality/freshness/confidence/timestamp/source in the VSS contract, including the documented Reverse, AirDistribution, and Fan conversions. | Signal specification | update | High | VSS property consistency and conversion tests |
| CDC-F-008 | Uniquely define quality transitions for out-of-order/dropped/delayed cases, and require an audit log. | Requirements specification | update | High | Log verification under drop/out-of-order injection |
| CDC-F-009 | Map the eight SOME/IP Fan levels (`0..7`) to VSS percentage using `round(level * 100 / 7)`; level 7 represents 100%. | Signal specification | add | High | Verify all eight level mappings and nearest-level write conversion |

## 4. Completion Criteria
- Every ruling item has a CDC-F ID assigned.
- Every item is assigned to one of the 3 categories.
- There are 0 unassigned categories.

## 5. Reflection Priority Order
1. CDC-F-006 (fix the communication contract)
2. CDC-F-007 (require VSS quality attributes)
3. CDC-F-005 (requirement to continue while MM is stopped)
4. CDC-F-001 (unify thresholds)
5. CDC-F-003 (2-sample recovery determination)
6. CDC-F-008 (fault-case transitions + audit)
7. CDC-F-009 (8-level Fan conversion)
8. CDC-F-004 (four-level confidence evaluation)
