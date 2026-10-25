"""README.md §6: per-signal staleness threshold boundaries."""

import pytest

from metapp.vehicle_state import Freshness, freshness_for_age
from metapp.vss_paths import SIGNALS, VSS_FUEL_LEVEL, VSS_SPEED

SPEED_SPEC = SIGNALS[VSS_SPEED]  # 300 ms / 2000 ms
FUEL_SPEC = SIGNALS[VSS_FUEL_LEVEL]  # 1500 ms / 3000 ms


@pytest.mark.parametrize(
    "age_ms,expected",
    [
        (0, Freshness.OK),
        (299, Freshness.OK),
        (300, Freshness.STALE),
        (1999, Freshness.STALE),
        (2000, Freshness.UNKNOWN),
        (10_000, Freshness.UNKNOWN),
    ],
)
def test_speed_thresholds(age_ms, expected):
    assert freshness_for_age(SPEED_SPEC, age_ms) == expected


@pytest.mark.parametrize(
    "age_ms,expected",
    [
        (0, Freshness.OK),
        (1499, Freshness.OK),
        (1500, Freshness.STALE),
        (2999, Freshness.STALE),
        (3000, Freshness.UNKNOWN),
    ],
)
def test_fuel_thresholds(age_ms, expected):
    assert freshness_for_age(FUEL_SPEC, age_ms) == expected
