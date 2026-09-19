"""Registry changes, options, restart, and data-loss protection."""

import asyncio
from decimal import Decimal
from unittest.mock import patch

import pytest
from homeassistant.helpers import entity_registry as er

from custom_components.mfi.const import CONF_SOURCE_DEVICE
from custom_components.mfi.source import destination_lock
from custom_components.mfi.storage import StorageError


async def test_source_rename_preserves_counter(isolated_hass, create_mqtt_device, setup_companion):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    registry = er.async_get(isolated_hass)
    energy = er.async_entries_for_config_entry(registry, entry.entry_id)[0]
    port = next(iter(entry.runtime_data.ports.values()))
    port.accumulator.total = Decimal("1.5")
    await entry.runtime_data.async_flush()
    old = isolated_hass.states.get(device.sources[0].entity_id)
    registry.async_update_entity(device.sources[0].entity_id, new_entity_id="sensor.renamed_power")
    isolated_hass.states.async_set("sensor.renamed_power", old.state, old.attributes)
    await isolated_hass.async_block_till_done()
    assert port.binding.entity_id == "sensor.renamed_power"
    assert (
        er.async_entries_for_config_entry(registry, entry.entry_id)[0].unique_id == energy.unique_id
    )
    assert port.accumulator.total >= Decimal("1.5")


async def test_reload_does_not_reset_total(isolated_hass, create_mqtt_device, setup_companion):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    next(iter(manager.ports.values())).accumulator.total = Decimal("2.5")
    assert await isolated_hass.config_entries.async_unload(entry.entry_id)
    committed = await manager.store.async_load()
    assert await isolated_hass.config_entries.async_setup(entry.entry_id)
    await isolated_hass.async_block_till_done()
    assert entry.runtime_data is not manager
    assert entry.runtime_data.committed == committed
    assert len(er.async_entries_for_config_entry(er.async_get(isolated_hass), entry.entry_id)) == 1
    assert isolated_hass.states.get(device.sources[0].entity_id).state == "100"


async def test_missing_checkpoint_blocks_reload(isolated_hass, create_mqtt_device, setup_companion):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    store = entry.runtime_data.store
    assert await isolated_hass.config_entries.async_unload(entry.entry_id)
    await store.async_remove()
    assert not await isolated_hass.config_entries.async_setup(entry.entry_id)
    assert entry.state.value == "setup_error"
    assert not store.path.exists()


async def test_exclude_reenable_and_failed_options(
    isolated_hass, create_mqtt_device, setup_companion
):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    options = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    options = await isolated_hass.config_entries.options.async_configure(
        options["flow_id"], {"next_step_id": "exclude"}
    )
    options = await isolated_hass.config_entries.options.async_configure(
        options["flow_id"], {"excluded_sources": [key]}
    )
    assert options["type"] == "create_entry"
    assert not manager.ports[key].available
    with patch.object(manager.store, "async_save", side_effect=StorageError("full")):
        with pytest.raises(StorageError):
            await manager.async_set_exclusions(set())
    assert manager.ports[key].binding.excluded
    await manager.async_set_exclusions(set())
    assert manager.ports[key].available


async def test_new_port_auto_added_and_old_identity_rebind(
    isolated_hass, create_mqtt_device, setup_companion
):
    device = await create_mqtt_device(1, model_id="58952")
    entry = await setup_companion(device)
    manager = entry.runtime_data
    registry = er.async_get(isolated_hass)
    await create_mqtt_device(2, model_id="58952")
    await isolated_hass.async_block_till_done()
    assert len(manager.ports) == 2
    assert len(er.async_entries_for_config_entry(registry, entry.entry_id)) == 2

    old_port = next(iter(manager.ports.values()))
    old_id = old_port.binding.registry_id
    old_port.accumulator.total = Decimal("5.5")
    isolated_hass.states.async_set(old_port.binding.entity_id, "unavailable")
    await isolated_hass.async_block_till_done()
    mqtt_entry = isolated_hass.config_entries.async_get_entry(manager.source_config_entry_id)
    replacement = registry.async_get_or_create(
        "sensor",
        "mqtt",
        "renamed_port_power",
        config_entry=mqtt_entry,
        device_id=device.id,
        original_device_class="power",
        unit_of_measurement="W",
        capabilities={"state_class": "measurement"},
    )
    isolated_hass.states.async_set(
        replacement.entity_id,
        "100",
        {"device_class": "power", "state_class": "measurement", "unit_of_measurement": "W"},
    )
    await isolated_hass.async_block_till_done()
    assert manager.ambiguous
    assert len(manager.ports) == 2
    await manager.async_rebind(old_port.binding.binding_id, replacement.entity_id)
    await isolated_hass.async_block_till_done()
    assert old_port.binding.registry_id == replacement.id
    assert old_id in manager.ignored_sources
    assert not manager.ambiguous
    assert len(manager.ports) == 2
    assert old_port.accumulator.total >= Decimal("5.5")


