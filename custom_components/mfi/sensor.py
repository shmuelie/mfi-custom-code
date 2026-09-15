"""Energy entities backed exclusively by committed checkpoints."""

from decimal import Decimal
from typing import TYPE_CHECKING

from homeassistant.components.sensor import SensorDeviceClass, SensorEntity, SensorStateClass
from homeassistant.const import UnitOfEnergy
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN
from .source import SourceManager

if TYPE_CHECKING:
    from . import MfiConfigEntry


async def async_setup_entry(
    hass: HomeAssistant, entry: MfiConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    manager = entry.runtime_data
    added: set[str] = set()

    @callback
    def add_committed_ports() -> None:
        entities = []
        for port in manager.committed.ports:
            source = er.async_get(hass).async_get(port.registry_id)
            if (
                port.binding_id not in added
                and not port.excluded
                and not (source is not None and source.disabled)
            ):
                entities.append(MfiEnergySensor(manager, port.binding_id))
                added.add(port.binding_id)
        if entities:
            async_add_entities(entities)

    entry.async_on_unload(manager.async_subscribe(add_committed_ports))
    add_committed_ports()


class MfiEnergySensor(SensorEntity):
    """One cumulative energy counter for one bound power source."""

    _attr_should_poll = False
    _attr_has_entity_name = True
    _attr_device_class = SensorDeviceClass.ENERGY
    _attr_state_class = SensorStateClass.TOTAL
    _attr_native_unit_of_measurement = UnitOfEnergy.KILO_WATT_HOUR
    _attr_suggested_display_precision = 4

    def __init__(self, manager: SourceManager, binding_id: str) -> None:
        self._manager = manager
        self._binding_id = binding_id
        self._attr_unique_id = f"{manager.entry.entry_id}_{binding_id}_energy"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, manager.entry.entry_id)},
            name=f"{manager.entry.title} Energy",
            manufacturer="Ubiquiti Networks",
            model="mFi energy companion",
        )

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(self._manager.async_subscribe(self.async_write_ha_state))

    @property
    def name(self) -> str:
        source_name = self._manager.ports[self._binding_id].binding.name
        if source_name.endswith(" Power"):
            source_name = source_name[:-6]
        return f"{source_name} Energy"

    @property
    def available(self) -> bool:
        return (
            self._manager.storage_error is None and self._manager.ports[self._binding_id].available
        )

    @property
    def native_value(self) -> Decimal | None:
        for port in self._manager.committed.ports:
            if port.binding_id == self._binding_id:
                return port.total
        return None

    @property
    def extra_state_attributes(self) -> dict[str, str]:
        port = self._manager.ports[self._binding_id]
        return {
            "source": port.binding.entity_id,
            "source_device_id": self._manager.source_device_id,
            "status": self._manager.storage_error or port.reason or "tracking",
        }
