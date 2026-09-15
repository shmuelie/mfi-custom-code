"""mFi energy companion for existing Home Assistant MQTT entities."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryError

from .const import CONF_STORAGE_ID
from .repairs import set_issue
from .source import SourceManager
from .storage import CheckpointStore, StorageError

type MfiConfigEntry = ConfigEntry[SourceManager]


async def async_setup_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> bool:
    """Restore our counters without creating or migrating MQTT entities."""
    try:
        store = CheckpointStore(hass, entry.data[CONF_STORAGE_ID])
        checkpoint = await store.async_load()
    except StorageError as error:
        set_issue(hass, entry.entry_id, "storage", str(error))
        raise ConfigEntryError(str(error)) from error
    set_issue(hass, entry.entry_id, "storage", None)
    entry.runtime_data = SourceManager(hass, entry, store, checkpoint)
    await hass.config_entries.async_forward_entry_setups(entry, [Platform.SENSOR])
    await entry.runtime_data.async_start()
    return True


async def async_unload_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> bool:
    """Flush and detach only the companion's entities and listeners."""
    manager = entry.runtime_data
    await manager.async_stop()
    if manager.storage_error is not None:
        await manager.async_start()
        return False
    unloaded = await hass.config_entries.async_unload_platforms(entry, [Platform.SENSOR])
    if not unloaded:
        await manager.async_start()
    return unloaded


async def async_remove_entry(hass: HomeAssistant, entry: MfiConfigEntry) -> None:
    """Remove this entry's checkpoint, never MQTT resources or Recorder history."""
    await CheckpointStore(hass, entry.data[CONF_STORAGE_ID]).async_remove()
    for kind in ("sources", "storage"):
        set_issue(hass, entry.entry_id, kind, None)
