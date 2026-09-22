"""Native MQTT discovery, per-port children, freshness, and relay confirmation."""

import asyncio
import json
from copy import deepcopy
from datetime import timedelta
from decimal import Decimal
from types import SimpleNamespace
from unittest.mock import Mock, patch

import pytest
from homeassistant.components import mqtt
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.service_info.mqtt import MqttServiceInfo
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import (
    async_fire_mqtt_message,
    async_fire_time_changed_exact,
)

from custom_components.mfi.protocol import parse_availability, parse_descriptor, parse_report

DEVICE = "0123456789abcdef0123456789abcdef"
SESSION = "11111111111111111111111111111111"


def descriptor(count=1):
    return {
        "schema_version": 1,
        "device_id": DEVICE,
        "name": "mPower Pro",
        "manufacturer": "Ubiquiti Networks",
        "model_id": "58952",
        "model": "mPower Pro",
        "firmware_version": "2.1",
        "publisher_version": "2.1.0",
        "refresh_interval": 60,
        "expire_after": 180,
        "ports": [
            {
                "id": index,
                "name": f"Port {index}",
                "capabilities": ["power", "current", "voltage", "relay"],
            }
            for index in range(1, count + 1)
        ],
    }


def report(sequence=1, power=100, session=SESSION, request_id=None, relay="OFF"):
    value = {
        "session_id": session,
        "sequence": sequence,
        "power": {"status": "ok", "value": power},
        "current": {"status": "ok", "value": 1},
        "voltage": {"status": "ok", "value": 120},
        "relay": {"status": "ok", "value": relay},
    }
    if request_id is not None:
        value["request_id"] = request_id
    return value


@pytest.fixture
async def native_setup(isolated_hass, mqtt_mock, mqtt_client_mock):
    hass = isolated_hass
    mqtt_client_mock.disconnect.side_effect = lambda: mqtt_client_mock.on_socket_close(
        mqtt_client_mock, None, Mock(fileno=Mock(return_value=-1))
    )
    await hass.async_start()

    async def setup(count=1, exclude=None):
        config = descriptor(count)
        info = MqttServiceInfo(
            topic=f"mfi/{DEVICE}/config",
            payload=json.dumps(config),
            qos=1,
            retain=True,
            subscribed_topic="mfi/+/config",
            timestamp=0,
        )
        flow = await hass.config_entries.flow.async_init(
            "mfi", context={"source": "mqtt"}, data=info
        )
        assert flow["step_id"] == "native", flow
        result = await hass.config_entries.flow.async_configure(
            flow["flow_id"], {"confirm_new": True, "excluded_sources": exclude or []}
        )
        assert result["type"] == "create_entry", result
        await hass.async_block_till_done()
        entry = result["result"]
        assert entry.state.value == "loaded"
        return SimpleNamespace(entry=entry, runtime=entry.runtime_data, descriptor=config)

    yield setup
    for entry in hass.config_entries.async_entries("mfi"):
        if entry.state.value == "loaded":
            await hass.config_entries.async_unload(entry.entry_id)
    for entry in hass.config_entries.async_entries("mqtt"):
        if entry.state.value == "loaded":
            await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()


async def send(hass, suffix, payload, retain=False):
    async_fire_mqtt_message(hass, f"mfi/{DEVICE}/{suffix}", json.dumps(payload), retain=retain)
    await hass.async_block_till_done()


async def online(hass):
    await send(hass, "availability", {"session_id": SESSION, "state": "online"}, retain=True)


@pytest.mark.parametrize("count", [1, 8])
async def test_native_children_and_shared_rename(isolated_hass, native_setup, count):
    value = await native_setup(count)
    registry = er.async_get(isolated_hass)
    entities = er.async_entries_for_config_entry(registry, value.entry.entry_id)
    assert len(entities) == count * 5
    assert all(entity.platform == "mfi" for entity in entities)
    assert len(value.runtime.children) == count
    assert len(list(dr.async_get(isolated_hass).devices)) == 1
    child = value.runtime.children[1]
    assert child.parent_device_id == value.runtime.parent.id
    ids = {entry.entity_id for entry in entities}
    await online(isolated_hass)
    for port in range(1, count + 1):
        await send(isolated_hass, f"port/{port}/state", report())
    dr.async_get(isolated_hass).async_update_child_device(child.id, name_by_user="Desk lamp")
    await isolated_hass.async_block_till_done()
    port_entities = [entry for entry in entities if entry.device_id == child.id]
    assert len(port_entities) == 5
    assert all(
        isolated_hass.states.get(entry.entity_id).name.startswith("Desk lamp ")
        for entry in port_entities
    )
    assert ids == {
        entry.entity_id
        for entry in er.async_entries_for_config_entry(registry, value.entry.entry_id)
    }


