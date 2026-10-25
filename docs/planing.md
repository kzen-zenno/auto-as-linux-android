# AAOS16 VHAL ↔ KUKSA Connection Plan

Status: executed. Phases 0–2 and 4 are done and Phase 3 is partly done; see [doing.md](doing.md) for the results and [checking.md](checking.md) for the manual check procedure. The open issues that remain are listed in §7.

## 1. Purpose and goals

Connect the VHAL of AAOS 16 (Cuttlefish `aosp_cf_x86_64_auto`) to the KUKSA Databroker of `auto-as-linux`.

- **Goal**: Make the vehicle state carried by the Gateway ECU (autoas-as) → SOME/IP → provider → KUKSA Databroker chain (the VSS of SYS-003) readable as VHAL properties through the AAOS `CarPropertyManager`.
- **Reverse direction (HVAC)**: When AAOS calls `setProperty` on an HVAC property, the write reaches the KUKSA actuator update → provider → `WriteVehicleState` → Gateway ECU.
- **Out of scope**: A real-hardware VHAL (HIDL / real ECU), and multiple Seat/Door/Airbag instances (future extension in SYS-003 §7.1).

## 2. Current state (findings)

### 2.1 auto-as-linux side (per specs)

```
Gateway ECU ──SOME/IP(TCP, 100ms)──> vehicle-state-provider ──kuksa.val.v1 gRPC──> Databroker :55555
                                  <── WriteVehicleState ────┘ (actuator 0x1006..0x100A)
```

- The Databroker loads `vss_release_6.0.json` + `vss_fmw_overlay.json` (the custom `...Row1.Driver.IsDefrosting`). The root `docker-compose.yml` (`auto-as-linux/`) already provides the `kuksa-net` network (172.30.0.0/24) and publishes port `55555` on the host.
- The published VSS is the 14 entries of SYS-003 §3. Each value carries `quality / freshness / confidence / timestamp / source` and has the quality `OK / INVALID / STALE`.
- The only actuators (writable) are the 5 HVAC entries (SYS-002 keys 0x1006..0x100A, SWARCH-000 §5.2). Everything else is read-only.
- metapp (PyQt) already runs as a plain KUKSA VAL gRPC client of the Databroker. AAOS sits in the same position (the `Android / MM applications` ↔ Databroker of SWARCH-000 §3), which **matches the path assumed by the specs**.
- SWRS-Android-VehicleApp (SWRS-APP-001 to 003) requires: subscribing to VSS for display and writes, a degraded display for INVALID/STALE, and success/failure feedback for writes.

### 2.2 AAOS16 (Cuttlefish) side (checked in `aaos16/workspace/aosp`)

```
Car Service / CarPropertyManager
   │ AIDL IVehicle
android.hardware.automotive.vehicle@V3-cf-service   (guest, DefaultVehicleHal + GRPCVehicleHardware)
   │ gRPC (vsock, or ethernet 192.168.98.1 : ro.boot.vhal_proxy_server_port)
vhal_proxy_server                                   (host; contains FakeVehicleHardware)
```

- The guest VHAL (`device/google/cuttlefish/guest/hals/vehicle/VehicleService.cpp`) is **only a gRPC client**. The properties actually live in `FakeVehicleHardware` (a JSON-defined simulated vehicle) in the host-side `vhal_proxy_server` (`device/google/cuttlefish/host/commands/vhal_proxy_server/`).
- The most natural insertion point is therefore **the host-side `IVehicleHardware` implementation (= replacing/extending `FakeVehicleHardware`)**. The guest image's VHAL, SELinux policy and VINTF manifest stay untouched.
- The main methods to implement in `IVehicleHardware` (`hardware/interfaces/automotive/vehicle/aidl/impl/current/hardware/include/IVehicleHardware.h`): `getAllPropertyConfigs / setValues / getValues / registerOnPropertyChangeEvent / subscribe / checkHealth / dump`.
- The gRPC contract is `.../impl/current/grpc/proto/VehicleServer.proto`.
- The build runs in the AOSP tree inside the `aaos16-builder` container (Ubuntu 24.04). Rebuilding the host binary is self-contained with `m vhal_proxy_server` (confirmed).
- Note: `aaos16-builder` is not on `kuksa-net`, so the Databroker (`databroker:55555`) cannot be resolved by name. Use the host-published port `55555`, or join the `aaos16` container to `vehicle_service_kuksa-net` (see §5).

