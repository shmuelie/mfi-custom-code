"""Distribution tests use fixture commits, never a broker or remote repository."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import stat
import struct
import subprocess
import sys
import zipfile
import zlib
from pathlib import Path
from types import ModuleType

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[2]


def _load(name: str, filename: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, PROJECT_ROOT / "home-assistant" / filename)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


distribution = _load("build_distribution", "build_distribution.py")


def _git(repository: Path, *arguments: str) -> str:
    return subprocess.run(
        [
            "git",
            "-c",
            "core.hooksPath=/dev/null",
            "-c",
            "commit.gpgsign=false",
            "-c",
            "user.name=Distribution Test",
            "-c",
            "user.email=distribution@example.invalid",
            "-C",
            str(repository),
            *arguments,
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def _commit(repository: Path) -> str:
    _git(repository, "add", "--all")
    _git(repository, "commit", "--quiet", "-m", "Local fixture snapshot")
    return _git(repository, "rev-parse", "HEAD")


@pytest.fixture
def source_repository(tmp_path: Path) -> tuple[Path, str]:
    repository = tmp_path / "source"
    repository.mkdir()
    _git(repository, "-c", "init.templateDir=", "init", "--quiet")
    for name in distribution.RUNTIME_FILES:
        destination = repository / distribution.RUNTIME_PREFIX / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if name.endswith(".py"):
            destination.write_text('"""Fixture runtime module."""\n')
        elif name.endswith(".json"):
            destination.write_text("{}\n")
        elif name.endswith(".yaml"):
            destination.write_text("{}\n")
        else:
            destination.write_bytes(
                (PROJECT_ROOT / "custom_components/mfi/brand/icon.png").read_bytes()
            )
    manifest = {
        "domain": "mfi",
        "name": "mFi",
        "version": "0.1.0",
        "codeowners": ["@shmuelie"],
        "config_flow": True,
        "dependencies": ["mqtt"],
        "documentation": "https://example.invalid/mfi",
        "issue_tracker": "https://example.invalid/mfi/issues",
        "integration_type": "device",
        "iot_class": "local_push",
        "requirements": [],
    }
    (repository / "custom_components/mfi/manifest.json").write_text(json.dumps(manifest))
    (repository / "home-assistant").mkdir()
    (repository / "home-assistant/hacs.json").write_text(json.dumps(distribution.HACS_METADATA))
    (repository / "home-assistant/README.md").write_text("# Fixture distribution\n")
    (repository / "home-assistant/CHANGELOG.md").write_text(
        "# Changes\n\n## [0.1.0] - Unreleased\n"
    )
    (repository / "LICENSE").write_bytes((PROJECT_ROOT / "LICENSE").read_bytes())
    return repository, _commit(repository)


def test_layout_versions_provenance_and_checksums(source_repository: tuple[Path, str]) -> None:
    repository, commit = source_repository
    output = distribution.build_distribution(repository, commit, "0.1.0")
    assert output == repository / "build/home-assistant" / f"0.1.0-{commit[:12]}"
    prepared = output / "repository"
    assert {path.name for path in prepared.iterdir()} == {
        "README.md",
        "CHANGELOG.md",
        "hacs.json",
        "LICENSE",
        "custom_components",
    }
    assert {path.name for path in (prepared / "custom_components").iterdir()} == {"mfi"}
    assert (prepared / "LICENSE").read_bytes() == (repository / "LICENSE").read_bytes()
    with zipfile.ZipFile(output / "mfi.zip") as archive:
        assert set(archive.namelist()) == distribution.RUNTIME_FILES | {"LICENSE"}
        assert archive.namelist() == sorted(archive.namelist())
        assert archive.testzip() is None
        assert json.loads(archive.read("manifest.json"))["version"] == "0.1.0"
        assert archive.read("LICENSE") == (repository / "LICENSE").read_bytes()
        for info in archive.infolist():
            assert info.date_time == (1980, 1, 1, 0, 0, 0)
            assert stat.S_ISREG(info.external_attr >> 16)
            assert (
                archive.read(info)
                == (prepared / "custom_components/mfi" / info.filename).read_bytes()
            )
    provenance = json.loads((output / "provenance.json").read_bytes())
    assert provenance["source_commit"] == commit
    assert provenance["source_repository"] == "shmuelie/mfi-custom-code"
    assert provenance["destination_repository"] == "shmuelie/mfi-home-assistant"
    assert provenance["version"] == "0.1.0"
    assert provenance["intended_release_tag"] == "v0.1.0"
    assert provenance["excluded_runtime_paths"] == []
    assert (
        provenance["sha256"]["mfi.zip"]
        == hashlib.sha256((output / "mfi.zip").read_bytes()).hexdigest()
    )
    checksums = {}
    for line in (output / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        checksums[name] = digest
        assert digest == hashlib.sha256((output / name).read_bytes()).hexdigest()
    assert {
        path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()
    } == (checksums.keys() | {"SHA256SUMS"})
    assert {name: digest for name, digest in checksums.items() if name != "provenance.json"} == (
        provenance["sha256"]
    )
    assert _git(repository, "status", "--porcelain", "--untracked-files=no") == ""


def test_current_source_snapshot_packages_complete_runtime(tmp_path: Path) -> None:
    repository = tmp_path / "actual-source"
    runtime = repository / "custom_components/mfi"
    shutil.copytree(
        PROJECT_ROOT / "custom_components/mfi",
        runtime,
        symlinks=True,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
    )
    for source in (*distribution.ROOT_FILES, "home-assistant/build_distribution.py"):
        destination = repository / source
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(PROJECT_ROOT / source, destination, follow_symlinks=False)
    source_files = {
        path.relative_to(runtime).as_posix(): path.read_bytes()
        for path in runtime.rglob("*")
        if path.is_file()
    }
    assert source_files.keys() == distribution.RUNTIME_FILES
    version = json.loads(source_files["manifest.json"])["version"]
    _git(repository, "-c", "init.templateDir=", "init", "--quiet")
    commit = _commit(repository)
    result = subprocess.run(
        [
            sys.executable,
            str(repository / "home-assistant/build_distribution.py"),
            "--source-commit",
            commit,
            "--version",
            version,
        ],
        cwd=repository,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    output = repository / "build/home-assistant" / f"{version}-{commit[:12]}"
    prepared = output / "repository"
    source_files["LICENSE"] = (repository / "LICENSE").read_bytes()
    with zipfile.ZipFile(output / "mfi.zip") as archive:
        assert set(archive.namelist()) == source_files.keys()
        assert archive.testzip() is None
        for name, contents in source_files.items():
            assert archive.read(name) == contents
            assert (prepared / "custom_components/mfi" / name).read_bytes() == contents
    for source, destination in distribution.ROOT_FILES.items():
        assert (prepared / destination).read_bytes() == (repository / source).read_bytes()
    assert {
        path.relative_to(prepared).as_posix() for path in prepared.rglob("*") if path.is_file()
    } == (
        set(distribution.ROOT_FILES.values())
        | {distribution.RUNTIME_PREFIX + name for name in source_files}
    )
    provenance = json.loads((output / "provenance.json").read_bytes())
    assert provenance["source_commit"] == commit
    assert provenance["version"] == version
    assert provenance["intended_release_tag"] == f"v{version}"
    assert provenance["excluded_runtime_paths"] == []
    checksums = {}
    for line in (output / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        assert hashlib.sha256((output / name).read_bytes()).hexdigest() == digest
        checksums[name] = digest
    assert provenance["sha256"] == {
        name: digest for name, digest in checksums.items() if name != "provenance.json"
    }
    assert {
        path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()
    } == (checksums.keys() | {"SHA256SUMS"})
    assert _git(repository, "status", "--porcelain", "--untracked-files=no") == ""


def test_committed_source_not_head_index_or_worktree(source_repository: tuple[Path, str]) -> None:
    repository, commit = source_repository
    source = repository / "custom_components/mfi/energy.py"
    original = source.read_bytes()
    source.write_text('"""Newer committed content."""\n')
    _commit(repository)
    source.write_text('"""Staged content."""\n')
    _git(repository, "add", "custom_components/mfi/energy.py")
    source.write_text('"""Unstaged content."""\n')
    output = distribution.build_distribution(repository, commit, "0.1.0")
    with zipfile.ZipFile(output / "mfi.zip") as archive:
        assert archive.read("energy.py") == original
    assert source.read_text() == '"""Unstaged content."""\n'
    assert "Staged content" in _git(repository, "show", ":custom_components/mfi/energy.py")


def test_tracked_and_untracked_exclusions(source_repository: tuple[Path, str]) -> None:
    repository, _ = source_repository
    excluded = {
        ".env",
        "secrets.json",
        "secrets.py",
        "private.pem",
        "test_sensor.py",
        "__pycache__/energy.cpython-314.pyc",
        "build/mfi-cli",
        "native.so",
        "brand/icon.svg",
    }
    for name in excluded:
        path = repository / "custom_components/mfi" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"fixture-only excluded payload")
    (repository / "mfi-cli").write_bytes(b"\x7fELF")
    commit = _commit(repository)
    (repository / "custom_components/mfi/untracked.py").write_text("raise RuntimeError\n")
    output = distribution.build_distribution(repository, commit, "0.1.0")
    provenance = json.loads((output / "provenance.json").read_bytes())
    assert provenance["excluded_runtime_paths"] == sorted(
        distribution.RUNTIME_PREFIX + name for name in excluded
    )
    with zipfile.ZipFile(output / "mfi.zip") as archive:
        assert set(archive.namelist()) == distribution.RUNTIME_FILES | {"LICENSE"}
    assert not (output / "repository/mfi-cli").exists()


@pytest.mark.parametrize("version", ["0.1.1", "1.0.0"])
def test_version_mismatch_rejected(source_repository: tuple[Path, str], version: str) -> None:
    repository, commit = source_repository
    with pytest.raises(distribution.DistributionError, match="manifest.json version"):
        distribution.build_distribution(repository, commit, version)
    assert not (repository / "build").exists()


@pytest.mark.parametrize(
    "version", ["../outside", "/tmp/outside", "v0.1.0", "01.1.0", "0.1.0-rc.1"]
)
def test_version_cannot_redirect_output(source_repository: tuple[Path, str], version: str) -> None:
    repository, commit = source_repository
    with pytest.raises(distribution.DistributionError, match="stable"):
        distribution.build_distribution(repository, commit, version)
    assert not (repository / "build").exists()


@pytest.mark.parametrize("commit", ["HEAD", "main", "deadbeef", "--all", "../outside"])
def test_requires_explicit_full_commit(source_repository: tuple[Path, str], commit: str) -> None:
    repository, _ = source_repository
    with pytest.raises(distribution.DistributionError, match="full lowercase"):
        distribution.build_distribution(repository, commit, "0.1.0")
    assert not (repository / "build").exists()


@pytest.mark.parametrize(
    ("path", "content", "message"),
    [
        ("home-assistant/CHANGELOG.md", "## [0.0.9] - Old\n", "CHANGELOG"),
        ("home-assistant/CHANGELOG.md", "## [0.1.0]\n## [0.1.0]\n", "CHANGELOG"),
        ("home-assistant/CHANGELOG.md", "## [0.2.0-beta]\n## [0.1.0]\n", "CHANGELOG"),
        ("home-assistant/hacs.json", '{"content_in_root": true}', "hacs.json"),
        ("custom_components/mfi/strings.json", "[]", "JSON object"),
        ("custom_components/mfi/energy.py", "\0ELF", "null bytes"),
        ("custom_components/mfi/brand/icon.png", "not a png", "256 x 256 PNG"),
        ("LICENSE", "Unreviewed license", "license"),
    ],
)
def test_invalid_metadata_rejected(
    source_repository: tuple[Path, str], path: str, content: str, message: str
) -> None:
    repository, _ = source_repository
    (repository / path).write_text(content)
    commit = _commit(repository)
    with pytest.raises((distribution.DistributionError, SyntaxError), match=message):
        distribution.build_distribution(repository, commit, "0.1.0")
    assert not (repository / "build").exists()


def test_required_runtime_must_exist_in_commit(source_repository: tuple[Path, str]) -> None:
    repository, _ = source_repository
    source = repository / "custom_components/mfi/sensor.py"
    source.unlink()
    commit = _commit(repository)
    source.write_text('"""Untracked replacement must not be packaged."""\n')
    with pytest.raises(distribution.DistributionError, match="missing required files.*sensor.py"):
        distribution.build_distribution(repository, commit, "0.1.0")


def test_source_submodule_rejected(source_repository: tuple[Path, str]) -> None:
    repository, commit = source_repository
    _git(
        repository,
        "update-index",
        "--add",
        "--cacheinfo",
        f"160000,{commit},custom_components/mfi/vendor",
    )
    _git(repository, "commit", "--quiet", "-m", "Fixture submodule")
    commit = _git(repository, "rev-parse", "HEAD")
    with pytest.raises(distribution.DistributionError, match="submodules"):
        distribution.build_distribution(repository, commit, "0.1.0")
    assert not (repository / "build").exists()


def test_annotated_tag_object_is_not_a_commit(source_repository: tuple[Path, str]) -> None:
    repository, _ = source_repository
    _git(repository, "-c", "tag.gpgsign=false", "tag", "-a", "fixture", "-m", "Fixture tag")
    tag_object = _git(repository, "rev-parse", "fixture")
    with pytest.raises(distribution.DistributionError, match="not a tag object"):
        distribution.build_distribution(repository, tag_object, "0.1.0")


@pytest.mark.parametrize("size", [0, distribution.MAX_FILE_BYTES + 1])
def test_empty_and_oversized_files_rejected(source_repository: tuple[Path, str], size: int) -> None:
    repository, _ = source_repository
    (repository / "custom_components/mfi/energy.py").write_bytes(b"x" * size)
    commit = _commit(repository)
    with pytest.raises(distribution.DistributionError, match="Empty or oversized"):
        distribution.build_distribution(repository, commit, "0.1.0")
    assert not (repository / "build").exists()


@pytest.mark.parametrize(
    "name",
    ["custom_components/mfi/energy.py", "custom_components/mfi/excluded.link", "LICENSE"],
)
def test_source_symlink_rejected(source_repository: tuple[Path, str], name: str) -> None:
    repository, _ = source_repository
    path = repository / name
    path.unlink(missing_ok=True)
    path.symlink_to(repository / "home-assistant/README.md")
    commit = _commit(repository)
    with pytest.raises(distribution.DistributionError, match="Symlinks"):
        distribution.build_distribution(repository, commit, "0.1.0")
    assert not (repository / "build").exists()


@pytest.mark.parametrize("ancestor", ["build", "build/home-assistant"])
@pytest.mark.parametrize("dangling", [False, True])
def test_output_symlink_ancestor_rejected(
    source_repository: tuple[Path, str], tmp_path: Path, ancestor: str, dangling: bool
) -> None:
    repository, commit = source_repository
    outside = tmp_path / "outside"
    if not dangling:
        outside.mkdir()
        (outside / "keep").write_text("untouched")
    path = repository / ancestor
    path.parent.mkdir(parents=True, exist_ok=True)
    path.symlink_to(outside, target_is_directory=True)
    with pytest.raises(distribution.DistributionError, match="real directory"):
        distribution.build_distribution(repository, commit, "0.1.0")
    if dangling:
        assert not outside.exists()
    else:
        assert {child.name for child in outside.iterdir()} == {"keep"}
        assert (outside / "keep").read_text() == "untouched"


@pytest.mark.parametrize("existing", ["directory", "file", "symlink"])
def test_never_overwrites_existing_output(
    source_repository: tuple[Path, str], tmp_path: Path, existing: str
) -> None:
    repository, commit = source_repository
    output = repository / "build/home-assistant" / f"0.1.0-{commit[:12]}"
    output.parent.mkdir(parents=True)
    if existing == "directory":
        output.mkdir()
        (output / "keep").write_text("untouched")
    elif existing == "file":
        output.write_text("untouched")
    else:
        output.symlink_to(tmp_path / "does-not-exist", target_is_directory=True)
    with pytest.raises(distribution.DistributionError, match="refusing to overwrite"):
        distribution.build_distribution(repository, commit, "0.1.0")
    if existing == "directory":
        assert {path.name for path in output.iterdir()} == {"keep"}
    elif existing == "file":
        assert output.read_text() == "untouched"
    else:
        assert output.is_symlink()
        assert not (tmp_path / "does-not-exist").exists()


def test_repeat_build_does_not_change_artifacts(source_repository: tuple[Path, str]) -> None:
    repository, commit = source_repository
    output = distribution.build_distribution(repository, commit, "0.1.0")
    original = {
        path.relative_to(output): path.read_bytes() for path in output.rglob("*") if path.is_file()
    }
    with pytest.raises(distribution.DistributionError, match="already exists"):
        distribution.build_distribution(repository, commit, "0.1.0")
    assert original == {
        path.relative_to(output): path.read_bytes() for path in output.rglob("*") if path.is_file()
    }


def test_same_commit_produces_identical_artifacts(source_repository: tuple[Path, str]) -> None:
    repository, commit = source_repository
    first = distribution.build_distribution(repository, commit, "0.1.0")
    saved = first.with_name("saved-fixture-build")
    first.rename(saved)
    second = distribution.build_distribution(repository, commit, "0.1.0")
    for name in ("mfi.zip", "provenance.json", "SHA256SUMS"):
        assert (second / name).read_bytes() == (saved / name).read_bytes()


def test_cli_errors_are_explicit() -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(PROJECT_ROOT / "home-assistant/build_distribution.py"),
            "--source-commit",
            "HEAD",
            "--version",
            "0.1.0",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 1
    assert "Distribution build failed:" in result.stderr
    assert result.stdout == ""


def test_original_png_matches_svg_and_has_valid_chunks() -> None:
    generator = _load("generate_icon", "generate_icon.py")
    image = (PROJECT_ROOT / "custom_components/mfi/brand/icon.png").read_bytes()
    assert image == generator.render(PROJECT_ROOT / "home-assistant/assets/icon.svg")
    assert image[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    compressed = bytearray()
    while offset < len(image):
        length = struct.unpack(">I", image[offset : offset + 4])[0]
        kind = image[offset + 4 : offset + 8]
        data = image[offset + 8 : offset + 8 + length]
        checksum = struct.unpack(">I", image[offset + 8 + length : offset + 12 + length])[0]
        assert checksum == zlib.crc32(kind + data)
        if kind == b"IDAT":
            compressed.extend(data)
        offset += 12 + length
    assert offset == len(image)
    pixels = zlib.decompress(compressed)
    assert len(pixels) == 256 * (1 + 256 * 4)
    assert pixels[1:5] == b"\0\0\0\0"