async def test_native_constant_power_expiry_and_invalid_role(isolated_hass, native_setup, freezer):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    for sequence in range(2, 62):
        freezer.tick(timedelta(seconds=60))
        async_fire_time_changed_exact(isolated_hass, dt_util.utcnow())
        await send(isolated_hass, "port/1/state", report(sequence))
    await value.runtime.async_flush()
    assert abs(value.runtime.committed.ports[0].total - Decimal(".1")) < Decimal(".000001")
    bad = report(62)
    bad["power"] = {"status": "error", "reason": "read_failed"}
    await send(isolated_hass, "port/1/state", bad)
    assert not value.runtime.role_available(1, "power")
    assert value.runtime.role_available(1, "current")
    stopped = next(iter(value.runtime.ports.values())).accumulator.total
    freezer.tick(timedelta(seconds=600))
    async_fire_time_changed_exact(isolated_hass, dt_util.utcnow())
    await isolated_hass.async_block_till_done()
    assert next(iter(value.runtime.ports.values())).accumulator.total == stopped
    assert not value.runtime.role_available(1, "relay")


async def test_retained_replay_sequences_and_reconnect_gate(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report(), retain=True)
    assert not value.runtime.role_available(1, "power")
    await send(isolated_hass, "port/1/state", report(5))
    assert value.runtime.role_available(1, "power")
    await send(isolated_hass, "port/1/state", report(4, power=999))
    assert value.runtime.role_value(1, "power") == 100
    async_dispatcher_send(isolated_hass, mqtt.MQTT_CONNECTION_STATE, False)
    await isolated_hass.async_block_till_done()
    assert not value.runtime.role_available(1, "power")
    async_dispatcher_send(isolated_hass, mqtt.MQTT_CONNECTION_STATE, True)
    await isolated_hass.async_block_till_done()
    await send(isolated_hass, "availability", {"session_id": SESSION, "state": "online"})
    assert not value.runtime.role_available(1, "power")
    await send(isolated_hass, "port/1/state", report(6))
    assert value.runtime.role_available(1, "power")


async def test_power_entity_disabled_does_not_disable_energy(isolated_hass, native_setup, freezer):
    value = await native_setup()
    registry = er.async_get(isolated_hass)
    power_id = registry.async_get_entity_id("sensor", "mfi", f"{DEVICE}_port_1_power")
    registry.async_update_entity(power_id, disabled_by=er.RegistryEntryDisabler.USER)
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    freezer.tick(60)
    value.runtime._tick(None)
    await value.runtime.async_flush()
    assert value.runtime.committed.ports[0].total > 0


