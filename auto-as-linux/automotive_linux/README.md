# Automotive Linux — KUKSA + SOME/IP Vehicle State Service

A spec-driven implementation of a Linux Vehicle Service: a KUKSA Databroker
feeder that speaks a custom `VehicleStateService` SOME/IP contract to a
simulated AUTOSAR-CP Gateway ECU. Built with Docker Compose on Ubuntu 24.04
LTS, without polluting the host.

- [Automotive Linux — KUKSA + SOME/IP Vehicle State Service](#automotive-linux--kuksa--someip-vehicle-state-service)
  - [Repository Layout](#repository-layout)
  - [Overview](#overview)
    - [Components](#components)
  - [System Architecture](#system-architecture)
  - [SOME/IP Contract](#someip-contract)
  - [Directory Layout](#directory-layout)
  - [Module Summary](#module-summary)
  - [Prerequisites](#prerequisites)
  - [Running via Docker Compose](#running-via-docker-compose)
    - [1. Build \& Start](#1-build--start)
    - [2. Check Container Status](#2-check-container-status)
    - [3. Confirm Normal Operation via Logs](#3-confirm-normal-operation-via-logs)
    - [4. Inspect the Databroker's Live Data Directly via grpcurl](#4-inspect-the-databrokers-live-data-directly-via-grpcurl)
    - [5. Writing an Actuator Value (HVAC)](#5-writing-an-actuator-value-hvac)
    - [6. Cleanup](#6-cleanup)
  - [VSS Paths Fed](#vss-paths-fed)
  - [VehicleStateService (ECU Simulator) Behavior](#vehiclestateservice-ecu-simulator-behavior)
  - [Building \& Running Standalone (outside Docker Compose)](#building--running-standalone-outside-docker-compose)
    - [Prerequisites](#prerequisites-1)
    - [Building](#building)
    - [Running](#running)
  - [Configuration](#configuration)
  - [Extending](#extending)
  - [Related Files](#related-files)
  - [Known Limitations / Caveats](#known-limitations--caveats)

---

## Repository Layout

```text
.
├── specs/            # SYS / SWARCH / SWRS specification documents
│   ├── sys/          # signal & protocol specs (CAN, SOME/IP, VSS, CDC-F rulings)
│   ├── swarch/        # software architecture (this implementation)
│   └── swrs/          # software requirements
└── vehicle_service/  # the implementation described below
```

The specs are the source of truth for the protocol/behavior decisions this
implementation follows, in particular
[`specs/sys/SYS-002-someip.md`](specs/sys/SYS-002-someip.md) (the SOME/IP
contract), [`specs/sys/SYS-003-vss.md`](specs/sys/SYS-003-vss.md) (VSS
mapping), and [`specs/swarch/SWARCH-000-overview.md`](specs/swarch/SWARCH-000-overview.md)
(overall architecture).

## Overview

[SOME/IP](https://www.autosar.org/fileadmin/standards/R22-11/FO/AUTOSAR_PRS_SOMEIPProtocol.pdf)
is an automotive communication protocol supporting remote procedure calls,
event notifications, and service discovery. This repository implements a
`VehicleStateService` over SOME/IP on top of
[COVESA / vsomeip](https://github.com/COVESA/vsomeip/): a simulated Gateway
ECU (`vehicle_state_service`) periodically distributes vehicle state as an
event, and a provider (`someip_feeder`) subscribes to it, converts the
values, and feeds them into KUKSA Databroker over the `kuksa.val.v1.VAL`
gRPC API. HVAC actuator writes flow the other way: a `Set` on Databroker is
observed by the provider and sent back to the ECU as a `WriteVehicleState`
SOME/IP request.

### Components

- Eclipse KUKSA Databroker
- `vehicle-state-provider` — SOME/IP client + KUKSA VSS feeder
  (`kuksa-someip-provider`'s `someip_feeder` binary)
- `vehicle-state-server` — VehicleStateService SOME/IP server, simulating the
  Gateway ECU (`vehicle_state_service` example)
- Docker Compose
- gRPC Client (grpcurl)

## System Architecture

```text
+--------------------------------------------------------+
| Docker Network (kuksa-net, 172.30.0.0/24)               |
|                                                          |
|  +-------------------+                                  |
|  | Databroker        |                                  |
|  | Port: 55555       |                                  |
|  +--------+----------+                                  |
|           | gRPC (kuksa.val.v1)                          |
|           |                                              |
|  +--------v----------+                                  |
|  | vehicle-state-    |                                  |
|  | provider          |  172.30.0.13                     |
|  +--------+----------+                                  |
|           ^                                              |
|           | SOME/IP TCP (0x1770.0x0001)                  |
|           |                                              |
|  +--------+----------+                                  |
|  | vehicle-state-     |                                  |
|  | server (ECU sim)  |  172.30.0.12                     |
|  +-------------------+                                  |
|                                                          |
+--------------------------------------------------------+
```

## SOME/IP Contract

Both `vehicle_state_service` and `someip_feeder` speak the
`VehicleStateService` contract defined in
[`specs/sys/SYS-002-someip.md`](specs/sys/SYS-002-someip.md) and
[`specs/swarch/SWARCH-002-someip-server.md`](specs/swarch/SWARCH-002-someip-server.md):

| Item | Value |
|---|---:|
| Service | `0x1770` |
| Instance | `0x0001` |
| Version | `1.0` |
| Event | `0x8001` (event group `0x0001`), every 100 ms |
| `GetVehicleStateSnapshot` | method `0x0001` |
| `WriteVehicleState` | method `0x0002` (accepts keys `0x1006`-`0x100A` only) |
| Transport | TCP only — no UDP/"unreliable" endpoint is configured |
| TCP port | `30512` (deployment-level choice, not mandated by SYS-002) |

The payload for both the event and the methods is a bounded, little-endian
LKV (Last-Known-Value) list; see
`vehicle_service/workspace/kuksa-someip-provider/src/lib/vehicle_state/lkv_codec.h`
and SYS-002 §5.2 for the exact layout.

## Directory Layout

`docker-compose.yml` for this stack lives at the repository root (one
directory up from `automotive_linux/`), not inside `vehicle_service/` —
moved there so it can also cover `metapp` (see
[`../metapp/README.md`](../metapp/README.md) §8.2).

```text
vehicle_service/
│
├── workspace/
│   └── kuksa-someip-provider/    # shared provider source tree
│                                  # (bind-mounted into both containers below)
│       ├── src/
│       │   ├── someip_feeder/          # provider entrypoint + VehicleState adapter
│       │   ├── lib/
│       │   │   ├── vehicle_state/      # LKV codec + shared protocol constants
│       │   │   ├── someip_client/      # generic vsomeip client wrapper
│       │   │   └── broker_feeder/      # KUKSA Databroker integration
│       │   └── examples/
│       │       └── vehicle_state_service/  # Gateway ECU simulator
│       └── patches/               # vsomeip patches, applied via CMake FetchContent
│
├── databroker/
│   └── Dockerfile
│
├── vehicle-state-server/         # ECU simulator (VehicleStateService) container
│   ├── Dockerfile
│   ├── start.sh
│   └── config/
│       └── vsomeip.json
│
└── provider-vehicle-state/       # SOME/IP-to-Databroker provider container
    ├── Dockerfile
    ├── start.sh
    └── config/
        └── vsomeip.json
```

`workspace/kuksa-someip-provider/` is shared by both the `vehicle-state-server`
and `vehicle-state-provider` containers via bind mount, backed by the same
source tree, so it can be edited directly on the host. Each container does an
out-of-source build (`cmake -S/-B`) of this shared source into its own build
directory (named Docker volumes `vehiclestate-build-cache` /
`vehiclestateprovider-build-cache`) — `vehicle-state-server` builds and runs
`vehicle_state_service`, and `vehicle-state-provider` builds and runs
`someip_feeder`.

## Module Summary

Within `vehicle_service/workspace/kuksa-someip-provider/`:

- `src/someip_feeder/` — the provider entrypoint (`main.cc`) and
  `vehicle_state_adapter.{h,cc}`, which owns SOME/IP subscription, LKV
  decoding, Databroker feeding, and actuator-target writeback.
- `src/lib/vehicle_state/` — the generic LKV codec (`lkv_codec.{h,cc}`,
  SYS-002 §5.2 wire format) and shared protocol constants
  (`vehicle_state_ids.h`: service/instance/event/method IDs, key table).
  Depended on by both `someip_feeder` and `vehicle_state_service`.
- `src/lib/someip_client/` — generic vsomeip client wrapper (service
  discovery, subscribe, request/response); does not depend on
  VehicleState-specific types.
- `src/lib/broker_feeder/` — KUKSA Databroker integration:
  `data_broker_feeder` (feeds values via `kuksa.val.v1.VAL.Set`) and
  `actuator_subscriber` (observes actuator targets via
  `kuksa.val.v1.VAL.Subscribe`).
- `src/examples/vehicle_state_service/` — the Gateway ECU simulator:
  `vehicle_state_service.cc` (vsomeip lifecycle, offer/notify/method
  handlers), `vehicle_state_model.{h,cc}` (deterministic simulation state),
  `write_policy.{h,cc}` (HVAC write validation), and
  `vehicle_state_probe.cc` (a dev-only CLI client for manual verification —
  `--watch`, `--snapshot`, `--write`, and several malformed-input scenarios).
- `patches/` — vsomeip patches applied on top of the vendored `3.1.20.3`
  source (build warnings, logger fixes, a default-major-version fix, and
  application-level debug logging), not yet pushed upstream.

## Prerequisites

Ubuntu 24.04 LTS. For running the full stack via Docker Compose:

```bash
sudo apt update

sudo apt install -y \
    docker.io \
    docker-compose-v2

sudo systemctl enable docker
sudo systemctl start docker

docker --version
docker compose version
```

Building `someip_feeder` / `vehicle_state_service` standalone (outside a
container) additionally needs `cmake`, a C++ toolchain, and Conan — see
[Building & Running Standalone](#building--running-standalone-outside-docker-compose).

## Running via Docker Compose

### 1. Build & Start

From the repository root (`docker-compose.yml` lives there, not under
`vehicle_service/` — see [Directory Layout](#directory-layout)):

```bash
docker compose build
docker compose up -d
```

On first launch, the `vehicle-state-server` and `vehicle-state-provider`
containers run Conan builds of grpc/boost plus a source build of vsomeip3, so
this can take a few to a dozen or so minutes (subsequent runs are cached in
named volumes and much faster).

### 2. Check Container Status

```bash
docker compose ps
```

Confirm all three of `databroker` / `vehicle-state-server` /
`vehicle-state-provider` show `Up`. If any keeps `Restarting`, that indicates
a build or startup error.

### 3. Confirm Normal Operation via Logs

```bash
docker compose logs -f databroker vehicle-state-server vehicle-state-provider
```

Things look healthy if you see the following in the logs.

- **databroker**: `Listening on 0.0.0.0:55555`, `Populating metadata from file 'data/vss-core/vss_release_6.0.json'`
- **vehicle-state-server**: `OFFER(...): [1770.0001:1.0] (true)`, periodic `notify` log lines roughly every 100 ms once a subscriber is present
- **vehicle-state-provider**: `connected to data broker.` / `Service [1770.0001] is available.` / `SUBSCRIBE_ACK` for event group `0x0001`, followed by periodic `FeedValue`/`FeedValues` log lines as VehicleState events are converted and fed to Databroker

Confirm the following errors are **not** present (signs of previously-seen
known issues resurfacing):

- `PROTOC_PROGRAM_TEMP-NOTFOUND` (a build config issue)
- gRPC status 12 = `UNIMPLEMENTED` (databroker API version mismatch)
- `SOME/IP eventgroups require SD to be enabled!` (an SD config issue)
- `Didn't receive a multicast SD message for ...ms.` repeating indefinitely (SD's OFFER/SUBSCRIBE aren't actually arriving)
- `Routing info for remote service could not be found!` (a TCP/UDP request-transport mismatch; see "Known Limitations / Caveats" below)

### 4. Inspect the Databroker's Live Data Directly via grpcurl

`grpcurl` doesn't need to be installed on the host — you can just use the
Docker image as-is (connected to `kuksa-net`).

```bash
docker run --rm --network vehicle_service_kuksa-net fullstorydev/grpcurl:latest \
  -plaintext \
  -d '{"entries":[{"path":"Vehicle.Speed","view":"VIEW_CURRENT_VALUE"}]}' \
  databroker:55555 kuksa.val.v1.VAL/Get
```

If everything's working, you'll get back a value whose `timestamp` and
`float` both update on every run (the simulator drives `Speed` and
`Powertrain.CombustionEngine.Speed` as a continuous triangle wave):

```json
{
  "entries": [
    {
      "path": "Vehicle.Speed",
      "value": { "timestamp": "...", "float": 89.6 }
    }
  ]
}
```

Other VSS paths can be checked the same way by changing `path`. You can also
fetch multiple paths in a single request.

```bash
docker run --rm --network vehicle_service_kuksa-net fullstorydev/grpcurl:latest \
  -plaintext \
  -d '{"entries":[
    {"path":"Vehicle.Speed","view":"VIEW_CURRENT_VALUE"},
    {"path":"Vehicle.Powertrain.CombustionEngine.Speed","view":"VIEW_CURRENT_VALUE"},
    {"path":"Vehicle.Powertrain.FuelSystem.RelativeLevel","view":"VIEW_CURRENT_VALUE"}
  ]}' \
  databroker:55555 kuksa.val.v1.VAL/Get
```

### 5. Writing an Actuator Value (HVAC)

Only the four HVAC paths in [VSS Paths Fed](#vss-paths-fed) marked writable
are writable. A `Set` on any of them is picked up by
`vehicle-state-provider`, converted to a `WriteVehicleState` SOME/IP
request, applied by `vehicle-state-server`, and reflected back to Databroker
in the next periodic event.

```bash
docker run --rm --network vehicle_service_kuksa-net fullstorydev/grpcurl:latest \
  -plaintext \
  -d '{"updates":[{"entry":{"path":"Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature","actuator_target":{"float":22.5}},"fields":["FIELD_ACTUATOR_TARGET"]}]}' \
  databroker:55555 kuksa.val.v1.VAL/Set
```

Re-run the `Get` command from step 4 a moment later to confirm the new value
propagated through the SOME/IP round trip.

### 6. Cleanup

```bash
docker compose down          # removes containers and network (build-cache volumes remain)
docker compose down -v       # also fully removes build-cache and Conan cache volumes
```

## VSS Paths Fed

See `specs/swarch/SWARCH-001-linux-vehicle-service.md` §2.3 for the full
mapping table, and `specs/sys/SYS-005-vss-catalog-reference.md` for the
verified catalog entries.

| Path | Meaning |
|---|---|
| `Vehicle.Speed` | Vehicle speed |
| `Vehicle.Powertrain.CombustionEngine.Speed` | Engine speed (rpm) |
| `Vehicle.Powertrain.Transmission.CurrentGear` | `-1` while Reverse is engaged |
| `Vehicle.Chassis.ParkingBrake.IsEngaged` | Parking brake state |
| `Vehicle.Cabin.HVAC.IsAirConditioningActive` | A/C on/off (vehicle-wide; writable) |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature` | A/C temperature (writable) |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed` | A/C fan speed, percent (writable) |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.AirDistribution` | A/C air distribution mode (writable) |
| `Vehicle.ADAS.ESC.IsError` | ESC error flag |
| `Vehicle.ADAS.ABS.IsError` | ABS error flag |
| `Vehicle.Cabin.Seat.Row1.DriverSide.IsBelted` | Driver seatbelt state |
| `Vehicle.Cabin.Door.Row1.DriverSide.IsOpen` | Driver door state |
| `Vehicle.Cabin.Seat.Row1.DriverSide.Airbag.IsDeployed` | Driver airbag state |
| `Vehicle.Powertrain.FuelSystem.RelativeLevel` | Fuel level, percent |

The Defrost key (`0x1009`) has no defined VSS path in the current catalog and
is not fed to or read from Databroker (see SYS-005 §5).

## VehicleStateService (ECU Simulator) Behavior

`vehicle_state_service`
(`vehicle_service/workspace/kuksa-someip-provider/src/examples/vehicle_state_service/`),
running in the `vehicle-state-server` container, is a Gateway ECU simulator
that offers a single SOME/IP service over TCP (see
[SOME/IP Contract](#someip-contract) above for the IDs).

- `GetVehicleStateSnapshot` (`0x0001`) returns the current LKV list on
  demand; `WriteVehicleState` (`0x0002`) accepts HVAC writes (keys
  `0x1006`-`0x100A`), validates them atomically, and applies them to the
  internal model.
- `Speed` and `Powertrain.CombustionEngine.Speed` move as a deterministic
  triangle wave; `FuelSystem.RelativeLevel` decrements slowly and wraps.
- HVAC values (`IsAirConditioningActive`, `Temperature`, `FanSpeed`,
  `AirDistribution`, Defrost) only change in response to an accepted
  `WriteVehicleState`.
- All other status keys (Gear, ParkingBrake, ESC/ABS error, Seat/Door/Airbag)
  hold fixed valid values.

## Building & Running Standalone (outside Docker Compose)

For development directly inside
`vehicle_service/workspace/kuksa-someip-provider/` (all paths below are
relative to that directory).

### Prerequisites

1. Install cmake and build requirements:
    ```bash
    sudo apt-get install -y cmake g++ build-essential
    ```
1. Install and configure Conan (if needed):
    ```bash
    sudo apt-get install -y python3 python3-pip
    pip3 install "conan<2.0"
    ```
    **NOTE:** Sometimes the latest conan recipe revisions are broken, but a
    local build succeeds using a cached older revision. If a build fails on
    CI, clearing the local conan cache can reproduce the error.
    ```bash
    rm -rf ~/.conan/data
    ```
    Last known working revisions are hardcoded in `conanfile.txt`'s
    `[requires]` section.
1. Have a KUKSA Databroker reachable (or use `docker compose up databroker`
   from the repository root):
    ```bash
    docker run --rm -it -p 55555:55555/tcp --name databroker ghcr.io/eclipse-kuksa/kuksa-databroker:main
    ```

### Building

Out-of-source CMake + Conan build (this is what `vehicle-state-server/start.sh`
and `provider-vehicle-state/start.sh` do inside their containers):

```bash
mkdir -p build && cd build
conan install .. --build=missing -s build_type=Release
source activate.sh
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build . \
  --target someip_feeder --target vehicle_state_service --target vehicle_state_probe \
  --target vsomeip3-cfg --target vsomeip3-sd -j"$(nproc)"
```

`vsomeip3-cfg`/`vsomeip3-sd` are vsomeip's JSON-config and Service-Discovery
plugin shared libraries, loaded dynamically at runtime — they must be built
explicitly since nothing links against them at build time.

vsomeip3 is vendored via CMake `FetchContent` (see the top-level
`CMakeLists.txt`) from `https://github.com/COVESA/vsomeip.git` at tag
`3.1.20.3`, with the patches in `patches/` applied automatically.

### Running

```bash
export LD_LIBRARY_PATH="build/_deps/vsomeip3-build:${LD_LIBRARY_PATH}"

# ECU simulator
VSOMEIP_CONFIGURATION=.../vsomeip.json VSOMEIP_APPLICATION_NAME=vehicle_state_server \
  ./build/src/examples/vehicle_state_service/vehicle_state_service

# Provider
VSOMEIP_CONFIGURATION=.../vsomeip.json VSOMEIP_APPLICATION_NAME=vehicle_state_provider \
  ./build/src/someip_feeder/someip_feeder --target=databroker-host:55555
```

`someip_feeder --help` lists its CLI arguments (`--target=<ip>:<port>`,
`--token <FILE>` for an authorized Databroker connection).

The `vehicle_state_probe` dev tool (built alongside `vehicle_state_service`)
can exercise the server directly without a real provider, e.g.
`vehicle_state_probe --watch` to print incoming events, or
`vehicle_state_probe --write` to send a `WriteVehicleState` request.

## Configuration

vsomeip requires a combination of a JSON config file and environment
variables:

- `VSOMEIP_CONFIGURATION`: path to the vsomeip config JSON file.
- `VSOMEIP_APPLICATION_NAME`: vsomeip application name, must match the
  config file's `.applications[].name`.

The two deployed configs are
`vehicle_service/vehicle-state-server/config/vsomeip.json` (server,
TCP-only reliable endpoint on `172.30.0.12:30512`) and
`vehicle_service/provider-vehicle-state/config/vsomeip.json` (provider,
pointing at that same remote endpoint), each annotated inline with
`_comment` fields citing the relevant spec sections (vsomeip's JSON parser
is strict and ignores unknown keys, so these are safe, inert documentation).
Both assign fixed IPs and declare the `0x1770.0x0001` service explicitly in
their `services` section — SOME/IP Service Discovery over a Docker bridge
network has been observed to fail to exchange OFFER/SUBSCRIBE when relying
only on `service-discovery.enable: true` without an explicit `services`
section, even though raw UDP multicast reaches fine.

`someip_feeder`'s own SOME/IP client/service IDs come directly from
`src/lib/vehicle_state/vehicle_state_ids.h` (not environment-variable
overrides).

## Extending

- `SomeIPClient` (`src/lib/someip_client/someip_client.h`) provides generic
  service discovery, event subscription, and request/response, dispatching
  incoming payloads to a callback:
  ```c++
  typedef std::function <
    int (vsomeip::service_t service, vsomeip::instance_t instance, vsomeip::method_t event,
        const uint8_t *payload, size_t size)
  > message_callback_t;
  ```
- `SomeIPConfig` carries the service/instance/eventgroup/event/method IDs
  and transport (TCP/UDP) to use; `VehicleStateAdapter::InitSomeipClient()`
  builds one from `vehicle_state_ids.h`'s constants.
- `VehicleStateAdapter::on_someip_message()` and `on_actuator_change()`
  (`src/someip_feeder/vehicle_state_adapter.cc`) are the reference example
  for decoding an incoming LKV event and converting an actuator write into
  an outgoing `WriteVehicleState` request, respectively.

## Related Files

- `../docker-compose.yml` (repository root) — service definitions, network,
  volumes, including `metapp`
- `vehicle_service/databroker/Dockerfile` — KUKSA Databroker build definition
- `vehicle_service/vehicle-state-server/Dockerfile`,
  `vehicle_service/vehicle-state-server/start.sh`,
  `vehicle_service/vehicle-state-server/config/vsomeip.json` — ECU simulator
  build/startup/config
- `vehicle_service/provider-vehicle-state/Dockerfile`,
  `vehicle_service/provider-vehicle-state/start.sh`,
  `vehicle_service/provider-vehicle-state/config/vsomeip.json` — provider
  build/startup/config
- `vehicle_service/workspace/kuksa-someip-provider/` — the shared provider
  source tree (`someip_feeder`, `vehicle_state_service` example,
  `vehicle_state`, `someip_client`, and `broker_feeder` libraries)

## Known Limitations / Caveats

- **kuksa-databroker is built fresh from the latest main branch each time.**
  The provider uses the `kuksa.val.v1.VAL` gRPC API (`Get`/`Set`/`Subscribe`/
  `GetMetadata`).
- **SOME/IP Service Discovery (SD) works by assigning containers fixed IPs
  and explicitly declaring a `services` section in `vsomeip.json`** (fixed
  port and, where applicable, eventgroup multicast settings per service).
  With only `service-discovery.enable: true` set (no `services` section), SD
  can fail to actually exchange OFFER/SUBSCRIBE even though raw UDP
  multicast reaches fine over the Docker bridge network. Both containers'
  `vsomeip.json` assign fixed IPs (`172.30.0.12` / `172.30.0.13`) and
  declare the `0x1770.0x0001` service explicitly.
- **`VehicleStateService` is TCP-only** (no "unreliable"/UDP endpoint is
  configured at all, per SYS-002). A vsomeip client that calls
  `runtime::create_request()` with no argument defaults to `_reliable=false`
  (UDP), which then fails silently with "Routing info for remote service
  could not be found!" since no UDP routing entry exists for a TCP-only
  service. `someip_client.cc`'s `SendRequest()` passes the client's own
  configured transport (`create_request(use_tcp_)`) to avoid this.
