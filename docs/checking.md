# End-to-End Manual Check Procedure (in the Browser)

Inject CAN signals with webcan and confirm in the browser that they reach the VHAL of AAOS16 through KUKSA, and that operating HVAC from AAOS comes back all the way to CAN.

```
webcan (CAN injection) → Gateway ECU → provider → KUKSA Databroker ─┬→ metapp (instrument display)
                                                                    └→ VHAL (Cuttlefish / AAOS16)
AAOS HVAC operation → VHAL → KUKSA (actuator target) → provider → Gateway ECU → CAN 0x526 → webcan (stand-in HVAC ECU) → CAN 0x323 → (returns through the flow above)
```

For the design see [SWARCH-003](../specs/swarch/SWARCH-003-aaos-vhal-bridge.md); for the results see [doing.md](doing.md).

## 1. Screens

| Screen | URL | Purpose |
|---|---|---|
| webcan | http://localhost:8800/ | Inject and observe CAN signals, switch the HVAC ECU emulation ON/OFF |
| metapp (noVNC) | http://localhost:6080/vnc.html | Instrument that shows the KUKSA values (speed, rpm, gear, warning lamps) |
| Cuttlefish (WebRTC) | https://localhost:8443/ | The AAOS16 screen. HVAC is operated here too (ignore the self-signed certificate warning). When opened, `cvd-1` appears under "Available devices", so press **Connect**. To open it directly: https://localhost:8443/client.html?deviceId=cvd-1 |

For a remote host, use SSH port forwarding: `ssh -L 8800:localhost:8800 -L 6080:localhost:6080 -L 8443:localhost:8443 <user>@<host>`

## 2. Preparation (start-up)

```bash
# 2-1. KUKSA stack (databroker / gateway-ecu+webcan / provider / metapp)
cd ~/work/auto-as-linux-andorid/auto-as-linux
docker compose up -d databroker gateway-ecu vehicle-state-provider metapp
docker compose ps          # all 4 are Up

# 2-2. AAOS16 (Cuttlefish). vhal_proxy_server connects to KUKSA through KUKSA_ADDR
cd ~/work/auto-as-linux-andorid/aaos16
docker compose up -d
docker compose exec aaos16 sudo /etc/init.d/cuttlefish-host-resources start   # once after the container is created
docker compose exec aaos16 cvd.sh start                                       # takes a few minutes to boot
```

If `vhal_proxy_server` has not been rebuilt or the patch has not been applied, do §7 of [aaos16/README.md](../aaos16/README.md) first.

Start-up check (optional):

```bash
docker exec aaos16-builder grep "KuksaVehicleHardware" /workspace/aosp/cuttlefish_runtime/logs/launcher.log | tail -3
# "subscribed to 13 paths" should appear
```

## 3. Check procedure

### Step A: Send CAN signals from webcan

1. Open http://localhost:8800/. The header should show `WS connected` (green) and the "HVAC ECU emulation" box should be **checked**.
2. Under "Open CAN Bus", **Open** Device `simulator_v2` / Port `0` (not needed if CAN0 is already listed).
3. Under "Send by Signal (DBC)", choose `FMW_VCU` for the ECU and press **Start All FMW_VCU Messages (cyclic)**.
4. Set the following for each Message and press **Send/Start** (leave Period at 100). Enter physical values. Speed / TacoMeter can also be swept automatically ("Auto Sweep"). To compare fixed values, enter the values in the table below.

| Message | Signal | Input value | Expected VHAL value |
|---|---|---|---|
| Speed | VehicleSpeedRaw | 72 | `PERF_VEHICLE_SPEED` = 20.0 m/s |
| TacoMeter | EngineSpeedRaw | 2500 | `ENGINE_RPM` = 2500 |
| ParkingBrake | ParkingBrakeState | 1 | `PARKING_BRAKE_ON` = TRUE |
| Seatbelt | Row1DriverIsBelted | 1 | `SEAT_BELT_BUCKLED` (ROW_1_LEFT) = TRUE |
| DoorOpen | Row1DriverIsOpen | 1 | `DOOR_POS` (ROW_1_LEFT) = 1 |
| Airbag | Row1DriverAirbagDeployed | 1 | `SEAT_AIRBAGS_DEPLOYED` (ROW_1_LEFT) = FRONT(0x2) |
| LowFuel | FuelRelativeLevel | 40 | `FUEL_LEVEL` = 6000 mL, `FUEL_LEVEL_LOW` = FALSE |
| Reverse | ReverseState | 1 | `GEAR_SELECTION` = GEAR_REVERSE(0x2) |
| Aircon | AC_OnOff=1, Defrost=1, Mode=1, FanLevel=5, TargetTempRaw=22 | as listed | `HVAC_AC_ON`=TRUE, `HVAC_DEFROSTER`=TRUE, `HVAC_FAN_SPEED`=5, `HVAC_TEMPERATURE_SET`=22.0 |

5. Each message should appear in "Active Periodic Sends" as `fixed` (or `sweep`), and `tx` frames such as 0x120 should flow in the Trace below.

### Step B: See the KUKSA values in metapp

1. Open http://localhost:6080/vnc.html and press **Connect**.
2. Speed 72 km/h, rpm 2500, gear R, parking brake and fuel 40% should be displayed.
3. When a webcan value is changed, metapp should follow within 1 second (ESC / ABS are not shown in AAOS by design, so they are not checked on the AAOS side).

### Step C: See the values in the VHAL of AAOS16

