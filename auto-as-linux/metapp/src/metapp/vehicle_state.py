"""Latest value per VSS path plus derived freshness (README.md §4.1, §6)."""

import time
from dataclasses import dataclass
from enum import Enum, auto
from typing import Any, Dict, Optional

from PyQt6.QtCore import QObject, QTimer, pyqtSignal

from .vss_paths import ALL_READ_PATHS, SIGNALS, SignalSpec

# Staleness is a function of elapsed time, not just of receiving (or not
# receiving) messages, so it must be re-derived on a timer, not only on update.
_POLL_INTERVAL_MS = 200


class Freshness(Enum):
    OK = auto()
    STALE = auto()
    UNKNOWN = auto()


def freshness_for_age(spec: SignalSpec, age_ms: float) -> Freshness:
    if age_ms >= spec.unknown_after_ms:
        return Freshness.UNKNOWN
    if age_ms >= spec.stale_after_ms:
        return Freshness.STALE
    return Freshness.OK


@dataclass
class SignalState:
    value: Optional[Any] = None
    timestamp: Optional[float] = None  # seconds since epoch (UTC)
    freshness: Freshness = Freshness.UNKNOWN


class VehicleState(QObject):
    stateChanged = pyqtSignal()
    connectionChanged = pyqtSignal(bool)

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self._signals: Dict[str, SignalState] = {path: SignalState() for path in ALL_READ_PATHS}
        self._connected = False

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._recompute_freshness)
        self._timer.start(_POLL_INTERVAL_MS)

    def signal(self, path: str) -> SignalState:
        return self._signals[path]

    def is_connected(self) -> bool:
        return self._connected

    def set_connected(self, connected: bool) -> None:
        if connected == self._connected:
            return
        self._connected = connected
        self.connectionChanged.emit(connected)

    def update_value(self, path: str, value: Any, timestamp: float) -> None:
        state = self._signals.get(path)
        if state is None:
            return
        state.value = value
        state.timestamp = timestamp
        state.freshness = Freshness.OK
        self.stateChanged.emit()

    def mark_all_unknown(self) -> None:
        # A dropped connection is a certain loss of freshness, not a guess (README.md §4.2).
        for state in self._signals.values():
            state.freshness = Freshness.UNKNOWN
        self.stateChanged.emit()

    def _recompute_freshness(self) -> None:
        now = time.time()
        changed = False
        for path, state in self._signals.items():
            if state.timestamp is None:
                continue
            spec = SIGNALS[path]
            age_ms = (now - state.timestamp) * 1000.0
            new_freshness = freshness_for_age(spec, age_ms)
            if new_freshness != state.freshness:
                state.freshness = new_freshness
                changed = True
        if changed:
            self.stateChanged.emit()