## 3. Comparison of approaches

| Option | Description | Pros | Cons |
|---|---|---|---|
| **A. New `KuksaVehicleHardware` on the host side (chosen)** | Implement `IVehicleHardware` and replace `FakeVehicleHardware` in `vhal_proxy_server`. Internally it talks to the Databroker as a kuksa.val.v1 gRPC client | No guest changes. Handles the VHAL subscribe/error/timestamp contract properly. Clean architecture | Adds C++ inside the AOSP tree. Heavier to build and debug |
| B. External bridge (Python/Rust) acting as a gRPC client of the proxy server | Keep the existing `vhal_proxy_server` (Fake) as is. The bridge calls `setValues` through `VehicleServer.proto`, subscribes to changes and writes them to KUKSA | No AOSP rebuild; fast to get running; easy to experiment | Duplicate state with the Fake, so the source of truth is unclear. Crude handling of write ACKs and quality |
| C. Put a Databroker client on the guest side | The VHAL or a Car app calls KUKSA directly | Shorter path | Needs guest image changes, SELinux and a guest→host network path. Goes beyond the VHAL's responsibility |

**Decision: go with option A** (user decision). The external bridge (option B) will not be built. The conversion logic is implemented as pure functions inside `KuksaVehicleHardware` and pinned by unit tests.

## 4. VSS ↔ VHAL property mapping

Seat/Door/Airbag are fixed to the initial instance `Row1.DriverSide` (SYS-003 §7.1). The AAOS areaId is `VehicleAreaSeat::ROW_1_LEFT` / `VehicleAreaDoor::ROW_1_LEFT` (**decision: the driver seat is on the left = `ROW_1_LEFT`**).

