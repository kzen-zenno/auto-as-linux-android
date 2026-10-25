# SYS-005: VSS Catalog Reference (Verified Against a Running Databroker)

## 1. Purpose

SYS-002 and SYS-003 name specific COVESA VSS paths. Several of those names
were written from memory/assumption rather than checked against a real VSS
catalog, and two turned out not to exist as written (see §4). This file
records the actual catalog data, extracted directly from a running
`kuksa-databroker` instance, so future changes to SYS-002/SYS-003 can be
checked against ground truth instead of repeating the same mistake.

## 2. Verified Source

- **Catalog**: COVESA VSS release **6.0**, file `data/vss-core/vss_release_6.0.json`
  bundled inside the `eclipse-kuksa/kuksa-databroker` build.
- **Server image**: built from `vehicle_service/databroker/Dockerfile`
  (`git clone https://github.com/eclipse-kuksa/kuksa-databroker.git`, no
  pinned tag/commit; started with `--vss data/vss-core/vss_release_6.0.json`).
- **Verified on**: 2026-08-23.
- **Verification method**: `docker exec databroker cat
  /app/kuksa-databroker/data/vss-core/vss_release_6.0.json`, parsed directly
  (not read from COVESA documentation), then cross-checked against a live
  `Set`/`Get`/`Subscribe` round trip via `databroker-cli` against the running
  container.

**Caveat**: the Dockerfile does not pin a commit, so a future image rebuild
could bundle a different VSS release if upstream changes the default
`--vss` file. Re-run the verification method above before trusting this
table again after any `databroker` image rebuild.

## 3. Verified Entries (signals used by SYS-002 / SYS-003)

| Path | type | datatype | unit | allowed / range |
|---|---|---|---|---|
| `Vehicle.Speed` | sensor | float | km/h | - |
| `Vehicle.Powertrain.CombustionEngine.Speed` | sensor | float | rpm | - |
| `Vehicle.Powertrain.Transmission.CurrentGear` | sensor | int8 | - | - |
| `Vehicle.Chassis.ParkingBrake.IsEngaged` | actuator | boolean | - | - |
| `Vehicle.Cabin.HVAC.IsAirConditioningActive` | actuator | boolean | - | - (vehicle-wide, not per-station) |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature` | actuator | float | Celsius | - |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed` | actuator | uint8 | percent | 0..100 |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.AirDistribution` | actuator | string | - | `UP`, `MIDDLE`, `DOWN` |
| `Vehicle.ADAS.ESC.IsError` | sensor | boolean | - | - |
| `Vehicle.ADAS.ABS.IsError` | sensor | boolean | - | - |
| `Vehicle.Cabin.Seat.Row1.DriverSide.IsBelted` | sensor | boolean | - | - |
| `Vehicle.Cabin.Door.Row1.DriverSide.IsOpen` | actuator | boolean | - | - |
| `Vehicle.Cabin.Seat.Row1.DriverSide.Airbag.IsDeployed` | sensor | boolean | - | - |
| `Vehicle.Powertrain.FuelSystem.RelativeLevel` | sensor | uint8 | percent | 0..100 |

`Vehicle.Cabin.HVAC.Station.{Row1,Row2,Row3,Row4}.{Driver,Passenger}` all
exist with `AirDistribution` / `FanSpeed` / `Temperature` children, so
`Row1.DriverSide` (the instance fixed earlier for Seat/Door/Airbag) has a
direct HVAC-station equivalent (`Row1.Driver`) for those three signals.
`Vehicle.Cabin.Seat.Row1.DriverSide.*` and
`Vehicle.Cabin.Door.Row1.DriverSide.*` were confirmed to exist with these
exact instance names in the catalog.

`type` (`sensor` / `actuator` / `attribute`) is the catalog's own
classification; it does not restrict which direction the SOME/IP provider
publishes data through `kuksa.val.v1.VAL.Set` (that RPC writes `value`
regardless of the entry's declared type in the databroker version verified
here).

## 4. Corrections This Reference Produced

Two paths named in SYS-002/SYS-003 do not exist in the catalog above and
were corrected there (with this file as the citation) after a live `Set`
against the running databroker returned `NOT_FOUND`:

| Previously written (wrong) | Actual catalog path |
|---|---|
| `Vehicle.Powertrain.Transmission.Gear` | `Vehicle.Powertrain.Transmission.CurrentGear` |
| `Vehicle.Cabin.HVAC.Station.Row1.Driver.IsAirConditioningActive` | `Vehicle.Cabin.HVAC.IsAirConditioningActive` (vehicle-wide, no station/instance) |

## 5. Gap Closed by Overlay: No Standard Path for Aircon Defrost

`0x1009 Aircon Defrost` (SYS-002) has no equivalent anywhere in this
catalog under `Vehicle.Cabin.HVAC.*` or elsewhere searched. SYS-002/SYS-003
already call this out as a "custom overlay" requiring a non-standard path;
this reference confirms there is no standard path to use instead. A custom
path cannot be fed to, or subscribed against, an unmodified real
`kuksa.val.v1` databroker (`Set`/`Subscribe` both reject unknown paths --
and a `Subscribe` request that mixes one unknown path with otherwise-valid
paths fails entirely, not just for the unknown one).

**Resolved** by starting the databroker with an additional overlay file
rather than by dropping the signal. The chosen path is

```
Vehicle.Cabin.HVAC.Station.Row1.Driver.IsDefrosting   boolean, actuator
```

`actuator` because SYS-002 4.1 lists `0x1009` as a write-request key
(the writable range is `0x1006..0x100A`) and SWARCH-002 6 scenario 4
exercises a defrost write.

Deployment: `vehicle_service/databroker/vss_fmw_overlay.json`, passed as the
second element of the databroker's comma-separated `--vss` list
(`data/vss-core/vss_release_6.0.json,/app/vss_fmw_overlay.json`).
kuksa-databroker processes each file in the list independently and
registers one entry per *leaf* datapoint, so the overlay repeating the
`Vehicle` -> `Cabin` -> `HVAC` -> `Station` -> `Row1` -> `Driver` branch
chain does not collide with the standard catalog; only the new leaf is
added. Note that every entry -- branches included -- must carry a
`description`, which the parser requires.

A databroker started **without** this overlay reintroduces the
all-or-nothing `Subscribe` failure described above for all HVAC actuator
paths, not just defrost.

## 6. Trace

- SYS-002: SOME/IP-to-VSS key mapping this reference verifies
- SYS-003: VSS property definitions this reference verifies
- SYS-000: owner of the overall SYS structure and open-item tracking
