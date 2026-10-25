#!/usr/bin/env python3
"""End-to-end check: webcan (CAN) -> Gateway ECU -> provider -> KUKSA -> VHAL.

Injects fixed CAN signals through webcan, then compares the value in the KUKSA
Databroker with the value an AAOS client reads through the VHAL
(`cmd car_service get-property-value`). Requires the auto-as-linux stack and
the Cuttlefish instance (with KUKSA_ADDR set) to be running.

Usage: kuksa_vhal_e2e.py [--settle SECONDS]
"""
import argparse
import json
import re
import subprocess
import sys
import time
import urllib.request

WEBCAN = "http://localhost:8800"
BUILDER = "aaos16-builder"
ADB = "out/host/linux-x86/bin/adb -s 127.0.0.1:6520"


def webcan_periodic(message, signals):
    body = json.dumps({"message": message, "signals": signals, "period_ms": 100}).encode()
    req = urllib.request.Request(
        f"{WEBCAN}/api/buses/0/periodic/signals", body, {"Content-Type": "application/json"})
    urllib.request.urlopen(req).read()


def ensure_bus():
    buses = json.load(urllib.request.urlopen(f"{WEBCAN}/api/buses"))
    if not buses:
        req = urllib.request.Request(
            f"{WEBCAN}/api/buses",
            json.dumps({"device": "simulator_v2", "port": 0, "baudrate": 500000}).encode(),
            {"Content-Type": "application/json"})
        urllib.request.urlopen(req).read()


def kuksa_get(path):
    q = json.dumps({"entries": [{"path": path, "view": "VIEW_CURRENT_VALUE", "fields": ["FIELD_VALUE"]}]})
    out = subprocess.run(
        ["docker", "run", "--rm", "--network", "vehicle_service_kuksa-net",
         "fullstorydev/grpcurl:latest", "-plaintext", "-d", q, "databroker:55555",
         "kuksa.val.v1.VAL/Get"], capture_output=True, text=True).stdout
    entry = json.loads(out)["entries"][0]
    value = entry.get("value")
    if not value:
        return None
    return next(v for k, v in value.items() if k != "timestamp")


def vhal_get(prop, area=0):
    out = subprocess.run(
        ["docker", "exec", BUILDER, "bash", "-c",
         f"cd /workspace/aosp && {ADB} shell cmd car_service get-property-value {prop} {area}"],
        capture_output=True, text=True).stdout
    m = re.search(r"Status: (\w+).*Value: (.*)\}", out)
    return (m.group(1), m.group(2).strip()) if m else (None, out.strip())


def last_aircon_command():
    """Decode the latest AirconCommand (0x526) frame seen by webcan."""
    frames = [f for f in json.load(urllib.request.urlopen(f"{WEBCAN}/api/trace?limit=500"))
              if f["canid"] == "526"]
    if not frames:
        return None
    d = bytes.fromhex(frames[-1]["data"])
    return {"fan": d[1], "temp": 16 + d[2] * 0.5, "ac": d[3] & 1, "mode": (d[3] >> 2) & 7}


def kuksa_target(path):
    q = json.dumps({"entries": [{"path": path, "view": "VIEW_TARGET_VALUE", "fields": ["FIELD_ACTUATOR_TARGET"]}]})
    out = subprocess.run(
        ["docker", "run", "--rm", "--network", "vehicle_service_kuksa-net",
         "fullstorydev/grpcurl:latest", "-plaintext", "-d", q, "databroker:55555",
         "kuksa.val.v1.VAL/Get"], capture_output=True, text=True).stdout
    t = json.loads(out)["entries"][0].get("actuatorTarget")
    return next(v for k, v in t.items() if k != "timestamp") if t else None


def vhal_set(prop, area, value):
    return subprocess.run(
        ["docker", "exec", BUILDER, "bash", "-c",
         f"cd /workspace/aosp && {ADB} shell cmd car_service set-property-value {prop} {area} {value}"],
        capture_output=True, text=True).stdout.strip()


def wait_vhal(prop, area, pred, timeout=10.0):
    """Poll a VHAL property until pred(value_text) holds. Returns (ok, value, seconds)."""
    t0 = time.time()
    while True:
        _, v = vhal_get(prop, area)
        if v and pred(v):
            return True, v, time.time() - t0
        if time.time() - t0 > timeout:
            return False, v, time.time() - t0


def num(s):
    return float(re.match(r"-?[\d.]+", s).group(0))


FAILS = 0