| VSS (SYS-003) | Direction | VHAL property | Conversion / notes |
|---|---|---|---|
| `Vehicle.Speed` (km/h) | KUKSA→VHAL | `PERF_VEHICLE_SPEED` (float) | **km/h → m/s** |
| `...CombustionEngine.Speed` (rpm) | KUKSA→VHAL | `ENGINE_RPM` (float) | VHAL is also rpm. No unit conversion |
| `...ParkingBrake.IsEngaged` | KUKSA→VHAL | `PARKING_BRAKE_ON` (bool) | Direct |
| `...HVAC.IsAirConditioningActive` | Both | `HVAC_AC_ON` (bool, seat area) | The vehicle-wide value is reflected to the driver seat area |
| `...Row1.Driver.Temperature` (degC) | Both | `HVAC_TEMPERATURE_SET` (float, ROW_1_LEFT) | Range 16.0–30.0 (writes from the VHAL are clamped to it). The Fake config range 17.5–32.5 is left as it is |
| `...Row1.Driver.FanSpeed` (%) | Both | `HVAC_FAN_SPEED` (int32, ROW_1_LEFT) | VHAL uses steps. SOME/IP has 8 levels 0–7 and VSS is `round(level*100/7)` (SYS-003 §7.1); the VHAL level is `round(percent*7/100)`. The Fake config range is 1–7, and level 0 is written to the store directly |
| `...Row1.Driver.AirDistribution` | Both | `HVAC_FAN_DIRECTION` (bitmask) | `UP`→FACE(0x1), `DOWN`→FLOOR(0x2), `MIDDLE`→FACE\|FLOOR(0x3). A write with any other bit pattern (including the DEFROST bit 0x4) is rejected with `INVALID_ARG`; defrost is carried by `HVAC_DEFROSTER` |
| `...Row1.Driver.IsDefrosting` (overlay) | Both | `HVAC_DEFROSTER` (bool, `FRONT_WINDSHIELD`) | Maps the custom overlay to the standard VHAL property |
| `Vehicle.ADAS.ABS.IsError` | — | **Not connected** (decision) | No standard match. Not exposed in the VHAL |
| `Vehicle.ADAS.ESC.IsError` | — | **Not connected** (decision) | Same as above |
| `...Seat.Row1.DriverSide.IsBelted` | KUKSA→VHAL | `SEAT_BELT_BUCKLED` (bool, ROW_1_LEFT) | Direct |
| `...Door.Row1.DriverSide.IsOpen` | KUKSA→VHAL | `DOOR_POS` (int32, ROW_1_LEFT) | bool → 0 / 1 (the Fake config maximum is 1). `DOOR_MOVE` is not used |
| `...Seat.Row1.DriverSide.Airbag.IsDeployed` | KUKSA→VHAL | `SEAT_AIRBAGS_DEPLOYED` (int32, bitmask of `VehicleAirbagLocation`) | true sets `FRONT` (0x02), false sets 0 (confirmed in `VehicleProperty.aidl`) |
| `...FuelSystem.RelativeLevel` (%) | KUKSA→VHAL | `FUEL_LEVEL` (float, mL) with `INFO_FUEL_CAPACITY`, and `FUEL_LEVEL_LOW` | `FUEL_LEVEL = RelativeLevel × capacity` with the fixed capacity 15000 mL (the Fake `INFO_FUEL_CAPACITY`). `FUEL_LEVEL_LOW = (RelativeLevel <= 0%)` (decision: threshold 0%) |
| `...Transmission.CurrentGear` | KUKSA→VHAL | `GEAR_SELECTION` / `CURRENT_GEAR` (int32) | -1→REVERSE(2), 0→NEUTRAL(1), 1..6→FIRST(16).., 126→PARK(4), 127→DRIVE(8). Reflected to both properties |

### 4.1 Handling quality metadata (quality / STALE / INVALID)

Decision: **STALE is ignored.** The VHAL has no per-property quality attribute, and `VehiclePropertyStatus` has no STALE.

| VSS quality | Reflection in the VHAL |
|---|---|
| `OK` | `AVAILABLE` + value |
| `STALE` | Ignored. The last value stays `AVAILABLE` (consistent with SYS-003 §7 "retain the value while STALE"). If no update arrives, the VHAL value does not change either |
| `INVALID` | The value is not updated; the last valid value is kept and the status stays `AVAILABLE` (implemented this way) |

confidence / source / freshness have nowhere to go in the VHAL either, so they are dropped and appear only in `dump` and the logs.

Note: Ignoring STALE means SWRS-APP-002 (degraded display) is not satisfied on the AAOS side. This requirement is out of scope for this connection, and SWRS-Android-VehicleApp §4.1 states that the VHAL path does not show STALE.

### 4.2 Write (HVAC) contract

SYS-003 §9: "the result of a write is reflected into the state property after the ACK". `setValues` therefore:

1. Issues a `Set` (target value) on the corresponding KUKSA actuator.
2. Emits `onPropertyChangeEvent` only once the state (current value) has been updated.
3. Returns `onPropertySetErrorEvent` (`TRY_AGAIN`) if it is not reflected within 3 seconds (a failed Databroker `Set` is answered `INTERNAL_ERROR`, an invalid value `INVALID_ARG`; this corresponds to the failure notification of SWRS-APP-003; no retry: SYS-003 §10).

The VHAL value is never rewritten optimistically first.

## 5. Network configuration

```
[host]
  docker network vehicle_service_kuksa-net (172.30.0.0/24)
     databroker :55555  ← already published on the host at 0.0.0.0:55555 (not only 127.0.0.1)
  docker aaos16-builder  (privileged, contains Cuttlefish)
     vhal_proxy_server (KuksaVehicleHardware)  ──gRPC──> Databroker
     crosvm ── vsock / eth ──> guest VHAL
```

