"""Explicit, offline ownership migration; this module never publishes to MQTT.

First configure the legacy energy companion. An administrator supplies the
publisher's migration-map export and confirms the external backup, device
reference audit, publisher cutover, and exact retained-discovery cleanup.
Installation and ordinary entry setup never perform any of those operations.
"""

from __future__ import annotations

import asyncio
import json
import re
from collections.abc import Callable, Mapping
from functools import partial
from pathlib import Path
from typing import TYPE_CHECKING, Any

import voluptuous as vol
from homeassistant.config_entries import ConfigEntryDisabler, ConfigEntryState
from homeassistant.core import HomeAssistant, ServiceCall, SupportsResponse
from homeassistant.exceptions import ConfigEntryError, HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import issue_registry as ir
from homeassistant.helpers.entity import entity_sources
from homeassistant.helpers.json import save_json
from homeassistant.helpers.service import async_register_admin_service
from homeassistant.util.hass_dict import HassKey

from .const import CONF_SOURCE_CONFIG_ENTRY, CONF_SOURCE_DEVICE, CONF_STORAGE_ID, DOMAIN
from .source import destination_conflict, destination_lock
from .storage import Checkpoint, CheckpointStore, async_complete_io

if TYPE_CHECKING:
    from homeassistant.config_entries import ConfigEntry

_ID = re.compile(r"[0-9a-f]{32}")
_TOPIC_PART = re.compile(r"[^/+#\x00-\x1f\x7f]+")
_COORDINATOR: HassKey[MigrationCoordinator] = HassKey("mfi_migration_coordinator")
_ACTIVATING: HassKey[set[str]] = HassKey("mfi_migration_activating")
_PERSIST_TIMEOUT = 15.0
_PERSIST_INTERVAL = 0.1
_PHASES = (
    "prepared",
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
    "rolling_back",
    "rollback_ready",
    "rolled_back",
)
_ENTITY_USER_FIELDS = (
    "aliases",
    "area_id",
    "categories",
    "device_class",
    "hidden_by",
    "icon",
    "labels",
    "name",
    "options",
)
_DEVICE_USER_FIELDS = ("area_id", "labels", "name_by_user")


class MigrationError(HomeAssistantError):
    """The maintenance transaction requires correction before it may continue."""


def _require(condition: object, message: str) -> None:
    if not condition:
        raise MigrationError(message)


def _object(value: object, message: str) -> dict[str, Any]:
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value):
        raise MigrationError(message)
    return value


def _json_copy(value: Mapping[str, Any]) -> dict[str, Any]:
    return _object(json.loads(json.dumps(value)), "Expected a JSON object")


def _device_snapshot(device: dr.DeviceEntry | dr.ChildDeviceEntry) -> dict[str, Any]:
    result = _json_copy(device.dict_repr)
    for key in ("identifiers", "connections", "labels"):
        if key in result:
            result[key] = sorted(result[key])
    for key in ("created_at", "modified_at", "config_entries", "config_entries_subentries"):
        result.pop(key, None)
    return result


def _entity_snapshot(entity: er.RegistryEntry) -> dict[str, Any]:
    result = _json_copy(entity.extended_dict)
    result["original_name"] = entity.original_name
    result["labels"] = sorted(result["labels"])
    result["unit_of_measurement"] = entity.unit_of_measurement
    result["supported_features"] = entity.supported_features
    result["previous_unique_id"] = entity.previous_unique_id
    for key in ("created_at", "modified_at"):
        result.pop(key, None)
    return result


def _user_options(options: dict[str, Any]) -> dict[str, Any]:
    """HA updates precision suggestions when a new sensor implementation loads."""
    result = _json_copy(options)
    sensor = result.get("sensor")
    if isinstance(sensor, dict):
        sensor.pop("suggested_display_precision", None)
        if not sensor:
            result.pop("sensor")
    return result


def _read_json(path: Path) -> dict[str, Any] | None:
    """Read strictly; a missing file is distinct from corrupt/unreadable storage."""
    try:
        with path.open(encoding="utf-8") as stream:
            result = json.load(stream)
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as error:
        raise MigrationError(f"Cannot read migration/persistence evidence: {path.name}") from error
    return _object(result, f"Invalid persistence evidence: {path.name}")


class JournalStore:
    """Private, atomic, error-propagating migration journal."""

    def __init__(self, hass: HomeAssistant, storage_id: str) -> None:
        _require(isinstance(storage_id, str) and _ID.fullmatch(storage_id), "Invalid journal ID")
        self.hass = hass
        self.storage_id = storage_id
        self.path = Path(hass.config.path(".storage", f"mfi_migration.{storage_id}"))

    async def async_load(self) -> dict[str, Any] | None:
        result = await async_complete_io(self.hass.async_add_executor_job(_read_json, self.path))
        if result is not None:
            _require(
                type(result.get("version")) is int
                and result["version"] == 1
                and result.get("storage_id") == self.storage_id
                and result.get("phase") in _PHASES
                and isinstance(result.get("entities"), list)
                and isinstance(result.get("children"), dict)
                and isinstance(result.get("entry"), dict),
                "Invalid migration journal; restore its backup before recovery",
            )
            self._validate(result)
        return result

    def _validate(self, journal: dict[str, Any]) -> None:
        from .protocol import parse_descriptor

        try:
            entry = journal["entry"]
            _require(
                isinstance(entry["entry_id"], str)
                and isinstance(entry["data"], dict)
                and entry["data"].get(CONF_STORAGE_ID) == self.storage_id
                and isinstance(entry["options"], dict)
                and type(entry["version"]) is int
                and type(entry["minor_version"]) is int
                and isinstance(journal["mqtt_entry_id"], str)
                and isinstance(journal["intents"], list)
                and all(isinstance(intent, str) for intent in journal["intents"])
                and all(
                    isinstance(pid, str) and isinstance(cid, str)
                    for pid, cid in journal["children"].items()
                ),
                "Invalid migration preimage",
            )
            descriptor = parse_descriptor(json.dumps(journal["descriptor"]))
            ports = {str(port.id): port for port in descriptor.ports}
            bindings = _object(journal["port_bindings"], "Invalid binding map")
            energy_ids = _object(journal["energy_unique_ids"], "Invalid energy map")
            checkpoint = Checkpoint.from_dict(journal["checkpoint"], self.storage_id)
            Checkpoint.from_dict(journal["original_checkpoint"], self.storage_id)
            _require(
                set(bindings)
                == {pid for pid, port in ports.items() if "power" in port.capabilities}
                and set(bindings.values()) == {p.binding_id for p in checkpoint.ports}
                and len(bindings) == len(checkpoint.ports)
                and set(energy_ids) <= set(bindings)
                and set(journal["children"]) <= set(ports),
                "Invalid journal physical-port mapping",
            )
            seen: set[str] = set()
            roles: set[tuple[str, str]] = set()
            for item in journal["entities"]:
                before = item["before"]
                pid, role = item["port_id"], item["role"]
                _require(
                    pid in ports and (pid, role) not in roles and before["id"] not in seen,
                    "Duplicate or unknown journal entity",
                )
                seen.add(before["id"])
                roles.add((pid, role))
                _require(
                    all(
                        field in before
                        for field in (
                            *_ENTITY_USER_FIELDS,
                            "disabled_by",
                            "entity_id",
                            "unique_id",
                            "platform",
                            "config_entry_id",
                            "config_subentry_id",
                            "device_id",
                        )
                    )
                    and item["native_unique_id"]
                    == (
                        energy_ids.get(pid)
                        if role == "energy"
                        else descriptor.unique_id(int(pid), role)
                    ),
                    "Invalid journal entity preimage",
                )
            _require(
                roles
                == {(str(port.id), role) for port in descriptor.ports for role in port.capabilities}
                | {(pid, "energy") for pid in energy_ids},
                "Incomplete journal entity inventory",
            )
            _require(isinstance(journal["parent"]["id"], str), "Invalid parent preimage")
            _require(
                journal["companion"] is None or isinstance(journal["companion"]["id"], str),
                "Invalid companion preimage",
            )
            _require(isinstance(journal["discovery_topics"], list), "Invalid discovery inventory")
        except (KeyError, TypeError, ValueError, HomeAssistantError) as error:
            raise MigrationError(
                "Invalid migration journal; restore its backup before recovery"
            ) from error

    def _save(self, data: dict[str, Any]) -> None:
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            save_json(str(self.path), data, private=True, atomic_writes=True)
        except (OSError, HomeAssistantError) as error:
            raise MigrationError(
                "Migration journal could not be saved; do not resume MQTT"
            ) from error

    async def async_save(self, data: dict[str, Any]) -> None:
        await async_complete_io(self.hass.async_add_executor_job(self._save, _json_copy(data)))


