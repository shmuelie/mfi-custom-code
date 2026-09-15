"""Non-secret operational diagnostics for the companion."""

from homeassistant.core import HomeAssistant

from . import MfiConfigEntry


async def async_get_config_entry_diagnostics(
    hass: HomeAssistant, entry: MfiConfigEntry
) -> dict[str, object]:
    manager = entry.runtime_data
    return {
        "generation": manager.committed.generation,
        "storage_error": manager.storage_error,
        "ambiguous_sources": manager.ambiguous,
        "ports": [
            {
                "source": port.binding.entity_id,
                "status": port.reason or "tracking",
                "source_status": port.source_reason or "available",
                "excluded": port.binding.excluded,
                "committed_kwh": str(
                    next(
                        (
                            item.total
                            for item in manager.committed.ports
                            if item.binding_id == binding_id
                        ),
                        None,
                    )
                ),
            }
            for binding_id, port in manager.ports.items()
        ],
    }