Decision: connect **through the host-published port** (the `aaos16` container does not join `kuksa-net`).

- Pass the host address as seen from `aaos16-builder` (the docker0 gateway, or `extra_hosts: host.docker.internal:host-gateway`) plus `:55555` to `KuksaVehicleHardware` as the Databroker address (environment variable `KUKSA_ADDR`).
- `extra_hosts` and `KUKSA_ADDR` are set in `aaos16/docker-compose.yml` (`KUKSA_ADDR=host.docker.internal:55555`).
- `auto-as-linux` is assumed to be brought up with `docker compose up` first. If the Databroker comes up later or restarts, reconnect and resubscribe (done: reconnects every second and resubscribes; see Phase 3 in §6).

## 6. Implementation phases

### Phase 0: Connectivity check (no code changes)
- [x] Start `auto-as-linux` (`gateway-ecu` with webcan). Inject CAN signals from the webcan UI (`:8800`) and confirm with `grpcurl` that the Databroker values change (`automotive_linux/README.md` §3–4)
- [x] Start Cuttlefish with `aaos16` and confirm with `adb shell cmd car_service get-property-value` / `dumpsys car_service` that the existing Fake VHAL properties are visible (baseline)
- [x] Confirm that the host-published Databroker (`:55555`) is reachable from `aaos16-builder` (`grpcurl -plaintext <host-addr>:55555 list`)
- [x] Fix the type, unit, areaId and enum values of each property in §4 using `VehicleProperty.aidl`, and finalize the mapping table (airbag type, `ENGINE_RPM` unit, `DOOR_POS` range)

### Phase 1: `KuksaVehicleHardware` skeleton and KUKSA→VHAL (read)
- [x] Create a new module (`Android.bp`) next to `device/google/cuttlefish/host/commands/vhal_proxy_server/`. Make it switchable with `FakeVehicleHardware` through a start argument / environment variable
- [x] **Composition approach**: only the mapped properties come from Kuksa; the rest is delegated to `FakeVehicleHardware` (keeps the standard properties Car Service needs to start)
- [x] Bring the kuksa.val.v1 proto into the AOSP build (copy `proto/kuksa_data_broker`, or generate it with Soong)
- [x] Generate `getAllPropertyConfigs()` from the mapping table. KUKSA subscription (`kuksa.val.v1.VAL/Subscribe`) → `onPropertyChangeEvent` → not done: the Fake default config contains all the properties, so it is reused through inheritance (see doing.md)
- [x] Implement the conversions (units, enums, bitmasks) as pure functions with unit tests (km/h→m/s, Fan 0–7↔%, Gear, Fuel)
- [x] Manage the patch as `aaos16/patches/0002-...patch` (direct edits are lost on `repo sync`)

### Phase 2: VHAL→KUKSA (HVAC write)
- [x] `setValues` → KUKSA `Set` (only the 5 HVAC actuators; read-only ones get `ACCESS_DENIED`)
- [x] Wait for the state to be reflected, then `onPropertyChangeEvent`. On timeout, `onPropertySetErrorEvent` (§4.2)

