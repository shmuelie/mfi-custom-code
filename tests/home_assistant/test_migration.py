"""Offline migration against HA's registries and real scheduled atomic storage.

Only runtime setup/unload is replaced: neither MQTT nor device I/O is performed.
Persistence acknowledgements, journal readers/writers and registry mutations
are real, including the deleted-device restoration path.
"""

import asyncio
import copy
import json
from dataclasses import replace
from decimal import Decimal
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest
import voluptuous as vol
from homeassistant.config_entries import HANDLERS, ConfigEntryDisabler, ConfigEntryState, ConfigFlow
from homeassistant.core import CoreState
from homeassistant.exceptions import ConfigEntryError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.entity import entity_sources
from homeassistant.util.file import WriteError
from pytest_homeassistant_custom_component.common import (
    MockConfigEntry,
    MockModule,
    async_test_home_assistant,
    mock_integration,
    mock_platform,
)

from custom_components.mfi import migration
from custom_components.mfi.const import (
    CONF_SOURCE_CONFIG_ENTRY,
    CONF_SOURCE_DEVICE,
    CONF_STORAGE_ID,
)
from custom_components.mfi.storage import Checkpoint, CheckpointStore, PortSnapshot


@pytest.fixture
def hass_config_dir(tmp_path):
    return str(tmp_path)


@pytest.fixture
def hass_storage(monkeypatch):
    """Do not install the default in-memory Store mock: it masks disk evidence."""
    monkeypatch.setattr("homeassistant.helpers.registry.SAVE_DELAY", 0.001)
    monkeypatch.setattr("homeassistant.helpers.registry.SAVE_DELAY_LONG", 0.001)
    monkeypatch.setattr("homeassistant.config_entries.SAVE_DELAY", 0.001)
    monkeypatch.setattr(migration, "_PERSIST_TIMEOUT", 2)
    monkeypatch.setattr(migration, "_PERSIST_INTERVAL", 0.005)
    return {}


def install_offline_runtimes(hass, monkeypatch):
    async def setup(hass, entry):
        if entry.domain == "mfi":
            await migration.async_assert_ready(hass, entry)
        return True

    async def unload(hass, entry):
        return True

    class OfflineFlow(ConfigFlow):
        VERSION = 2

    for domain in ("mqtt", "mfi"):
        mock_integration(
            hass,
            MockModule(domain, async_setup_entry=setup, async_unload_entry=unload),
        )
        mock_platform(hass, f"{domain}.config_flow")
        monkeypatch.setitem(HANDLERS, domain, OfflineFlow if domain == "mfi" else ConfigFlow)


