"""Native port relays with explicit, non-replayed hardware confirmation."""

from typing import Any

from homeassistant.components.switch import SwitchDeviceClass, SwitchEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import MfiConfigEntry
from .entity import NativeEntity
from .mqtt import NativeRuntime


async def async_setup_entry(
    hass: HomeAssistant, entry: MfiConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    runtime = entry.runtime_data
    if isinstance(runtime, NativeRuntime):
        async_add_entities(
            [
                NativeRelay(runtime, port.id, "relay")
                for port in runtime.descriptor.ports
                if "relay" in port.capabilities
            ]
        )


class NativeRelay(NativeEntity, SwitchEntity):
    _attr_device_class = SwitchDeviceClass.OUTLET

    @property
    def is_on(self) -> bool | None:
        value = self.runtime.role_value(self.port_id, "relay")
        return None if value is None else value == "ON"

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self.runtime.async_set_relay(self.port_id, True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.runtime.async_set_relay(self.port_id, False)
