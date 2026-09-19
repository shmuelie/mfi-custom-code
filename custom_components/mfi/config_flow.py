"""Select existing MQTT devices without requesting broker credentials."""

import asyncio
import logging
from typing import Any
from uuid import uuid4

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import selector

from . import MfiConfigEntry
from .const import (
    CONF_EXCLUDED,
    CONF_FRESHNESS,
    CONF_SOURCE_CONFIG_ENTRY,
    CONF_SOURCE_DEVICE,
    CONF_STORAGE_ID,
    DOMAIN,
    MODEL_PORT_COUNTS,
)
from .source import (
    RuntimeChangedError,
    SourceManager,
    candidate_devices,
    destination_conflict,
    destination_lock,
    eligible_source,
    make_binding,
    source_entries,
)
from .storage import Checkpoint, CheckpointStore, StorageError

_LOGGER = logging.getLogger(__name__)


def _multi_select(options: dict[str, str]) -> selector.SelectSelector:
    return selector.SelectSelector(
        selector.SelectSelectorConfig(
            options=[
                selector.SelectOptionDict(value=value, label=label)
                for value, label in options.items()
            ],
            multiple=True,
            mode=selector.SelectSelectorMode.LIST,
        )
    )


class MfiConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """One companion entry per existing MQTT device."""

    VERSION = 1

    def __init__(self) -> None:
        self._device_id: str | None = None
        self._storage_id = uuid4().hex
        self._bootstrap: CheckpointStore | None = None
        self._bootstrap_task: asyncio.Task[None] | None = None
        self._aborted = False

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: config_entries.ConfigEntry) -> MfiOptionsFlow:
        return MfiOptionsFlow()

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if not self.hass.config_entries.async_entries("mqtt"):
            return self.async_abort(reason="mqtt_required")
        devices = {device.id: device for device in candidate_devices(self.hass)}
        if not devices:
            return self.async_abort(reason="no_devices")
        errors: dict[str, str] = {}
        if user_input is not None:
            device = devices.get(user_input[CONF_SOURCE_DEVICE])
            if device is not None:
                await self.async_set_unique_id(f"{device.config_entry_id}_{device.id}")
                self._abort_if_unique_id_configured()
                self._device_id = device.id
                return await self.async_step_confirm()
            errors["base"] = "invalid_device"
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_SOURCE_DEVICE): vol.In(
                        {
                            key: device.name_by_user or device.name or key
                            for key, device in devices.items()
                        }
                    )
                }
            ),
            errors=errors,
        )

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        assert self._device_id is not None
        device = dr.async_get(self.hass).async_get(self._device_id)
        if not isinstance(device, dr.DeviceEntry):
            return self.async_abort(reason="device_removed")
        sources = [
            entity
            for entity in source_entries(self.hass, device.id)
            if eligible_source(self.hass, entity)
        ]
        if not sources:
            return self.async_abort(reason="no_devices")
        count = MODEL_PORT_COUNTS.get(device.model_id or "")
        if count is not None and len(sources) > count:
            return self.async_abort(reason="ambiguous_sources")
        errors: dict[str, str] = {}
        if user_input is not None:
            excluded = set(user_input.get(CONF_EXCLUDED, []))
            if not user_input.get(CONF_FRESHNESS):
                errors[CONF_FRESHNESS] = "confirmation_required"
            elif count is None and not user_input.get("model_confirmed"):
                errors["model_confirmed"] = "confirmation_required"
            elif not excluded <= {entity.id for entity in sources}:
                errors[CONF_EXCLUDED] = "invalid_source"
            else:
                try:
                    return await self._async_create_companion(device, sources, excluded)
                except StorageError:
                    errors["base"] = "storage_error"
        fields: dict[vol.Marker, object] = {
            vol.Required(CONF_FRESHNESS, default=False): bool,
            vol.Optional(CONF_EXCLUDED, default=[]): _multi_select(
                {
                    entity.id: entity.name or entity.original_name or entity.entity_id
                    for entity in sources
                }
            ),
        }
        if count is None:
            fields[vol.Required("model_confirmed", default=False)] = bool
        return self.async_show_form(
            step_id="confirm",
            data_schema=vol.Schema(fields),
            errors=errors,
            description_placeholders={
                "device": device.name_by_user or device.name or "mFi",
                "ports": str(len(sources)),
                "model": device.model or device.model_id or "Unknown",
            },
        )

    async def _async_create_companion(
        self, device: dr.DeviceEntry, sources: list[er.RegistryEntry], excluded: set[str]
    ) -> config_entries.ConfigFlowResult:
        unique_id = f"{device.config_entry_id}_{device.id}"
        async with destination_lock(self.hass, unique_id):
            await self.async_set_unique_id(unique_id)
            if reason := destination_conflict(self.hass, unique_id, flow_id=self.flow_id):
                return self.async_abort(reason=reason)
            self._bootstrap = CheckpointStore(self.hass, self._storage_id)
            checkpoint = Checkpoint(
                self._storage_id,
                0,
                tuple(make_binding(entity, entity.id in excluded) for entity in sources),
            )
            self._bootstrap_task = self.hass.async_create_task(
                self._bootstrap.async_save(checkpoint), "mFi initial checkpoint"
            )
            await asyncio.shield(self._bootstrap_task)
            if self._aborted:
                return self.async_abort(reason="setup_cancelled")
            if reason := destination_conflict(self.hass, unique_id, flow_id=self.flow_id):
                return self.async_abort(reason=reason)
            current = dr.async_get(self.hass).async_get(device.id)
            if current is None or current.config_entry_id != device.config_entry_id:
                return self.async_abort(reason="device_removed")
            self._bootstrap = None
            return self.async_create_entry(
                title=device.name_by_user or device.name or "mFi",
                data={
                    CONF_SOURCE_DEVICE: device.id,
                    CONF_SOURCE_CONFIG_ENTRY: device.config_entry_id,
                    CONF_STORAGE_ID: self._storage_id,
                    CONF_FRESHNESS: True,
                },
            )

    @callback
    def async_remove(self) -> None:
        self._aborted = True
        if self._bootstrap is not None:
            self.hass.async_create_task(self._cleanup_bootstrap(), "mFi canceled setup cleanup")
        super().async_remove()

    async def _cleanup_bootstrap(self) -> None:
        if self._bootstrap_task is not None:
            try:
                await asyncio.shield(self._bootstrap_task)
            except StorageError as error:
                _LOGGER.debug("Canceled bootstrap did not save: %s", error)
        if self._bootstrap is not None:
            await self._bootstrap.async_remove()


