"""Native MQTT transport and per-port lifecycle, using HA's broker connection."""

import asyncio
import json
import logging
from collections import deque
from dataclasses import dataclass
from decimal import Decimal
from functools import partial
from typing import TYPE_CHECKING
from uuid import uuid4

from homeassistant.components import mqtt
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.core import CALLBACK_TYPE, HomeAssistant, State, callback
from homeassistant.exceptions import ConfigEntryNotReady, HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.event import async_call_later, async_track_time_interval

from .const import (
    CONF_DESCRIPTOR,
    CONF_ENERGY_UNIQUE_IDS,
    CONF_PORT_BINDINGS,
    DOMAIN,
    REPORT_INTERVAL,
)
from .protocol import Descriptor, Port, Report, parse_availability, parse_descriptor, parse_report
from .repairs import set_issue
from .source import SourceManager, TrackedPort
from .storage import Checkpoint, CheckpointStore

if TYPE_CHECKING:
    from . import MfiConfigEntry

_LOGGER = logging.getLogger(__name__)


@dataclass
class Sample:
    report: Report
    received: float


@dataclass
class PendingCommand:
    request_id: str
    session_id: str
    value: str
    done: asyncio.Future[None]


class NativeRuntime(SourceManager):
    """Reuse the proven checkpoint writer; native reports replace registry sources."""

    def __init__(
        self,
        hass: HomeAssistant,
        entry: MfiConfigEntry,
        store: CheckpointStore,
        checkpoint: Checkpoint,
        descriptor: Descriptor,
    ) -> None:
        super().__init__(hass, entry, store, checkpoint)
        self.descriptor = descriptor
        if entry.unique_id != descriptor.device_id:
            raise ValueError("Native config-entry identity does not match the descriptor")
        mapping = entry.data.get(CONF_PORT_BINDINGS)
        if (
            not isinstance(mapping, dict)
            or set(mapping) != {str(p.id) for p in descriptor.ports if "power" in p.capabilities}
            or any(
                not isinstance(value, str) or value not in self.ports for value in mapping.values()
            )
            or len(set(mapping.values())) != len(mapping)
            or set(mapping.values()) != set(self.ports)
        ):
            raise ValueError("Native physical-port bindings do not match the checkpoint")
        self.bindings: dict[int, str] = {int(key): value for key, value in mapping.items()}
        overrides = entry.data.get(CONF_ENERGY_UNIQUE_IDS, {})
        if (
            not isinstance(overrides, dict)
            or not set(overrides) <= set(mapping)
            or any(not isinstance(value, str) or not value for value in overrides.values())
            or len(set(overrides.values())) != len(overrides)
        ):
            raise ValueError("Invalid preserved native energy identities")
        self._binding_ports = {value: key for key, value in self.bindings.items()}
        self.port_descriptors = {port.id: port for port in descriptor.ports}
        self.children: dict[int, dr.ChildDeviceEntry] = {}
        registry = dr.async_get(hass)
        self.parent = registry.async_get_or_create(
            config_entry_id=entry.entry_id,
            identifiers={(DOMAIN, descriptor.device_id)},
            name=descriptor.name,
            manufacturer=descriptor.manufacturer,
            model=descriptor.model,
            model_id=descriptor.model_id,
            sw_version=descriptor.firmware_version,
        )
        self._register_children()
        self.samples: dict[int, Sample] = {}
        self._sequences: dict[int, tuple[str, int]] = {}
        self._errors: dict[int, str] = {}
        self._expiry: dict[int, CALLBACK_TYPE] = {}
        self._session: str | None = None
        self._retired_sessions: deque[str] = deque(maxlen=32)
        self._online = False
        self._connected = False
        self._conflict = False
        self._descriptor_error = False
        self._availability_error: str | None = None
        self._connection_generation = 0
        self._commands: dict[int, PendingCommand] = {}
        self._command_locks = {port.id: asyncio.Lock() for port in descriptor.ports}

    @property
    def source_device_id(self) -> str:
        return self.parent.id

    @property
    def source_config_entry_id(self) -> str:
        return self.entry.entry_id

    @callback
    def _register_children(self) -> None:
        registry = dr.async_get(self.hass)
        for port in self.descriptor.ports:
            self.children[port.id] = registry.async_get_or_create_child(
                config_entry_id=self.entry.entry_id,
                parent_device_id=self.parent.id,
                identifiers={(DOMAIN, f"{self.descriptor.device_id}_port_{port.id}")},
                name=port.name,
            )

    def energy_unique_id(self, port_id: int) -> str:
        overrides = self.entry.data.get(CONF_ENERGY_UNIQUE_IDS, {})
        return str(overrides.get(str(port_id), self.descriptor.unique_id(port_id, "energy")))

    async def async_start(self) -> None:
        self._ensure_current()
        try:
            ready = await asyncio.wait_for(mqtt.async_wait_for_mqtt_client(self.hass), 10)
        except TimeoutError as error:
            raise ConfigEntryNotReady("Waiting for Home Assistant's MQTT client") from error
        if not ready:
            raise ConfigEntryNotReady("MQTT is not configured")
        self._stopping = False
        self._session = None
        self._online = False
        self._retired_sessions.clear()
        self._conflict = False
        self._clear_samples()
        self._connected = mqtt.is_connected(self.hass)
        self._unsubs.extend(
            [
                mqtt.async_subscribe_connection_status(self.hass, self._connection_changed),
                self.hass.bus.async_listen(EVENT_HOMEASSISTANT_STOP, self._ha_stopping),
                async_track_time_interval(self.hass, self._tick, REPORT_INTERVAL),
                self._clear_expiry,
            ]
        )
        self.entry.async_on_unload(self._unsubscribe)
        try:
            self._unsubs.append(
                await mqtt.async_subscribe(
                    self.hass,
                    f"{self.descriptor.base_topic}/availability",
                    self._availability,
                    qos=1,
                )
            )
            self._unsubs.append(
                await mqtt.async_subscribe(
                    self.hass, f"{self.descriptor.base_topic}/config", self._descriptor, qos=1
                )
            )
            for port in self.descriptor.ports:
                self._unsubs.append(
                    await mqtt.async_subscribe(
                        self.hass,
                        f"{self.descriptor.base_topic}/port/{port.id}/state",
                        partial(self._report, port),
                        qos=0,
                    )
                )
        except HomeAssistantError:
            self._unsubscribe()
            raise
        self._reconcile()
        await self.async_flush()

    async def async_stop(self) -> None:
        self._fail_commands("mFi integration is stopping")
        await super().async_stop()

    @callback
    def _clear_expiry(self) -> None:
        for cancel in self._expiry.values():
            cancel()
        self._expiry.clear()

    @callback
    def _fail_commands(self, reason: str) -> None:
        self._connection_generation += 1
        for pending in self._commands.values():
            if not pending.done.done():
                pending.done.set_exception(HomeAssistantError(reason))

    @callback
    def _clear_samples(self) -> None:
        self.samples.clear()
        self._errors.clear()
        self._clear_expiry()
        self._reconcile()

    @callback
    def _connection_changed(self, connected: bool) -> None:
        if self._stopping:
            return
        self._connected = connected
        self._online = False
        self._session = None
        self._retired_sessions.clear()
        self._conflict = False
        self._fail_commands("MQTT connection changed; command outcome is uncertain")
        self._clear_samples()

    @callback
    def _availability(self, message: mqtt.ReceiveMessage) -> None:
        if self._stopping or not self._connected:
            return
        try:
            session, online = parse_availability(message.payload)
        except ValueError as error:
            if str(error) != self._availability_error:
                _LOGGER.warning("Invalid mFi availability for %s: %s", self.entry.title, error)
            self._availability_error = str(error)
            self._online = False
            self._fail_commands("Invalid publisher availability")
            self._clear_samples()
            return
        self._availability_error = None
        if session in self._retired_sessions:
            return
        if self._session is not None and self._session != session:
            if self._online:
                self._conflict = True
                self._fail_commands("Conflicting mFi publisher sessions")
                set_issue(
                    self.hass, self.entry.entry_id, "native", "Conflicting publisher sessions"
                )
                self._clear_samples()
                return
            self._retired_sessions.append(self._session)
            self.samples = {
                key: value
                for key, value in self.samples.items()
                if value.report.session_id == session
            }
        self._session = session
        self._online = online
        if not online:
            self._fail_commands("mFi publisher went offline; command outcome is uncertain")
            self._clear_samples()
        self._reconcile()

    @callback
    def _descriptor(self, message: mqtt.ReceiveMessage) -> None:
        if self._stopping:
            return
        try:
            new = parse_descriptor(message.payload, message.topic)
            if (
                new.device_id != self.descriptor.device_id
                or {(p.id, p.capabilities) for p in new.ports}
                != {(p.id, p.capabilities) for p in self.descriptor.ports}
                or new.refresh_interval != self.descriptor.refresh_interval
                or new.expire_after != self.descriptor.expire_after
            ):
                raise ValueError("Native capabilities or timing changed; reconfigure explicitly")
        except ValueError as error:
            self._descriptor_error = True
            self._fail_commands("Native descriptor is invalid or incompatible")
            set_issue(self.hass, self.entry.entry_id, "native", str(error))
            self._clear_samples()
            return
        self._descriptor_error = False
        if not self._conflict:
            set_issue(self.hass, self.entry.entry_id, "native", None)
        if new != self.descriptor:
            self.descriptor = new
            self.port_descriptors = {port.id: port for port in new.ports}
            parent = dr.async_get(self.hass).async_update_device(
                self.parent.id, name=new.name, sw_version=new.firmware_version
            )
            if parent is None:
                raise HomeAssistantError("Native parent device was removed")
            self.parent = parent
            self._register_children()
            self.hass.config_entries.async_update_entry(
                self.entry, data={**self.entry.data, CONF_DESCRIPTOR: new.as_dict()}
            )
        self._reconcile()

    @callback
    def _report(self, port: Port, message: mqtt.ReceiveMessage) -> None:
        if self._stopping or not self._connected or message.retain or self._conflict:
            return
        try:
            report = parse_report(message.payload, port)
        except ValueError as error:
            if self._errors.get(port.id) != str(error):
                _LOGGER.warning("Invalid native port %s report: %s", port.id, error)
            self._errors[port.id] = str(error)
            self._reconcile()
            return
        if report.session_id in self._retired_sessions:
            return
        if self._session is not None and report.session_id != self._session:
            return
        old = self._sequences.get(port.id)
        if old is not None and old[0] == report.session_id and report.sequence <= old[1]:
            return
        self._sequences[port.id] = (report.session_id, report.sequence)
        previous = self.samples.get(port.id)
        self.samples[port.id] = Sample(report, self.hass.loop.time())
        self._errors.pop(port.id, None)
        if cancel := self._expiry.pop(port.id, None):
            cancel()
        self._expiry[port.id] = async_call_later(
            self.hass, self.descriptor.expire_after, partial(self._expired, port.id)
        )
        for role, reading in report.readings.items():
            prior = previous.report.readings.get(role) if previous is not None else None
            if reading.error is not None and (prior is None or prior.error != reading.error):
                _LOGGER.warning("Native port %s %s failed: %s", port.id, role, reading.error)
            elif reading.error is None and prior is not None and prior.error is not None:
                _LOGGER.info("Native port %s %s recovered", port.id, role)
        pending = self._commands.get(port.id)
        if (
            pending is not None
            and not pending.done.done()
            and pending.session_id == report.session_id
            and pending.request_id == report.request_id
            and self.role_available(port.id, "relay")
        ):
            relay = report.readings["relay"]
            if relay.value == pending.value:
                pending.done.set_result(None)
            else:
                pending.done.set_exception(
                    HomeAssistantError("Relay did not reach requested state")
                )
        elif (
            pending is not None
            and not pending.done.done()
            and pending.session_id == report.session_id
            and pending.request_id == report.request_id
            and (failed_relay := report.readings.get("relay")) is not None
            and failed_relay.error is not None
        ):
            pending.done.set_exception(HomeAssistantError(f"Relay failed: {failed_relay.error}"))
        self._reconcile()

    @callback
    def _expired(self, port_id: int, _now: object) -> None:
        self._expiry.pop(port_id, None)
        if not self._stopping:
            self._errors[port_id] = "expired"
            self._reconcile()

    def role_available(self, port_id: int, role: str) -> bool:
        sample = self.samples.get(port_id)
        return (
            not self._stopping
            and self._connected
            and self._online
            and not self._conflict
            and not self._descriptor_error
            and port_id not in self._errors
            and sample is not None
            and sample.report.session_id == self._session
            and self.hass.loop.time() - sample.received < self.descriptor.expire_after
            and role in sample.report.readings
            and sample.report.readings[role].error is None
        )

    def role_value(self, port_id: int, role: str) -> Decimal | str | None:
        sample = self.samples.get(port_id)
        if sample is None or role not in sample.report.readings:
            return None
        return sample.report.readings[role].value

    @callback
    def _update_port(self, port: TrackedPort, state: State | None, now: float) -> None:
        port_id = self._binding_ports[port.binding.binding_id]
        value = self.role_value(port_id, "power")
        power = value if isinstance(value, Decimal) else None
        valid = self.role_available(port_id, "power") and power is not None
        sample = self.samples.get(port_id)
        reading = sample.report.readings.get("power") if sample is not None else None
        port.source_reason = (
            None
            if valid
            else self._errors.get(port_id)
            or (reading.error if reading is not None else None)
            or "unavailable"
        )
        port.reason = "excluded" if port.binding.excluded else port.source_reason
        port.accumulator.update(power if valid and not port.binding.excluded else None, now)

    @callback
    def _reconcile(self) -> None:
        if self._stopping:
            return
        now = self.hass.loop.time()
        for port in self.ports.values():
            self._update_port(port, None, now)
        self._notify()

    async def async_set_relay(self, port_id: int, enabled: bool) -> None:
        generation = self._connection_generation
        lifecycle = self._lifecycle
        if port_id not in self._command_locks:
            raise HomeAssistantError("Unknown physical port")
        async with self._command_locks[port_id]:
            self._ensure_configurable(lifecycle)
            if (
                generation != self._connection_generation
                or not self.role_available(port_id, "relay")
                or not mqtt.is_connected(self.hass)
                or self._session is None
            ):
                raise HomeAssistantError("Relay is unavailable or its MQTT session changed")
            pending = PendingCommand(
                uuid4().hex,
                self._session,
                "ON" if enabled else "OFF",
                self.hass.loop.create_future(),
            )
            self._commands[port_id] = pending
            try:
                async with asyncio.timeout(10):
                    await mqtt.async_publish(
                        self.hass,
                        f"{self.descriptor.base_topic}/port/{port_id}/set",
                        json.dumps(
                            {
                                "session_id": pending.session_id,
                                "request_id": pending.request_id,
                                "value": pending.value,
                            }
                        ),
                        qos=0,
                        retain=False,
                    )
                    await pending.done
            except TimeoutError as error:
                raise HomeAssistantError(
                    "Relay confirmation timed out; outcome is uncertain"
                ) from error
            finally:
                self._commands.pop(port_id, None)
                if not pending.done.done():
                    pending.done.cancel()
                elif not pending.done.cancelled():
                    pending.done.exception()
