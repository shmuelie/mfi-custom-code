"""Checkpoint validation and real atomic writes in a temporary directory."""

import asyncio
import threading
from dataclasses import replace
from decimal import Decimal
from pathlib import Path
from unittest.mock import patch
from uuid import uuid4

import pytest
from homeassistant.util.file import WriteError

from custom_components.mfi.storage import Checkpoint, CheckpointStore, PortSnapshot, StorageError


@pytest.fixture
def checkpoint():
    return Checkpoint(
        uuid4().hex,
        0,
        (
            PortSnapshot(
                uuid4().hex, uuid4().hex, "power_1", "sensor.power_1", "Port 1", Decimal("1.5")
            ),
        ),
    )


async def test_roundtrip_and_generation(isolated_hass, checkpoint):
    store = CheckpointStore(isolated_hass, checkpoint.storage_id)
    await store.async_save(checkpoint)
    assert await store.async_load() == checkpoint
    assert store.path.stat().st_mode & 0o077 == 0
    updated = checkpoint.next_generation((replace(checkpoint.ports[0], total=Decimal("1.6")),))
    await store.async_save(updated)
    assert await store.async_load() == updated


async def test_failed_write_preserves_previous_checkpoint(isolated_hass, checkpoint):
    store = CheckpointStore(isolated_hass, checkpoint.storage_id)
    await store.async_save(checkpoint)
    with patch("custom_components.mfi.storage.save_json", side_effect=WriteError("disk full")):
        with pytest.raises(StorageError):
            await store.async_save(checkpoint.next_generation(()))
    assert await store.async_load() == checkpoint


async def test_missing_checkpoint_is_not_zero(isolated_hass, checkpoint):
    with pytest.raises(StorageError, match="missing"):
        await CheckpointStore(isolated_hass, checkpoint.storage_id).async_load()


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("version", 2),
        ("version", True),
        ("generation", -1),
        ("generation", True),
        ("storage_id", "another"),
        ("ports", None),
        ("ignored_sources", [1]),
    ],
)
def test_invalid_envelope(checkpoint, field, value):
    data = checkpoint.as_dict()
    data[field] = value
    with pytest.raises(StorageError):
        Checkpoint.from_dict(data, checkpoint.storage_id)


@pytest.mark.parametrize("total", ["NaN", "Infinity", "-0.01", "invalid"])
def test_invalid_total(checkpoint, total):
    data = checkpoint.as_dict()
    data["ports"][0]["total"] = total
    with pytest.raises(StorageError):
        Checkpoint.from_dict(data, checkpoint.storage_id)


def test_duplicate_binding_rejected(checkpoint):
    with pytest.raises(StorageError, match="Duplicate"):
        Checkpoint.from_dict(
            replace(checkpoint, ports=checkpoint.ports * 2).as_dict(), checkpoint.storage_id
        )


@pytest.mark.parametrize("storage_id", ["../escape", "", "invalid", "a" * 33])
def test_storage_path_is_scoped(isolated_hass, storage_id):
    with pytest.raises(StorageError):
        CheckpointStore(isolated_hass, storage_id)


@pytest.mark.parametrize("cancel_count", [1, 2, 3])
async def test_canceled_write_cannot_race_reloaded_store(isolated_hass, checkpoint, cancel_count):
    store = CheckpointStore(isolated_hass, checkpoint.storage_id)
    started = threading.Event()
    release = threading.Event()
    save = store._save

    def slow_save(snapshot):
        started.set()
        if not release.wait(timeout=10):
            raise RuntimeError("Test did not release the writer")
        save(snapshot)

    with patch.object(store, "_save", new=slow_save):
        first = asyncio.create_task(store.async_save(checkpoint))
        await isolated_hass.async_add_executor_job(started.wait)
        later = checkpoint.next_generation((replace(checkpoint.ports[0], total=Decimal("2")),))
        reloaded = CheckpointStore(isolated_hass, checkpoint.storage_id)
        try:
            for _ in range(cancel_count):
                first.cancel()
                await asyncio.sleep(0)
            second = asyncio.create_task(reloaded.async_save(later))
            await asyncio.sleep(0)
            assert not first.done()
            assert not second.done()
        finally:
            release.set()
        with pytest.raises(asyncio.CancelledError):
            await first
        await second
    assert await reloaded.async_load() == later


async def test_corrupt_json_is_not_overwritten(isolated_hass, checkpoint):
    store = CheckpointStore(isolated_hass, checkpoint.storage_id)
    await store.async_save(checkpoint)
    await isolated_hass.async_add_executor_job(store.path.write_text, "{broken")
    with pytest.raises(StorageError, match="read"):
        await store.async_load()
    assert await isolated_hass.async_add_executor_job(store.path.read_text) == "{broken"


async def test_repeatedly_canceled_remove_cannot_delete_a_newer_checkpoint(
    isolated_hass, checkpoint
):
    store = CheckpointStore(isolated_hass, checkpoint.storage_id)
    await store.async_save(checkpoint)
    started = threading.Event()
    release = threading.Event()
    unlink = Path.unlink

    def delayed_unlink(path, missing_ok=False):
        if path == store.path:
            started.set()
            if not release.wait(timeout=10):
                raise RuntimeError("Test did not release the removal")
        unlink(path, missing_ok=missing_ok)

    later = checkpoint.next_generation((replace(checkpoint.ports[0], total=Decimal("2")),))
    with patch.object(Path, "unlink", new=delayed_unlink):
        removing = asyncio.create_task(store.async_remove())
        await isolated_hass.async_add_executor_job(started.wait)
        try:
            for _ in range(3):
                removing.cancel()
                await asyncio.sleep(0)
            saving = asyncio.create_task(
                CheckpointStore(isolated_hass, checkpoint.storage_id).async_save(later)
            )
            await asyncio.sleep(0)
            assert not removing.done()
            assert not saving.done()
        finally:
            release.set()
        with pytest.raises(asyncio.CancelledError):
            await removing
        await saving
    assert await store.async_load() == later
