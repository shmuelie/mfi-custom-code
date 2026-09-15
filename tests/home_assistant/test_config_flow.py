"""User confirmation, duplicate protection, and preservation of MQTT ownership."""

from unittest.mock import patch

import pytest
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er

from custom_components.mfi.const import CONF_FRESHNESS, CONF_SOURCE_DEVICE
from custom_components.mfi.storage import StorageError


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
