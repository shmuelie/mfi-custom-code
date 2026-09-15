"""User confirmation, duplicate protection, and preservation of MQTT ownership."""

import asyncio
from decimal import Decimal
from unittest.mock import patch

import pytest
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er

from custom_components.mfi.const import (
    CONF_FRESHNESS,
    CONF_SOURCE_CONFIG_ENTRY,
    CONF_SOURCE_DEVICE,
)
from custom_components.mfi.storage import CheckpointStore, StorageError


async def test_mqtt_is_required(isolated_hass):
    flow = await isolated_hass.config_entries.flow.async_init("mfi", context={"source": "user"})
    assert flow["reason"] == "mqtt_required"


@pytest.mark.parametrize("count", [1, 8])
async def test_setup_and_duplicate(isolated_hass, create_mqtt_device, setup_companion, count):
    device = await create_mqtt_device(count)
    entry = await setup_companion(device)
    assert entry.state.value == "loaded"
    entities = er.async_entries_for_config_entry(er.async_get(isolated_hass), entry.entry_id)
    assert len(entities) == count
    assert all(entity.platform == "mfi" for entity in entities)
    assert all(entity.device_id != device.id for entity in entities)
    assert dr.async_get(isolated_hass).async_get(device.id).config_entry_id != entry.entry_id
    flow = await isolated_hass.config_entries.flow.async_init("mfi", context={"source": "user"})
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_SOURCE_DEVICE: device.id}
    )
    assert flow["reason"] == "already_configured"


async def test_confirmation_and_save_failure(isolated_hass, create_mqtt_device):
    device = await create_mqtt_device()
    flow = await isolated_hass.config_entries.flow.async_init("mfi", context={"source": "user"})
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_SOURCE_DEVICE: device.id}
    )
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_FRESHNESS: False}
    )
    assert flow["errors"][CONF_FRESHNESS] == "confirmation_required"
    with patch(
        "custom_components.mfi.storage.CheckpointStore.async_save",
        side_effect=StorageError("disk full"),
    ):
        flow = await isolated_hass.config_entries.flow.async_configure(
            flow["flow_id"], {CONF_FRESHNESS: True}
        )
    assert flow["errors"]["base"] == "storage_error"
    assert not isolated_hass.config_entries.async_entries("mfi")
    isolated_hass.config_entries.flow.async_abort(flow["flow_id"])


async def test_initial_exclusion(isolated_hass, create_mqtt_device, setup_companion):
    device = await create_mqtt_device(8)
    entry = await setup_companion(device, [device.sources[0].id])
    entities = er.async_entries_for_config_entry(er.async_get(isolated_hass), entry.entry_id)
    assert len(entities) == 7
    assert len(entry.runtime_data.ports) == 8


async def test_unknown_model_needs_confirmation(isolated_hass, create_mqtt_device):
    device = await create_mqtt_device(model_id="future")
    flow = await isolated_hass.config_entries.flow.async_init("mfi", context={"source": "user"})
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_SOURCE_DEVICE: device.id}
    )
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_FRESHNESS: True, "model_confirmed": False}
    )
    assert flow["errors"]["model_confirmed"] == "confirmation_required"


async def test_pending_setup_reserves_device_against_recovery(
    isolated_hass, create_mqtt_device, setup_companion
):
    original = await create_mqtt_device(device_id="original")
    entry = await setup_companion(original)
    manager = entry.runtime_data
    next(iter(manager.ports.values())).accumulator.total = Decimal("8.5")
    await manager.async_flush()
    replacement = await create_mqtt_device(device_id="replacement")
    flow = await isolated_hass.config_entries.flow.async_init("mfi", context={"source": "user"})
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_SOURCE_DEVICE: replacement.id}
    )
    with pytest.raises(ValueError, match="setup"):
        await manager.async_change_device(replacement.id)
    isolated_hass.config_entries.flow.async_abort(flow["flow_id"])
    assert isolated_hass.config_entries.async_get_entry(entry.entry_id) is entry
    assert manager.source_device_id == original.id
    assert (await manager.store.async_load()).ports[0].total >= Decimal("8.5")


async def test_recovery_rechecks_pending_setup_after_saving(
    isolated_hass, create_mqtt_device, setup_companion
):
    original = await create_mqtt_device(device_id="original")
    entry = await setup_companion(original)
    manager = entry.runtime_data
    replacement = await create_mqtt_device(device_id="replacement")
    next(iter(manager.ports.values())).accumulator.total = Decimal("8.5")
    started = asyncio.Event()
    release = asyncio.Event()
    save = manager.store.async_save

    async def delayed_save(snapshot):
        started.set()
        await release.wait()
        await save(snapshot)

    with patch.object(manager.store, "async_save", side_effect=delayed_save):
        recovering = asyncio.create_task(manager.async_change_device(replacement.id))
        await asyncio.wait_for(started.wait(), 5)
        try:
            flow = await isolated_hass.config_entries.flow.async_init(
                "mfi", context={"source": "user"}
            )
            flow = await isolated_hass.config_entries.flow.async_configure(
                flow["flow_id"], {CONF_SOURCE_DEVICE: replacement.id}
            )
        finally:
            release.set()
        with pytest.raises(ValueError, match="setup"):
            await recovering
    isolated_hass.config_entries.flow.async_abort(flow["flow_id"])
    assert manager.source_device_id == original.id
    assert (await manager.store.async_load()).ports[0].total >= Decimal("8.5")


@pytest.mark.parametrize("during_save", [False, True])
async def test_stale_confirmation_never_replaces_an_existing_counter(
    isolated_hass, create_mqtt_device, setup_companion, during_save
):
    original = await create_mqtt_device(device_id="original")
    entry = await setup_companion(original)
    manager = entry.runtime_data
    next(iter(manager.ports.values())).accumulator.total = Decimal("8.5")
    await manager.async_flush()
    replacement = await create_mqtt_device(device_id="replacement")
    flow = await isolated_hass.config_entries.flow.async_init("mfi", context={"source": "user"})
    flow = await isolated_hass.config_entries.flow.async_configure(
        flow["flow_id"], {CONF_SOURCE_DEVICE: replacement.id}
    )
    started = asyncio.Event()
    release = asyncio.Event()
    save = CheckpointStore.async_save

    async def delayed_save(store, snapshot):
        started.set()
        await release.wait()
        await save(store, snapshot)

    def claim_destination():
        isolated_hass.config_entries.async_update_entry(
            entry,
            unique_id=f"{manager.source_config_entry_id}_{replacement.id}",
            data={
                **entry.data,
                CONF_SOURCE_DEVICE: replacement.id,
                CONF_SOURCE_CONFIG_ENTRY: manager.source_config_entry_id,
            },
        )

    with patch.object(CheckpointStore, "async_save", new=delayed_save):
        if not during_save:
            claim_destination()
            release.set()
        submit = asyncio.create_task(
            isolated_hass.config_entries.flow.async_configure(
                flow["flow_id"], {CONF_FRESHNESS: True}
            )
        )
        if during_save:
            await asyncio.wait_for(started.wait(), 5)
            claim_destination()
            release.set()
        result = await submit
    await isolated_hass.async_block_till_done()
    assert result["type"] == "abort"
    assert result["reason"] == "already_configured"
    assert isolated_hass.config_entries.async_entries("mfi") == [entry]
    assert (await manager.store.async_load()).ports[0].total >= Decimal("8.5")
    assert len(list(manager.store.path.parent.glob("mfi_energy.*"))) == 1