### Phase 3: Integration tests
- [ ] Per SWRS-LV-004: confirm 0 VSS path/type mismatches over 100 cases (compare KUKSA-side and VHAL-side values) → not done: only a single comparison of all 13 signals (`aaos16/tests/kuksa_vhal_e2e.py`). The 100-case test is not done
- [x] HVAC write round trip: `cmd car_service set-property-value` → KUKSA actuator → provider → Gateway ECU → state event → VHAL reflection
- [x] Degradation: stop the Gateway ECU → STALE is ignored, INVALID does not update the value → STALE being ignored was confirmed (the value is retained when gateway-ecu is stopped). INVALID is not tested
- [x] Reconnect and resubscribe after a Databroker restart / network cut, and start order (works even if the Databroker comes up later)
- [ ] Confirm that the "2 consecutive valid samples" recovery rule (SWRS-LV-007) is not broken on the VHAL side → not done (the VHAL side only reflects the provider's recovery decision and has no decision of its own)

### Phase 4: Specs and documentation
- [x] Add the new spec (following the Working Rules of AGENTS.md and the existing structure): a Trace entry in SYS-003, `specs/swarch/SWARCH-003-aaos-vhal-bridge.md` (deployment view, responsibilities, data flow), a concrete `SWRS-Android-VehicleApp` (§4.1: how APP-001 to 003 apply to the VHAL path), and updates to the open items and Trace of SYS-000 / SWARCH-000 §9
- [x] Add the start procedure to `aaos16/README.md` (applying the patch, `KUKSA_ADDR`, and how to switch back to the plain Fake)

## 7. Decisions and open issues

### Decisions (user decisions)

| Item | Decision |
|---|---|
| Approach | Option A (`KuksaVehicleHardware`) only. No external bridge |
| ESC / ABS errors | Not connected to the VHAL |
| STALE | Ignored (§4.1) |
| Driver seat | Left (`ROW_1_LEFT`) |
| Fuel | The `FUEL_LEVEL_LOW` threshold is 0% |
| Databroker connection | Through the host-published port (`:55555`) |
| Real data input | Inject CAN signals from webcan (`:8800` of `gateway-ecu`) |

### Resolved issues

1. **Total fuel capacity (`INFO_FUEL_CAPACITY`)**: the "0%" answer is the `FUEL_LEVEL_LOW` threshold. The capacity uses the Fake default of 15000 mL.
2. **vsomeip EventGroup subscription (SWARCH-000-R-005)**: resolved by the user. The cause was the vSOMEIP version and opening the UDP ports. Values injected with webcan reach the Databroker.
3. **`DOOR_POS` opening value and airbag bit definition**: fixed in Phase 0 (`DOOR_POS` 0/1, airbag `FRONT` = 0x02).

### Open issues

1. The 100-case comparison of SWRS-LV-004 is not done (only a single comparison of all 13 signals).
2. The behavior on INVALID input is covered by the unit test only, not by an integration test.
3. The recovery rule "2 consecutive valid samples" (SWRS-LV-007) is not verified on the VHAL side. The VHAL only reflects the provider's decision.
4. A long-run test is not done.

## 8. Risks

| Risk | Impact | Response |
|---|---|---|
| The AOSP build is heavy (limited to `m vhal_proxy_server` for the host binary only) | Slow iteration | Build only `m vhal_proxy_server` and keep ccache |
| Removing `FakeVehicleHardware` drops the standard properties required to start Car Service (INFO_*, power/user/VMS, etc.) | Cannot boot | Do not replace it; use a **Fake + Kuksa composition** (only the mapped properties from Kuksa, the rest delegated to the Fake) |
| Dependent properties: `HVAC_*` etc. that Car Service references at startup must be in the config | Boot failure | Use the existing Fake default config JSON as the base (done: no config is generated; values are written into the Fake store) |
| The vsomeip EventGroup subscription did not complete (SWARCH-000-R-005) | Values might not flow into KUKSA even when injected with webcan | Resolved (cause: the vSOMEIP version and the UDP ports); input through webcan works |
| Mixing up units/enums (km/h↔m/s, Gear) | Inaccurate display | Pinned by the Phase 1 unit tests |

## 9. Materials referenced

- specs: SWARCH-000 (overview and data flow §3/§5), SWARCH-001, SWRS-Linux-VehicleService, SWRS-Android-VehicleApp, SYS-000, SYS-003 (§3 path table, §7 quality, §9 update contract)
- auto-as-linux: `docker-compose.yml`, `automotive_linux/README.md`, `vehicle_service/databroker/`
- aaos16: `README.md`, `docker-compose.yml`, AOSP `device/google/cuttlefish/{guest/hals/vehicle, host/commands/vhal_proxy_server}`, `hardware/interfaces/automotive/vehicle/aidl/impl/current/{grpc,hardware}`, `VehicleProperty.aidl`
