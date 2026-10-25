# SWARCH-002: VehicleState SOME/IP Server

## 1. Role

`someip-server` simulates the AUTOSAR-CP Gateway ECU for local integration and
protocol testing. It owns a deterministic vehicle-state model and exposes the
SOME/IP contract in SYS-002. It does not connect to KUKSA Databroker.

The implementation is a self-contained example under the shared provider
source tree (`src/examples/vehicle_state_service`), with its own
application initialization, state handler, offer thread, signal handling,
and vSomeIP event subscription mechanics. It does not depend on any wiper
PoC payload or service constants (the wiper example has since been
retired; see SWARCH-000 §7).

**A real (non-simulator) Gateway ECU implementation now also exists**,
`autoas-as/src/app/fmw` (see `autoas-as/src/app/fmw/README.md`). It sources
`EvVehicleState`/`GetVehicleStateSnapshot`/`WriteVehicleState` from actual
decoded CAN signals (SYS-001) rather than a deterministic simulation, and
uses this project's own native `SomeIp`/`SomeIpXf`/`Sd`/`SoAd` stack (plain-C
generated skeleton) instead of vSomeIP, which required revising the LKV wire
format to the fixed-length schema in SYS-002 §5.2. It is not wire-compatible
with `someip-server`/the Linux-side provider described in this document; see
the SYS-002 §5.2 migration note and SWARCH-000 §9.

## 2. Offered service

| Item | Value |
|---|---:|
| Service | `0x1770` |
| Instance | `0x0001` |
| Version | `1.0` |
| Event | `0x8001` |
| Event group | `0x0001` |
| Snapshot method | `0x0001` |
| Write method | `0x0002` |
| State transport | TCP |
| Method transport | TCP |
| Event period | 100 ms |
| TCP port | `30512` (deployment-level choice; not mandated by SYS-002, only recorded in `vehicle_service/vehicle-state-server/config/vsomeip.json` and the matching remote entry in `vehicle_service/provider-vehicle-state/config/vsomeip.json`) |

The server offers the service after vSomeIP registration, accepts subscriptions
to the event group, and stops offering it before application shutdown. All
service and event endpoints are reliable TCP endpoints; no UDP event endpoint
is configured.

The `Version` row above is normative on the wire in both of the places
SYS-002 §5.1.1 lists (the SD entry's major version, and every message
header's interface version). On the `autoas-as` Gateway ECU these come from
`src/app/fmw/config/SomeIp/Network.json`: the server entry's `"major"` /
`"minor"` feed `Sd_ServerServices[].MajorVersion` / `.MinorVersion`, and each
method's / event's `"version"` feeds the SOME/IP header's interface version.
Both must be kept at 1 together, and the test client's
`test_client/config/Network.json` must match — `Sd.c` rejects an OFFER whose
major version differs from the client service's configured one.

## 3. Internal modules

| Module | Responsibility |
|---|---|
| `vehicle_state_service` | vSomeIP lifecycle, offers, handlers, and shutdown |
| `vehicle_state_model` | Thread-safe current values and deterministic simulation |
| `lkv_codec` | Encode/decode and bounds checking of LKV payloads |
| `vehicle_state_ids` | Service, method, event, and key constants |
| `write_policy` | HVAC-only write validation and result construction |

The model is initialized with valid values for every mandatory signal. A
simulation tick updates status values without changing key identity. HVAC values
change only after an accepted write unless simulation is explicitly configured
to exercise a failure case.

## 4. Method behavior

### GetVehicleStateSnapshot

- Request payload is empty or contains a protocol version.
- Response is the current LKV list using the same codec as the event.
- The response is sent over the request's reliable transport.

### WriteVehicleState

- Decode the entire request before mutating the model.
- Accept only keys `0x1006` to `0x100A`.
- Validate type and SYS-002 range for every entry.
- Apply all entries atomically when all are valid.
- Return an ACK containing result and the applied LKV entries.
- Publish the applied values in the next `EvVehicleState` event.

Malformed payloads, unsupported keys, and out-of-range values return an error
result and leave the model unchanged.

## 5. Event loop and shutdown

The event loop waits for the event subscription to become active, takes a
consistent model snapshot, stamps it with the send time and source `Gateway
ECU`, encodes it, and calls vSomeIP `notify`. The model lock is not held while
performing the network call.

SIGINT and SIGTERM set a stop flag, wake the event loop, stop offering the
service, join worker threads, and stop the vSomeIP application. No detached
worker may outlive the application object.

## 6. Test scenarios

1. Discover `0x1770.0001` and subscribe to `0x0001`.
2. Receive at least ten events and verify monotonically increasing timestamps.
3. Request a snapshot and compare it with the latest event by key and value.
4. Write valid HVAC ON/OFF, temperature, fan, defrost, and mode values; verify
   ACK followed by an event reflecting each value.
5. Send malformed, unknown-key, wrong-type, and out-of-range writes; verify no
   partial mutation.
6. Stop event transmission for four cycles and verify provider re-subscription
   and degraded state handling.
7. Resume transmission and verify recovery only after two valid samples.

## 7. Specification decisions finalized upstream

The following were previously open questions; they are now finalized by
SYS-002 / SYS-003 and ruled on in SYS-004, and are binding on this
implementation:

- All SYS-002 interfaces use TCP (SYS-004 CDC-F-006). vSomeIP configuration
  must use reliable endpoints for both the periodic event and the
  request/response methods.
- SYS-003 describes HVAC fan as percent `0..100`, while SYS-002 defines key
  `0x1008` as `0..7`. The adapter converts using `round(level * 100 / 7)` and
  maps writes to the nearest level; this makes 100% level 7 (SYS-004
  CDC-F-009).
- The binary encoding of `entries[]`, `source`, and `value` is defined in
  SYS-002 as a bounded little-endian LKV message. `someip-server` and the
  Linux-side provider must share that codec and reject malformed lengths.
  (This still describes the original variable-length TLV; the `autoas-as`
  Gateway ECU implementation uses the revised fixed-length schema instead --
  see SYS-002 §5.2's migration note. The two codecs are not interchangeable.)
- Confidence is encoded as four levels: `0=Unknown`, `1=Low (40%)`,
  `2=Medium (60%)`, and `3=High (100%)` (SYS-004 CDC-F-004).
- SYS-002 §5 sets Service Discovery TTL to twice the event cycle by default,
  i.e. 200 ms for `EvVehicleState`, rounded up to the nearest whole second
  since the SOME/IP-SD TTL field only counts seconds. The server's vSomeIP
  offer configuration must set this TTL explicitly rather than relying on a
  stack default: `vehicle-state-server/config/vsomeip.json` sets
  `service-discovery.ttl` to `1`. This requirement is scoped to the offering
  side's OFFER entries; the provider's own config does not set it, since it
  only sends FIND/SUBSCRIBE entries.
- SYS-003 lists several wildcard VSS paths. The initial server profile
  configures a single instance, `Row1.DriverSide`, for Seat, Door, and Airbag;
  the server must not infer an instance from the key alone, and additional
  instances are a future extension.

## 8. Trace

- SYS-002: SOME/IP service, event, method, and LKV payload contract this
  server implements
- SYS-003: VSS conversion rules (Fan, AirDistribution, Reverse/Gear) the
  server's payload values must remain compatible with
- SYS-004: CDC-F rulings finalizing TCP-only transport, confidence levels,
  and Fan conversion
- SWARCH-000: deployment view and overall migration boundary
- SWARCH-001: provider-side counterpart consuming this server's contract