@pytest.fixture
async def installation(isolated_hass, monkeypatch):
    hass = isolated_hass
    install_offline_runtimes(hass, monkeypatch)
    mqtt = MockConfigEntry(domain="mqtt", title="Shared broker")
    mqtt.add_to_hass(hass)
    devices = dr.async_get(hass)
    entities = er.async_get(hass)
    parent = devices.async_get_or_create(
        config_entry_id=mqtt.entry_id,
        identifiers={("mqtt", "legacy_strip")},
        name="Strip",
        manufacturer="Ubiquiti Networks",
        model="mPower Pro",
        model_id="58952",
    )
    devices.async_update_device(parent.id, name_by_user="My strip", labels={"strip_label"})
    storage_id = uuid4().hex
    entry = MockConfigEntry(
        domain="mfi",
        title="Strip Energy",
        version=2,
        unique_id=parent.id,
        data={
            "mode": "companion",
            CONF_SOURCE_DEVICE: parent.id,
            CONF_SOURCE_CONFIG_ENTRY: mqtt.entry_id,
            CONF_STORAGE_ID: storage_id,
            "freshness_confirmed": True,
        },
    )
    entry.add_to_hass(hass)
    companion = devices.async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={("mfi", entry.entry_id)},
        name="Strip Energy",
        model="Energy companion",
        manufacturer="mFi",
    )
    devices.async_update_device(
        companion.id,
        name_by_user="Preserve this name",
        labels={"energy_label"},
        configuration_url="http://example.invalid",
        hw_version="1",
        sw_version="2",
    )
    native_id = uuid4().hex
    descriptor = {
        "schema_version": 1,
        "device_id": native_id,
        "name": "Strip",
        "manufacturer": "Ubiquiti Networks",
        "model": "mPower Pro",
        "model_id": "58952",
        "firmware_version": "2.1",
        "publisher_version": "1.0",
        "refresh_interval": 60,
        "expire_after": 180,
        "ports": [],
    }
    export = {"schema_version": 1, "device_id": native_id, "ports": []}
    mapping = {}
    snapshots = []
    for pid in (1, 3):
        roles = {}
        mapping[str(pid)] = {}
        descriptor["ports"].append(
            {
                "id": pid,
                "name": f"Port {pid}",
                "capabilities": ["power", "current", "voltage", "relay"],
            }
        )
        for role in descriptor["ports"][-1]["capabilities"]:
            domain = "switch" if role == "relay" else "sensor"
            unique_id = f"old_label_{pid}_{role}"
            source = entities.async_get_or_create(
                domain,
                "mqtt",
                unique_id,
                config_entry=mqtt,
                device_id=parent.id,
                original_name=f"Port {pid} {role}",
                disabled_by=er.RegistryEntryDisabler.USER if pid == 3 and role == "power" else None,
                unit_of_measurement={"power": "W", "current": "A", "voltage": "V"}.get(role),
            )
            entities.async_update_entity(
                source.entity_id,
                name=f"Custom {pid} {role}",
                labels={"entity_label"},
                icon="mdi:power",
                hidden_by=er.RegistryEntryHider.USER,
            )
            mapping[str(pid)][role] = source.entity_id
            roles[role] = {
                "unique_id": unique_id,
                "discovery_topic": f"homeassistant/{domain}/old/{pid}_{role}/config",
                "state_topic": f"legacy/{pid}/{role}/state",
            }
            if role == "relay":
                roles[role]["command_topic"] = f"legacy/{pid}/relay/set"
            if role == "power":
                binding_id = uuid4().hex
                snapshots.append(
                    PortSnapshot(
                        binding_id,
                        source.id,
                        unique_id,
                        source.entity_id,
                        f"Port {pid}",
                        Decimal("12.345"),
                    )
                )
                energy = entities.async_get_or_create(
                    "sensor",
                    "mfi",
                    f"{entry.entry_id}_{binding_id}_energy",
                    config_entry=entry,
                    device_id=companion.id,
                    unit_of_measurement="kWh",
                    disabled_by=er.RegistryEntryDisabler.USER if pid == 3 else None,
                )
                entities.async_update_entity(energy.entity_id, name=f"Energy override {pid}")
        export["ports"].append({"id": pid, "roles": roles})
    checkpoint = Checkpoint(storage_id, 5, tuple(snapshots))
    store = CheckpointStore(hass, storage_id)
    await store.async_save(checkpoint)
    for config_entry in (mqtt, entry):
        hass.config_entries.async_update_entry(config_entry, title=f"{config_entry.title} fixture")
        assert await hass.config_entries.async_setup(config_entry.entry_id)
    await hass.async_block_till_done()
    return SimpleNamespace(
        hass=hass,
        entry=entry,
        mqtt=mqtt,
        parent=devices.async_get(parent.id),
        companion=devices.async_get(companion.id),
        devices=devices,
        entities=entities,
        checkpoint=checkpoint,
        store=store,
        storage_id=storage_id,
        coordinator=migration.MigrationCoordinator(hass),
        prepare={
            "entry_id": entry.entry_id,
            "descriptor": descriptor,
            "migration_map": export,
            "mapping": mapping,
            "discovery_prefix": "homeassistant",
        },
    )


async def execute(env, action, **overrides):
    data = {
        "journal_id": env.storage_id,
        "approve": True,
        "broker_clean": True,
        "publisher_native": True,
        "backup_confirmed": True,
        "references_reviewed": True,
        "broker_absent": True,
        "publisher_stopped": True,
    }
    data.update(overrides)
    return await env.coordinator.async_execute(action, data)


async def prepared(env):
    return await env.coordinator.async_execute("prepare", env.prepare)