1. Open https://localhost:8443/ and confirm that the Cuttlefish screen (AAOS) is displayed.
2. **HVAC (bar at the bottom of the screen)**: temperature 22, fan 5, A/C ON and defrost ON should match the Aircon values of Step A. The screen follows when the webcan Aircon is changed.
3. Properties other than HVAC (speed, rpm, gear, door, seat belt, airbag, fuel, parking brake) do not appear on a standard AAOS screen, so check them from a terminal:

```bash
# On the same host as the browser screen. Compare with "Expected VHAL value" in the table of Step A
adb_vget() { docker exec aaos16-builder bash -c "cd /workspace/aosp && out/host/linux-x86/bin/adb -s 127.0.0.1:6520 shell cmd car_service get-property-value $1 $2"; }
adb_vget PERF_VEHICLE_SPEED 0
adb_vget ENGINE_RPM 0
adb_vget GEAR_SELECTION 0
adb_vget FUEL_LEVEL 0
adb_vget SEAT_BELT_BUCKLED 1
adb_vget DOOR_POS 1
adb_vget SEAT_AIRBAGS_DEPLOYED 1
adb_vget PARKING_BRAKE_ON 0
```

To check everything automatically, run `python3 aaos16/tests/kuksa_vhal_e2e.py` (13 read items and write items).

### Step D: Operate HVAC from AAOS and bring it back to CAN

1. Confirm that the webcan "HVAC ECU emulation" is **ON**, and prepare to filter the Trace on `526`.
2. On the HVAC bar of the Cuttlefish screen (https://localhost:8443/), change the temperature to **26**, the fan to **7**, A/C to **OFF**, and the air direction to **floor**, for example.
3. Expected behavior (all within about 0.1–0.5 seconds):
   - **`rx 526`** (AirconCommand) appears in the webcan Trace and its value changes. The gate bits of Byte0 (bit0 AC / bit1 Temp / bit2 Fan / bit3 Def) are set.
   - Then the state is updated in **`tx 323`** (Aircon), for example Byte1 (FanLevel) = 7 and Byte2 (TargetTempRaw) = 20 (= 26°C).
   - The AAOS screen stays at the value that was set (it is not reverted). metapp reflects it too.
4. It can also be checked from a terminal: after `cmd car_service set-property-value HVAC_TEMPERATURE_SET 1 24`, `adb_vget HVAC_TEMPERATURE_SET 1` becomes 24.0.

### Step E: With the HVAC ECU emulation OFF

1. Turn the webcan header "HVAC ECU emulation" **OFF**.
2. Change the temperature in the AAOS HVAC.
3. Expected: `rx 526` appears in webcan but the `tx 323` value does not change, and **the AAOS value returns to the original after about 3 seconds**. A warning appears in the log:

```bash
docker exec aaos16-builder grep "not reflected" /workspace/aosp/cuttlefish_runtime/logs/launcher.log | tail -2
# KuksaVehicleHardware: 1 write(s) not reflected by Databroker in time
```

4. Turn the emulation **ON** again.

### Step F: Abnormal cases

| Operation | Expected |
|---|---|
| `docker restart databroker` | The VHAL bridge reconnects and resubscribes in about 1 second. The VHAL keeps the last value until the provider feeds KUKSA again, and then follows |
| `docker stop gateway-ecu` | The AAOS values stay at the last value (STALE is ignored by design). They follow again after restarting |
| Send 300 as Speed | The Gateway ECU rounds it to 255 km/h (SYS-001). The VHAL shows `PERF_VEHICLE_SPEED` = 70.83 m/s. Rejection of out-of-range values (keeping the last valid value) is confirmed by the unit test of the VHAL bridge and is hard to reproduce in this procedure |

Note: after stopping `gateway-ecu`, `vehicle-state-provider` may stop receiving events (KUKSA is no longer updated). In that case, recover with `docker compose restart vehicle-state-provider`.

## 4. Clean-up

```bash
docker exec aaos16-builder cvd.sh stop                       # stop Cuttlefish
cd ~/work/auto-as-linux-andorid/auto-as-linux && docker compose down   # stop the KUKSA stack (optional)
```

## 5. Troubleshooting

| Symptom | Check / action |
|---|---|
| The webcan screen does not open / `gateway-ecu` restarts repeatedly (`No SConstruct file found`) | The container's bind mount is invalid. Run `docker compose up -d --no-deps gateway-ecu` in `auto-as-linux/` |
| The Trace is empty even though Open was pressed in webcan | CAN0 is not open. Step A-2 |
| Neither metapp nor the VHAL updates | Check whether `handle_event` appears in the provider log (`docker logs --tail 20 vehicle-state-provider`). If it does not, restart the provider |
| KUKSA updates but only the VHAL does not | Check `KuksaVehicleHardware` in `launcher.log`. If `subscribed to 13 paths` is missing, check `KUKSA_ADDR` (`docker exec aaos16-builder printenv KUKSA_ADDR`) and reachability of port 55555 on the host |
| Speed and rpm change on their own | The webcan Auto Sweep is running. Hold it in "Active Periodic Sends", or resend a fixed value |
| The AAOS HVAC reverts even when operated | The emulation is OFF, or the provider/ECU path is stopped. Isolate it by looking at `rx 526` / `tx 323` in Steps D and E |
| `adb connect` to Cuttlefish fails | See Troubleshooting (restart procedure) in [aaos16/README.md](../aaos16/README.md) |
