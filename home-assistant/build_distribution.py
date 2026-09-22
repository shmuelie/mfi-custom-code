#!/usr/bin/env python3
"""Prepare a local, integration-only snapshot from an explicit Git commit."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import stat
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

RUNTIME_PREFIX = "custom_components/mfi/"
RUNTIME_FILES = frozenset(
    {
        "__init__.py",
        "manifest.json",
        "const.py",
        "config_flow.py",
        "source.py",
        "energy.py",
        "sensor.py",
        "storage.py",
        "repairs.py",
        "diagnostics.py",
        "protocol.py",
        "mqtt.py",
        "entity.py",
        "switch.py",
        "migration.py",
        "services.yaml",
        "strings.json",
        "translations/en.json",
        "brand/icon.png",
    }
)
ROOT_FILES = {
    "home-assistant/README.md": "README.md",
    "home-assistant/CHANGELOG.md": "CHANGELOG.md",
    "home-assistant/hacs.json": "hacs.json",
    "LICENSE": "LICENSE",
}
HACS_METADATA = {
    "name": "mFi",
    "content_in_root": False,
    "zip_release": True,
    "filename": "mfi.zip",
    "hide_default_branch": True,
    "homeassistant": "2026.9.2",
}
VERSION_PATTERN = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
MAX_FILE_BYTES = 1024 * 1024


class DistributionError(Exception):
    """A snapshot cannot safely be prepared."""


def _git(repository: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "--no-replace-objects", "-C", str(repository), *arguments],
        check=False,
        capture_output=True,
    )
    if result.returncode:
        raise DistributionError(
            f"Git {' '.join(arguments)} failed: "
            f"{result.stderr.decode('utf-8', errors='replace').strip()}"
        )
    return result.stdout


def _json_object(data: bytes, name: str) -> dict[str, object]:
    value = json.loads(data)
    if not isinstance(value, dict):
        raise DistributionError(f"{name} must contain a JSON object")
    return value


def _snapshot(repository: Path, source_commit: str) -> tuple[dict[str, bytes], list[str]]:
    requested = {
        **{RUNTIME_PREFIX + name: RUNTIME_PREFIX + name for name in RUNTIME_FILES},
        **ROOT_FILES,
    }
    entries = _git(
        repository,
        "ls-tree",
        "-r",
        "-z",
        "--full-tree",
        source_commit,
        "--",
        "custom_components/mfi",
        *ROOT_FILES,
    )
    files: dict[str, bytes] = {}
    excluded: list[str] = []
    for entry in entries.split(b"\0"):
        if not entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        mode, kind, object_id = metadata.decode("ascii").split()
        path = raw_path.decode("utf-8")
        if mode not in {"100644", "100755"} or kind != "blob":
            raise DistributionError(f"Symlinks and submodules are forbidden: {path}")
        if path not in requested:
            excluded.append(path)
            continue
        size = int(_git(repository, "cat-file", "-s", object_id))
        if not 0 < size <= MAX_FILE_BYTES:
            raise DistributionError(f"Empty or oversized source file: {path}")
        files[requested[path]] = _git(repository, "cat-file", "blob", object_id)
    missing = set(requested.values()) - files.keys()
    if missing:
        raise DistributionError(f"Commit is missing required files: {', '.join(sorted(missing))}")
    return files, sorted(excluded)


def _validate(files: dict[str, bytes], version: str) -> None:
    for name, data in files.items():
        if name.endswith(".py"):
            ast.parse(data.decode("utf-8"), filename=name)
        elif name.endswith(".json"):
            _json_object(data, name)
        elif not name.endswith(".png"):
            text = data.decode("utf-8")
            if "\0" in text:
                raise DistributionError(f"NUL byte in text file: {name}")
    manifest = _json_object(files[RUNTIME_PREFIX + "manifest.json"], "manifest.json")
    expected_manifest = {
        "domain": "mfi",
        "name": "mFi",
        "version": version,
        "config_flow": True,
        "dependencies": ["mqtt"],
        "integration_type": "device",
        "iot_class": "local_push",
        "requirements": [],
    }
    for key, expected in expected_manifest.items():
        if manifest.get(key) != expected:
            raise DistributionError(f"manifest.json {key} must be {expected!r}")
    for key in ("documentation", "issue_tracker"):
        value = manifest.get(key)
        if not isinstance(value, str) or not value.startswith("https://"):
            raise DistributionError(f"manifest.json {key} must be an HTTPS URL")
    owners = manifest.get("codeowners")
    if (
        not isinstance(owners, list)
        or not owners
        or any(not isinstance(owner, str) or not owner.startswith("@") for owner in owners)
    ):
        raise DistributionError("manifest.json must name GitHub codeowners")
    hacs = _json_object(files["hacs.json"], "hacs.json")
    # Comparing JSON also distinguishes booleans from integers.
    if json.dumps(hacs, sort_keys=True) != json.dumps(HACS_METADATA, sort_keys=True):
        raise DistributionError("hacs.json does not match the reviewed distribution contract")
    releases = [
        heading
        for heading in re.findall(
            r"^## \[([^\]\r\n]+)\]", files["CHANGELOG.md"].decode("utf-8"), re.MULTILINE
        )
        if heading != "Unreleased"
    ]
    if not releases or releases[0] != version or len(set(releases)) != len(releases):
        raise DistributionError("Latest unique CHANGELOG.md version must match --version")
    icon = files[RUNTIME_PREFIX + "brand/icon.png"]
    if (
        len(icon) < 33
        or icon[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"
        or struct.unpack(">II", icon[16:24]) != (256, 256)
    ):
        raise DistributionError("brand/icon.png must be a 256 x 256 PNG")
    if not files["LICENSE"].decode("utf-8").startswith("MIT License\n"):
        raise DistributionError("Review applicable license before changing distribution licensing")


def _create_output(repository: Path, name: str) -> Path:
    parent = repository
    for component in ("build", "home-assistant"):
        parent = parent / component
        try:
            parent.mkdir()
        except FileExistsError:
            pass
        if not stat.S_ISDIR(parent.lstat().st_mode):
            raise DistributionError(f"Output ancestor must be a real directory: {parent}")
    output = parent / name
    try:
        output.mkdir()
    except FileExistsError as error:
        raise DistributionError(
            f"Output already exists; refusing to overwrite: {output}"
        ) from error
    return output


def build_distribution(repository: Path, source_commit: str, version: str) -> Path:
    """Read only committed, allowlisted blobs; create a fresh confined output."""
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", source_commit):
        raise DistributionError("--source-commit must be a full lowercase Git commit SHA")
    if not re.fullmatch(VERSION_PATTERN, version):
        raise DistributionError("--version must be a stable MAJOR.MINOR.PATCH version")
    repository = repository.resolve(strict=True)
    top_level = Path(_git(repository, "rev-parse", "--show-toplevel").decode().strip())
    if top_level.resolve() != repository:
        raise DistributionError("Builder must run from the source repository root")
    commit = (
        _git(repository, "rev-parse", "--verify", f"{source_commit}^{{commit}}").decode().strip()
    )
    if commit != source_commit:
        raise DistributionError("--source-commit must identify a commit, not a tag object")
    files, excluded = _snapshot(repository, source_commit)
    _validate(files, version)
    # The independently downloadable ZIP must carry the applicable license too.
    files[RUNTIME_PREFIX + "LICENSE"] = files["LICENSE"]
    output = _create_output(repository, f"{version}-{source_commit[:12]}")
    prepared = output / "repository"
    for name, data in sorted(files.items()):
        destination = prepared / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as stream:
            stream.write(data)
    archive = output / "mfi.zip"
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
        for name in sorted(RUNTIME_FILES | {"LICENSE"}):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            bundle.writestr(info, files[RUNTIME_PREFIX + name])
    checksums = {
        f"repository/{name}": hashlib.sha256(data).hexdigest()
        for name, data in sorted(files.items())
    }
    checksums["mfi.zip"] = hashlib.sha256(archive.read_bytes()).hexdigest()
    provenance = {
        "schema_version": 1,
        "source_repository": "shmuelie/mfi-custom-code",
        "source_commit": source_commit,
        "destination_repository": "shmuelie/mfi-home-assistant",
        "version": version,
        "intended_release_tag": f"v{version}",
        "sha256": checksums.copy(),
        "excluded_runtime_paths": excluded,
    }
    provenance_bytes = (json.dumps(provenance, indent=2, sort_keys=True) + "\n").encode()
    with (output / "provenance.json").open("xb") as stream:
        stream.write(provenance_bytes)
    checksums["provenance.json"] = hashlib.sha256(provenance_bytes).hexdigest()
    with (output / "SHA256SUMS").open("x", encoding="utf-8", newline="\n") as stream:
        stream.writelines(f"{digest}  {name}\n" for name, digest in sorted(checksums.items()))
    return output


def main() -> int:
    """Command-line entry point; deliberately no publication or output override."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-commit", required=True, help="Full committed source SHA (not HEAD)"
    )
    parser.add_argument("--version", required=True, help="Expected release version, e.g. 0.1.0")
    arguments = parser.parse_args()
    try:
        output = build_distribution(
            Path(__file__).resolve().parent.parent, arguments.source_commit, arguments.version
        )
    except (DistributionError, OSError, ValueError, SyntaxError) as error:
        print(f"Distribution build failed: {error}", file=sys.stderr)
        return 1
    print(f"Prepared local distribution: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