async def quiesced(env):
    await prepared(env)
    return await execute(env, "quiesce")


async def journal(env):
    return await migration.JournalStore(env.hass, env.storage_id).async_load()


async def test_prepare_is_immutable_inventory_only(installation):
    env = installation
    parent = migration._device_snapshot(env.parent)
    companion = migration._device_snapshot(env.companion)
    before = {e.id: migration._entity_snapshot(e) for e in env.entities.entities.values()}
    result = await prepared(env)
    assert result["phase"] == "prepared"
    assert migration._device_snapshot(env.devices.async_get(env.parent.id)) == parent
    assert migration._device_snapshot(env.devices.async_get(env.companion.id)) == companion
    assert before == {e.id: migration._entity_snapshot(e) for e in env.entities.entities.values()}
    assert env.entry.state is ConfigEntryState.LOADED
    assert env.mqtt.state is ConfigEntryState.LOADED
    assert env.entry.data["mode"] == "companion"
    assert await env.store.async_load() == env.checkpoint
    assert result["port_bindings"] == dict(
        zip(("1", "3"), (p.binding_id for p in env.checkpoint.ports), strict=True)
    )
    result["parent"]["name"] = "Not the journal"
    assert (await journal(env))["parent"]["name"] == parent["name"]
    with pytest.raises(migration.MigrationError, match="already exists"):
        await prepared(env)
    await migration.async_assert_ready(env.hass, env.entry)


async def test_apply_finish_and_rollback_preserve_identity_and_latest_energy(installation):
    env = installation
    await quiesced(env)
    initial = await journal(env)
    assert env.mqtt.disabled_by is ConfigEntryDisabler.USER
    assert (await execute(env, "apply"))["phase"] == "companion_retired"
    assert env.devices.async_get(env.companion.id) is None
    assert env.entry.version == 2
    assert env.entry.data["mode"] == "native"
    for item in initial["entities"]:
        current = env.entities.async_get(item["before"]["entity_id"])
        assert current.id == item["before"]["id"]
        assert current.unique_id == item["native_unique_id"]
        assert current.platform == "mfi"
        assert current.disabled_by == item["before"]["disabled_by"]
    assert (await execute(env, "finish"))["phase"] == "active"
    assert env.mqtt.state is ConfigEntryState.LOADED
    assert env.mqtt.disabled_by is None
    assert env.entry.state is ConfigEntryState.LOADED
    newer = env.checkpoint.next_generation(
        tuple(replace(port, total=port.total + Decimal("0.321")) for port in env.checkpoint.ports)
    )
    await env.store.async_save(newer)
    assert (await execute(env, "rollback"))["phase"] == "rollback_ready"
    assert env.mqtt.disabled_by is ConfigEntryDisabler.USER
    assert (
        migration._device_snapshot(env.devices.async_get(env.companion.id)) == initial["companion"]
    )
    assert await env.store.async_load() == newer
    for item in initial["entities"]:
        current = migration._entity_snapshot(env.entities.async_get(item["before"]["entity_id"]))
        for field in ("id", "entity_id", "unique_id", "platform", "config_entry_id", "device_id"):
            assert current[field] == item["before"][field]
    with pytest.raises(migration.MigrationError, match="Restore"):
        await execute(env, "rollback")
    assert (await execute(env, "rollback", legacy_restored=True))["phase"] == "rolled_back"
    assert migration._device_snapshot(env.devices.async_get(env.parent.id)) == initial["parent"]
    assert await env.store.async_load() == newer


