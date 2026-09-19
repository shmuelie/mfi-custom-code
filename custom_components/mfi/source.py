"""Registry-backed source tracking and committed-only energy reporting."""

import asyncio
import logging
from collections.abc import AsyncIterator, Callable
from contextlib import asynccontextmanager
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING
from uuid import uuid4

from homeassistant.components.sensor import SensorDeviceClass, SensorStateClass
from homeassistant.const import (
    ATTR_RESTORED,
    EVENT_HOMEASSISTANT_STOP,
    STATE_UNAVAILABLE,
    STATE_UNKNOWN,
    EntityStateAttribute,
)
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, State, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.event import (
    async_track_state_added_domain,
    async_track_state_change_event,
    async_track_time_interval,
)
from homeassistant.util.hass_dict import HassKey

from .const import (
    CONF_SOURCE_CONFIG_ENTRY,
    CONF_SOURCE_DEVICE,
    DOMAIN,
    MANUFACTURER,
    MODEL_PORT_COUNTS,
    REPORT_INTERVAL,
)
from .energy import EnergyAccumulator, parse_power
from .repairs import set_issue
from .storage import Checkpoint, CheckpointStore, PortSnapshot, StorageError

if TYPE_CHECKING:
    from . import MfiConfigEntry

_LOGGER = logging.getLogger(__name__)
_DESTINATION_LOCKS: HassKey[dict[str, asyncio.Lock]] = HassKey("mfi_destination_locks")


class RuntimeChangedError(ValueError):
    """An operation belongs to a stopping or replaced companion runtime."""


def destination_lock(hass: HomeAssistant, unique_id: str) -> asyncio.Lock:
    """Serialize creation and recovery for the same MQTT device."""
    return hass.data.setdefault(_DESTINATION_LOCKS, {}).setdefault(unique_id, asyncio.Lock())


def destination_conflict(
    hass: HomeAssistant,
    unique_id: str,
    *,
    entry_id: str | None = None,
    flow_id: str | None = None,
) -> str | None:
    """Include pending setup reservations, which remain active during creation."""
    if any(
        entry.entry_id != entry_id and entry.unique_id == unique_id
        for entry in hass.config_entries.async_entries(DOMAIN)
    ):
        return "already_configured"
    if any(
        flow["flow_id"] != flow_id
        for flow in hass.config_entries.flow.async_progress_by_handler(
            DOMAIN, include_uninitialized=True, match_context={"unique_id": unique_id}
        )
    ):
        return "already_in_progress"
    return None


def source_entries(hass: HomeAssistant, device_id: str) -> list[er.RegistryEntry]:
    """Get MQTT sensors, including disabled sensors and incomplete metadata."""
    return [
        entity
        for entity in er.async_entries_for_device(
            er.async_get(hass), device_id, include_disabled_entities=True
        )
        if entity.platform == "mqtt" and entity.domain == "sensor"
    ]


def eligible_source(hass: HomeAssistant, entity: er.RegistryEntry) -> bool:
    """Check source semantics, not labels or entity-ID suffixes."""
    state = hass.states.get(entity.entity_id)
    attrs = state.attributes if state is not None else {}
    device_class = attrs.get(EntityStateAttribute.DEVICE_CLASS) or (
        entity.device_class or entity.original_device_class
    )
    unit = attrs.get(EntityStateAttribute.UNIT_OF_MEASUREMENT) or entity.unit_of_measurement
    state_class = attrs.get("state_class") or (entity.capabilities or {}).get("state_class")
    return (
        entity.domain == "sensor"
        and entity.platform == "mqtt"
        and device_class == SensorDeviceClass.POWER
        and state_class == SensorStateClass.MEASUREMENT
        and unit in ("W", "kW")
    )


def candidate_devices(hass: HomeAssistant) -> list[dr.DeviceEntry]:
    """List mFi-like MQTT devices; unknown models need explicit confirmation."""
    devices = []
    for device in dr.async_get(hass).devices:
        config_entry = hass.config_entries.async_get_entry(device.config_entry_id)
        if (
            config_entry is not None
            and config_entry.domain == "mqtt"
            and device.manufacturer == MANUFACTURER
            and any(eligible_source(hass, entity) for entity in source_entries(hass, device.id))
        ):
            devices.append(device)
    return devices


