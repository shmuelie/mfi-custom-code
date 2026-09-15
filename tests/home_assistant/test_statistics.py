"""Verify that real Recorder statistics accept the companion's energy entity."""

from datetime import timedelta
from decimal import Decimal

import pytest
from homeassistant.components.recorder.statistics import (
    list_statistic_ids,
    statistics_during_period,
)
from homeassistant.helpers import entity_registry as er
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import async_fire_mqtt_message
from pytest_homeassistant_custom_component.components.recorder.common import (
    async_wait_recording_done,
    do_adhoc_statistics,
    get_start_time,
)


@pytest.fixture(autouse=True)
def custom_integrations(recorder_mock, enable_custom_integrations):
    """Initialize the Recorder mocks before HA and the custom integration."""


@pytest.mark.parametrize("enable_statistics", [True])
async def test_energy_generates_sum_statistics(
    recorder_mock,
    isolated_hass,
    create_mqtt_device,
    setup_companion,
    freezer,
):
    freezer.move_to(get_start_time(dt_util.utcnow()) + timedelta(minutes=5))
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    async_fire_mqtt_message(isolated_hass, "mfi/test_mfi/port_1/state", '{"value":0}')
    await isolated_hass.async_block_till_done()
    start = get_start_time(dt_util.utcnow())
    entity_id = er.async_entries_for_config_entry(er.async_get(isolated_hass), entry.entry_id)[
        0
    ].entity_id
    await async_wait_recording_done(isolated_hass)
    freezer.tick(timedelta(seconds=30))
    next(iter(entry.runtime_data.ports.values())).accumulator.total = Decimal("0.1")
    await entry.runtime_data.async_flush()
    await async_wait_recording_done(isolated_hass)
    freezer.tick(timedelta(minutes=5))
    do_adhoc_statistics(isolated_hass, start=start)
    await async_wait_recording_done(isolated_hass)
    metadata = await isolated_hass.async_add_executor_job(
        list_statistic_ids, isolated_hass, {entity_id}
    )
    assert len(metadata) == 1
    assert metadata[0]["statistics_unit_of_measurement"] == "kWh"
    assert metadata[0]["has_sum"]
    values = await isolated_hass.async_add_executor_job(
        statistics_during_period,
        isolated_hass,
        start,
        start + timedelta(minutes=5),
        {entity_id},
        "5minute",
        None,
        {"sum", "state"},
    )
    assert values[entity_id][0]["sum"] == pytest.approx(0.1)
