"""Warning-lamp row: ESC / ABS / Seatbelt / Door / Airbag / Fuel (README.md §7.1, §7.3)."""

from enum import Enum, auto

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QHBoxLayout, QLabel, QWidget

from ..vehicle_state import Freshness, VehicleState
from ..vss_paths import (
    VSS_ABS_ERROR,
    VSS_AIRBAG,
    VSS_DOOR_OPEN,
    VSS_ESC_ERROR,
    VSS_FUEL_LEVEL,
    VSS_SEATBELT,
)


class LampState(Enum):
    OFF = auto()
    ON = auto()
    INDETERMINATE = auto()


_COLORS = {
    LampState.OFF: "#2b2b2b",
    LampState.ON: "#d43b3b",
    LampState.INDETERMINATE: "#a8791a",
}


class _Lamp(QLabel):
    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setFixedSize(76, 40)
        self.set_state(LampState.OFF)

    def set_state(self, state: LampState) -> None:
        self.setStyleSheet(
            f"background-color: {_COLORS[state]}; color: #f0f0f0;"
            "border-radius: 6px; font-weight: bold;"
        )


class WarningPanel(QWidget):
    # Per §7.3, the fail-safe/indeterminate fallback applies from STALE
    # onward (not only at UNKNOWN) -- §7.1 describes lamps as a simpler
    # fresh/not-fresh model than §6's two-threshold table.
    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        self._esc = _Lamp("ESC")
        self._abs = _Lamp("ABS")
        self._belt = _Lamp("BELT")
        self._door = _Lamp("DOOR")
        self._airbag = _Lamp("AIRBAG")
        self._fuel = _Lamp("FUEL")
        for lamp in (self._esc, self._abs, self._belt, self._door, self._airbag, self._fuel):
            layout.addWidget(lamp)

    def refresh(self, vehicle_state: VehicleState) -> None:
        self._esc.set_state(self._fail_safe_on(vehicle_state, VSS_ESC_ERROR, bool))
        self._abs.set_state(self._fail_safe_on(vehicle_state, VSS_ABS_ERROR, bool))
        self._belt.set_state(self._indeterminate_off(vehicle_state, VSS_SEATBELT, active_when=False))
        self._door.set_state(self._indeterminate_off(vehicle_state, VSS_DOOR_OPEN, active_when=True))
        self._airbag.set_state(self._fail_safe_on(vehicle_state, VSS_AIRBAG, bool))
        self._fuel.set_state(self._fail_safe_on(vehicle_state, VSS_FUEL_LEVEL, lambda v: v == 0))

    @staticmethod
    def _fail_safe_on(vehicle_state: VehicleState, path: str, is_active) -> LampState:
        """ESC/ABS/Airbag/Fuel-empty: forced ON while not fresh (fail-safe, SYS-001)."""

        state = vehicle_state.signal(path)
        if state.freshness != Freshness.OK:
            return LampState.ON
        return LampState.ON if is_active(state.value) else LampState.OFF

    @staticmethod
    def _indeterminate_off(vehicle_state: VehicleState, path: str, active_when: bool) -> LampState:
        """Seatbelt/Door: indeterminate (dim) while not fresh, not forced ON."""

        state = vehicle_state.signal(path)
        if state.freshness != Freshness.OK:
            return LampState.INDETERMINATE
        return LampState.ON if bool(state.value) == active_when else LampState.OFF
