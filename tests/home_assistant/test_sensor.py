"""Energy values come from durable totals, never the mutable accumulator."""

import asyncio
from decimal import Decimal
from unittest.mock import patch

import pytest
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


@pytest.mark.parametrize("operation", ["exclude", "ignore", "rebind"])
async def test_metadata_acknowledges_its_commit_not_a_later_save(
    isolated_hass, create_mqtt_device, setup_companion, operation
):
    device = await create_mqtt_device()
    entry = await setup_companion(device)
    manager = entry.runtime_data
    registry = er.async_get(isolated_hass)
    key = next(iter(manager.ports))
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
    started = asyncio.Event()
    release = asyncio.Event()
    save = manager.store.async_save
    writes = 0

    async def first_save_succeeds(snapshot):
        nonlocal writes
        writes += 1
        if writes != 1:
            raise StorageError("Later write failed")
        started.set()
        await release.wait()
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=first_save_succeeds):
        if operation == "exclude":
            request = manager.async_set_exclusions({key})
        elif operation == "ignore":
            request = manager.async_ignore_sources({replacement.id})
        else:
            request = manager.async_rebind(key, replacement.entity_id)
        pending = asyncio.create_task(request)
        await asyncio.wait_for(started.wait(), 5)
        registry.async_update_entity(
            manager.ports[key].binding.entity_id, name="Renamed while saving"
        )
        await asyncio.sleep(0)
        manager._request_save()
        release.set()
        await pending
        await manager._save_task
    assert writes == 2
    assert manager.storage_error is not None
    stored = await manager.store.async_load()
    assert stored == manager.committed
    if operation == "exclude":
        assert stored.ports[0].excluded and manager.ports[key].binding.excluded
    elif operation == "ignore":
        assert replacement.id in stored.ignored_sources
        assert replacement.id in manager.ignored_sources
    else:
        assert stored.ports[0].registry_id == replacement.id
        assert manager.ports[key].binding.registry_id == replacement.id


@pytest.mark.parametrize("fail_save", [False, True])
async def test_unignore_does_not_enroll_from_an_uncommitted_change(
    isolated_hass, create_mqtt_device, setup_companion, fail_save
):
    device = await create_mqtt_device(1, model_id="58952")
    entry = await setup_companion(device)
    manager = entry.runtime_data
    registry = er.async_get(isolated_hass)
    original = isolated_hass.states.get(device.sources[0].entity_id)
    isolated_hass.states.async_set(original.entity_id, "unavailable", original.attributes)
    extra = registry.async_get_or_create(
        "sensor",
        "mqtt",
        "extra_power",
        config_entry=isolated_hass.config_entries.async_get_entry(manager.source_config_entry_id),
        device_id=device.id,
        original_device_class="power",
        unit_of_measurement="W",
        capabilities={"state_class": "measurement"},
    )
    isolated_hass.states.async_set(extra.entity_id, "100", original.attributes)
    await isolated_hass.async_block_till_done()
    assert manager.ambiguous
    await manager.async_ignore_sources({extra.id})
    isolated_hass.states.async_set(original.entity_id, original.state, original.attributes)
    await isolated_hass.async_block_till_done()
    assert len(manager.ports) == 1
    flow = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    flow = await isolated_hass.config_entries.options.async_configure(
        flow["flow_id"], {"next_step_id": "ignore"}
    )
    started = asyncio.Event()
    release = asyncio.Event()
    save = manager.store.async_save

    async def delayed_save(snapshot):
        started.set()
        await release.wait()
        if fail_save:
            raise StorageError("Transient disk fault")
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_save):
        unignoring = asyncio.create_task(
            isolated_hass.config_entries.options.async_configure(flow["flow_id"], {"ignored": []})
        )
        await asyncio.wait_for(started.wait(), 5)
        try:
            isolated_hass.states.async_set(extra.entity_id, "200", original.attributes)
            await asyncio.sleep(0)
            await asyncio.sleep(0)
            pending_count = len(manager.ports)
        finally:
            release.set()
        result = await unignoring
    await isolated_hass.async_block_till_done()
    assert pending_count == 1
    if fail_save:
        assert result["errors"]["base"] == "storage_error"
        assert len(manager.ports) == 1
        assert manager.ignored_sources == {extra.id}
        await manager.async_flush()
        assert manager.storage_error is None
        result = await isolated_hass.config_entries.options.async_configure(
            flow["flow_id"], {"ignored": []}
        )
    assert result["type"] == "create_entry"
    await isolated_hass.async_block_till_done()
    assert len(manager.ports) == 2
    assert manager.ignored_sources == set()
    checkpoint = await manager.store.async_load()
    assert len(checkpoint.ports) == 2
    assert checkpoint.ignored_sources == ()
    assert await isolated_hass.config_entries.async_reload(entry.entry_id)
    assert entry.runtime_data.storage_error is None
    assert len(entry.runtime_data.ports) == 2