@pytest.mark.parametrize(
    "phase",
    [
        "quiescing",
        "quiesced",
        "broker_clean",
        "entities_transferred",
        "parent_transferred",
        "children_attached",
        "companion_retirement_pending",
        "companion_retired",
        "activating",
        "active",
    ],
)
async def test_resume_after_each_durable_phase(installation, phase):
    env = installation
    await prepared(env)
    save = migration.JournalStore._save
    fired = False

    def crash(store, data):
        nonlocal fired
        save(store, data)
        if not fired and data["phase"] == phase:
            fired = True
            raise migration.MigrationError("simulated crash")

    with patch.object(migration.JournalStore, "_save", crash):
        with pytest.raises(migration.MigrationError, match="simulated crash"):
            await execute(env, "quiesce")
            await execute(env, "apply")
            await execute(env, "finish")
    assert fired
    env.coordinator = migration.MigrationCoordinator(env.hass)
    current = (await journal(env))["phase"]
    if current in ("quiescing", "quiesced"):
        await execute(env, "quiesce")
    if current not in ("activating", "active"):
        await execute(env, "apply")
    assert (await execute(env, "finish"))["phase"] == "active"
    assert len(dr.async_entries_for_parent_device(env.devices, env.parent.id)) == 2
    assert await env.store.async_load() == env.checkpoint


@pytest.mark.parametrize(
    "operation",
    [
        "async_update_entity_platform",
        "async_update_device",
        "async_get_or_create_child",
        "async_update_entity",
        "async_remove_device",
    ],
)
async def test_resume_after_registry_mutation_before_journal_ack(installation, operation):
    env = installation
    await quiesced(env)
    registry = (
        env.entities
        if operation in ("async_update_entity_platform", "async_update_entity")
        else env.devices
    )
    original = getattr(registry, operation)
    fired = False

    def crash(*args, **kwargs):
        nonlocal fired
        result = original(*args, **kwargs)
        if not fired:
            fired = True
            raise migration.MigrationError("after mutation")
        return result

    with patch.object(registry, operation, crash):
        with pytest.raises(migration.MigrationError, match="after mutation"):
            await execute(env, "apply")
    env.coordinator = migration.MigrationCoordinator(env.hass)
    assert (await execute(env, "apply"))["phase"] == "companion_retired"
    assert (await execute(env, "finish"))["phase"] == "active"


async def test_parent_first_is_never_attempted(installation):
    env = installation
    await quiesced(env)
    update = env.devices.async_update_device
    checked = False

    def verify(device_id, **kwargs):
        nonlocal checked
        if device_id == env.parent.id and kwargs.get("new_config_entry_id") == env.entry.entry_id:
            checked = True
            for entity in env.entities.entities.values():
                assert entity.platform == "mfi"
                assert entity.device_id is None
            assert not dr.async_entries_for_parent_device(env.devices, env.parent.id)
        return update(device_id, **kwargs)

    with patch.object(env.devices, "async_update_device", verify):
        await execute(env, "apply")
    assert checked


@pytest.mark.parametrize(
    "confirmation",
    [
        "approve",
        "backup_confirmed",
        "references_reviewed",
        "publisher_native",
        "broker_clean",
    ],
)
async def test_apply_requires_all_explicit_confirmations(installation, confirmation):
    env = installation
    await quiesced(env)
    with pytest.raises(migration.MigrationError):
        await execute(env, "apply", **{confirmation: False})
    assert (await journal(env))["phase"] == "quiesced"
    assert env.devices.async_get(env.parent.id).config_entry_id == env.mqtt.entry_id


@pytest.mark.parametrize("evidence", ["missing", "corrupt", "wrong", "unreadable"])
async def test_durable_maintenance_evidence_is_mandatory(installation, monkeypatch, evidence):
    env = installation
    await quiesced(env)
    monkeypatch.setattr(migration, "_PERSIST_TIMEOUT", 0)
    path = Path(env.hass.config.path(".storage", "core.config_entries"))
    if evidence == "missing":
        await env.hass.async_add_executor_job(path.unlink)
    elif evidence == "corrupt":
        await env.hass.async_add_executor_job(path.write_text, "{broken")
    elif evidence == "wrong":
        raw = await env.hass.async_add_executor_job(migration._read_json, path)
        for row in raw["data"]["entries"]:
            row["disabled_by"] = None
        await env.hass.async_add_executor_job(path.write_text, json.dumps(raw))
    else:
        read = migration._read_json

        def fail(target):
            if target == path:
                raise migration.MigrationError("Cannot read persistence evidence")
            return read(target)

        monkeypatch.setattr(migration, "_read_json", fail)
    with pytest.raises(migration.MigrationError):
        await execute(env, "apply")
    assert (await journal(env))["phase"] == "quiesced"
    assert env.devices.async_get(env.parent.id).config_entry_id == env.mqtt.entry_id


