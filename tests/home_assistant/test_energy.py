"""Deterministic interval accounting with no Home Assistant runtime."""

from decimal import Decimal

import pytest

from custom_components.mfi.energy import EnergyAccumulator, parse_power


def test_constant_load():
    meter = EnergyAccumulator()
    meter.update(Decimal(100), 0)
    for now in range(60, 3601, 60):
        meter.advance(now)
    assert abs(meter.total - Decimal("0.1")) < Decimal("0.000001")


def test_step_uses_previous_power_and_never_counts_twice():
    meter = EnergyAccumulator()
    meter.update(Decimal(100), 0)
    meter.advance(1800)
    meter.update(Decimal(200), 1800)
    meter.advance(3600)
    meter.advance(3600)
    assert meter.total == Decimal("0.15")


def test_invalid_interval_and_restoration_are_not_backfilled():
    meter = EnergyAccumulator(Decimal("12.5"))
    meter.update(Decimal(100), 0)
    meter.update(None, 1800)
    meter.advance(2400)
    meter.update(Decimal(100), 2400)
    meter.advance(4200)
    assert meter.total == Decimal("12.6")
    restored = EnergyAccumulator(meter.total)
    restored.advance(10000)
    restored.update(Decimal(100), 10000)
    assert restored.total == meter.total


@pytest.mark.parametrize("value", ["unknown", "unavailable", "NaN", "Infinity", "-1", "bad"])
def test_invalid_power(value):
    with pytest.raises(ValueError):
        parse_power(value, "W")


def test_units_and_zero():
    assert parse_power("0.1", "kW") == parse_power("100", "W")
    meter = EnergyAccumulator()
    meter.update(parse_power("0", "W"), 0)
    meter.advance(3600)
    assert meter.total == 0
    with pytest.raises(ValueError):
        parse_power("100", "Wh")


@pytest.mark.parametrize("time", [-1, float("nan"), float("inf")])
def test_invalid_accounting_clock(time):
    meter = EnergyAccumulator()
    meter.update(Decimal(100), 0)
    with pytest.raises(ValueError):
        meter.advance(time)
    assert meter.total == 0
