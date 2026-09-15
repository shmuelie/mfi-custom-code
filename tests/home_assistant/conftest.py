"""Home Assistant fixtures with an isolated config directory and mocked MQTT."""

import json
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from homeassistant.helpers import entity_registry as er
from pytest_homeassistant_custom_component.common import async_fire_mqtt_message

from custom_components.mfi.const import CONF_FRESHNESS, CONF_SOURCE_DEVICE


@pytest.fixture(autouse=True)
def custom_integrations(enable_custom_integrations):
    """Allow loading this repository's custom integration."""


@pytest.fixture
def isolated_hass(hass, tmp_path):
    hass.config.config_dir = str(tmp_path)
    return hass


@pytest.fixture
async def create_mqtt_device(isolated_hass, mqtt_mock, mqtt_client_mock):
    hass = isolated_hass
    mqtt_client_mock.disconnect.side_effect = lambda: mqtt_client_mock.on_socket_close(
        mqtt_client_mock, None, Mock(fileno=Mock(return_value=-1))
    )
    await hass.async_start()

    async def create(count=1, device_id="test_mfi", model_id=None):
        if model_id is None:
            model_id = "58993" if count == 1 else "58952"
        sources = []
        for index in range(1, count + 1):
            base = f"mfi/{device_id}/port_{index}"
            unique_id = f"{device_id}_port_{index}_power"
            config = {
                "name": f"Port {index} Power",
                "unique_id": unique_id,
                "state_topic": f"{base}/state",
                "value_template": "{{ value_json.value }}",
                "unit_of_measurement": "W",
                "device_class": "power",
                "state_class": "measurement",
                "expire_after": 180,
                "availability": [
                    {"topic": f"mfi/{device_id}/availability"},
                    {"topic": f"{base}/availability"},
                ],
                "availability_mode": "all",
                "device": {
                    "identifiers": [device_id],
                    "manufacturer": "Ubiquiti Networks",
                    "model": "mPower",
                    "model_id": model_id,
                    "name": device_id,
                },
            }
            async_fire_mqtt_message(
                hass, f"homeassistant/sensor/{device_id}/{index}/config", json.dumps(config)
            )
            await hass.async_block_till_done()
            async_fire_mqtt_message(hass, f"mfi/{device_id}/availability", "online")
            async_fire_mqtt_message(hass, f"{base}/availability", "online")
            async_fire_mqtt_message(hass, f"{base}/state", '{"value":100}')
            await hass.async_block_till_done()
            registry = er.async_get(hass)
            entity_id = registry.async_get_entity_id("sensor", "mqtt", unique_id)
            assert entity_id is not None
            sources.append(registry.async_get(entity_id))
        return SimpleNamespace(
            id=sources[0].device_id,
            sources=sources,
            device_id=device_id,
            count=count,
            model_id=model_id,
        )

    yield create
    for entry in hass.config_entries.async_entries("mfi"):
        if entry.state.value == "loaded":
            await hass.config_entries.async_unload(entry.entry_id)
    for entry in hass.config_entries.async_entries("mqtt"):
        await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()


@pytest.fixture
def setup_companion(isolated_hass):
    hass = isolated_hass

    async def setup(device, excluded=None):
        flow = await hass.config_entries.flow.async_init("mfi", context={"source": "user"})
        flow = await hass.config_entries.flow.async_configure(
            flow["flow_id"], {CONF_SOURCE_DEVICE: device.id}
        )
        assert flow["step_id"] == "confirm"
        answers = {CONF_FRESHNESS: True, "excluded_sources": excluded or []}
        if device.model_id not in ("58952", "58993"):
            answers["model_confirmed"] = True
        flow = await hass.config_entries.flow.async_configure(flow["flow_id"], answers)
        assert flow["type"] == "create_entry", flow
        await hass.async_block_till_done()
        return flow["result"]

    return setup
