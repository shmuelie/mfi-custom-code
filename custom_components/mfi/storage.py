"""Versioned checkpoints whose successful writes acknowledge durable totals."""

import asyncio
import math
import re
from collections.abc import Mapping
from dataclasses import dataclass, replace
from decimal import Decimal, InvalidOperation
from functools import partial
from pathlib import Path

from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.json import save_json
from homeassistant.util.hass_dict import HassKey
from homeassistant.util.json import load_json

from .const import STORAGE_VERSION

_ID = re.compile(r"[0-9a-f]{32}")
_WRITE_LOCKS: HassKey[dict[str, asyncio.Lock]] = HassKey("mfi_checkpoint_locks")


async def async_complete_io[T](job: asyncio.Future[T]) -> T:
    """Keep a lock until I/O or its commit acknowledgement finishes."""
    cancelled = False
    while not job.done():
        try:
            await asyncio.shield(job)
        except asyncio.CancelledError:
            cancelled = True
    result = job.result()
    if cancelled:
        raise asyncio.CancelledError
    return result


class StorageError(HomeAssistantError):
    """A checkpoint could not be read, validated, or durably saved."""


def _string(data: Mapping[str, object], key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        raise StorageError(f"Invalid checkpoint field: {key}")
    return value


@dataclass(frozen=True)
class PortSnapshot:
    """A source binding and its committed total."""

    binding_id: str
    registry_id: str
    unique_id: str
    entity_id: str
    name: str
    total: Decimal = Decimal(0)
    excluded: bool = False

    def as_dict(self) -> dict[str, object]:
        return {
            "binding_id": self.binding_id,
            "registry_id": self.registry_id,
            "unique_id": self.unique_id,
            "entity_id": self.entity_id,
            "name": self.name,
            "total": str(self.total),
            "excluded": self.excluded,
        }

    @classmethod
    def from_dict(cls, data: Mapping[str, object]) -> PortSnapshot:
        binding_id = _string(data, "binding_id")
        if _ID.fullmatch(binding_id) is None:
            raise StorageError("Invalid port binding ID")
        try:
            total = Decimal(_string(data, "total"))
        except InvalidOperation as error:
            raise StorageError("Invalid saved energy total") from error
        if not total.is_finite() or not math.isfinite(float(total)) or total < 0:
            raise StorageError("Saved energy must be finite and nonnegative")
        excluded = data.get("excluded")
        if not isinstance(excluded, bool):
            raise StorageError("Invalid port exclusion")
        entity_id = _string(data, "entity_id")
        if not entity_id.startswith("sensor."):
            raise StorageError("Invalid source entity domain")
        return cls(
            binding_id,
            _string(data, "registry_id"),
            _string(data, "unique_id"),
            entity_id,
            _string(data, "name"),
            total,
            excluded,
        )


@dataclass(frozen=True)
class Checkpoint:
    """An immutable generation, including excluded and orphaned bindings."""

    storage_id: str
    generation: int
    ports: tuple[PortSnapshot, ...]
    ignored_sources: tuple[str, ...] = ()

    def as_dict(self) -> dict[str, object]:
        return {
            "version": STORAGE_VERSION,
            "storage_id": self.storage_id,
            "generation": self.generation,
            "ports": [port.as_dict() for port in self.ports],
            "ignored_sources": list(self.ignored_sources),
        }

    @classmethod
    def from_dict(cls, data: Mapping[str, object], storage_id: str) -> Checkpoint:
        if type(data.get("version")) is not int or data["version"] != STORAGE_VERSION:
            raise StorageError("Unsupported checkpoint version")
        if data.get("storage_id") != storage_id:
            raise StorageError("Checkpoint belongs to another integration entry")
        generation = data.get("generation")
        if type(generation) is not int or generation < 0:
            raise StorageError("Invalid checkpoint generation")
        raw_ports = data.get("ports")
        if not isinstance(raw_ports, list):
            raise StorageError("Invalid checkpoint ports")
        ports: list[PortSnapshot] = []
        for raw in raw_ports:
            if not isinstance(raw, dict) or any(not isinstance(key, str) for key in raw):
                raise StorageError("Invalid checkpoint port")
            ports.append(PortSnapshot.from_dict(raw))
        if len({port.binding_id for port in ports}) != len(ports):
            raise StorageError("Duplicate port binding")
        if len({port.registry_id for port in ports}) != len(ports):
            raise StorageError("Duplicate source binding")
        ignored = data.get("ignored_sources")
        if not isinstance(ignored, list) or any(
            not isinstance(item, str) or not item for item in ignored
        ):
            raise StorageError("Invalid ignored sources")
        if any(port.registry_id in ignored for port in ports):
            raise StorageError("A bound source cannot also be ignored")
        return cls(storage_id, generation, tuple(ports), tuple(ignored))

    def next_generation(self, ports: tuple[PortSnapshot, ...]) -> Checkpoint:
        return replace(self, generation=self.generation + 1, ports=ports)


class CheckpointStore:
    """Use HA's error-propagating atomic JSON writer, not Store.async_save."""

    def __init__(self, hass: HomeAssistant, storage_id: str) -> None:
        if _ID.fullmatch(storage_id) is None:
            raise StorageError("Invalid storage ID")
        self.hass = hass
        self.storage_id = storage_id
        self.path = Path(hass.config.path(".storage", f"mfi_energy.{storage_id}"))
        self._lock = hass.data.setdefault(_WRITE_LOCKS, {}).setdefault(storage_id, asyncio.Lock())

    async def async_load(self) -> Checkpoint:
        try:
            async with self._lock:
                raw = await async_complete_io(
                    self.hass.async_add_executor_job(partial(load_json, self.path, default=None))
                )
        except HomeAssistantError as error:
            raise StorageError("Could not read energy checkpoint") from error
        if not isinstance(raw, dict):
            raise StorageError("Energy checkpoint is missing or invalid; restore it from backup")
        return Checkpoint.from_dict(raw, self.storage_id)

    def _save(self, checkpoint: Checkpoint) -> None:
        data = checkpoint.as_dict()
        Checkpoint.from_dict(data, self.storage_id)
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            save_json(str(self.path), data, private=True, atomic_writes=True)
        except (HomeAssistantError, OSError) as error:
            raise StorageError("Could not save energy checkpoint") from error

    async def async_save(self, checkpoint: Checkpoint) -> None:
        async with self._lock:
            await async_complete_io(self.hass.async_add_executor_job(self._save, checkpoint))

    async def async_remove(self) -> None:
        async with self._lock:
            await async_complete_io(
                self.hass.async_add_executor_job(partial(self.path.unlink, missing_ok=True))
            )