def check(name, ok, detail):
    global FAILS
    print(("PASS " if ok else "FAIL ") + name + "  " + detail)
    if not ok:
        FAILS += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--settle", type=float, default=2.0)
    args = ap.parse_args()

    ensure_bus()
    # Fixed, easy to verify values.
    webcan_periodic("Speed", {"VehicleSpeedRaw": 72})             # 72 km/h (physical value)
    webcan_periodic("TacoMeter", {"EngineSpeedRaw": 2500})          # 2500 rpm (physical value)
    webcan_periodic("ParkingBrake", {"ParkingBrakeState": 1, "AutoHoldActive": 0})
    webcan_periodic("Seatbelt", {"Row1DriverIsBelted": 1})
    webcan_periodic("DoorOpen", {"Row1DriverIsOpen": 1})
    webcan_periodic("Airbag", {"Row1DriverAirbagDeployed": 1})
    webcan_periodic("LowFuel", {"FuelRelativeLevel": 40})
    webcan_periodic("Reverse", {"ReverseState": 1})
    webcan_periodic("Aircon", {"AC_OnOff": 1, "Defrost": 1, "Mode": 1, "FanLevel": 5, "TargetTempRaw": 22})
    time.sleep(args.settle)

    st, v = vhal_get("PERF_VEHICLE_SPEED")
    check("speed", st == "AVAILABLE" and abs(num(v) - 20.0) < 0.05, f"kuksa={kuksa_get('Vehicle.Speed')} vhal={v}")
    st, v = vhal_get("ENGINE_RPM")
    check("rpm", abs(num(v) - 2500) < 1, f"kuksa={kuksa_get('Vehicle.Powertrain.CombustionEngine.Speed')} vhal={v}")
    st, v = vhal_get("PARKING_BRAKE_ON")
    check("parking brake", "true" in v.lower() or v.strip().startswith("1"), f"vhal={v}")
    st, v = vhal_get("SEAT_BELT_BUCKLED", 1)
    check("seat belt", "true" in v.lower() or v.strip().startswith("1"), f"vhal={v}")
    st, v = vhal_get("DOOR_POS", 1)
    check("door open", num(v) == 1, f"vhal={v}")
    st, v = vhal_get("SEAT_AIRBAGS_DEPLOYED", 1)
    check("airbag", "FRONT" in v or "0x2" in v or num(v) == 2, f"vhal={v}")
    st, v = vhal_get("FUEL_LEVEL")
    check("fuel level", abs(num(v) - 6000) < 1, f"kuksa={kuksa_get('Vehicle.Powertrain.FuelSystem.RelativeLevel')} vhal={v}")
    st, v = vhal_get("FUEL_LEVEL_LOW")
    check("fuel low", "false" in v.lower() or num(v) == 0, f"vhal={v}")
    st, v = vhal_get("GEAR_SELECTION")
    check("gear reverse", "REVERSE" in v, f"kuksa={kuksa_get('Vehicle.Powertrain.Transmission.CurrentGear')} vhal={v}")
    st, v = vhal_get("HVAC_AC_ON", 1)
    check("ac on", "true" in v.lower() or v.strip().startswith("1"), f"vhal={v}")
    st, v = vhal_get("HVAC_TEMPERATURE_SET", 1)
    check("temperature", abs(num(v) - 22.0) < 0.01, f"kuksa={kuksa_get('Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature')} vhal={v}")
    st, v = vhal_get("HVAC_FAN_SPEED", 1)
    check("fan", num(v) == 5, f"kuksa={kuksa_get('Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed')} vhal={v}")
    st, v = vhal_get("HVAC_DEFROSTER", 1)
    check("defrost", "true" in v.lower() or v.strip().startswith("1"), f"vhal={v}")

    # --- Write path: VHAL -> KUKSA actuator target -> provider -> ECU -> CAN
    # AirconCommand (0x526). webcan emulates the HVAC ECU: it applies a changed
    # command to its Aircon (0x323) state frame, which flows back to the VHAL.
    # The emulation reacts to command *changes*, so each write below picks a
    # value different from the command currently on the bus.
    cmd = last_aircon_command()
    temp = 24.0 if cmd and abs(cmd["temp"] - 26.0) < 0.01 else 26.0
    fan = 6 if cmd and cmd["fan"] == 7 else 7
    ac = 1 if cmd and cmd["ac"] == 0 else 0
    direction, direction_name = (1, "FACE") if cmd and cmd["mode"] == 5 else (2, "FLOOR")
    temp_path = "Vehicle.Cabin.HVAC.Station.Row1.Driver.Temperature"
    fan_path = "Vehicle.Cabin.HVAC.Station.Row1.Driver.FanSpeed"

    out = vhal_set("HVAC_TEMPERATURE_SET", 1, temp)
    check("temperature write accepted", "successfully" in out, out)
    ok, v, dt = wait_vhal("HVAC_TEMPERATURE_SET", 1, lambda x: abs(num(x) - temp) < 0.01)
    check("temperature follows write", ok, f"target={kuksa_target(temp_path)} vhal={v} after {dt:.1f}s")

    out = vhal_set("HVAC_FAN_SPEED", 1, fan)
    check("fan write accepted", "successfully" in out, out)
    ok, v, dt = wait_vhal("HVAC_FAN_SPEED", 1, lambda x: num(x) == fan)
    check("fan follows write", ok, f"target={kuksa_target(fan_path)} vhal={v} after {dt:.1f}s")

    out = vhal_set("HVAC_AC_ON", 1, ac)
    check("ac write accepted", "successfully" in out, out)
    ok, v, dt = wait_vhal("HVAC_AC_ON", 1, lambda x: ("true" in x.lower()) == bool(ac))
    check("ac follows write", ok, f"vhal={v} after {dt:.1f}s")

    out = vhal_set("HVAC_FAN_DIRECTION", 1, direction)
    check("air distribution write accepted", "successfully" in out, out)
    ok, v, dt = wait_vhal("HVAC_FAN_DIRECTION", 1, lambda x: direction_name in x)
    check("air distribution follows write", ok, f"vhal={v} after {dt:.1f}s")

    print("OK" if not FAILS else f"{FAILS} FAILED")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
