"""Consumption-only left integration, independent of Home Assistant."""

import math
from dataclasses import dataclass, field
from decimal import Decimal, DecimalException

WATT_SECONDS_PER_KWH = Decimal(3_600_000)


def parse_power(value: str, unit: str) -> Decimal:
    """Parse a finite, nonnegative power reading and normalize to watts."""
    if unit not in ("W", "kW"):
        raise ValueError(f"Unsupported power unit: {unit}")
    try:
        power = Decimal(value)
        if not power.is_finite() or power < 0:
            raise ValueError("Power must be finite and nonnegative")
        watts = power * 1000 if unit == "kW" else power
    except DecimalException as error:
        raise ValueError("Power is not numeric") from error
    if not math.isfinite(float(watts)):
        raise ValueError("Power exceeds the supported numeric range")
    return watts


@dataclass
class EnergyAccumulator:
    """Track a total with one cursor shared by samples and timer callbacks."""

    total: Decimal = Decimal(0)
    _power: Decimal | None = field(default=None, init=False)
    _time: float | None = field(default=None, init=False)

    def __post_init__(self) -> None:
        if not self.total.is_finite() or not math.isfinite(float(self.total)) or self.total < 0:
            raise ValueError("Energy total must be finite and nonnegative")

    def advance(self, now: float) -> None:
        """Settle the current valid interval without changing its power."""
        if not math.isfinite(now) or (self._time is not None and now < self._time):
            raise ValueError("Accounting clock must be finite and monotonic")
        if self._time is not None and self._power is not None:
            elapsed = Decimal(str(now - self._time))
            self.total += self._power * elapsed / WATT_SECONDS_PER_KWH
            self._time = now

    def update(self, power: Decimal | None, now: float) -> None:
        """Settle the old interval, then start a sample or suspend accounting."""
        if power is not None and (not power.is_finite() or power < 0):
            raise ValueError("Power must be finite and nonnegative")
        self.advance(now)
        self._power = power
        self._time = now if power is not None else None
