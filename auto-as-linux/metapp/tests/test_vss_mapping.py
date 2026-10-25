"""README.md §5/§7.2: the read-path set and the gear label mapping."""

from metapp.vss_paths import (
    ALL_READ_PATHS,
    SIGNALS,
    VSS_ABS_ERROR,
    VSS_AIRBAG,
    VSS_DOOR_OPEN,
    VSS_ENGINE_SPEED,
    VSS_ESC_ERROR,
    VSS_FUEL_LEVEL,
    VSS_GEAR,
    VSS_PARKING_BRAKE,
    VSS_SEATBELT,
    VSS_SPEED,
    gear_label,
)

EXPECTED_PATHS = {
    VSS_SPEED,
    VSS_ENGINE_SPEED,
    VSS_GEAR,
    VSS_PARKING_BRAKE,
    VSS_ESC_ERROR,
    VSS_ABS_ERROR,
    VSS_SEATBELT,
    VSS_DOOR_OPEN,
    VSS_AIRBAG,
    VSS_FUEL_LEVEL,
}


def test_all_read_paths_matches_readme_table():
    assert set(ALL_READ_PATHS) == EXPECTED_PATHS
    assert set(SIGNALS.keys()) == EXPECTED_PATHS


def test_gear_label_mapping():
    assert gear_label(-1) == "R"
    assert gear_label(0) == "N"
    assert gear_label(1) == "1"
    assert gear_label(6) == "6"
    assert gear_label(126) == "P"
    assert gear_label(127) == "D"


def test_gear_label_unmapped_or_missing_is_dash():
    assert gear_label(7) == "--"
    assert gear_label(-2) == "--"
    assert gear_label(None) == "--"