async def test_failed_unload_preserves_counter_until_restart(
    isolated_hass, create_mqtt_device, setup_companion
):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    next(iter(manager.ports.values())).accumulator.total = Decimal("1")
    with patch.object(manager.store, "async_save", side_effect=StorageError("full")):
        assert not await isolated_hass.config_entries.async_unload(entry.entry_id)
    assert entry.runtime_data is manager
    assert entry.state.value == "failed_unload"
    await manager.async_flush()
    assert manager.committed.ports[0].total >= Decimal("1")
    await manager.async_stop()


async def test_initially_disabled_power_is_not_enabled(
    isolated_hass, create_mqtt_device, setup_companion
):
    device = await create_mqtt_device(8)
    registry = er.async_get(isolated_hass)
    registry.async_update_entity(
        device.sources[0].entity_id, disabled_by=er.RegistryEntryDisabler.USER
    )
    await isolated_hass.async_block_till_done()
    entry = await setup_companion(device)
    assert len(er.async_entries_for_config_entry(registry, entry.entry_id)) == 7
    assert registry.async_get(device.sources[0].entity_id).disabled


async def test_hostname_change_requires_explicit_device_and_port_mapping(
    isolated_hass, create_mqtt_device, setup_companion
):
    old_device = await create_mqtt_device()
    entry = await setup_companion(old_device)
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    manager.ports[key].accumulator.total = Decimal("8.5")
    replacement = await create_mqtt_device(device_id="renamed_mfi")
    await manager.async_change_device(replacement.id)
    assert manager.source_device_id == replacement.id
    assert not manager.ports[key].available
    assert len(manager.ports) == 1
    await manager.async_rebind(key, replacement.sources[0].entity_id)
    assert manager.ports[key].available
    assert manager.committed.ports[0].total >= Decimal("8.5")
    assert entry.unique_id.endswith(replacement.id)


async def test_cannot_take_another_companions_device(
    isolated_hass, create_mqtt_device, setup_companion
):
    first = await create_mqtt_device(device_id="first_mfi")
    second = await create_mqtt_device(device_id="second_mfi")
    first_entry = await setup_companion(first)
    await setup_companion(second)
    with pytest.raises(ValueError, match="already"):
        await first_entry.runtime_data.async_change_device(second.id)


async def test_concurrent_recovery_cannot_claim_one_device_twice(
    isolated_hass, create_mqtt_device, setup_companion
):
    first = await setup_companion(await create_mqtt_device(device_id="first"))
    second = await setup_companion(await create_mqtt_device(device_id="second"))
    destination = await create_mqtt_device(device_id="destination")
    started = asyncio.Event()
    release = asyncio.Event()
    manager = first.runtime_data
    next(iter(manager.ports.values())).accumulator.total = Decimal(1)
    save = manager.store.async_save

    async def delayed_save(snapshot):
        started.set()
        await release.wait()
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_save):
        one = asyncio.create_task(manager.async_change_device(destination.id))
        await asyncio.wait_for(started.wait(), 5)
        two = asyncio.create_task(second.runtime_data.async_change_device(destination.id))
        await asyncio.sleep(0)
        release.set()
        results = await asyncio.gather(one, two, return_exceptions=True)
    assert sum(isinstance(result, ValueError) for result in results) == 1
    owners = [
        entry
        for entry in isolated_hass.config_entries.async_entries("mfi")
        if entry.runtime_data.source_device_id == destination.id
    ]
    assert len(owners) == 1
    assert manager.store.path.exists() and second.runtime_data.store.path.exists()


@pytest.mark.parametrize("policy", ["excluded", "disabled"])
async def test_suspended_binding_blocks_ambiguous_identity_enrollment(
    isolated_hass, create_mqtt_device, setup_companion, policy
):
    device = await create_mqtt_device(1, model_id="58952")
    registry = er.async_get(isolated_hass)
    if policy == "disabled":
        registry.async_update_entity(
            device.sources[0].entity_id, disabled_by=er.RegistryEntryDisabler.USER
        )
        await isolated_hass.async_block_till_done()
    entry = await setup_companion(device, [device.sources[0].id] if policy == "excluded" else None)
    manager = entry.runtime_data
    isolated_hass.states.async_set(device.sources[0].entity_id, "unavailable")
    replacement = registry.async_get_or_create(
        "sensor",
        "mqtt",
        "replacement_power",
        config_entry=isolated_hass.config_entries.async_get_entry(manager.source_config_entry_id),
        device_id=device.id,
        original_device_class="power",
        unit_of_measurement="W",
        capabilities={"state_class": "measurement"},
    )
    isolated_hass.states.async_set(
        replacement.entity_id,
        "100",
        {"device_class": "power", "state_class": "measurement", "unit_of_measurement": "W"},
    )
    await isolated_hass.async_block_till_done()
    assert manager.ambiguous
    assert len(manager.ports) == 1
    assert not er.async_entries_for_config_entry(registry, entry.entry_id)


