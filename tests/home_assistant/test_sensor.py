"""Energy values come from durable totals, never the mutable accumulator."""

import asyncio
from decimal import Decimal
from unittest.mock import patch

from homeassistant.helpers import entity_registry as er

from custom_components.mfi.storage import StorageError


async def test_committed_only_and_availability(isolated_hass, create_mqtt_device, setup_companion):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    port = next(iter(manager.ports.values()))
    energy_id = er.async_entries_for_config_entry(er.async_get(isolated_hass), entry.entry_id)[
        0
    ].entity_id
    state = isolated_hass.states.get(energy_id)
    assert state.attributes["device_class"] == "energy"
    assert state.attributes["state_class"] == "total"
    assert state.attributes["unit_of_measurement"] == "kWh"
    port.accumulator.total = Decimal("0.1")
    gate = asyncio.Event()
    original_save = manager.store.async_save

    async def delayed_save(snapshot):
        await gate.wait()
        await original_save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_save):
        flush = asyncio.create_task(manager.async_flush())
        await asyncio.sleep(0)
        assert isolated_hass.states.get(energy_id).state == "0"
        port.accumulator.total = Decimal("0.2")
        isolated_hass.states.async_set(device.sources[0].entity_id, "unavailable")
        await asyncio.sleep(0)
        gate.set()
        await flush
    await isolated_hass.async_block_till_done()
    assert manager.committed.ports[0].total == Decimal("0.1")
    assert isolated_hass.states.get(energy_id).state == "unavailable"


async def test_storage_failure_and_retry(isolated_hass, create_mqtt_device, setup_companion):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    port = next(iter(manager.ports.values()))
    port.accumulator.total = Decimal("1.25")
    with patch.object(manager.store, "async_save", side_effect=StorageError("disk full")):
        await manager.async_flush()
    assert manager.committed.ports[0].total == 0
    assert manager.storage_error is not None
    await manager.async_flush()
    assert manager.committed.ports[0].total == Decimal("1.25")
    assert manager.storage_error is None
