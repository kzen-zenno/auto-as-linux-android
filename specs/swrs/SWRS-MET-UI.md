# SWRS-MET-UI: Linux/PyQt MET UI Application Requirements

## 1. Purpose
Defines the requirements for `metapp`, the Linux/PyQt6 instrument-cluster
(MET UI) application, to subscribe to VSS properties from KUKSA Databroker
and display them with correct staleness/fail-safe behavior. Same client role
as the Android/MM app (SWRS-Android-VehicleApp) but read-only, and running
natively on Linux instead of Android.

Unlike the other SWRS files in this set, this one documents an
**implemented** application rather than a template awaiting fill-in; see
[`metapp/README.md`](../../metapp/README.md) for the full design this
reflects (threading model, staleness derivation, UI layout, containerized
deployment).

## 2. Reference Relationships
- Source SYS: [SYS-001-can-signal.md](../sys/SYS-001-can-signal.md) (per-signal
  timeout values, reused as the staleness thresholds), [SYS-003-vss.md](../sys/SYS-003-vss.md)
  (VSS paths, units, gear-label mapping)
- Upstream SWRS: [SWRS-Linux-VehicleService.md](SWRS-Linux-VehicleService.md)
  (the Databroker feed this app subscribes to)
- Sibling: [SWRS-Android-VehicleApp.md](SWRS-Android-VehicleApp.md) — same
  client role, different platform; MET UI has no write/HVAC path
- Detailed design: [`metapp/README.md`](../../metapp/README.md) — this SWRS
  states the SHALL requirements, the README documents how each is met

## 3. Scope of Responsibility
- Subscribing (read-only) to the mandatory MET signal set (speed, ESC/ABS
  error, seatbelt, door, airbag, fuel level) plus the conventional cluster
  signals (engine speed, gear, parking brake) via
  `kuksa.val.v1.VAL/Subscribe`
- Deriving per-signal data freshness locally (OK / STALE / UNKNOWN), since
  Databroker does not forward an explicit staleness marker on the wire
- Reflecting freshness in the UI with signal-appropriate fail-safe behavior
  — never silently reverting to a "no fault" appearance on stale data
- Reconnecting to Databroker on connection loss with backoff, and surfacing
  connection state
- No SOME/IP, CAN, write, or HVAC responsibilities (out of scope; see
  `metapp/README.md` §2 Non-goals)

## 4. SHALL Requirements

| Requirement ID | SHALL Statement | Acceptance Criteria | Reference |
|---|---|---|---|
| SWRS-MET-001 | MET UI shall display `Vehicle.Speed` and `Vehicle.Powertrain.CombustionEngine.Speed` as needle gauges, updated on every Databroker push. | Needle position reflects the latest OK-freshness value; gauge shows the SYS-001-derived fallback (`0`) only once the signal reaches `UNKNOWN` (2000 ms since the value's own timestamp, both signals). | SYS-001 §2; `metapp/README.md` §5, §6, §7.1 |
| SWRS-MET-002 | MET UI shall independently re-derive per-signal freshness (`OK` / `STALE` / `UNKNOWN`) from each `Datapoint`'s own timestamp, using SYS-001's per-signal stale/unknown timeout pairs, re-evaluated on a fixed poll (<= 200 ms) independent of whether new data arrives. | A signal that stops updating (upstream `DEGRADED`) is shown `STALE` within its SYS-001 stale timeout and `UNKNOWN` within its unknown timeout, even while the gRPC stream itself stays connected. | SYS-001 §2; `metapp/README.md` §6 |
| SWRS-MET-003 | MET UI shall display ESC, ABS, Seatbelt, Door, Airbag, and Fuel(-empty) warning lamps with fail-safe behavior appropriate to each signal: ESC/ABS/Airbag/Fuel-empty forced ON while not `OK`-fresh; Seatbelt/Door shown indeterminate (dim/blink) while not `OK`-fresh. | Each lamp's ON condition and not-fresh fallback matches `metapp/README.md` §7.3's table exactly. Fuel is a lamp only (ON iff `RelativeLevel == 0`) — no 0-100% numeric fuel display exists in the UI. | SYS-001 §2; `metapp/README.md` §7.3 |
| SWRS-MET-004 | MET UI shall map `Vehicle.Powertrain.Transmission.CurrentGear` to a driver-facing gear label per SYS-003 §6, showing `--` when the value is unmapped or the signal is `UNKNOWN`. | Gear label table matches SYS-003 §6 verbatim (`R`/`N`/`1`-`6`/`P`/`D`/`--`). | SYS-003 §6; `metapp/README.md` §7.2 |
| SWRS-MET-005 | MET UI shall detect Databroker connection loss (`grpc.RpcError`, `VSSClientError`, or the subscribe stream ending) and, on detection, immediately mark every displayed signal `UNKNOWN` rather than waiting out each signal's individual timeout, then reconnect with capped exponential backoff (1 s -> 5 s) and re-subscribe the full path set. | On a simulated Databroker outage, every lamp/gauge shows its `UNKNOWN` fallback within one freshness-poll cycle of the disconnect; normal display resumes once Databroker is reachable again and a fresh sample arrives. | `metapp/README.md` §4.2 |
| SWRS-MET-006 | MET UI shall run strictly read-only against Databroker: it shall never call `kuksa.val.v1.VAL/Set` or otherwise write any VSS path. | Source inspection: no `Set`/actuator-write call exists anywhere in `metapp/src/`. | `metapp/README.md` §2 Non-goals |

## 5. Verification Viewpoints
- Normal case: does each gauge/lamp reflect a freshly-arriving `OK` value
  without noticeable redraw lag? Verified against the live stack (real
  `Vehicle.Speed`/`...CombustionEngine.Speed` updates observed end to end;
  `metapp/README.md` §10).
- Abnormal case: does a signal with no upstream stimulus at all (no CAN
  traffic feeding it) correctly show its `UNKNOWN` fallback rather than a
  frozen stale value being mistaken for a live reading? Verified during
  debugging in this same session — see
  `thinking_logs/2026-09-14-metapp-gauge-freeze-and-webcan-autosweep.md`.
- Timeout: does a signal that stops updating mid-session (upstream goes
  `DEGRADED`, or its CAN source is silenced) progress `OK` -> `STALE` ->
  `UNKNOWN` on exactly the documented SYS-001 timeouts, not earlier or
  later? Deliberate fault-injection against the full stack (e.g.
  `docker compose stop vehicle-state-provider`/`gateway-ecu` while `metapp`
  is running) remains open — see §6 below.

## 6. Update Notes
- Fault injection (`docker compose stop vehicle-state-provider` /
  `gateway-ecu` against a running MET UI) is designed for but not yet
  formally exercised as a repeatable test — see `metapp/README.md` §10 and
  SWARCH-000 §9's `SWARCH-000-R-004` (re-subscription/recovery verification
  this depends on).
- Multi-instance Seat/Door/Airbag display is a documented future extension
  (`metapp/README.md` §11), not a current requirement, since the provider
  currently publishes only the `Row1.DriverSide` instance.
- MET UI's own staleness thresholds are reused from SYS-001's CAN-layer
  timeout column (a deliberate design choice, not a SYS-003 requirement);
  if SYS-003 later gains its own client-facing freshness contract, this
  file and `metapp/README.md` §6 should be updated together.
