"""Shared QPainter arc-gauge widget for the speedometer/tachometer (README.md §7.1)."""

import math
from typing import Optional

from PyQt6.QtCore import QPointF, QRectF, Qt
from PyQt6.QtGui import QColor, QFont, QPainter, QPen
from PyQt6.QtWidgets import QWidget

from ..vehicle_state import Freshness

_START_ANGLE_DEG = 225.0  # needle angle at value == minimum (Qt convention: CCW from 3 o'clock)
_SWEEP_DEG = 270.0  # total clockwise sweep from minimum to maximum


class ArcGauge(QWidget):
    """270-degree needle gauge; caller picks the §6 fallback value, this only styles by freshness."""

    def __init__(
        self,
        title: str,
        unit: str,
        minimum: float,
        maximum: float,
        major_step: float,
        fallback_value: float = 0.0,
        parent=None,
    ):
        super().__init__(parent)
        self._title = title
        self._unit = unit
        self._minimum = minimum
        self._maximum = maximum
        self._major_step = major_step
        self._fallback_value = fallback_value
        self._value: Optional[float] = None
        self._freshness = Freshness.UNKNOWN
        self.setMinimumSize(220, 220)

    def set_reading(self, value: Optional[float], freshness: Freshness) -> None:
        self._value = value
        self._freshness = freshness
        self.update()

    def _effective_value(self) -> float:
        if self._value is not None and self._freshness != Freshness.UNKNOWN:
            return self._value
        return self._fallback_value

    def _angle_for_value(self, value: float) -> float:
        span = self._maximum - self._minimum
        fraction = 0.0 if span == 0 else (value - self._minimum) / span
        return _START_ANGLE_DEG - fraction * _SWEEP_DEG

    def _needle_color(self) -> QColor:
        if self._freshness == Freshness.UNKNOWN:
            return QColor("#777777")
        if self._freshness == Freshness.STALE:
            return QColor("#c9a227")
        return QColor("#e03030")

    def paintEvent(self, event) -> None:  # noqa: N802 (Qt override)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.translate(self.width() / 2.0, self.height() / 2.0)

        side = min(self.width(), self.height())
        radius = side / 2.0 - 12
        rect = QRectF(-radius, -radius, radius * 2, radius * 2)

        painter.setPen(QPen(QColor("#3a3a3a"), 10, Qt.PenStyle.SolidLine, Qt.PenCapStyle.FlatCap))
        painter.drawArc(rect, int(_START_ANGLE_DEG * 16), int(-_SWEEP_DEG * 16))

        tick_font = QFont()
        tick_font.setPointSize(8)
        painter.setFont(tick_font)
        painter.setPen(QPen(QColor("#c0c0c0"), 2))
        steps = int(round((self._maximum - self._minimum) / self._major_step))
        for i in range(steps + 1):
            value = self._minimum + i * self._major_step
            angle_rad = math.radians(self._angle_for_value(value))
            outer = QPointF(radius * math.cos(angle_rad), -radius * math.sin(angle_rad))
            inner = QPointF((radius - 12) * math.cos(angle_rad), -(radius - 12) * math.sin(angle_rad))
            painter.drawLine(inner, outer)

        clamped = min(max(self._effective_value(), self._minimum), self._maximum)
        needle_angle = math.radians(self._angle_for_value(clamped))
        needle_color = self._needle_color()
        needle_len = radius - 18
        painter.setPen(QPen(needle_color, 4))
        painter.drawLine(
            QPointF(0, 0),
            QPointF(needle_len * math.cos(needle_angle), -needle_len * math.sin(needle_angle)),
        )
        painter.setBrush(needle_color)
        painter.drawEllipse(QPointF(0, 0), 6, 6)

        value_font = QFont()
        value_font.setPointSize(18)
        value_font.setBold(True)
        painter.setFont(value_font)
        painter.setPen(QColor("#f0f0f0"))
        painter.drawText(
            QRectF(-radius, radius * 0.25, radius * 2, 30),
            Qt.AlignmentFlag.AlignCenter,
            f"{clamped:.0f}",
        )

        label_font = QFont()
        label_font.setPointSize(9)
        painter.setFont(label_font)
        painter.setPen(QColor("#a0a0a0"))
        painter.drawText(
            QRectF(-radius, radius * 0.55, radius * 2, 20),
            Qt.AlignmentFlag.AlignCenter,
            self._unit,
        )
        painter.drawText(
            QRectF(-radius, radius * 0.78, radius * 2, 20),
            Qt.AlignmentFlag.AlignCenter,
            self._title,
        )