async def test_relay_requires_correlated_confirmation_and_qos_zero(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    published = asyncio.Event()
    command = {}

    async def publish(hass, topic, payload, qos, retain):
        assert topic == f"mfi/{DEVICE}/port/1/set"
        assert qos == 0 and retain is False
        command.update(json.loads(payload))
        published.set()

    with patch("custom_components.mfi.mqtt.mqtt.async_publish", side_effect=publish):
        action = asyncio.create_task(value.runtime.async_set_relay(1, True))
        await published.wait()
        assert value.runtime.role_value(1, "relay") == "OFF"
        async_fire_mqtt_message(
            isolated_hass,
            f"mfi/{DEVICE}/port/1/state",
            json.dumps(report(2, request_id=command["request_id"], relay="ON")),
        )
        await action
    assert value.runtime.role_value(1, "relay") == "ON"


async def test_disconnect_fails_command_without_retry(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    published = asyncio.Event()

    async def publish(*args, **kwargs):
        published.set()

    with patch("custom_components.mfi.mqtt.mqtt.async_publish", side_effect=publish) as pub:
        action = asyncio.create_task(value.runtime.async_set_relay(1, True))
        await published.wait()
        async_dispatcher_send(isolated_hass, mqtt.MQTT_CONNECTION_STATE, False)
        with pytest.raises(HomeAssistantError, match="connection changed"):
            await action
        async_dispatcher_send(isolated_hass, mqtt.MQTT_CONNECTION_STATE, True)
        await asyncio.sleep(0)
        assert pub.call_count == 1


async def test_publisher_session_rotation_and_duplicate_after_offline(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report(10))
    await send(isolated_hass, "availability", {"session_id": SESSION, "state": "offline"})
    await send(isolated_hass, "availability", {"session_id": SESSION, "state": "online"})
    await send(isolated_hass, "port/1/state", report(10))
    assert not value.runtime.role_available(1, "power")
    await send(isolated_hass, "port/1/state", report(11))
    assert value.runtime.role_available(1, "power")
    await send(isolated_hass, "availability", {"session_id": SESSION, "state": "offline"})
    new_session = "2" * 32
    await send(isolated_hass, "availability", {"session_id": new_session, "state": "offline"})
    await send(isolated_hass, "port/1/state", report(1, session=new_session))
    assert not value.runtime.role_available(1, "power")
    await send(isolated_hass, "availability", {"session_id": new_session, "state": "online"})
    assert value.runtime.role_available(1, "power")
    await send(isolated_hass, "availability", {"session_id": SESSION, "state": "online"})
    assert value.runtime.role_available(1, "power")
    assert value.runtime._session == new_session


async def test_exclusions_and_reload_preserve_total_and_wait_for_live_data(
    isolated_hass, native_setup, freezer
):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    freezer.tick(60)
    value.runtime._tick(None)
    await value.runtime.async_flush()
    total = value.runtime.committed.ports[0].total
    key = value.runtime.bindings[1]
    await value.runtime.async_set_exclusions({key})
    freezer.tick(60)
    await send(isolated_hass, "port/1/state", report(2))
    assert value.runtime.ports[key].accumulator.total == total
    assert await isolated_hass.config_entries.async_reload(value.entry.entry_id)
    new = value.entry.runtime_data
    assert new.committed.ports[0].total == total
    assert not new.role_available(1, "power")
    assert new.ports[key].binding.excluded
    await send(isolated_hass, "availability", {"session_id": SESSION, "state": "online"})
    await send(isolated_hass, "port/1/state", report(3))
    assert new.role_available(1, "power")
    assert not new.ports[key].available


async def test_correlated_relay_error_is_not_success(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    published = asyncio.Event()
    command = {}

    async def publish(*args, **kwargs):
        command.update(json.loads(args[2]))
        published.set()

    with patch("custom_components.mfi.mqtt.mqtt.async_publish", side_effect=publish):
        action = asyncio.create_task(value.runtime.async_set_relay(1, True))
        await published.wait()
        failed = report(2, request_id=command["request_id"])
        failed["relay"] = {"status": "error", "reason": "write_failed"}
        async_fire_mqtt_message(isolated_hass, f"mfi/{DEVICE}/port/1/state", json.dumps(failed))
        with pytest.raises(HomeAssistantError, match="write_failed"):
            await action


async def test_incompatible_descriptor_blocks_control(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    changed = descriptor()
    changed["ports"][0]["capabilities"].remove("current")
    await send(isolated_hass, "config", changed)
    assert not value.runtime.role_available(1, "relay")
    with pytest.raises(HomeAssistantError, match="unavailable"):
        await value.runtime.async_set_relay(1, True)
    await send(isolated_hass, "config", descriptor())
    assert not value.runtime.role_available(1, "power")
    await send(isolated_hass, "port/1/state", report(2))
    assert value.runtime.role_available(1, "power")


async def test_native_discovery_is_idempotent(isolated_hass, native_setup):
    value = await native_setup()
    info = MqttServiceInfo(
        topic=f"mfi/{DEVICE}/config",
        payload=json.dumps(descriptor()),
        qos=1,
        retain=True,
        subscribed_topic="mfi/+/config",
        timestamp=0,
    )
    flow = await isolated_hass.config_entries.flow.async_init(
        "mfi", context={"source": "mqtt"}, data=info
    )
    assert flow["type"] == "abort" and flow["reason"] == "already_configured"
    assert isolated_hass.config_entries.async_entries("mfi") == [value.entry]


async def test_relay_publish_failure_propagates_without_optimistic_state(
    isolated_hass, native_setup
):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    with patch(
        "custom_components.mfi.mqtt.mqtt.async_publish",
        side_effect=HomeAssistantError("Publisher disconnected"),
    ) as publishing:
        with pytest.raises(HomeAssistantError, match="disconnected"):
            await value.runtime.async_set_relay(1, True)
    assert publishing.call_count == 1
    assert value.runtime.role_value(1, "relay") == "OFF"
    assert value.runtime._commands == {}


async def test_conflicting_publisher_session_blocks_until_reconnect(isolated_hass, native_setup):
    value = await native_setup()
    await online(isolated_hass)
    await send(isolated_hass, "port/1/state", report())
    await send(isolated_hass, "availability", {"session_id": "2" * 32, "state": "online"})
    assert not value.runtime.role_available(1, "relay")
    await send(isolated_hass, "port/1/state", report(2))
    assert not value.runtime.role_available(1, "power")
    with pytest.raises(HomeAssistantError):
        await value.runtime.async_set_relay(1, True)


@pytest.mark.parametrize(
    "field,value",
    [
        ("schema_version", True),
        ("schema_version", 2),
        ("device_id", "bad"),
        ("refresh_interval", 0),
        ("expire_after", 61),
    ],
)
def test_invalid_descriptors(field, value):
    payload = descriptor()
    payload[field] = value
    with pytest.raises(ValueError):
        parse_descriptor(json.dumps(payload))


@pytest.mark.parametrize("value", [True, -1, "1", None, float("inf")])
def test_invalid_report_numbers(value):
    port = parse_descriptor(json.dumps(descriptor())).ports[0]
    payload = report(power=value)
    with pytest.raises(ValueError):
        parse_report(json.dumps(payload), port)


def test_duplicate_keys_and_ids():
    with pytest.raises(ValueError, match="Duplicate"):
        parse_availability('{"session_id":"a","session_id":"b","state":"online"}')
    payload = descriptor()
    payload["ports"].append(deepcopy(payload["ports"][0]))
    with pytest.raises(ValueError, match="Duplicate"):
        parse_descriptor(json.dumps(payload))
