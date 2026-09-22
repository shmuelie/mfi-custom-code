"""Native entity identity and shared child-device association."""

from homeassistant.helpers.entity import Entity

from .mqtt import NativeRuntime


class NativeEntity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, runtime: NativeRuntime, port_id: int, role: str) -> None:
        self.runtime = runtime
        self.port_id = port_id
        self.role = role
        self._attr_unique_id = (
            runtime.energy_unique_id(port_id)
            if role == "energy"
            else runtime.descriptor.unique_id(port_id, role)
        )
        self._attr_translation_key = role
        self._attr_name = role.capitalize()
        self.device_entry = runtime.children[port_id]

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(self.runtime.async_subscribe(self.async_write_ha_state))

    @property
    def available(self) -> bool:
        return self.runtime.role_available(self.port_id, self.role)
