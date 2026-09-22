"""Actual companion-to-native setup with HA MQTT and persistent registry stores."""

import json
from decimal import Decimal

import pytest
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.service_info.mqtt import MqttServiceInfo
from pytest_homeassistant_custom_component.common import async_fire_mqtt_message

from custom_components.mfi import migration


@pytest.fixture
def hass_config_dir(tmp_path):
    return str(tmp_path)


@pytest.fixture
def hass_storage(monkeypatch):
    monkeypatch.setattr("homeassistant.helpers.registry.SAVE_DELAY", 0.001)
    monkeypatch.setattr("homeassistant.helpers.registry.SAVE_DELAY_LONG", 0.001)
    monkeypatch.setattr("homeassistant.config_entries.SAVE_DELAY", 0.001)
    monkeypatch.setattr(migration, "_PERSIST_TIMEOUT", 2)
    monkeypatch.setattr(migration, "_PERSIST_INTERVAL", 0.005)
    return {}


async def test_real_native_activation_preserves_companion_history_bindings(
    isolated_hass, create_mqtt_device, setup_companion
):
    hass = isolated_hass
    device = await create_mqtt_device()
    registry = er.async_get(hass)
    roles = {
        "power": {
            "unique_id": device.sources[0].unique_id,
            "discovery_topic": "homeassistant/sensor/test_mfi/1/config",
            "state_topic": "mfi/test_mfi/port_1/state",
        }
    }
    mapping = {"power": device.sources[0].entity_id}
    for role, unit in (("current", "A"), ("voltage", "V"), ("relay", None)):
        domain = "switch" if role == "relay" else "sensor"
        topic = f"homeassistant/{domain}/test_mfi/{role}/config"
        unique_id = f"test_mfi_port_1_{role}"
        state_topic = f"legacy/port_1/{role}/state"
        config = {
            "name": role.capitalize(),
            "unique_id": unique_id,
            "state_topic": state_topic,
            "device": {
                "identifiers": ["test_mfi"],
                "manufacturer": "Ubiquiti Networks",
                "model": "mPower",
                "model_id": "58993",
                "name": "test_mfi",
            },
        }
        roles[role] = {
            "unique_id": unique_id,
            "discovery_topic": topic,
            "state_topic": state_topic,
        }
        if role == "relay":
            config["command_topic"] = "legacy/port_1/relay/set"
            roles[role]["command_topic"] = config["command_topic"]
        else:
            config.update(device_class=role, state_class="measurement", unit_of_measurement=unit)
        async_fire_mqtt_message(hass, topic, json.dumps(config))
        await hass.async_block_till_done()
        mapping[role] = registry.async_get_entity_id(domain, "mqtt", unique_id)
        assert mapping[role] is not None
    entry = await setup_companion(device)
    registry.async_update_entity_options(mapping["power"], "sensor", {"display_precision": 2})
    registry.async_update_entity(mapping["power"], name="Preserved user power")
    await hass.async_block_till_done()
    assert hass.services.has_service("mfi", "migration_prepare")
    energy = er.async_entries_for_config_entry(registry, entry.entry_id)[0]
    binding = next(iter(entry.runtime_data.ports.values()))
    binding.accumulator.total = Decimal("8.5")
    await entry.runtime_data.async_flush()
    before = {key: registry.async_get(entity_id).id for key, entity_id in mapping.items()}
    companion_id = energy.device_id
    device_id = "abcdef0123456789abcdef0123456789"
    descriptor = {
        "schema_version": 1,
        "device_id": device_id,
        "name": "test_mfi",
        "manufacturer": "Ubiquiti Networks",
        "model": "mPower",
        "model_id": "58993",
        "firmware_version": "test",
        "publisher_version": "test",
        "refresh_interval": 60,
        "expire_after": 180,
        "ports": [{"id": 1, "name": "Port 1", "capabilities": list(roles)}],
    }
    coordinator = migration.MigrationCoordinator(hass)
    prepare_data = {
        "entry_id": entry.entry_id,
        "descriptor": descriptor,
        "migration_map": {
            "schema_version": 1,
            "device_id": device_id,
            "ports": [{"id": 1, "roles": roles}],
        },
        "mapping": {"1": mapping},
        "discovery_prefix": "homeassistant",
    }
    discovery = MqttServiceInfo(
        topic=f"mfi/{device_id}/config",
        payload=json.dumps(descriptor),
        qos=1,
        retain=True,
        subscribed_topic="mfi/+/config",
        timestamp=0,
    )
    flow = await hass.config_entries.flow.async_init(
        "mfi", context={"source": "mqtt"}, data=discovery
    )
    with pytest.raises(migration.MigrationError, match="pending setup"):
        await coordinator.async_execute("prepare", prepare_data)
    hass.config_entries.flow.async_abort(flow["flow_id"])
    prepared = await coordinator.async_execute("prepare", prepare_data)
    blocked = await hass.config_entries.flow.async_init(
        "mfi", context={"source": "mqtt"}, data=discovery
    )
    assert blocked["reason"] == "migration_pending"
    commands = {
        "journal_id": prepared["journal_id"],
        "approve": True,
        "broker_clean": True,
        "publisher_native": True,
        "backup_confirmed": True,
        "references_reviewed": True,
        "broker_absent": True,
    }
    await coordinator.async_execute("quiesce", commands)
    await coordinator.async_execute("apply", commands)
    result = await coordinator.async_execute("finish", commands)
    assert result["phase"] == "active"
    assert entry.runtime_data.committed.ports[0].total >= Decimal("8.5")
    assert entry.runtime_data.parent.id == device.id
    assert dr.async_get(hass).async_get(companion_id) is None
    assert registry.async_get(energy.entity_id).unique_id == energy.unique_id
    for role, entity_id in mapping.items():
        entity = registry.async_get(entity_id)
        assert entity.id == before[role]
        assert entity.platform == "mfi"
        assert entity.device_id == entry.runtime_data.children[1].id
    assert registry.async_get(mapping["power"]).name == "Preserved user power"
    assert registry.async_get(mapping["power"]).options["sensor"]["display_precision"] == 2
    assert len(er.async_entries_for_config_entry(registry, entry.entry_id)) == 5
    rediscovery = await hass.config_entries.flow.async_init(
        "mfi", context={"source": "mqtt"}, data=discovery
    )
    assert rediscovery["reason"] == "already_configured"
    native = entry.runtime_data
    native.ports[next(iter(native.ports))].accumulator.total = Decimal("8.75")
    await native.async_flush()
    commands["publisher_stopped"] = True
    result = await coordinator.async_execute("rollback", commands)
    assert result["phase"] == "rollback_ready"
    commands["legacy_restored"] = True
    result = await coordinator.async_execute("rollback", commands)
    assert result["phase"] == "rolled_back"
    assert entry.data.get("mode", "companion") == "companion"
    assert entry.runtime_data.committed.ports[0].total == Decimal("8.75")
    assert dr.async_get(hass).async_get(companion_id) is not None
    for role, entity_id in mapping.items():
        entity = registry.async_get(entity_id)
        assert entity.id == before[role]
        assert entity.platform == "mqtt"
        assert entity.device_id == device.id
