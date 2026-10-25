"""VSS path constants and the read-signal/staleness tables (README.md §5-7.2)."""

from dataclasses import dataclass

VSS_SPEED = "Vehicle.Speed"
VSS_ENGINE_SPEED = "Vehicle.Powertrain.CombustionEngine.Speed"
VSS_GEAR = "Vehicle.Powertrain.Transmission.CurrentGear"
VSS_PARKING_BRAKE = "Vehicle.Chassis.ParkingBrake.IsEngaged"
VSS_ESC_ERROR = "Vehicle.ADAS.ESC.IsError"
VSS_ABS_ERROR = "Vehicle.ADAS.ABS.IsError"
VSS_SEATBELT = "Vehicle.Cabin.Seat.Row1.DriverSide.IsBelted"
VSS_DOOR_OPEN = "Vehicle.Cabin.Door.Row1.DriverSide.IsOpen"
VSS_AIRBAG = "Vehicle.Cabin.Seat.Row1.DriverSide.Airbag.IsDeployed"
VSS_FUEL_LEVEL = "Vehicle.Powertrain.FuelSystem.RelativeLevel"


@dataclass(frozen=True)
class SignalSpec:
    path: str
    stale_after_ms: int
    unknown_after_ms: int


# Thresholds reused from SYS-001 §2's CAN-layer timeouts, per README.md §6.
SIGNALS = {
    VSS_SPEED: SignalSpec(VSS_SPEED, stale_after_ms=300, unknown_after_ms=2000),
    VSS_ENGINE_SPEED: SignalSpec(VSS_ENGINE_SPEED, stale_after_ms=150, unknown_after_ms=2000),
    VSS_GEAR: SignalSpec(VSS_GEAR, stale_after_ms=150, unknown_after_ms=2000),
    VSS_PARKING_BRAKE: SignalSpec(VSS_PARKING_BRAKE, stale_after_ms=300, unknown_after_ms=2000),
    VSS_ESC_ERROR: SignalSpec(VSS_ESC_ERROR, stale_after_ms=300, unknown_after_ms=2000),
    VSS_ABS_ERROR: SignalSpec(VSS_ABS_ERROR, stale_after_ms=300, unknown_after_ms=2000),
    VSS_SEATBELT: SignalSpec(VSS_SEATBELT, stale_after_ms=300, unknown_after_ms=2000),
    VSS_DOOR_OPEN: SignalSpec(VSS_DOOR_OPEN, stale_after_ms=300, unknown_after_ms=2000),
    VSS_AIRBAG: SignalSpec(VSS_AIRBAG, stale_after_ms=300, unknown_after_ms=2000),
    VSS_FUEL_LEVEL: SignalSpec(VSS_FUEL_LEVEL, stale_after_ms=1500, unknown_after_ms=3000),
}

ALL_READ_PATHS = tuple(SIGNALS.keys())

GEAR_LABELS = {
    -1: "R",
    0: "N",
    1: "1",
    2: "2",
    3: "3",
    4: "4",
    5: "5",
    6: "6",
    126: "P",
    127: "D",
}


def gear_label(value) -> str:
    if value is None:
        return "--"
    try:
        return GEAR_LABELS.get(int(value), "--")
    except (TypeError, ValueError):
        return "--"