async def test_failed_atomic_intent_prevents_registry_change(installation):
    env = installation
    await quiesced(env)
    before = await journal(env)
    with patch.object(migration, "save_json", side_effect=WriteError("disk full")):
        with pytest.raises(migration.MigrationError, match="could not be saved"):
            await execute(env, "apply")
    assert await journal(env) == before
    assert env.devices.async_get(env.parent.id).config_entry_id == env.mqtt.entry_id
    assert all(
        item.platform == "mqtt"
        for item in er.async_entries_for_device(
            env.entities, env.parent.id, include_disabled_entities=True
        )
    )


@pytest.mark.parametrize("unexpected", ["entity", "disabled_entity", "child", "via", "metadata"])
async def test_companion_retirement_refuses_stale_or_nonempty_device(installation, unexpected):
    env = installation
    await quiesced(env)
    original = env.coordinator._native_invariants

    def inject(data):
        original(data)
        if unexpected in ("entity", "disabled_entity"):
            env.entities.async_get_or_create(
                "sensor",
                "mfi",
                "unexpected",
                config_entry=env.entry,
                device_id=env.companion.id,
                disabled_by=er.RegistryEntryDisabler.USER
                if unexpected == "disabled_entity"
                else None,
            )
        elif unexpected == "child":
            env.devices.async_get_or_create_child(
                config_entry_id=env.entry.entry_id,
                parent_device_id=env.companion.id,
                identifiers={("mfi", "unexpected")},
            )
        elif unexpected == "via":
            env.devices.async_get_or_create(
                config_entry_id=env.entry.entry_id,
                identifiers={("mfi", "dependent")},
                via_device_id=env.companion.id,
            )
        else:
            env.devices.async_update_device(env.companion.id, name_by_user="Changed")

    with patch.object(env.coordinator, "_native_invariants", inject):
        with pytest.raises(migration.MigrationError):
            await execute(env, "apply")
    assert env.devices.async_get(env.companion.id) is not None
    assert (await journal(env))["phase"] == "children_attached"


@pytest.mark.parametrize("change", ["descriptor", "export_id", "role", "port", "topic", "binding"])
async def test_prepare_rejects_ambiguous_inputs(installation, change):
    env = installation
    data = copy.deepcopy(env.prepare)
    if change == "descriptor":
        data["descriptor"]["ports"][1]["id"] = 1
    elif change == "export_id":
        data["migration_map"]["device_id"] = uuid4().hex
    elif change == "role":
        data["mapping"]["1"]["current"] = data["mapping"]["1"]["power"]
    elif change == "port":
        data["migration_map"]["ports"][1]["id"] = 2
    elif change == "topic":
        data["migration_map"]["ports"][0]["roles"]["relay"]["discovery_topic"] = "homeassistant/#"
    else:
        await env.store.async_save(replace(env.checkpoint, ports=env.checkpoint.ports[:1]))
    with pytest.raises(migration.MigrationError):
        await env.coordinator.async_execute("prepare", data)
    assert await journal(env) is None
    assert env.mqtt.disabled_by is None


async def test_setup_gate_blocks_missing_corrupt_and_pending_journals(installation):
    env = installation
    await quiesced(env)
    with pytest.raises(ConfigEntryError, match="quiesced"):
        await migration.async_assert_ready(env.hass, env.entry)
    path = migration.JournalStore(env.hass, env.storage_id).path
    await env.hass.async_add_executor_job(path.write_text, "{}")
    with pytest.raises(ConfigEntryError, match="Invalid"):
        await migration.async_assert_ready(env.hass, env.entry)
    await env.hass.async_add_executor_job(path.unlink)
    env.hass.config_entries.async_update_entry(
        env.entry, data={**env.entry.data, "migration_journal": env.storage_id}
    )
    with pytest.raises(ConfigEntryError, match="missing"):
        await migration.async_assert_ready(env.hass, env.entry)


