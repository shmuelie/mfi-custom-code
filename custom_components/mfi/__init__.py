"""mFi energy companion for existing Home Assistant MQTT entities."""

import json

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryError
from homeassistant.helpers.typing import ConfigType

from . import migration
from .const import CONF_DESCRIPTOR, CONF_MODE, CONF_STORAGE_ID, MODE_NATIVE
from .mqtt import NativeRuntime
from .protocol import parse_descriptor
from .repairs import set_issue
from .source import SourceManager
from .storage import CheckpointStore, StorageError

type MfiConfigEntry = ConfigEntry[SourceManager]


async def async_setup(hass: HomeAssistant, config: ConfigType) -> bool:
    """Register opt-in administrative actions without performing a migration."""
    await migration.async_setup(hass)
    return True


async def async_setup_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> bool:
    """Restore our counters without creating or migrating MQTT entities."""
    await migration.async_assert_ready(hass, entry)
    try:
        store = CheckpointStore(hass, entry.data[CONF_STORAGE_ID])
        checkpoint = await store.async_load()
    except StorageError as error:
        set_issue(hass, entry.entry_id, "storage", str(error))
        raise ConfigEntryError(str(error)) from error
    set_issue(hass, entry.entry_id, "storage", None)
    if entry.data.get(CONF_MODE) == MODE_NATIVE:
        try:
            descriptor = parse_descriptor(json.dumps(entry.data[CONF_DESCRIPTOR]))
            entry.runtime_data = NativeRuntime(hass, entry, store, checkpoint, descriptor)
        except (ValueError, KeyError) as error:
            raise ConfigEntryError(f"Invalid native configuration: {error}") from error
        platforms = [Platform.SENSOR, Platform.SWITCH]
    else:
        entry.runtime_data = SourceManager(hass, entry, store, checkpoint)
        platforms = [Platform.SENSOR]
    loaded = False
    try:
        if isinstance(entry.runtime_data, NativeRuntime):
            await entry.runtime_data.async_start()
        await hass.config_entries.async_forward_entry_setups(entry, platforms)
        if not isinstance(entry.runtime_data, NativeRuntime):
            await entry.runtime_data.async_start()
        loaded = True
    finally:
        if not loaded:
            await entry.runtime_data.async_stop()
            await hass.config_entries.async_unload_platforms(entry, platforms)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> bool:
    """Flush and detach only the companion's entities and listeners."""
    manager = entry.runtime_data
    await manager.async_stop()
    if manager.storage_error is not None:
        await manager.async_start()
        return False
    platforms = (
        [Platform.SENSOR, Platform.SWITCH]
        if isinstance(manager, NativeRuntime)
        else [Platform.SENSOR]
    )
    unloaded = await hass.config_entries.async_unload_platforms(entry, platforms)
    if not unloaded:
        await manager.async_start()
    return unloaded


async def async_remove_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> None:
    """Remove this entry's checkpoint, never MQTT resources or Recorder history."""
    journal = await migration.JournalStore(hass, entry.data[CONF_STORAGE_ID]).async_load()
    if journal is not None and journal["phase"] not in ("prepared", "active", "rolled_back"):
        raise ConfigEntryError(
            "Pending migration data was retained. Restore the removed config entry "
            "from its consistent backup before migration recovery."
        )
    await CheckpointStore(hass, entry.data[CONF_STORAGE_ID]).async_remove()
    for kind in ("sources", "storage", "native"):
        set_issue(hass, entry.entry_id, kind, None)


async def async_migrate_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> bool:
    """A version upgrade never transfers MQTT ownership."""
    if entry.version == 1:
        hass.config_entries.async_update_entry(
            entry, version=2, data={CONF_MODE: "companion", **entry.data}
        )
    return entry.version == 2
