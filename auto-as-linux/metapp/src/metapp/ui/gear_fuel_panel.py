"""Gear and parking-brake row (README.md §7.1)."""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QHBoxLayout, QLabel, QWidget

from ..vehicle_state import Freshness, VehicleState
from ..vss_paths import VSS_GEAR, VSS_PARKING_BRAKE, gear_label


class GearFuelPanel(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)

        self._gear_label = QLabel("--")
        self._gear_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._gear_label.setFixedSize(56, 56)
        self._gear_label.setStyleSheet(
            "font-size: 28px; font-weight: bold; border: 2px solid #666; border-radius: 6px;"
        )

        self._parking_brake_label = QLabel("(P)")
        self._parking_brake_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._parking_brake_label.setFixedSize(48, 32)

        layout.addWidget(QLabel("Gear:"))
        layout.addWidget(self._gear_label)
        layout.addStretch()
        layout.addWidget(self._parking_brake_label)

        self._set_parking_brake_style(False)

    def refresh(self, vehicle_state: VehicleState) -> None:
        gear_state = vehicle_state.signal(VSS_GEAR)
        if gear_state.freshness == Freshness.UNKNOWN:
            self._gear_label.setText("--")
        else:
            self._gear_label.setText(gear_label(gear_state.value))

        brake_state = vehicle_state.signal(VSS_PARKING_BRAKE)
        if brake_state.freshness == Freshness.UNKNOWN:
            applied = True  # conservative fallback per README.md §6
        else:
            applied = bool(brake_state.value)
        self._set_parking_brake_style(applied)

    def _set_parking_brake_style(self, applied: bool) -> None:
        color = "#d43b3b" if applied else "#2b2b2b"
        self._parking_brake_label.setStyleSheet(
            f"background-color: {color}; color: #f0f0f0; font-weight: bold; border-radius: 4px;"
        )