async def test_loaded_entity_blocks_transfer(installation):
    env = installation
    await quiesced(env)
    entity_id = env.prepare["mapping"]["1"]["power"]
    entity_sources(env.hass)[entity_id] = {"domain": "sensor", "platform": "mqtt"}
    try:
        with pytest.raises(migration.MigrationError, match="still loaded"):
            await execute(env, "apply")
    finally:
        entity_sources(env.hass).pop(entity_id)


async def test_failed_unload_never_disables_mqtt(installation):
    env = installation
    await prepared(env)
    with patch.object(env.hass.config_entries, "async_unload", AsyncMock(return_value=False)):
        with pytest.raises(migration.MigrationError, match="unload/checkpoint"):
            await execute(env, "quiesce")
    assert env.mqtt.disabled_by is None
    assert (await journal(env))["phase"] == "quiescing"


async def test_registration_only_has_no_runtime_or_broker_actions(installation):
    env = installation
    with (
        patch.object(env.hass.config_entries, "async_unload", AsyncMock()) as unload,
        patch("homeassistant.components.mqtt.async_publish", AsyncMock()) as publish,
    ):
        await migration.async_setup(env.hass)
        await migration.async_setup(env.hass)
    unload.assert_not_called()
    publish.assert_not_called()
    assert await journal(env) is None
    for action in ("prepare", "quiesce", "apply", "finish", "rollback"):
        assert env.hass.services.has_service("mfi", f"migration_{action}")
    with pytest.raises(vol.Invalid):
        await env.hass.services.async_call(
            "mfi",
            "migration_quiesce",
            {"journal_id": env.storage_id, "approve": False},
            blocking=True,
            return_response=True,
        )


@pytest.mark.parametrize(
    "phase",
    [
        "entities_transferred",
        "parent_transferred",
        "children_attached",
        "companion_retirement_pending",
        "companion_retired",
        "rollback_ready",
    ],
)
async def test_real_storage_restart_and_original_companion_restoration(
    installation, monkeypatch, phase
):
    env = installation
    await quiesced(env)
    save = migration.JournalStore._save

    def crash(store, data):
        save(store, data)
        if data["phase"] == phase:
            raise migration.MigrationError("restart now")

    with patch.object(migration.JournalStore, "_save", crash):
        with pytest.raises(migration.MigrationError, match="restart now"):
            await execute(env, "apply")
            await execute(env, "rollback")
    original = await journal(env)
    await env.hass.async_stop(force=True)
    async with async_test_home_assistant(
        load_registries=False,
        config_dir=env.hass.config.config_dir,
        initial_state=CoreState.not_running,
    ) as restarted:
        try:
            install_offline_runtimes(restarted, monkeypatch)
            await restarted.config_entries.async_initialize()
            dr.async_setup(restarted)
            await dr.async_load(restarted)
            await er.async_load(restarted)
            await ir.async_load(restarted)
            await restarted.async_start()
            env.hass = restarted
            env.entry = restarted.config_entries.async_get_entry(env.entry.entry_id)
            env.mqtt = restarted.config_entries.async_get_entry(env.mqtt.entry_id)
            env.devices = dr.async_get(restarted)
            env.entities = er.async_get(restarted)
            env.store = CheckpointStore(restarted, env.storage_id)
            env.coordinator = migration.MigrationCoordinator(restarted)
            assert env.mqtt.disabled_by is ConfigEntryDisabler.USER
            with pytest.raises(ConfigEntryError):
                await migration.async_assert_ready(restarted, env.entry)
            result = await execute(env, "rollback", legacy_restored=True)
            if result["phase"] == "rollback_ready":
                result = await execute(env, "rollback", legacy_restored=True)
            assert result["phase"] == "rolled_back"
            assert (
                migration._device_snapshot(env.devices.async_get(env.companion.id))
                == original["companion"]
            )
            assert await env.store.async_load() == env.checkpoint
            for item in original["entities"]:
                entity = env.entities.async_get(item["before"]["entity_id"])
                assert entity.id == item["before"]["id"]
                assert entity.device_id == item["before"]["device_id"]
        finally:
            for entry in restarted.config_entries.async_entries():
                if entry.state is ConfigEntryState.LOADED:
                    await restarted.config_entries.async_unload(entry.entry_id)
            await restarted.async_stop(force=True)


