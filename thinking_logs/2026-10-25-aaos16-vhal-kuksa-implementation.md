# 2026-10-25 AAOS16 VHAL ↔ KUKSA Implementation

- Request: execute `docs/planing.md`, writing a summary to `docs/doing.md` at each milestone.
- Decisions already made (by the user): option A only, ESC/ABS not connected, STALE ignored, driver seat on the left, `FUEL_LEVEL_LOW` threshold 0%, connection through the published port, input through webcan.
- Design simplification: the Fake default config contains all the required properties, so `FakeVehicleHardware` is inherited and only the value injection (`writeValue`) and the HVAC `setValues` are replaced.
- Implementation: added `KuksaVehicleHardware` to `vhal_proxy_server` (enabled by `KUKSA_ADDR`). The patch is `aaos16/patches/0002-vhal-proxy-server-kuksa.patch`.
- Verification: unit test of the conversions OK, `m vhal_proxy_server` OK, `aaos16/tests/kuksa_vhal_e2e.py` all items PASS, reconnect on Databroker restart, write timeout notification and STALE being ignored confirmed.
- Specs: added SWARCH-003, updated SWARCH-000 / SYS-003 / SWRS-Android-VehicleApp.
- Not done: the 100-case test, INVALID input, a long-run test.
- Side effects: the containers were recreated by the `aaos16/docker-compose.yml` change, and Cuttlefish is left running (stop it with `cvd.sh stop`).

## Addendum: webcan HVAC ECU response
- Request: SWARCH-000-R-005 is already resolved (cause: the vSOMEIP version and opening the UDP ports) → update its Status to Done. Add handling for the problem that HVAC signals written from webcan get no response, and append to doing.md.
- Implementation: added a 0x526 → 0x323 HVAC ECU emulation to webcan (applies changes only; switch with `/api/hvac_emulation`). Changed the e2e test to need no manual rewrite, and it PASSed twice in a row.
- Side effects: recreated gateway-ecu / vehicle-state-provider from the new path (the bind mount of the old path was invalid).

## Addendum: bring planing.md in line with the implementation
- Request: fix the stale statements in `docs/planing.md` (for example, open issue 2 still treated SWARCH-000-R-005 as unresolved).
- Changes: added a status line; marked option A as chosen; fixed the mapping notes to the implemented values (fuel capacity 15000 mL, airbag `FRONT` 0x02, `DOOR_POS` 0/1, fan level rules, INVALID keeps the last value, 3 s write timeout); corrected the SWARCH-003 file name; replaced "to be added" items with what was done; split §7 into resolved issues (fuel capacity, R-005, DOOR_POS/airbag) and the open issues that remain (100-case test, INVALID integration test, 2-sample recovery on the VHAL side, long run); updated the R-005 and dependent-property risk rows.
