"""Top-level window wiring gauges/lamps/panel to VehicleState (README.md §7.1)."""

from PyQt6.QtWidgets import QHBoxLayout, QMainWindow, QVBoxLayout, QWidget

from ..vehicle_state import VehicleState
from ..vss_paths import VSS_ENGINE_SPEED, VSS_SPEED
from .gauge import ArcGauge
from .gear_fuel_panel import GearFuelPanel
from .warning_panel import WarningPanel


class MainWindow(QMainWindow):
    def __init__(self, vehicle_state: VehicleState, broker_addr: str, parent=None):
        super().__init__(parent)
        self._vehicle_state = vehicle_state
        self.setWindowTitle("metapp")

        self._warning_panel = WarningPanel()
        self._tach = ArcGauge("TACH", "rpm", minimum=0, maximum=8000, major_step=1000)
        self._speed = ArcGauge("SPEED", "km/h", minimum=0, maximum=255, major_step=50)
        self._gear_fuel_panel = GearFuelPanel()

        gauges_row = QHBoxLayout()
        gauges_row.addWidget(self._tach)
        gauges_row.addWidget(self._speed)

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addWidget(self._warning_panel)
        layout.addLayout(gauges_row)
        layout.addWidget(self._gear_fuel_panel)
        self.setCentralWidget(central)

        self._broker_addr = broker_addr
        self._update_status_bar(connected=False)

        vehicle_state.stateChanged.connect(self._refresh)
        vehicle_state.connectionChanged.connect(self._update_status_bar)

        self._refresh()

    def _refresh(self) -> None:
        speed_state = self._vehicle_state.signal(VSS_SPEED)
        self._speed.set_reading(speed_state.value, speed_state.freshness)

        engine_state = self._vehicle_state.signal(VSS_ENGINE_SPEED)
        self._tach.set_reading(engine_state.value, engine_state.freshness)

        self._warning_panel.refresh(self._vehicle_state)
        self._gear_fuel_panel.refresh(self._vehicle_state)

    def _update_status_bar(self, connected: bool) -> None:
        state = "connected" if connected else "disconnected"
        self.statusBar().showMessage(f"Databroker ({self._broker_addr}): {state}")