@pytest.mark.parametrize("phase", ["rolling_back", "rollback_ready", "rolled_back"])
async def test_rollback_phase_crashes_are_resumable(installation, phase):
    env = installation
    await quiesced(env)
    await execute(env, "apply")
    await execute(env, "finish")
    save = migration.JournalStore._save
    fired = False

    def crash(store, data):
        nonlocal fired
        save(store, data)
        if data["phase"] == phase and not fired:
            fired = True
            raise migration.MigrationError("rollback crash")

    with patch.object(migration.JournalStore, "_save", crash):
        with pytest.raises(migration.MigrationError, match="rollback crash"):
            await execute(env, "rollback")
            await execute(env, "rollback", legacy_restored=True)
    env.coordinator = migration.MigrationCoordinator(env.hass)
    result = await execute(env, "rollback", legacy_restored=True)
    if result["phase"] == "rollback_ready":
        result = await execute(env, "rollback", legacy_restored=True)
    assert result["phase"] == "rolled_back"


@pytest.mark.parametrize(
    "operation",
    [
        "async_update_entity",
        "async_remove_device",
        "async_update_device",
        "async_get_or_create",
        "async_update_entity_platform",
    ],
)
async def test_rollback_mutation_crashes_are_resumable(installation, operation):
    env = installation
    await quiesced(env)
    await execute(env, "apply")
    registry = (
        env.entities
        if operation in ("async_update_entity", "async_update_entity_platform")
        else env.devices
    )
    original = getattr(registry, operation)
    fired = False

    def crash(*args, **kwargs):
        nonlocal fired
        result = original(*args, **kwargs)
        if not fired:
            fired = True
            raise migration.MigrationError("rollback mutation")
        return result

    with patch.object(registry, operation, crash):
        with pytest.raises(migration.MigrationError, match="rollback mutation"):
            await execute(env, "rollback")
    env.coordinator = migration.MigrationCoordinator(env.hass)
    assert (await execute(env, "rollback"))["phase"] == "rollback_ready"
    assert (
        migration._device_snapshot(env.devices.async_get(env.companion.id))
        == (await journal(env))["companion"]
    )


async def test_native_parent_metadata_is_restored_on_rollback(installation):
    env = installation
    await quiesced(env)
    await execute(env, "apply")
    await execute(env, "finish")
    env.devices.async_update_device(
        env.parent.id, name="Native name", sw_version="new firmware", model="Updated model"
    )
    await execute(env, "rollback")
    await execute(env, "rollback", legacy_restored=True)
    assert (
        migration._device_snapshot(env.devices.async_get(env.parent.id))
        == (await journal(env))["parent"]
    )


async def test_unexpected_parent_member_blocks_parent_transfer(installation):
    env = installation
    await quiesced(env)
    env.entities.async_get_or_create(
        "sensor",
        "mqtt",
        "unmapped",
        config_entry=env.mqtt,
        device_id=env.parent.id,
        disabled_by=er.RegistryEntryDisabler.USER,
    )
    with patch.object(
        env.devices, "async_update_device", wraps=env.devices.async_update_device
    ) as update:
        with pytest.raises(migration.MigrationError, match="still has entities"):
            await execute(env, "apply")
    assert not any(
        call.kwargs.get("new_config_entry_id") == env.entry.entry_id
        for call in update.call_args_list
    )
    assert env.devices.async_get(env.parent.id).config_entry_id == env.mqtt.entry_id


async def test_cannot_adopt_unjournaled_native_entity(installation):
    env = installation
    await quiesced(env)
    item = (await journal(env))["entities"][0]
    env.entities.async_update_entity_platform(
        item["before"]["entity_id"],
        "mfi",
        new_config_entry_id=env.entry.entry_id,
        new_unique_id=item["native_unique_id"],
        new_device_id=None,
    )
    with pytest.raises(migration.MigrationError, match="does not match"):
        await execute(env, "apply")
    assert env.devices.async_get(env.parent.id).config_entry_id == env.mqtt.entry_id