def make_binding(entity: er.RegistryEntry, excluded: bool = False) -> PortSnapshot:
    return PortSnapshot(
        binding_id=uuid4().hex,
        registry_id=entity.id,
        unique_id=entity.unique_id,
        entity_id=entity.entity_id,
        name=entity.name or entity.original_name or entity.entity_id,
        excluded=excluded,
    )


@dataclass
class TrackedPort:
    binding: PortSnapshot
    accumulator: EnergyAccumulator
    reason: str | None = "waiting"
    source_reason: str | None = "waiting"

    @property
    def available(self) -> bool:
        return self.reason is None and not self.binding.excluded


class SourceManager:
    """Own a device's accounting, subscriptions, and single checkpoint writer."""

    def __init__(
        self,
        hass: HomeAssistant,
        entry: MfiConfigEntry,
        store: CheckpointStore,
        checkpoint: Checkpoint,
    ) -> None:
        self.hass = hass
        self.entry = entry
        self.store = store
        self.committed = checkpoint
        self.ports = {
            port.binding_id: TrackedPort(port, EnergyAccumulator(port.total))
            for port in checkpoint.ports
        }
        self.storage_error: str | None = None
        self.ignored_sources = set(checkpoint.ignored_sources)
        self.ambiguous = False
        self._listeners: set[Callable[[], None]] = set()
        self._unsubs: list[Callable[[], None]] = []
        self._state_unsub: Callable[[], None] | None = None
        self._reconcile_handle: asyncio.Handle | None = None
        self._save_task: asyncio.Task[None] | None = None
        self._save_again = False
        self._commit_waiters: list[asyncio.Future[StorageError | None]] = []
        self._configuration_lock = asyncio.Lock()
        self._stopping = False
        self._lifecycle = 0

    @property
    def source_device_id(self) -> str:
        return str(self.entry.data[CONF_SOURCE_DEVICE])

    @property
    def source_config_entry_id(self) -> str:
        return str(self.entry.data[CONF_SOURCE_CONFIG_ENTRY])

    def _ensure_current(self) -> None:
        if getattr(self.entry, "runtime_data", None) is not self:
            raise RuntimeChangedError("The mFi runtime has changed; reopen its configuration")

    def _ensure_configurable(self, lifecycle: int) -> None:
        self._ensure_current()
        if self._stopping or lifecycle != self._lifecycle:
            raise RuntimeChangedError("The mFi runtime is stopping; reopen its configuration")

    @asynccontextmanager
    async def _configuration(self) -> AsyncIterator[int]:
        lifecycle = self._lifecycle
        self._ensure_configurable(lifecycle)
        async with self._configuration_lock:
            self._ensure_configurable(lifecycle)
            yield lifecycle

    @callback
    def async_subscribe(self, listener: Callable[[], None]) -> Callable[[], None]:
        self._listeners.add(listener)
        return lambda: self._listeners.discard(listener)

    @callback
    def _notify(self) -> None:
        for listener in tuple(self._listeners):
            listener()

    async def async_start(self) -> None:
        self._ensure_current()
        self._stopping = False
        self._unsubs.extend(
            [
                self.hass.bus.async_listen(er.EVENT_ENTITY_REGISTRY_UPDATED, self._entity_updated),
                self.hass.bus.async_listen(dr.EVENT_DEVICE_REGISTRY_UPDATED, self._device_updated),
                async_track_state_added_domain(self.hass, "sensor", self._state_added),
                async_track_time_interval(self.hass, self._tick, REPORT_INTERVAL),
                self.hass.bus.async_listen(EVENT_HOMEASSISTANT_STOP, self._ha_stopping),
            ]
        )
        self.entry.async_on_unload(self._unsubscribe)
        self._reconcile()
        await self.async_flush()

    @callback
    def _unsubscribe(self) -> None:
        for unsub in self._unsubs:
            unsub()
        self._unsubs.clear()
        if self._state_unsub is not None:
            self._state_unsub()
            self._state_unsub = None
        if self._reconcile_handle is not None:
            self._reconcile_handle.cancel()
            self._reconcile_handle = None

    async def _ha_stopping(self, _event: Event) -> None:
        await self.async_stop()

    async def async_stop(self) -> None:
        if getattr(self.entry, "runtime_data", None) is not self:
            return
        if not self._stopping:
            self._stopping = True
            self._lifecycle += 1
            self._unsubscribe()
            now = self.hass.loop.time()
            for port in self.ports.values():
                port.accumulator.update(None, now)
                port.reason = "stopped"
                port.source_reason = "stopped"
        async with self._configuration_lock:
            await self.async_flush()

    @callback
    def _schedule_reconcile(self) -> None:
        if not self._stopping and self._reconcile_handle is None:
            self._reconcile_handle = self.hass.loop.call_soon(self._reconcile)

    @callback
    def _entity_updated(self, event: Event[er.EventEntityRegistryUpdatedData]) -> None:
        entity = er.async_get(self.hass).async_get(event.data["entity_id"])
        known = {port.binding.entity_id for port in self.ports.values()}
        if (
            (entity is not None and entity.device_id == self.source_device_id)
            or event.data["entity_id"] in known
            or event.data.get("old_entity_id") in known
        ):
            self._schedule_reconcile()

    @callback
    def _device_updated(self, event: Event[dr.EventDeviceRegistryUpdatedData]) -> None:
        if event.data["device_id"] == self.source_device_id:
            self._schedule_reconcile()

    @callback
    def _state_added(self, event: Event[EventStateChangedData]) -> None:
        entity = er.async_get(self.hass).async_get(event.data["entity_id"])
        if entity is not None and entity.device_id == self.source_device_id:
            self._schedule_reconcile()

    @callback
    def _state_changed(self, event: Event[EventStateChangedData]) -> None:
        if self._stopping:
            return
        for port in self.ports.values():
            if port.binding.entity_id == event.data["entity_id"]:
                self._update_port(port, event.data["new_state"], self.hass.loop.time())
        self._notify()
        self._schedule_reconcile()

    @callback
    def _update_port(self, port: TrackedPort, state: State | None, now: float) -> None:
        entity = er.async_get(self.hass).async_get(port.binding.registry_id)
        reason = None
        power = None
        if (
            entity is None
            or entity.device_id != self.source_device_id
            or entity.config_entry_id != self.source_config_entry_id
            or entity.platform != "mqtt"
        ):
            reason = "missing"
        elif entity.disabled:
            reason = "disabled"
        elif state is None:
            reason = "waiting"
        elif state.attributes.get(ATTR_RESTORED):
            reason = "restored"
        elif state.state in (STATE_UNAVAILABLE, STATE_UNKNOWN):
            reason = state.state
        elif not eligible_source(self.hass, entity):
            reason = "unsupported_source"
        else:
            try:
                power = parse_power(
                    state.state,
                    str(state.attributes.get(EntityStateAttribute.UNIT_OF_MEASUREMENT, "")),
                )
            except ValueError as error:
                reason = str(error)
        port.source_reason = reason
        if port.binding.excluded:
            reason = "excluded"
            power = None
        if reason != port.reason:
            if reason not in (None, "excluded", "disabled", "waiting", "unavailable", "unknown"):
                _LOGGER.warning("Energy source %s suspended: %s", port.binding.entity_id, reason)
            elif reason is None and port.reason not in (None, "waiting"):
                _LOGGER.info("Energy source %s recovered", port.binding.entity_id)
        port.accumulator.update(power, now)
        port.reason = reason

    @callback
    def _reconcile(self) -> None:
        self._reconcile_handle = None
        if self._stopping:
            return
        registry = er.async_get(self.hass)
        device = dr.async_get(self.hass).async_get(self.source_device_id)
        device_valid = (
            isinstance(device, dr.DeviceEntry)
            and device.config_entry_id == self.source_config_entry_id
        )
        entries = source_entries(self.hass, self.source_device_id) if device_valid else []
        candidates = [entity for entity in entries if eligible_source(self.hass, entity)]
        if self._state_unsub is not None:
            self._state_unsub()
        self._state_unsub = async_track_state_change_event(
            self.hass, [entity.entity_id for entity in entries], self._state_changed
        )
        now = self.hass.loop.time()
        for port in self.ports.values():
            entity = registry.async_get(port.binding.registry_id)
            if entity is None:
                entity_id = registry.async_get_entity_id("sensor", "mqtt", port.binding.unique_id)
                entity = registry.async_get(entity_id) if entity_id is not None else None
            if (
                device_valid
                and entity is not None
                and entity.device_id == self.source_device_id
                and entity.config_entry_id == self.source_config_entry_id
            ):
                port.binding = replace(
                    port.binding,
                    registry_id=entity.id,
                    entity_id=entity.entity_id,
                    name=entity.name or entity.original_name or entity.entity_id,
                )
                self._update_port(port, self.hass.states.get(entity.entity_id), now)
            else:
                port.accumulator.update(None, now)
                port.reason = "missing"
                port.source_reason = "missing"
        bound = {port.binding.registry_id for port in self.ports.values()}
        # Neither a pending ignore nor an uncommitted unignore permits enrollment.
        enrollment_ignored = self.ignored_sources | set(self.committed.ignored_sources)
        additions = [
            entity
            for entity in candidates
            if entity.id not in bound and entity.id not in enrollment_ignored
        ]
        count = (
            MODEL_PORT_COUNTS.get(device.model_id or "")
            if isinstance(device, dr.DeviceEntry)
            else None
        )
        orphaned = any(port.source_reason is not None for port in self.ports.values())
        self.ambiguous = bool(additions) and (
            orphaned
            or (
                count is not None
                and len(bound | {e.id for e in candidates if e.id not in enrollment_ignored})
                > count
            )
        )
        if not self.ambiguous:
            for entity in additions:
                binding = make_binding(entity)
                port = TrackedPort(binding, EnergyAccumulator())
                self.ports[binding.binding_id] = port
                self._update_port(port, self.hass.states.get(entity.entity_id), now)
        missing = [
            port.binding.entity_id for port in self.ports.values() if port.reason == "missing"
        ]
        detail = (
            "New sources conflict with existing port bindings. Use Configure to rebind or exclude."
            if self.ambiguous
            else f"Missing sources: {', '.join(missing)}"
            if missing
            else None
        )
        set_issue(self.hass, self.entry.entry_id, "sources", detail)
        self._notify()
        if self._metadata_changed() and self.storage_error is None:
            self._request_save()

    def _snapshot(self) -> Checkpoint:
        return replace(
            self.committed.next_generation(
                tuple(
                    replace(port.binding, total=port.accumulator.total)
                    for port in self.ports.values()
                )
            ),
            ignored_sources=tuple(sorted(self.ignored_sources)),
        )

    def _metadata_changed(self) -> bool:
        committed = {port.binding_id: port for port in self.committed.ports}
        return tuple(sorted(self.ignored_sources)) != self.committed.ignored_sources or any(
            port.binding.binding_id not in committed
            or replace(port.binding, total=committed[port.binding.binding_id].total)
            != committed[port.binding.binding_id]
            for port in self.ports.values()
        )

    @callback
    def _tick(self, _now: object) -> None:
        if self._stopping:
            return
        now = self.hass.loop.time()
        for port in self.ports.values():
            self._update_port(port, self.hass.states.get(port.binding.entity_id), now)
        self._request_save()

    @callback
    def _request_save(self) -> None:
        self._save_again = True
        if self._save_task is None or self._save_task.done():
            self._save_task = self.entry.async_create_task(
                self.hass, self._save_loop(), "mFi energy checkpoint"
            )

    async def _save_loop(self) -> None:
        while self._save_again:
            self._save_again = False
            waiters, self._commit_waiters = self._commit_waiters, []
            snapshot = self._snapshot()
            if (
                snapshot.ports == self.committed.ports
                and snapshot.ignored_sources == self.committed.ignored_sources
                and self.storage_error is None
            ):
                self._complete_commits(waiters, None)
                continue
            try:
                await self.store.async_save(snapshot)
            except StorageError as error:
                if self.storage_error is None:
                    _LOGGER.error("Energy checkpoint failed for %s: %s", self.entry.title, error)
                self.storage_error = str(error)
                set_issue(self.hass, self.entry.entry_id, "storage", self.storage_error)
                self._complete_commits(waiters + self._commit_waiters, error)
                self._commit_waiters.clear()
                self._save_again = False
                self._notify()
                break
            except asyncio.CancelledError:
                for waiter in waiters + self._commit_waiters:
                    waiter.cancel()
                self._commit_waiters.clear()
                raise
            self.committed = snapshot
            self.storage_error = None
            set_issue(self.hass, self.entry.entry_id, "storage", None)
            self._complete_commits(waiters, None)
            self._notify()

    @staticmethod
    def _complete_commits(
        waiters: list[asyncio.Future[StorageError | None]], error: StorageError | None
    ) -> None:
        for waiter in waiters:
            if not waiter.done():
                waiter.set_result(error)

    async def _async_commit(self) -> None:
        """Acknowledge the snapshot containing this request, not a later write."""
        self._ensure_configurable(self._lifecycle)
        waiter: asyncio.Future[StorageError | None] = self.hass.loop.create_future()
        self._commit_waiters.append(waiter)
        self._request_save()
        if (error := await asyncio.shield(waiter)) is not None:
            raise error

    async def async_flush(self) -> None:
        self._ensure_current()
        self._request_save()
        assert self._save_task is not None
        await asyncio.shield(self._save_task)

    async def async_set_exclusions(self, excluded: set[str]) -> None:
        async with self._configuration():
            if not excluded <= set(self.ports):
                raise ValueError("Unknown port binding")
            previous = {key: port.binding for key, port in self.ports.items()}
            for binding_id, port in self.ports.items():
                port.binding = replace(port.binding, excluded=binding_id in excluded)
                self._update_port(
                    port, self.hass.states.get(port.binding.entity_id), self.hass.loop.time()
                )
            self._notify()
            try:
                await self._async_commit()
            except StorageError:
                for key, binding in previous.items():
                    self.ports[key].binding = binding
                self._reconcile()
                raise

    async def async_rebind(self, binding_id: str, entity_id: str) -> None:
        async with self._configuration():
            entity = er.async_get(self.hass).async_get(entity_id)
            if (
                binding_id not in self.ports
                or entity is None
                or entity.device_id != self.source_device_id
                or entity.config_entry_id != self.source_config_entry_id
                or not eligible_source(self.hass, entity)
                or any(
                    port.binding.registry_id == entity.id
                    for key, port in self.ports.items()
                    if key != binding_id
                )
            ):
                raise ValueError(
                    "Replacement must be an unbound power source on the selected device"
                )
            port = self.ports[binding_id]
            previous_binding = port.binding
            previous_ignored = self.ignored_sources.copy()
            port.accumulator.update(None, self.hass.loop.time())
            if port.binding.registry_id != entity.id:
                self.ignored_sources.add(port.binding.registry_id)
            self.ignored_sources.discard(entity.id)
            port.binding = replace(
                port.binding,
                registry_id=entity.id,
                unique_id=entity.unique_id,
                entity_id=entity.entity_id,
                name=entity.name or entity.original_name or entity.entity_id,
            )
            try:
                await self._async_commit()
            except StorageError:
                port.binding = previous_binding
                self.ignored_sources = previous_ignored
                self._reconcile()
                raise
            self._reconcile()

    async def async_ignore_sources(self, registry_ids: set[str]) -> None:
        async with self._configuration():
            bound = {port.binding.registry_id for port in self.ports.values()}
            candidates = {entity.id for entity in source_entries(self.hass, self.source_device_id)}
            if registry_ids & bound or not registry_ids <= candidates | self.ignored_sources:
                raise ValueError("Only unbound sources on this device can be ignored")
            previous = self.ignored_sources
            self.ignored_sources = registry_ids
            try:
                await self._async_commit()
            except StorageError:
                self.ignored_sources = previous
                self._reconcile()
                raise
            self._reconcile()

    async def async_change_device(self, device_id: str) -> None:
        """Select the same physical device after its upstream identity changed."""
        async with self._configuration() as lifecycle:
            device = next(
                (item for item in candidate_devices(self.hass) if item.id == device_id), None
            )
            if device is None:
                raise ValueError("Select an eligible MQTT device")
            unique_id = f"{device.config_entry_id}_{device.id}"
            async with destination_lock(self.hass, unique_id):
                self._ensure_configurable(lifecycle)
                if destination_conflict(self.hass, unique_id, entry_id=self.entry.entry_id):
                    raise ValueError("This MQTT device already has a companion or a pending setup")
                now = self.hass.loop.time()
                for port in self.ports.values():
                    port.accumulator.update(None, now)
                try:
                    await self._async_commit()
                    self._ensure_configurable(lifecycle)
                    if destination_conflict(self.hass, unique_id, entry_id=self.entry.entry_id):
                        raise ValueError(
                            "This MQTT device already has a companion or a pending setup"
                        )
                    current = dr.async_get(self.hass).async_get(device_id)
                    if current is None or current.config_entry_id != device.config_entry_id:
                        raise ValueError("The selected MQTT device changed during recovery")
                except StorageError, ValueError:
                    self._reconcile()
                    raise
                self.hass.config_entries.async_update_entry(
                    self.entry,
                    unique_id=unique_id,
                    data={
                        **self.entry.data,
                        CONF_SOURCE_DEVICE: device.id,
                        CONF_SOURCE_CONFIG_ENTRY: device.config_entry_id,
                    },
                )
                self._reconcile()
