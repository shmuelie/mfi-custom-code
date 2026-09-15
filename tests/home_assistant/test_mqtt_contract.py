"""Exercise the companion through Home Assistant's actual MQTT entity code."""

import json
from datetime import timedelta
from decimal import Decimal

from homeassistant.helpers import entity_registry as er
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import (
    async_fire_mqtt_message,
    async_fire_time_changed_exact,
)


async def advance(hass, freezer, seconds):
    freezer.tick(timedelta(seconds=seconds))
    async_fire_time_changed_exact(hass, dt_util.utcnow())
    await hass.async_block_till_done()


async def test_constant_mqtt_power_and_expiry(
    isolated_hass, create_mqtt_device, setup_companion, freezer
):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    source_id = device.sources[0].entity_id
    first_reported = isolated_hass.states.get(source_id).last_reported
    for _ in range(60):
        await advance(isolated_hass, freezer, 60)
        async_fire_mqtt_message(
            isolated_hass, "mfi/test_mfi/port_1/state", json.dumps({"value": 100})
        )
        await isolated_hass.async_block_till_done()
    assert isolated_hass.states.get(source_id).last_reported == first_reported
    await manager.async_flush()
    before_expiry = manager.committed.ports[0].total
    assert abs(before_expiry - Decimal("0.1")) < Decimal("0.000001")

    for _ in range(3):
        await advance(isolated_hass, freezer, 60)
    await manager.async_flush()
    assert isolated_hass.states.get(source_id).state == "unavailable"
    assert abs(manager.committed.ports[0].total - before_expiry - Decimal("0.005")) < Decimal(
        "0.000001"
    )
    stopped = manager.committed.ports[0].total
    await advance(isolated_hass, freezer, 600)
    async_fire_mqtt_message(isolated_hass, "mfi/test_mfi/port_1/state", '{"value":100}')
    await isolated_hass.async_block_till_done()
    assert manager.committed.ports[0].total == stopped


async def test_offline_one_channel_keeps_other_ports(
    isolated_hass, create_mqtt_device, setup_companion, freezer
):
    device = await create_mqtt_device(8)
    entry = await setup_companion(device)
    manager = entry.runtime_data
    await advance(isolated_hass, freezer, 60)
    async_fire_mqtt_message(isolated_hass, "mfi/test_mfi/port_1/availability", "offline")
    await isolated_hass.async_block_till_done()
    assert next(iter(manager.ports.values())).reason == "unavailable"
    assert all(port.available for port in list(manager.ports.values())[1:])
    energy = er.async_entries_for_config_entry(er.async_get(isolated_hass), entry.entry_id)
    assert (
        sum(isolated_hass.states.get(entity.entity_id).state == "unavailable" for entity in energy)
        == 1
    )


async def test_retained_numeric_replay_is_not_mistaken_for_cleanup(
    isolated_hass, create_mqtt_device, setup_companion
):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    async_fire_mqtt_message(
        isolated_hass, "mfi/test_mfi/port_1/state", '{"value":321}', retain=True
    )
    await isolated_hass.async_block_till_done()
    assert isolated_hass.states.get(device.sources[0].entity_id).state == "321"
    assert next(iter(entry.runtime_data.ports.values())).available
    # Registry/state consumers cannot distinguish retained numeric replay.
    assert entry.data["freshness_confirmed"]