@pytest.mark.parametrize("initially_excluded", [False, True])
@pytest.mark.parametrize("fail_save", [False, True])
async def test_exclusion_accounting_changes_only_after_commit(
    isolated_hass, create_mqtt_device, setup_companion, freezer, initially_excluded, fail_save
):
    device = await create_mqtt_device()
    entry = await setup_companion(device, [device.sources[0].id] if initially_excluded else [])
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    flow = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    flow = await isolated_hass.config_entries.options.async_configure(
        flow["flow_id"], {"next_step_id": "exclude"}
    )
    started = asyncio.Event()
    release = asyncio.Event()
    save = manager.store.async_save

    async def delayed_save(snapshot):
        started.set()
        await release.wait()
        if fail_save:
            raise StorageError("Delayed write failure")
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_save):
        changing = asyncio.create_task(
            isolated_hass.config_entries.options.async_configure(
                flow["flow_id"],
                {"excluded_sources": [] if initially_excluded else [key]},
            )
        )
        await asyncio.wait_for(started.wait(), 5)
        try:
            freezer.tick(120)
            manager._tick(None)
            pending_excluded = manager.ports[key].binding.excluded
        finally:
            release.set()
        result = await changing
        await manager._save_task
    if fail_save:
        assert result["errors"]["base"] == "storage_error"
        isolated_hass.config_entries.options.async_abort(flow["flow_id"])
    else:
        assert result["type"] == "create_entry"
    assert pending_excluded == initially_excluded
    expected = Decimal(0) if initially_excluded else Decimal(100 * 120) / Decimal(3_600_000)
    assert abs(manager.ports[key].accumulator.total - expected) < Decimal("0.0000001")
    assert manager.ports[key].binding.excluded == (
        initially_excluded if fail_save else not initially_excluded
    )
    await manager.async_flush()
    assert abs((await manager.store.async_load()).ports[0].total - expected) < Decimal("0.0000001")
    assert await isolated_hass.config_entries.async_reload(entry.entry_id)
    assert abs(entry.runtime_data.committed.ports[0].total - expected) < Decimal("0.0000001")


async def test_failed_rebind_cannot_enroll_another_ambiguous_source(
    isolated_hass, create_mqtt_device, setup_companion
):
    device = await create_mqtt_device(1, model_id="58952")
    entry = await setup_companion(device)
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    registry = er.async_get(isolated_hass)
    original = isolated_hass.states.get(device.sources[0].entity_id)
    isolated_hass.states.async_set(original.entity_id, "unavailable", original.attributes)
    candidates = []
    for index in (1, 2):
        candidate = registry.async_get_or_create(
            "sensor",
            "mqtt",
            f"candidate_{index}",
            config_entry=isolated_hass.config_entries.async_get_entry(
                manager.source_config_entry_id
            ),
            device_id=device.id,
            original_device_class="power",
            unit_of_measurement="W",
            capabilities={"state_class": "measurement"},
        )
        isolated_hass.states.async_set(candidate.entity_id, "100", original.attributes)
        candidates.append(candidate)
    await isolated_hass.async_block_till_done()
    assert manager.ambiguous and len(manager.ports) == 1
    flow = await isolated_hass.config_entries.options.async_init(entry.entry_id)
    flow = await isolated_hass.config_entries.options.async_configure(
        flow["flow_id"], {"next_step_id": "rebind"}
    )
    started = asyncio.Event()
    release = asyncio.Event()

    async def failed_save(snapshot):
        started.set()
        await release.wait()
        raise StorageError("Rebind write failed")

    with patch.object(manager.store, "async_save", side_effect=failed_save):
        rebinding = asyncio.create_task(
            isolated_hass.config_entries.options.async_configure(
                flow["flow_id"], {"binding": key, "source": candidates[0].entity_id}
            )
        )
        await asyncio.wait_for(started.wait(), 5)
        try:
            isolated_hass.states.async_set(candidates[0].entity_id, "200", original.attributes)
            await asyncio.sleep(0)
            await asyncio.sleep(0)
        finally:
            release.set()
        result = await rebinding
        await manager._save_task
    assert result["errors"]["base"] == "storage_error"
    isolated_hass.config_entries.options.async_abort(flow["flow_id"])
    await manager.async_flush()
    assert await isolated_hass.config_entries.async_reload(entry.entry_id)
    current = entry.runtime_data
    assert len(current.ports) == 1
    assert current.ports[key].binding.registry_id == device.sources[0].id
    assert current.ambiguous
    entities = er.async_entries_for_config_entry(registry, entry.entry_id)
    assert len(entities) == 1


async def test_cancelled_options_keep_their_transaction_until_acknowledgement(
    isolated_hass, create_mqtt_device, setup_companion
):
    entry = await setup_companion(await create_mqtt_device())
    manager = entry.runtime_data
    key = next(iter(manager.ports))
    started = asyncio.Event()
    release = asyncio.Event()
    save = manager.store.async_save
    snapshots = []

    async def delayed_first_save(snapshot):
        snapshots.append(snapshot)
        if len(snapshots) == 1:
            started.set()
            await release.wait()
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_first_save):
        excluding = asyncio.create_task(manager.async_set_exclusions({key}))
        await asyncio.wait_for(started.wait(), 5)
        try:
            for _ in range(3):
                excluding.cancel()
                await asyncio.sleep(0)
            reenabling = asyncio.create_task(manager.async_set_exclusions(set()))
            await asyncio.sleep(0)
            assert not excluding.done()
            assert not reenabling.done()
            assert not manager.ports[key].binding.excluded
        finally:
            release.set()
        with pytest.raises(asyncio.CancelledError):
            await excluding
        await reenabling
    assert snapshots[0].ports[0].excluded
    assert not snapshots[-1].ports[0].excluded
    assert not manager.ports[key].binding.excluded
    assert not (await manager.store.async_load()).ports[0].excluded