async def test_checkpoint_regression_blocks_before_transfer(installation):
    env = installation
    await quiesced(env)
    await env.store.async_save(
        env.checkpoint.next_generation(
            tuple(replace(port, total=Decimal("0")) for port in env.checkpoint.ports)
        )
    )
    with patch.object(env.entities, "async_update_entity_platform") as transfer:
        with pytest.raises(migration.MigrationError, match="regressed"):
            await execute(env, "apply")
    transfer.assert_not_called()


async def test_activation_failure_remains_pending_not_successful(installation):
    env = installation
    await quiesced(env)
    await execute(env, "apply")
    with patch.object(env.hass.config_entries, "async_setup", AsyncMock(return_value=False)):
        with pytest.raises(migration.MigrationError):
            await execute(env, "finish")
    assert (await journal(env))["phase"] == "activating"
    assert (await execute(env, "finish"))["phase"] == "active"


async def test_missing_retirement_intent_does_not_accept_absent_companion(installation):
    env = installation
    await quiesced(env)
    env.devices.async_remove_device(env.companion.id)
    await env.hass.async_block_till_done()
    with pytest.raises(migration.MigrationError):
        await execute(env, "apply")
    assert (await journal(env))["phase"] != "companion_retired"


async def test_companion_id_loss_is_explicit_rollback_blocker(installation):
    env = installation
    await quiesced(env)
    await execute(env, "apply")
    original = env.devices.async_get_or_create

    def replaced_id(**kwargs):
        device = original(**kwargs)
        return replace_device_id(device)

    def replace_device_id(device):
        # Model HA failing to restore the original ID, without writing private registries.
        from attr import evolve

        return evolve(device, id=uuid4().hex)

    with patch.object(env.devices, "async_get_or_create", replaced_id):
        with pytest.raises(migration.MigrationError, match="Original companion device ID"):
            await execute(env, "rollback")
    assert (await journal(env))["phase"] == "rolling_back"
    assert env.mqtt.disabled_by is ConfigEntryDisabler.USER
    assert all(
        e.device_id is None
        for e in er.async_entries_for_config_entry(env.entities, env.entry.entry_id)
    )


async def test_cancelled_coordinator_keeps_lock_until_journal_operation_finishes(installation):
    env = installation
    await prepared(env)
    reached = asyncio.Event()
    release = asyncio.Event()
    guard = env.coordinator._guard

    async def slow_guard(data):
        reached.set()
        await release.wait()
        await guard(data)

    with patch.object(env.coordinator, "_guard", slow_guard):
        task = asyncio.create_task(execute(env, "quiesce"))
        await reached.wait()
        task.cancel()
        await asyncio.sleep(0)
        assert not task.done()
        assert env.coordinator.lock.locked()
        release.set()
        with pytest.raises(asyncio.CancelledError):
            await task
    assert (await journal(env))["phase"] == "quiesced"
    assert not env.coordinator.lock.locked()


async def test_service_admin_permission_and_response_schema(installation):
    env = installation
    from homeassistant.core import Context
    from homeassistant.exceptions import Unauthorized
    from homeassistant.util.yaml import load_yaml

    await env.hass.auth.async_create_user("Owner")
    user = await env.hass.auth.async_create_user("Not an admin")
    assert not user.is_admin
    await migration.async_setup(env.hass)
    with pytest.raises(Unauthorized):
        await env.hass.services.async_call(
            "mfi",
            "migration_prepare",
            env.prepare,
            blocking=True,
            return_response=True,
            context=Context(user_id=user.id),
        )
    response = await env.hass.services.async_call(
        "mfi",
        "migration_prepare",
        env.prepare,
        blocking=True,
        return_response=True,
    )
    assert response["journal_id"] == env.storage_id
    assert response["phase"] == "prepared"
    descriptions = await env.hass.async_add_executor_job(
        load_yaml, "custom_components/mfi/services.yaml"
    )
    assert set(descriptions) == {
        f"migration_{action}" for action in ("prepare", "quiesce", "apply", "finish", "rollback")
    }
    for name, service in env.hass.services.async_services()["mfi"].items():
        assert {getattr(key, "schema", key) for key in service.schema.schema} == set(
            descriptions[name]["fields"]
        )