async def async_assert_ready(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Setup gate, required before creating either a native or companion runtime."""
    try:
        journal = await JournalStore(hass, entry.data[CONF_STORAGE_ID]).async_load()
        if journal is None:
            _require(not entry.data.get("migration_journal"), "The migration journal is missing")
            return
        _require(journal["entry"]["entry_id"] == entry.entry_id, "Journal entry identity changed")
        native = entry.data.get("mode") == "native"
        if (journal["phase"] == "active" and native) or (
            journal["phase"] in ("prepared", "rolled_back") and not native
        ):
            return
        if entry.entry_id in hass.data.get(_ACTIVATING, set()) and (
            (journal["phase"] == "activating" and native)
            or (journal["phase"] == "rollback_ready" and not native)
        ):
            return
        raise MigrationError(
            f"Ownership migration is {journal['phase']}; use the administrative migration services"
        )
    except (MigrationError, KeyError) as error:
        _issue(hass, entry.entry_id, str(error))
        raise ConfigEntryError(str(error)) from error


async def async_assert_destination_available(
    hass: HomeAssistant, device_id: str, *, entry_id: str | None = None
) -> None:
    """A prepared migration reserves identity before its config entry is native."""
    for entry in hass.config_entries.async_entries(DOMAIN):
        if entry.entry_id == entry_id:
            continue
        storage_id = entry.data.get(CONF_STORAGE_ID)
        if storage_id is None:
            continue
        journal = await JournalStore(hass, storage_id).async_load()
        _require(
            journal is None
            or journal["phase"] == "rolled_back"
            or journal["descriptor"]["device_id"] != device_id,
            "This native device ID is reserved by an ownership migration",
        )


def _issue(hass: HomeAssistant, entry_id: str, message: str | None) -> None:
    issue_id = f"{entry_id}_migration"
    if message is None:
        ir.async_delete_issue(hass, DOMAIN, issue_id)
    else:
        ir.async_create_issue(
            hass,
            DOMAIN,
            issue_id,
            is_fixable=False,
            severity=ir.IssueSeverity.ERROR,
            translation_key="storage",
            translation_placeholders={"detail": message},
        )


async def _async_wait_persisted(
    hass: HomeAssistant, key: str, matches: Callable[[dict[str, Any]], bool]
) -> None:
    """Observe HA's scheduled public writes without invoking its private Store."""
    path = Path(hass.config.path(".storage", key))
    deadline = asyncio.get_running_loop().time() + _PERSIST_TIMEOUT
    while True:
        raw = await async_complete_io(hass.async_add_executor_job(_read_json, path))
        if raw is not None:
            _require(isinstance(raw.get("data"), dict), f"Invalid {key} persistence envelope")
            if matches(raw["data"]):
                return
        if asyncio.get_running_loop().time() >= deadline:
            raise MigrationError(
                f"{key} is not durably saved. Keep maintenance enabled, fix storage, and retry"
            )
        await asyncio.sleep(_PERSIST_INTERVAL)


def _rows(data: dict[str, Any], key: str) -> list[dict[str, Any]]:
    value = data.get(key)
    if not isinstance(value, list):
        raise MigrationError(f"Invalid persisted {key}")
    return [_object(row, f"Invalid persisted {key}") for row in value]


def _identifier_present(hass: HomeAssistant, identifier: tuple[str, str]) -> bool:
    devices = dr.async_get(hass)
    return bool(devices.async_get_devices(identifiers={identifier})) or any(
        devices.async_get_child_device_by_identifier(identifier, entry.entry_id) is not None
        for entry in hass.config_entries.async_entries()
    )


class MigrationCoordinator:
    """One domain-level lock also serializes all devices sharing an MQTT entry."""

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.lock = asyncio.Lock()

    def _entry(self, entry_id: str, domain: str) -> ConfigEntry:
        entry = self.hass.config_entries.async_get_entry(entry_id)
        _require(entry is not None and entry.domain == domain, f"Missing {domain} entry")
        assert entry is not None
        return entry

    def _unloaded(self, journal: dict[str, Any]) -> None:
        for entry_id, domain in (
            (journal["entry"]["entry_id"], DOMAIN),
            (journal["mqtt_entry_id"], "mqtt"),
        ):
            entry = self._entry(entry_id, domain)
            _require(
                entry.state
                in (
                    ConfigEntryState.NOT_LOADED,
                    ConfigEntryState.SETUP_ERROR,
                    ConfigEntryState.SETUP_RETRY,
                ),
                f"{domain} is not safely unloaded",
            )
            _require(not entry.setup_lock.locked(), f"{domain} setup/recovery is in progress")
        loaded = entity_sources(self.hass)
        _require(
            not any(item["before"]["entity_id"] in loaded for item in journal["entities"]),
            "An inventoried entity is still loaded",
        )

    async def _guard(self, journal: dict[str, Any]) -> None:
        mqtt_entry = self._entry(journal["mqtt_entry_id"], "mqtt")
        _require(
            mqtt_entry.disabled_by is ConfigEntryDisabler.USER, "MQTT maintenance is not enabled"
        )
        self._unloaded(journal)
        await _async_wait_persisted(
            self.hass,
            "core.config_entries",
            lambda data: any(
                row.get("entry_id") == mqtt_entry.entry_id and row.get("disabled_by") == "user"
                for row in _rows(data, "entries")
            ),
        )
        self._unloaded(journal)
        _require(
            mqtt_entry.disabled_by is ConfigEntryDisabler.USER,
            "MQTT maintenance changed while awaiting persistence",
        )

    async def _entry_persisted(self, entry: ConfigEntry) -> None:
        expected = {
            "entry_id": entry.entry_id,
            "data": _json_copy(dict(entry.data)),
            "version": entry.version,
            "minor_version": entry.minor_version,
            "options": _json_copy(dict(entry.options)),
            "unique_id": entry.unique_id,
            "disabled_by": entry.disabled_by,
        }
        await _async_wait_persisted(
            self.hass,
            "core.config_entries",
            lambda data: any(
                all(row.get(key) == value for key, value in expected.items())
                for row in _rows(data, "entries")
            ),
        )

    async def _registries_persisted(self, journal: dict[str, Any]) -> None:
        registry = er.async_get(self.hass)
        expected_entities = [self._entity(item) for item in journal["entities"]]
        fields = (
            "id",
            "entity_id",
            "unique_id",
            "platform",
            "config_entry_id",
            "config_subentry_id",
            "device_id",
            "disabled_by",
            "name",
            "icon",
            "area_id",
            "hidden_by",
            "options",
            "categories",
            "device_class",
        )
        await _async_wait_persisted(
            self.hass,
            "core.entity_registry",
            lambda data: all(
                any(
                    all(row.get(key) == getattr(entity, key) for key in fields)
                    and sorted(row.get("labels", [])) == sorted(entity.labels)
                    for row in _rows(data, "entities")
                )
                for entity in expected_entities
            ),
        )
        devices = dr.async_get(self.hass)
        ids = {journal["parent"]["id"], *journal["children"].values()}
        if journal["companion"] is not None:
            ids.add(journal["companion"]["id"])
        expected_devices = {
            device_id: _device_snapshot(device)
            if (device := devices.async_get(device_id))
            else None
            for device_id in ids
        }

        def devices_match(data: dict[str, Any]) -> bool:
            rows = _rows(data, "devices") + _rows(data, "child_devices")
            for device_id, expected in expected_devices.items():
                found = next((row for row in rows if row.get("id") == device_id), None)
                if expected is None:
                    if found is not None:
                        return False
                    companion = journal["companion"]
                    if (
                        companion is not None
                        and device_id == companion["id"]
                        and "retire_companion" in journal["intents"]
                    ):
                        deleted = next(
                            (
                                row
                                for row in _rows(data, "deleted_devices")
                                if row.get("id") == device_id
                            ),
                            None,
                        )
                        if (
                            deleted is None
                            or any(
                                deleted.get(key) != companion[key]
                                for key in (
                                    "config_entry_id",
                                    "config_subentry_id",
                                    "name_by_user",
                                    "area_id",
                                    "disabled_by",
                                )
                            )
                            or any(
                                sorted(deleted.get(key, [])) != companion[key]
                                for key in ("identifiers", "connections", "labels")
                            )
                        ):
                            return False
                    continue
                if found is None:
                    return False
                for key, value in expected.items():
                    actual = found.get(key)
                    if key in ("identifiers", "labels", "connections"):
                        actual = sorted(actual) if isinstance(actual, list) else None
                    if actual != value:
                        return False
            return True

        await _async_wait_persisted(self.hass, "core.device_registry", devices_match)
        _require(
            all(registry.async_get(entity.entity_id) == entity for entity in expected_entities),
            "Entity registry changed while awaiting persistence",
        )
        _require(
            all(
                (_device_snapshot(device) if (device := devices.async_get(device_id)) else None)
                == expected
                for device_id, expected in expected_devices.items()
            ),
            "Device registry changed while awaiting persistence",
        )

    def _entity(self, item: dict[str, Any]) -> er.RegistryEntry:
        before = item["before"]
        entity = er.async_get(self.hass).async_get(before["entity_id"])
        _require(entity is not None and entity.id == before["id"], "Entity identity changed")
        assert entity is not None
        return entity

    def _user_metadata(self, item: dict[str, Any]) -> None:
        current = _entity_snapshot(self._entity(item))
        changed = [
            field
            for field in _ENTITY_USER_FIELDS
            if field != "options" and current[field] != item["before"][field]
        ]
        if _user_options(current["options"]) != _user_options(item["before"]["options"]):
            changed.append("options")
        _require(
            not changed,
            f"User metadata changed for {current['entity_id']} ({', '.join(changed)}); "
            "resolve before continuing",
        )
        _require(
            current["disabled_by"] == item["before"]["disabled_by"]
            or (
                item["before"]["disabled_by"] is None
                and current["disabled_by"] in ("config_entry", "device")
            ),
            f"Disabled state changed for {current['entity_id']}",
        )

    def _restore_disabled(self, item: dict[str, Any]) -> None:
        disabled = item["before"]["disabled_by"]
        er.async_get(self.hass).async_update_entity(
            item["before"]["entity_id"],
            disabled_by=er.RegistryEntryDisabler(disabled) if disabled else None,
        )

    def _device_unchanged(
        self, expected: dict[str, Any], *, maintenance: bool = False
    ) -> dr.DeviceEntry:
        device = dr.async_get(self.hass).async_get(expected["id"])
        _require(isinstance(device, dr.DeviceEntry), "Inventoried main device is missing")
        assert isinstance(device, dr.DeviceEntry)
        current = _device_snapshot(device)
        if (
            maintenance
            and expected["disabled_by"] is None
            and current["disabled_by"] == "config_entry"
        ):
            current["disabled_by"] = None
        _require(current == expected, f"Device {device.id} changed since inventory")
        return device

    def _empty(self, device_id: str) -> None:
        _require(
            not er.async_entries_for_device(
                er.async_get(self.hass), device_id, include_disabled_entities=True
            ),
            f"Device {device_id} still has entities (including disabled entities)",
        )
        _require(
            not dr.async_entries_for_parent_device(dr.async_get(self.hass), device_id),
            f"Device {device_id} still has children",
        )
        _require(
            not any(
                getattr(device, "via_device_id", None) == device_id
                for device in dr.async_get(self.hass).devices
            ),
            f"Device {device_id} has dependent via-device references",
        )

    def _restore_device_metadata(self, before: dict[str, Any]) -> None:
        owner = self.hass.config_entries.async_get_entry(before["config_entry_id"])
        _require(owner is not None, "Original device owner is missing")
        assert owner is not None
        disabled = before["disabled_by"]
        if disabled is None and owner.disabled_by is not None:
            disabled = "config_entry"
        dr.async_get(self.hass).async_update_device(
            before["id"],
            area_id=before["area_id"],
            configuration_url=before["configuration_url"],
            entry_type=dr.DeviceEntryType(before["entry_type"]) if before["entry_type"] else None,
            hw_version=before["hw_version"],
            labels=set(before["labels"]),
            manufacturer=before["manufacturer"],
            model=before["model"],
            model_id=before["model_id"],
            name=before["name"],
            name_by_user=before["name_by_user"],
            serial_number=before["serial_number"],
            sw_version=before["sw_version"],
            via_device_id=before["via_device_id"],
            disabled_by=dr.DeviceEntryDisabler(disabled) if disabled else None,
        )

    async def _record(self, journal: dict[str, Any], **updates: Any) -> None:
        if intent := updates.get("intent"):
            if intent not in journal["intents"]:
                journal["intents"].append(intent)
        journal.update(updates)
        await JournalStore(self.hass, journal["storage_id"]).async_save(journal)

    async def _phase(self, journal: dict[str, Any], phase: str) -> None:
        if _PHASES.index(journal["phase"]) < _PHASES.index(phase):
            await self._record(journal, phase=phase, intent=None)

    async def async_execute(self, action: str, data: Mapping[str, Any]) -> dict[str, Any]:
        """Keep the lock and journal operation alive through service cancellation."""
        task = self.hass.async_create_task(
            self._async_execute(action, data), f"mFi migration {action}"
        )
        return await async_complete_io(task)

    async def _async_execute(self, action: str, data: Mapping[str, Any]) -> dict[str, Any]:
        async with self.lock:
            entry_id = data.get("entry_id", "migration")
            try:
                if action == "prepare":
                    journal = await self._prepare(data)
                else:
                    loaded = await JournalStore(self.hass, data["journal_id"]).async_load()
                    _require(loaded is not None, "Migration journal not found; prepare first")
                    assert loaded is not None
                    journal = loaded
                    entry_id = journal["entry"]["entry_id"]
                    await self._exclusive(journal["mqtt_entry_id"], entry_id)
                    _require(data.get("approve") is True, "Explicit approval must be true")
                    if action == "quiesce":
                        await self._quiesce(journal)
                    elif action == "apply":
                        await self._apply(journal, data)
                    elif action == "finish":
                        await self._finish(journal, data)
                    elif action == "rollback":
                        await self._rollback(journal, data)
                    else:
                        raise MigrationError("Unknown migration action")
                _issue(self.hass, entry_id, None)
                return self._response(journal)
            except (HomeAssistantError, OSError, ValueError, KeyError, TypeError) as error:
                _issue(self.hass, entry_id, str(error))
                if isinstance(error, MigrationError):
                    raise
                raise MigrationError(f"Migration stopped: {error}") from error

    async def _exclusive(self, mqtt_entry_id: str, entry_id: str) -> None:
        for other in self.hass.config_entries.async_entries(DOMAIN):
            if other.entry_id == entry_id:
                continue
            storage_id = other.data.get(CONF_STORAGE_ID)
            if storage_id is None:
                continue
            journal = await JournalStore(self.hass, storage_id).async_load()
            _require(
                journal is None
                or journal["mqtt_entry_id"] != mqtt_entry_id
                or journal["phase"] in ("active", "rolled_back"),
                "Another unfinished migration reserves this shared MQTT entry",
            )

    def _response(self, journal: dict[str, Any]) -> dict[str, Any]:
        return _json_copy(
            {
                "journal_id": journal["storage_id"],
                "phase": journal["phase"],
                "entry_id": journal["entry"]["entry_id"],
                "mqtt_entry_id": journal["mqtt_entry_id"],
                "legacy_discovery_topics": journal["discovery_topics"],
                "entities": journal["entities"],
                "parent": journal["parent"],
                "companion": journal["companion"],
                "port_bindings": journal["port_bindings"],
                "energy_unique_ids": journal["energy_unique_ids"],
                "effects": [
                    "Shared MQTT is temporarily unavailable for every MQTT device.",
                    "Entity IDs, registry IDs, energy binding IDs and latest totals are preserved.",
                    "Native energy can accumulate even when its Power entity is disabled.",
                    "Only the empty old Energy companion is retired; audit its device references.",
                    "Publisher cutover and exact retained-discovery cleanup are operator-only.",
                    "No broker publication or physical relay test is performed by this service.",
                ],
            }
        )

    def _native_data(self, journal: dict[str, Any]) -> dict[str, Any]:
        return {
            "mode": "native",
            "descriptor": journal["descriptor"],
            CONF_STORAGE_ID: journal["storage_id"],
            "port_bindings": journal["port_bindings"],
            "energy_unique_ids": journal["energy_unique_ids"],
            "migration_journal": journal["storage_id"],
        }

    def _entry_unchanged(self, journal: dict[str, Any], *, native: bool = False) -> None:
        entry = self._entry(journal["entry"]["entry_id"], DOMAIN)
        before = journal["entry"]
        expected = self._native_data(journal) if native else before["data"]
        _require(
            dict(entry.data) == expected
            and dict(entry.options) == before["options"]
            and entry.unique_id
            == (journal["descriptor"]["device_id"] if native else before["unique_id"])
            and entry.version == (2 if native else before["version"])
            and entry.minor_version == before["minor_version"]
            and entry.disabled_by is None,
            "Config entry identity, options or disabled state changed during migration",
        )

    async def _prepare(self, data: Mapping[str, Any]) -> dict[str, Any]:
        from .protocol import parse_descriptor

        descriptor = parse_descriptor(json.dumps(data["descriptor"]))
        async with destination_lock(self.hass, descriptor.device_id):
            _require(
                destination_conflict(self.hass, descriptor.device_id, entry_id=data["entry_id"])
                is None,
                "A native entry or pending setup already reserves this device ID",
            )
            await async_assert_destination_available(
                self.hass, descriptor.device_id, entry_id=data["entry_id"]
            )
            return await self._prepare_reserved(data)

    async def _prepare_reserved(self, data: Mapping[str, Any]) -> dict[str, Any]:
        from .protocol import parse_descriptor

        entry = self._entry(data["entry_id"], DOMAIN)
        _require(
            entry.data.get("mode", "companion") == "companion",
            "Prepare requires an existing legacy companion; configure it first",
        )
        _require(
            not entry.disabled_by and not entry.setup_lock.locked(), "Companion is disabled or busy"
        )
        storage_id = entry.data[CONF_STORAGE_ID]
        store = JournalStore(self.hass, storage_id)
        prior = await store.async_load()
        _require(
            prior is None, "A journal already exists; resume it rather than overwrite its preimage"
        )
        parsed = parse_descriptor(json.dumps(data["descriptor"]))
        descriptor = _json_copy(parsed.as_dict())
        device_id = parsed.device_id
        _require(
            not any(
                other.entry_id != entry.entry_id and other.unique_id == device_id
                for other in self.hass.config_entries.async_entries(DOMAIN)
            ),
            "A competing native entry already owns this device ID",
        )
        mqtt_entry = self._entry(entry.data[CONF_SOURCE_CONFIG_ENTRY], "mqtt")
        await self._exclusive(mqtt_entry.entry_id, entry.entry_id)
        _require(
            mqtt_entry.disabled_by in (None, ConfigEntryDisabler.USER),
            "Unsupported MQTT disable state",
        )
        _require(not mqtt_entry.setup_lock.locked(), "MQTT setup/recovery is in progress")
        devices = dr.async_get(self.hass)
        parent = devices.async_get(entry.data[CONF_SOURCE_DEVICE])
        _require(isinstance(parent, dr.DeviceEntry), "Legacy parent is not a main device")
        assert isinstance(parent, dr.DeviceEntry)
        _require(
            parent.config_entry_id == mqtt_entry.entry_id and parent.config_subentry_id is None,
            "Unexpected parent ownership or subentry",
        )
        _require(
            not parent.has_composite_identifiers and parent.composite_device_id is None,
            "Composite-device migration must be resolved first",
        )
        _require(
            not dr.async_entries_for_parent_device(devices, parent.id), "Legacy parent has children"
        )
        _require(
            not _identifier_present(self.hass, (DOMAIN, device_id)),
            "Native parent identifier already exists",
        )
        for port in descriptor["ports"]:
            _require(
                not _identifier_present(self.hass, (DOMAIN, f"{device_id}_port_{port['id']}")),
                "Native child identifier already exists",
            )
        checkpoint = await CheckpointStore(self.hass, storage_id).async_load()
        _require(not checkpoint.ignored_sources, "Resolve ignored/orphaned source bindings first")
        registry = er.async_get(self.hass)
        export = data["migration_map"]
        mapping = data["mapping"]
        topics = self._validate_export(descriptor, export, mapping, data["discovery_prefix"])
        items: list[dict[str, Any]] = []
        bindings: dict[str, str] = {}
        energy_ids: dict[str, str] = {}
        seen: set[str] = set()
        for port in descriptor["ports"]:
            pid = str(port["id"])
            exported_port = next(row for row in export["ports"] if row["id"] == port["id"])
            for role in port["capabilities"]:
                entity = registry.async_get(mapping[pid][role])
                _require(entity is not None, f"Missing {pid}/{role} entity")
                assert entity is not None
                _require(
                    entity.id not in seen
                    and entity.platform == "mqtt"
                    and entity.config_entry_id == mqtt_entry.entry_id
                    and entity.config_subentry_id is None
                    and entity.device_id == parent.id
                    and entity.domain == ("switch" if role == "relay" else "sensor")
                    and entity.unique_id == exported_port["roles"][role]["unique_id"],
                    f"Export/registry mismatch for physical port {pid}/{role}",
                )
                native_uid = f"{device_id}_port_{pid}_{role}"
                _require(
                    registry.async_get_entity_id(entity.domain, DOMAIN, native_uid) is None,
                    "Native entity unique ID already exists",
                )
                seen.add(entity.id)
                items.append(
                    {
                        "port_id": pid,
                        "role": role,
                        "before": _entity_snapshot(entity),
                        "native_unique_id": native_uid,
                        "transferred": False,
                    }
                )
                if role == "power":
                    binding = next(
                        (p for p in checkpoint.ports if p.registry_id == entity.id), None
                    )
                    _require(
                        binding is not None
                        and binding.unique_id == entity.unique_id
                        and binding.entity_id == entity.entity_id,
                        "Every physical power port needs a current companion binding",
                    )
                    assert binding is not None
                    bindings[pid] = binding.binding_id
                    energy_uid = f"{entry.entry_id}_{binding.binding_id}_energy"
                    energy_id = registry.async_get_entity_id("sensor", DOMAIN, energy_uid)
                    if energy_id is not None:
                        energy = registry.async_get(energy_id)
                        assert energy is not None
                        _require(
                            energy.config_entry_id == entry.entry_id
                            and energy.config_subentry_id is None,
                            "Energy unique ID belongs to another entry",
                        )
                        energy_ids[pid] = energy.unique_id
                        seen.add(energy.id)
                        items.append(
                            {
                                "port_id": pid,
                                "role": "energy",
                                "before": _entity_snapshot(energy),
                                "native_unique_id": energy.unique_id,
                                "transferred": False,
                            }
                        )
                    else:
                        _require(
                            binding.excluded, "Expected companion energy registry entry is missing"
                        )
        _require(
            set(bindings.values()) == {p.binding_id for p in checkpoint.ports},
            "Checkpoint contains unaccounted physical bindings",
        )
        raw_ids = {item["before"]["id"] for item in items if item["role"] != "energy"}
        _require(
            raw_ids
            == {
                e.id
                for e in er.async_entries_for_device(
                    registry, parent.id, include_disabled_entities=True
                )
            },
            "Parent has unexpected entities, including disabled entities",
        )
        energy_items = [item for item in items if item["role"] == "energy"]
        _require(
            {item["before"]["id"] for item in energy_items}
            == {e.id for e in er.async_entries_for_config_entry(registry, entry.entry_id)},
            "Companion has unexpected entities",
        )
        companion = devices.async_get_device_by_identifier((DOMAIN, entry.entry_id), entry.entry_id)
        if companion is not None:
            _require(
                isinstance(companion, dr.DeviceEntry)
                and companion.config_entry_id == entry.entry_id
                and companion.config_subentry_id is None
                and companion.id != parent.id,
                "Companion has unexpected device ownership",
            )
            _require(
                not dr.async_entries_for_parent_device(devices, companion.id),
                "Companion device has children",
            )
            _require(
                not companion.has_composite_identifiers and companion.composite_device_id is None,
                "Composite companion migration must be resolved first",
            )
            _require(
                {item["before"]["id"] for item in energy_items}
                == {
                    e.id
                    for e in er.async_entries_for_device(
                        registry, companion.id, include_disabled_entities=True
                    )
                },
                "Companion device membership does not match energy inventory",
            )
            _require(
                all(item["before"]["device_id"] == companion.id for item in energy_items),
                "Energy entity attached to an unexpected device",
            )
        else:
            _require(not energy_items, "Companion device is missing")
        allowed_data = {
            "mode",
            CONF_SOURCE_DEVICE,
            CONF_SOURCE_CONFIG_ENTRY,
            CONF_STORAGE_ID,
            "freshness_confirmed",
        }
        _require(
            set(entry.data) <= allowed_data,
            "Unsupported companion config data; review before migration",
        )
        journal = {
            "version": 1,
            "storage_id": storage_id,
            "phase": "prepared",
            "intent": None,
            "intents": [],
            "entry": {
                "entry_id": entry.entry_id,
                "data": _json_copy(dict(entry.data)),
                "options": _json_copy(dict(entry.options)),
                "version": entry.version,
                "minor_version": entry.minor_version,
                "unique_id": entry.unique_id,
            },
            "mqtt_entry_id": mqtt_entry.entry_id,
            "mqtt_disabled_by": mqtt_entry.disabled_by,
            "parent": _device_snapshot(parent),
            "companion": _device_snapshot(companion) if companion is not None else None,
            "descriptor": descriptor,
            "entities": items,
            "children": {},
            "original_checkpoint": checkpoint.as_dict(),
            "checkpoint": checkpoint.as_dict(),
            "port_bindings": bindings,
            "energy_unique_ids": energy_ids,
            "discovery_topics": topics,
            "migration_map": _json_copy(export),
        }
        await store.async_save(journal)
        return journal

    def _validate_export(
        self,
        descriptor: dict[str, Any],
        export: dict[str, Any],
        mapping: dict[str, dict[str, str]],
        prefix: str,
    ) -> list[str]:
        _require(_TOPIC_PART.fullmatch(prefix), "Discovery prefix must be one concrete MQTT level")
        _require(
            type(export.get("schema_version")) is int
            and export["schema_version"] == 1
            and export.get("device_id") == descriptor["device_id"],
            "Migration map device ID/schema does not match descriptor",
        )
        ports = export.get("ports")
        if not isinstance(ports, list) or not all(isinstance(p, dict) for p in ports):
            raise MigrationError("Invalid migration map ports")
        ids = [port.get("id") for port in ports]
        _require(
            all(type(pid) is int for pid in ids)
            and len(ids) == len(set(ids))
            and set(ids) == {p["id"] for p in descriptor["ports"]}
            and set(mapping) == {str(pid) for pid in ids},
            "Physical port inventory must match exactly",
        )
        topics: list[str] = []
        for port in descriptor["ports"]:
            row = next(p for p in ports if p["id"] == port["id"])
            roles = _object(row.get("roles"), "Invalid migration map roles")
            _require(
                set(roles) == set(port["capabilities"])
                and set(mapping[str(port["id"])]) == set(roles),
                "Physical port role inventory must match exactly",
            )
            for role, record in roles.items():
                _require(
                    isinstance(record, dict)
                    and isinstance(record.get("unique_id"), str)
                    and record["unique_id"],
                    "Exported unique ID is missing",
                )
                topic = record.get("discovery_topic")
                self._discovery_topic(topic, prefix, "switch" if role == "relay" else "sensor")
                topics.append(topic)
                for key in (
                    ("state_topic", "command_topic") if role == "relay" else ("state_topic",)
                ):
                    value = record.get(key)
                    _require(
                        isinstance(value, str)
                        and value
                        and all(_TOPIC_PART.fullmatch(part) for part in value.split("/")),
                        f"Exported {key} must be a concrete MQTT topic",
                    )
        metadata = export.get("metadata_discovery_topics", [])
        _require(isinstance(metadata, list), "Invalid metadata discovery topics")
        for topic in metadata:
            self._discovery_topic(topic, prefix, "sensor")
            topics.append(topic)
        _require(len(topics) == len(set(topics)), "Duplicate discovery topics in migration export")
        return sorted(topics)

    def _discovery_topic(self, topic: Any, prefix: str, component: str) -> None:
        _require(isinstance(topic, str), "Discovery topic must be a string")
        parts = topic.split("/")
        _require(
            len(parts) in (4, 5)
            and parts[0] == prefix
            and parts[1] == component
            and parts[-1] == "config"
            and all(_TOPIC_PART.fullmatch(part) for part in parts),
            "Only exact allowlisted legacy discovery config topics are supported",
        )

    async def _quiesce(self, journal: dict[str, Any]) -> None:
        _require(
            journal["phase"] in ("prepared", "quiescing", "quiesced"),
            "Quiesce is only valid before ownership transfer",
        )
        await self._phase(journal, "quiescing")
        self._entry_unchanged(journal)
        entry = self._entry(journal["entry"]["entry_id"], DOMAIN)
        if entry.state is ConfigEntryState.LOADED:
            _require(
                await self.hass.config_entries.async_unload(entry.entry_id),
                "Companion unload/checkpoint failed; MQTT has not been disabled",
            )
        _require(entry.state is ConfigEntryState.NOT_LOADED, "Companion is not safely unloaded")
        latest = await CheckpointStore(self.hass, journal["storage_id"]).async_load()
        self._checkpoint_mapping(journal, latest)
        await self._record(journal, checkpoint=latest.as_dict(), intent="disable_mqtt")
        _require(
            await self.hass.config_entries.async_set_disabled_by(
                journal["mqtt_entry_id"], ConfigEntryDisabler.USER
            ),
            "MQTT unload failed; maintenance is incomplete",
        )
        await self._guard(journal)
        await self._phase(journal, "quiesced")

    def _checkpoint_mapping(self, journal: dict[str, Any], checkpoint: Checkpoint) -> None:
        before = Checkpoint.from_dict(journal["checkpoint"], journal["storage_id"])
        _require(
            checkpoint.storage_id == before.storage_id
            and checkpoint.generation >= before.generation
            and len(checkpoint.ports) == len(before.ports)
            and checkpoint.ignored_sources == before.ignored_sources,
            "Checkpoint binding inventory changed",
        )
        previous = {port.binding_id: port for port in before.ports}
        for port in checkpoint.ports:
            old = previous.get(port.binding_id)
            _require(
                old is not None
                and port.registry_id == old.registry_id
                and port.unique_id == old.unique_id
                and port.entity_id == old.entity_id
                and port.excluded == old.excluded
                and port.total >= old.total,
                "Checkpoint binding changed or energy total regressed",
            )

    async def _apply(self, journal: dict[str, Any], data: Mapping[str, Any]) -> None:
        _require(
            data.get("broker_clean") is True
            and data.get("publisher_native") is True
            and data.get("backup_confirmed") is True
            and data.get("references_reviewed") is True,
            "Confirm backup, reference/helper audit, native publisher and exact broker cleanup",
        )
        _require(
            journal["phase"] in _PHASES[2:9], "Apply requires a quiesced, non-active migration"
        )
        await self._guard(journal)
        self._entry_unchanged(
            journal,
            native=(self._entry(journal["entry"]["entry_id"], DOMAIN).data.get("mode") == "native"),
        )
        await self._phase(journal, "broker_clean")
        entry = self._entry(journal["entry"]["entry_id"], DOMAIN)
        _require(
            entry.data.get("mode") != "native" or "native_entry" in journal["intents"],
            "Entry changed to native without migration intent",
        )
        checkpoint = await CheckpointStore(self.hass, journal["storage_id"]).async_load()
        self._checkpoint_mapping(journal, checkpoint)
        await self._record(journal, checkpoint=checkpoint.as_dict())
        registry = er.async_get(self.hass)
        devices = dr.async_get(self.hass)
        for item in journal["entities"]:
            self._user_metadata(item)
            if item["role"] == "energy":
                energy = self._entity(item)
                _require(
                    energy.platform == DOMAIN
                    and energy.config_entry_id == entry.entry_id
                    and energy.unique_id == item["before"]["unique_id"]
                    and energy.device_id
                    in (
                        None,
                        item["before"]["device_id"],
                        journal["children"].get(item["port_id"]),
                    ),
                    "Energy ownership changed",
                )
                if energy.device_id == item["before"]["device_id"]:
                    await self._record(journal, intent=f"detach:{energy.id}")
                    registry.async_update_entity(energy.entity_id, device_id=None)
                    await self._record(journal, intent=None)
                continue
            entity = self._entity(item)
            before = item["before"]
            if entity.platform == "mqtt":
                _require(
                    entity.unique_id == before["unique_id"]
                    and entity.config_entry_id == journal["mqtt_entry_id"]
                    and entity.config_subentry_id is None
                    and entity.device_id == journal["parent"]["id"],
                    "Legacy source ownership changed",
                )
                self._device_unchanged(journal["parent"], maintenance=True)
                await self._record(journal, intent=f"transfer:{entity.id}")
                registry.async_update_entity_platform(
                    entity.entity_id,
                    DOMAIN,
                    new_config_entry_id=entry.entry_id,
                    new_unique_id=item["native_unique_id"],
                    new_device_id=None,
                )
            else:
                _require(
                    entity.platform == DOMAIN
                    and entity.config_entry_id == entry.entry_id
                    and entity.unique_id == item["native_unique_id"]
                    and entity.config_subentry_id is None
                    and entity.device_id in (None, journal["children"].get(item["port_id"]))
                    and f"transfer:{entity.id}" in journal["intents"],
                    "Transferred source does not match the journal",
                )
            self._restore_disabled(item)
            item["transferred"] = True
            await self._record(journal, intent=None)
        await self._registries_persisted(journal)
        await self._phase(journal, "entities_transferred")
        parent = devices.async_get(journal["parent"]["id"])
        _require(isinstance(parent, dr.DeviceEntry), "Parent disappeared")
        assert isinstance(parent, dr.DeviceEntry)
        native_identifiers = {(DOMAIN, journal["descriptor"]["device_id"])}
        if parent.config_entry_id == journal["mqtt_entry_id"]:
            self._device_unchanged(journal["parent"], maintenance=True)
            self._empty(parent.id)
            _require(
                not _identifier_present(self.hass, (DOMAIN, journal["descriptor"]["device_id"])),
                "Native parent identifier appeared after inventory",
            )
            await self._record(journal, intent="transfer_parent")
            devices.async_update_device(
                parent.id,
                new_config_entry_id=entry.entry_id,
                new_config_subentry_id=None,
                new_identifiers=native_identifiers,
                new_connections=set(),
                disabled_by=dr.DeviceEntryDisabler(journal["parent"]["disabled_by"])
                if journal["parent"]["disabled_by"]
                else None,
            )
        else:
            _require(
                parent.config_entry_id == entry.entry_id
                and parent.config_subentry_id is None
                and parent.identifiers == native_identifiers
                and "transfer_parent" in journal["intents"],
                "Parent ownership/identity changed",
            )
        await self._registries_persisted(journal)
        await self._phase(journal, "parent_transferred")
        for port in journal["descriptor"]["ports"]:
            pid = str(port["id"])
            identifiers = {(DOMAIN, f"{journal['descriptor']['device_id']}_port_{pid}")}
            child = devices.async_get_child_device_by_identifier(
                (DOMAIN, f"{journal['descriptor']['device_id']}_port_{pid}"), entry.entry_id
            )
            if child is None:
                _require(
                    pid not in journal["children"], "Previously registered native child disappeared"
                )
                _require(
                    not _identifier_present(self.hass, next(iter(identifiers))),
                    "Native child identifier is owned by another device",
                )
                await self._record(journal, intent=f"child:{pid}")
                child = devices.async_get_or_create_child(
                    config_entry_id=entry.entry_id,
                    config_subentry_id=None,
                    identifiers=identifiers,
                    parent_device_id=parent.id,
                    name=port["name"],
                )
            else:
                _require(
                    isinstance(child, dr.ChildDeviceEntry)
                    and child.config_entry_id == entry.entry_id
                    and child.config_subentry_id is None
                    and child.parent_device_id == parent.id
                    and child.identifiers == identifiers
                    and (
                        journal["children"].get(pid) == child.id
                        or f"child:{pid}" in journal["intents"]
                    ),
                    "Unexpected native child; no devices were adopted",
                )
            journal["children"][pid] = child.id
            await self._record(journal, intent=None)
        for item in journal["entities"]:
            self._user_metadata(item)
            entity = self._entity(item)
            target = journal["children"][item["port_id"]]
            _require(
                entity.platform == DOMAIN
                and entity.config_entry_id == entry.entry_id
                and entity.unique_id == item["native_unique_id"]
                and entity.device_id in (None, item["before"]["device_id"], target),
                "Cannot attach entity with unexpected ownership",
            )
            await self._record(journal, intent=f"attach:{entity.id}")
            registry.async_update_entity(entity.entity_id, device_id=target)
            self._restore_disabled(item)
            await self._record(journal, intent=None)
        checkpoint = await CheckpointStore(self.hass, journal["storage_id"]).async_load()
        self._checkpoint_mapping(journal, checkpoint)
        await self._record(journal, checkpoint=checkpoint.as_dict())
        await self._registries_persisted(journal)
        await self._phase(journal, "children_attached")
        native_data = self._native_data(journal)
        _require(
            dict(entry.data) in (journal["entry"]["data"], native_data),
            "Config entry changed during migration",
        )
        await self._record(journal, intent="native_entry")
        self.hass.config_entries.async_update_entry(
            entry,
            data=native_data,
            unique_id=journal["descriptor"]["device_id"],
            version=2,
        )
        await self._entry_persisted(entry)
        await self._record(journal, intent=None)
        self._native_invariants(journal)
        companion = journal["companion"]
        if companion is not None:
            if devices.async_get(companion["id"]) is not None:
                self._device_unchanged(companion)
                self._empty(companion["id"])
                await self._phase(journal, "companion_retirement_pending")
                await self._record(journal, intent="retire_companion")
                devices.async_remove_device(companion["id"])
            else:
                _require(
                    journal["phase"] in ("companion_retirement_pending", "companion_retired")
                    and "retire_companion" in journal["intents"],
                    "Old companion is absent without retirement intent",
                )
            await self._registries_persisted(journal)
        await self._phase(journal, "companion_retired")

    def _native_invariants(self, journal: dict[str, Any]) -> None:
        self._entry_unchanged(journal, native=True)
        entry_id = journal["entry"]["entry_id"]
        devices = dr.async_get(self.hass)
        parent = devices.async_get(journal["parent"]["id"])
        _require(
            isinstance(parent, dr.DeviceEntry)
            and parent.config_entry_id == entry_id
            and parent.config_subentry_id is None
            and parent.identifiers == {(DOMAIN, journal["descriptor"]["device_id"])},
            "Native parent invariant failed",
        )
        assert isinstance(parent, dr.DeviceEntry)
        _require(
            all(
                _device_snapshot(parent)[field] == journal["parent"][field]
                for field in _DEVICE_USER_FIELDS
            ),
            "Parent user metadata changed",
        )
        _require(
            set(journal["children"])
            == {str(port["id"]) for port in journal["descriptor"]["ports"]},
            "Native child mapping is incomplete",
        )
        _require(
            {child.id for child in dr.async_entries_for_parent_device(devices, parent.id)}
            == set(journal["children"].values()),
            "Native child inventory changed",
        )
        for pid, child_id in journal["children"].items():
            child = devices.async_get(child_id)
            _require(
                isinstance(child, dr.ChildDeviceEntry)
                and child.config_entry_id == entry_id
                and child.config_subentry_id is None
                and child.parent_device_id == parent.id
                and child.identifiers
                == {(DOMAIN, f"{journal['descriptor']['device_id']}_port_{pid}")},
                "Native child ownership/identity invariant failed",
            )
        for item in journal["entities"]:
            entity = self._entity(item)
            _require(
                entity.platform == DOMAIN
                and entity.config_entry_id == entry_id
                and entity.config_subentry_id is None
                and entity.unique_id == item["native_unique_id"]
                and entity.device_id == journal["children"][item["port_id"]],
                "Native entity identity/ownership invariant failed",
            )
            self._user_metadata(item)
        _require(
            {
                entity.id
                for entity in er.async_entries_for_config_entry(er.async_get(self.hass), entry_id)
            }
            == {item["before"]["id"] for item in journal["entities"]},
            "Native entry contains unexpected entities",
        )

    async def _activate(self, journal: dict[str, Any], *, native: bool) -> None:
        entry = self._entry(journal["entry"]["entry_id"], DOMAIN)
        mqtt_entry = self._entry(journal["mqtt_entry_id"], "mqtt")
        previous = journal["mqtt_disabled_by"]
        await self._record(journal, intent="restore_mqtt")
        _require(
            await self.hass.config_entries.async_set_disabled_by(
                mqtt_entry.entry_id, ConfigEntryDisabler(previous) if previous else None
            ),
            "MQTT did not recover; maintenance is not complete",
        )
        await self._entry_persisted(mqtt_entry)
        if previous is None and mqtt_entry.state is not ConfigEntryState.LOADED:
            _require(not mqtt_entry.setup_lock.locked(), "MQTT recovery is already in progress")
            _require(
                await self.hass.config_entries.async_reload(mqtt_entry.entry_id),
                "MQTT recovery retry failed; maintenance is not complete",
            )
        _require(
            mqtt_entry.state is ConfigEntryState.LOADED,
            "MQTT is not loaded after restoration",
        )
        activating = self.hass.data.setdefault(_ACTIVATING, set())
        activating.add(entry.entry_id)
        try:
            if entry.state is not ConfigEntryState.LOADED:
                _require(not entry.setup_lock.locked(), "Entry recovery is already in progress")
                _require(
                    await self.hass.config_entries.async_reload(entry.entry_id),
                    "Entry activation failed; journal remains unfinished",
                )
            _require(entry.state is ConfigEntryState.LOADED, "Entry activation is not complete")
        finally:
            activating.remove(entry.entry_id)
        if native:
            self._native_invariants(journal)
        else:
            self._legacy_invariants(journal)
        checkpoint = await CheckpointStore(self.hass, journal["storage_id"]).async_load()
        self._checkpoint_mapping(journal, checkpoint)
        await self._record(journal, phase="active" if native else "rolled_back", intent=None)

    async def _finish(self, journal: dict[str, Any], data: Mapping[str, Any]) -> None:
        _require(
            data.get("broker_absent") is True and data.get("publisher_native") is True,
            "Reconfirm legacy discovery absence and exclusive native publisher before resuming",
        )
        _require(
            journal["phase"] in ("companion_retired", "activating", "active"),
            "Finish requires completed ownership transfer",
        )
        self._native_invariants(journal)
        if journal["phase"] == "active":
            return
        if journal["phase"] == "companion_retired":
            await self._guard(journal)
        await self._registries_persisted(journal)
        await self._entry_persisted(self._entry(journal["entry"]["entry_id"], DOMAIN))
        companion = journal["companion"]
        _require(
            companion is None or dr.async_get(self.hass).async_get(companion["id"]) is None,
            "Old companion has not been retired",
        )
        await self._phase(journal, "activating")
        await self._activate(journal, native=True)

    def _legacy_invariants(self, journal: dict[str, Any]) -> None:
        self._entry_unchanged(journal)
        self._device_unchanged(journal["parent"], maintenance=True)
        if journal["companion"] is not None:
            self._device_unchanged(journal["companion"])
        devices = dr.async_get(self.hass)
        _require(
            not dr.async_entries_for_parent_device(devices, journal["parent"]["id"])
            and all(devices.async_get(cid) is None for cid in journal["children"].values()),
            "Native children remain during rollback",
        )
        for item in journal["entities"]:
            current = _entity_snapshot(self._entity(item))
            before = item["before"]
            _require(
                all(
                    current[field] == before[field]
                    for field in (
                        "platform",
                        "unique_id",
                        "config_entry_id",
                        "config_subentry_id",
                        "device_id",
                    )
                ),
                "Legacy entity ownership was not restored",
            )
            self._user_metadata(item)

    async def _rollback(self, journal: dict[str, Any], data: Mapping[str, Any]) -> None:
        _require(
            data.get("publisher_stopped") is True,
            "Stop both legacy and native publishers before rollback",
        )
        if journal["phase"] == "rolled_back":
            self._legacy_invariants(journal)
            return
        if journal["phase"] == "rollback_ready":
            _require(
                data.get("legacy_restored") is True,
                "Restore the exact exported legacy discovery and legacy-only publisher, "
                "then confirm",
            )
            self._legacy_invariants(journal)
            await self._registries_persisted(journal)
            await self._entry_persisted(self._entry(journal["entry"]["entry_id"], DOMAIN))
            await self._activate(journal, native=False)
            return
        await self._record(journal, phase="rolling_back", intent="quiesce_rollback")
        entry = self._entry(journal["entry"]["entry_id"], DOMAIN)
        if entry.state is ConfigEntryState.LOADED:
            _require(
                await self.hass.config_entries.async_unload(entry.entry_id),
                "Native/companion unload failed; rollback stopped",
            )
        _require(
            await self.hass.config_entries.async_set_disabled_by(
                journal["mqtt_entry_id"], ConfigEntryDisabler.USER
            ),
            "MQTT unload failed; rollback stopped",
        )
        await self._guard(journal)
        latest = await CheckpointStore(self.hass, journal["storage_id"]).async_load()
        self._checkpoint_mapping(journal, latest)
        await self._record(journal, checkpoint=latest.as_dict(), intent=None)
        registry = er.async_get(self.hass)
        devices = dr.async_get(self.hass)
        for port in journal["descriptor"]["ports"]:
            pid = str(port["id"])
            found_child = devices.async_get_child_device_by_identifier(
                (DOMAIN, f"{journal['descriptor']['device_id']}_port_{pid}"), entry.entry_id
            )
            if found_child is not None and pid not in journal["children"]:
                _require(f"child:{pid}" in journal["intents"], "Unjournaled native child")
                journal["children"][pid] = found_child.id
                await self._record(journal)
        for item in journal["entities"]:
            self._user_metadata(item)
            entity = self._entity(item)
            before = item["before"]
            _require(
                (
                    entity.platform == DOMAIN
                    and entity.config_entry_id == entry.entry_id
                    and entity.unique_id == item["native_unique_id"]
                )
                or (
                    entity.platform == before["platform"]
                    and entity.config_entry_id == before["config_entry_id"]
                    and entity.unique_id == before["unique_id"]
                ),
                "Rollback entity ownership changed",
            )
            _require(
                entity.device_id
                in (None, before["device_id"], journal["children"].get(item["port_id"])),
                "Rollback entity is attached to an unexpected device",
            )
            await self._record(journal, intent=f"detach:{entity.id}")
            registry.async_update_entity(entity.entity_id, device_id=None)
        await self._registries_persisted(journal)
        for pid, child_id in journal["children"].items():
            child = devices.async_get(child_id)
            if child is not None:
                _require(
                    isinstance(child, dr.ChildDeviceEntry)
                    and child.config_entry_id == entry.entry_id
                    and child.parent_device_id == journal["parent"]["id"]
                    and child.identifiers
                    == {(DOMAIN, f"{journal['descriptor']['device_id']}_port_{pid}")},
                    "Rollback child ownership changed",
                )
                self._empty(child_id)
                await self._record(journal, intent=f"remove_child:{pid}")
                devices.async_remove_device(child_id)
            else:
                _require(
                    journal.get("removed_children", {}).get(pid)
                    or f"remove_child:{pid}" in journal["intents"],
                    "Native child disappeared without rollback intent",
                )
            journal.setdefault("removed_children", {})[pid] = True
            await self._record(journal, intent=None)
        await self._registries_persisted(journal)
        parent = devices.async_get(journal["parent"]["id"])
        _require(isinstance(parent, dr.DeviceEntry), "Rollback parent is missing")
        assert isinstance(parent, dr.DeviceEntry)
        before_parent = journal["parent"]
        _require(
            parent.config_entry_id in (entry.entry_id, journal["mqtt_entry_id"]),
            "Rollback parent ownership changed",
        )
        _require(
            parent.config_subentry_id is None
            and all(
                _device_snapshot(parent)[field] == before_parent[field]
                for field in _DEVICE_USER_FIELDS
            )
            and (
                parent.config_entry_id == journal["mqtt_entry_id"]
                and parent.identifiers == {tuple(pair) for pair in before_parent["identifiers"]}
                or parent.config_entry_id == entry.entry_id
                and "transfer_parent" in journal["intents"]
                and parent.identifiers == {(DOMAIN, journal["descriptor"]["device_id"])}
            ),
            "Rollback parent identity or user metadata changed",
        )
        self._empty(parent.id)
        await self._record(journal, intent="restore_parent")
        devices.async_update_device(
            parent.id,
            new_config_entry_id=journal["mqtt_entry_id"],
            new_config_subentry_id=None,
            new_identifiers={tuple(pair) for pair in before_parent["identifiers"]},
            new_connections={tuple(pair) for pair in before_parent["connections"]},
        )
        self._restore_device_metadata(before_parent)
        companion = journal["companion"]
        if companion is not None:
            restored = devices.async_get(companion["id"])
            if restored is None:
                _require(
                    "retire_companion" in journal["intents"],
                    "Companion disappeared without retirement intent",
                )
                _require(
                    not _identifier_present(self.hass, (DOMAIN, entry.entry_id)),
                    "A different companion already exists",
                )
                await self._record(journal, intent="restore_companion")
                restored = devices.async_get_or_create(
                    config_entry_id=entry.entry_id,
                    config_subentry_id=None,
                    identifiers={tuple(pair) for pair in companion["identifiers"]},
                    connections={tuple(pair) for pair in companion["connections"]},
                    name=companion["name"],
                    manufacturer=companion["manufacturer"],
                    model=companion["model"],
                )
                _require(
                    restored.id == companion["id"],
                    "Original companion device ID cannot be restored; manual repair required",
                )
            if "restore_companion" in journal["intents"]:
                _require(
                    isinstance(restored, dr.DeviceEntry)
                    and restored.config_entry_id == entry.entry_id
                    and all(
                        _device_snapshot(restored)[field] == companion[field]
                        for field in _DEVICE_USER_FIELDS
                    ),
                    "Restored companion ownership/user metadata changed",
                )
                self._restore_device_metadata(companion)
            self._device_unchanged(companion)
        await self._registries_persisted(journal)
        for item in journal["entities"]:
            entity = self._entity(item)
            before = item["before"]
            await self._record(journal, intent=f"restore_entity:{entity.id}")
            if item["role"] != "energy" and entity.platform != "mqtt":
                registry.async_update_entity_platform(
                    entity.entity_id,
                    "mqtt",
                    new_config_entry_id=journal["mqtt_entry_id"],
                    new_unique_id=before["unique_id"],
                    new_device_id=None,
                )
            registry.async_update_entity(entity.entity_id, device_id=before["device_id"])
            self._restore_disabled(item)
        await self._registries_persisted(journal)
        await self._record(journal, intent="restore_entry")
        self.hass.config_entries.async_update_entry(
            entry,
            data=journal["entry"]["data"],
            unique_id=journal["entry"]["unique_id"],
            version=journal["entry"]["version"],
            minor_version=journal["entry"]["minor_version"],
        )
        await self._entry_persisted(entry)
        self._legacy_invariants(journal)
        await self._record(journal, phase="rollback_ready", intent=None)


def _approved(value: Any) -> bool:
    if value is not True:
        raise vol.Invalid("must be explicitly true")
    return True


async def async_setup(hass: HomeAssistant) -> None:
    """Register domain services once; called from the integration's root setup."""
    if _COORDINATOR in hass.data:
        return
    coordinator = hass.data[_COORDINATOR] = MigrationCoordinator(hass)
    common: dict[Any, Any] = {vol.Required("journal_id"): str, vol.Required("approve"): _approved}
    schemas = {
        "prepare": vol.Schema(
            {
                vol.Required("entry_id"): str,
                vol.Required("descriptor"): dict,
                vol.Required("migration_map"): dict,
                vol.Required("mapping"): {str: {str: str}},
                vol.Optional("discovery_prefix", default="homeassistant"): str,
            }
        ),
        "quiesce": vol.Schema(common),
        "apply": vol.Schema(
            {
                **common,
                vol.Required("broker_clean"): _approved,
                vol.Required("publisher_native"): _approved,
                vol.Required("backup_confirmed"): _approved,
                vol.Required("references_reviewed"): _approved,
            }
        ),
        "finish": vol.Schema(
            {
                **common,
                vol.Required("broker_absent"): _approved,
                vol.Required("publisher_native"): _approved,
            }
        ),
        "rollback": vol.Schema(
            {
                **common,
                vol.Required("publisher_stopped"): _approved,
                vol.Optional("legacy_restored", default=False): bool,
            }
        ),
    }

    async def handle(action: str, call: ServiceCall) -> dict[str, Any]:
        return await coordinator.async_execute(action, call.data)

    for action, schema in schemas.items():
        async_register_admin_service(
            hass,
            DOMAIN,
            f"migration_{action}",
            partial(handle, action),
            schema,
            supports_response=SupportsResponse.ONLY,
        )