class MfiOptionsFlow(config_entries.OptionsFlow):
    """Exclude ports or explicitly rebind a counter while retaining its total."""

    @property
    def entry(self) -> MfiConfigEntry:
        return self.config_entry

    def _manager(self) -> SourceManager | None:
        if not hasattr(self.entry, "runtime_data"):
            return None
        return self.entry.runtime_data

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if self._manager() is None:
            return self.async_abort(reason="not_loaded")
        return self.async_show_menu(
            step_id="init", menu_options=["exclude", "rebind", "ignore", "device"]
        )

    async def async_step_exclude(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if (manager := self._manager()) is None:
            return self.async_abort(reason="not_loaded")
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                await manager.async_set_exclusions(set(user_input.get(CONF_EXCLUDED, [])))
            except RuntimeChangedError:
                return self.async_abort(reason="runtime_changed")
            except (StorageError, ValueError) as error:
                errors["base"] = (
                    "storage_error" if isinstance(error, StorageError) else "invalid_source"
                )
            else:
                return self.async_create_entry(title="", data={})
        return self.async_show_form(
            step_id="exclude",
            data_schema=vol.Schema(
                {
                    vol.Optional(
                        CONF_EXCLUDED,
                        default=[
                            key for key, port in manager.ports.items() if port.binding.excluded
                        ],
                    ): _multi_select(
                        {key: port.binding.name for key, port in manager.ports.items()}
                    )
                }
            ),
            errors=errors,
        )

    async def async_step_rebind(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if (manager := self._manager()) is None:
            return self.async_abort(reason="not_loaded")
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                await manager.async_rebind(user_input["binding"], user_input["source"])
            except RuntimeChangedError:
                return self.async_abort(reason="runtime_changed")
            except (StorageError, ValueError) as error:
                errors["base"] = (
                    "storage_error" if isinstance(error, StorageError) else "invalid_source"
                )
            else:
                return self.async_create_entry(title="", data={})
        bound = {port.binding.registry_id for port in manager.ports.values()}
        candidates = {
            entity.entity_id: entity.name or entity.original_name or entity.entity_id
            for entity in source_entries(self.hass, manager.source_device_id)
            if eligible_source(self.hass, entity) and entity.id not in bound
        }
        if not candidates:
            return self.async_abort(reason="no_replacements")
        return self.async_show_form(
            step_id="rebind",
            data_schema=vol.Schema(
                {
                    vol.Required("binding"): vol.In(
                        {key: port.binding.name for key, port in manager.ports.items()}
                    ),
                    vol.Required("source"): vol.In(candidates),
                }
            ),
            errors=errors,
        )

    async def async_step_ignore(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if (manager := self._manager()) is None:
            return self.async_abort(reason="not_loaded")
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                await manager.async_ignore_sources(set(user_input.get("ignored", [])))
            except RuntimeChangedError:
                return self.async_abort(reason="runtime_changed")
            except (StorageError, ValueError) as error:
                errors["base"] = (
                    "storage_error" if isinstance(error, StorageError) else "invalid_source"
                )
            else:
                return self.async_create_entry(title="", data={})
        bound = {port.binding.registry_id for port in manager.ports.values()}
        choices = {key: key for key in manager.ignored_sources}
        choices.update(
            {
                entity.id: entity.name or entity.original_name or entity.entity_id
                for entity in source_entries(self.hass, manager.source_device_id)
                if entity.id not in bound and eligible_source(self.hass, entity)
            }
        )
        return self.async_show_form(
            step_id="ignore",
            data_schema=vol.Schema(
                {
                    vol.Optional("ignored", default=list(manager.ignored_sources)): _multi_select(
                        choices
                    )
                }
            ),
            errors=errors,
        )

    async def async_step_device(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        if (manager := self._manager()) is None:
            return self.async_abort(reason="not_loaded")
        errors: dict[str, str] = {}
        if user_input is not None:
            if not user_input.get("same_device"):
                errors["same_device"] = "confirmation_required"
            else:
                try:
                    await manager.async_change_device(user_input[CONF_SOURCE_DEVICE])
                except RuntimeChangedError:
                    return self.async_abort(reason="runtime_changed")
                except (StorageError, ValueError) as error:
                    errors["base"] = (
                        "storage_error" if isinstance(error, StorageError) else "invalid_source"
                    )
                else:
                    return self.async_create_entry(title="", data={})
        return self.async_show_form(
            step_id="device",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_SOURCE_DEVICE): vol.In(
                        {
                            device.id: device.name_by_user or device.name or device.id
                            for device in candidate_devices(self.hass)
                        }
                    ),
                    vol.Required("same_device", default=False): bool,
                }
            ),
            errors=errors,
        )