async def test_reload_drains_recovery_and_rejects_queued_options(
    isolated_hass, create_mqtt_device, setup_companion
):
    original = await create_mqtt_device()
    entry = await setup_companion(original)
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    manager.ports[key].accumulator.total = Decimal("1")
    await manager.async_flush()
    destination = await create_mqtt_device(device_id="replacement")
    recovery_flow = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    recovery_flow = await isolated_hass.config_entries.options.async_configure(
        recovery_flow["flow_id"], {"next_step_id": "device"}
    )
    exclusion_flow = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    exclusion_flow = await isolated_hass.config_entries.options.async_configure(
        exclusion_flow["flow_id"], {"next_step_id": "exclude"}
    )
    gate = destination_lock(isolated_hass, f"{manager.source_config_entry_id}_{destination.id}")
    await gate.acquire()
    stopping = asyncio.Event()
    stop = manager.async_stop

    async def observed_stop():
        stopping.set()
        await stop()

    with patch.object(manager, "async_stop", new=observed_stop):
        recovering = asyncio.create_task(
            isolated_hass.config_entries.options.async_configure(
                recovery_flow["flow_id"],
                {CONF_SOURCE_DEVICE: destination.id, "same_device": True},
            )
        )
        await asyncio.sleep(0)
        assert manager._configuration_lock.locked()
        excluding = asyncio.create_task(
            isolated_hass.config_entries.options.async_configure(
                exclusion_flow["flow_id"], {"excluded_sources": [key]}
            )
        )
        await asyncio.sleep(0)
        unloading = asyncio.create_task(isolated_hass.config_entries.async_unload(entry.entry_id))
        try:
            await asyncio.wait_for(stopping.wait(), 5)
            await asyncio.sleep(0)
        finally:
            gate.release()
        recovery_result, exclusion_result, unloaded = await asyncio.gather(
            recovering, excluding, unloading
        )
    assert unloaded
    assert recovery_result["type"] == "abort"
    assert exclusion_result["type"] == "abort"
    assert entry.data[CONF_SOURCE_DEVICE] == original.id
    assert await isolated_hass.config_entries.async_setup(entry.entry_id)
    await isolated_hass.async_block_till_done()
    current = entry.runtime_data
    assert current is not manager
    current.ports[key].accumulator.total = Decimal("8.5")
    await current.async_flush()
    assert not current.ports[key].binding.excluded
    assert await isolated_hass.config_entries.async_reload(entry.entry_id)
    assert entry.runtime_data.committed.ports[0].total >= Decimal("8.5")
    assert not entry.runtime_data.committed.ports[0].excluded


@pytest.mark.parametrize("operation", ["exclude", "ignore", "rebind", "device", "flush"])
async def test_retired_manager_rejects_all_mutations(
    isolated_hass, create_mqtt_device, setup_companion, operation
):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    retired = entry.runtime_data
    key = next(iter(retired.ports))
    assert await isolated_hass.config_entries.async_reload(entry.entry_id)
    current = entry.runtime_data
    current.ports[key].accumulator.total = Decimal("8.5")
    await current.async_flush()
    checkpoint = await current.store.async_load()
    with pytest.raises(ValueError, match="runtime|stopping"):
        if operation == "exclude":
            await retired.async_set_exclusions({key})
        elif operation == "ignore":
            await retired.async_ignore_sources(set())
        elif operation == "rebind":
            await retired.async_rebind(key, device.sources[0].entity_id)
        elif operation == "device":
            await retired.async_change_device(device.id)
        else:
            await retired.async_flush()
    assert await current.store.async_load() == checkpoint


@pytest.mark.parametrize("fail_first_save", [False, True])
async def test_stop_settles_in_flight_configuration_before_final_snapshot(
    isolated_hass, create_mqtt_device, setup_companion, fail_first_save
):
    entry = await setup_companion(await create_mqtt_device())
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    started = asyncio.Event()
    release = asyncio.Event()
    save = manager.store.async_save
    writes = 0

    async def delayed_first_save(snapshot):
        nonlocal writes
        writes += 1
        if writes == 1:
            started.set()
            await release.wait()
            if fail_first_save:
                raise StorageError("Transient write failure")
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_first_save):
        configuring = asyncio.create_task(manager.async_set_exclusions({key}))
        await asyncio.wait_for(started.wait(), 5)
        stopping = asyncio.create_task(manager.async_stop())
        try:
            await asyncio.sleep(0)
            assert not stopping.done()
        finally:
            release.set()
        if fail_first_save:
            with pytest.raises(StorageError):
                await configuring
        else:
            await configuring
        await stopping
    stored = await manager.store.async_load()
    assert stored.ports[0].excluded is not fail_first_save
    assert manager.ports[key].binding.excluded is not fail_first_save
    assert stored == manager.committed
    assert manager.storage_error is None


@pytest.mark.parametrize("step", ["exclude", "ignore", "rebind", "device"])
async def test_open_options_abort_after_runtime_is_unloaded(
    isolated_hass, create_mqtt_device, setup_companion, step
):
    entry = await setup_companion(await create_mqtt_device())
    flow = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    assert await isolated_hass.config_entries.async_unload(entry.entry_id)
    result = await isolated_hass.config_entries.options.async_configure(
        flow["flow_id"], {"next_step_id": step}
    )
    assert result["type"] == "abort"
    assert result["reason"] == "not_loaded"
