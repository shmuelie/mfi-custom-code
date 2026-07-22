# mfi-update

Self-update support for the mFi tools: fetch the latest release for a given tool
from GitHub Releases and atomically replace the running binary in place.

Used by `mfi-cli`, `mfi-mqtt-client`, and `mfi-rest-server`. See
[Updating](../docs/updating.md) for the end-user behavior and configuration.

## Overview

- **`semver`** — parse/compare `MAJOR.MINOR.PATCH` (true numeric ordering).
- **`config`** — repo, proxy, insecure, bin dir, interval, enable; resolves proxy
  from `http_proxy`/`https_proxy` when unset.
- **`downloader`** — builds and runs a `wget`/`curl` command (proxy +
  `--no-check-certificate`), so TLS is the firmware's, not ours.
- **`release`** — parse GitHub `matching-refs` and release JSON to find the latest
  tag and the asset download URL for a tool.
- **`updater`** — orchestrates check → download → ELF-validate → atomic rename →
  `execv` re-exec, with injectable fetch/download/apply seams for testing.
- **`periodic_updater`** — drives interval-based checks from a long-running loop.

## Dependencies

### External

- [nlohmann_json](https://github.com/nlohmann/json) — GitHub API JSON parsing

## Details

- **Language**: C++20
- **Namespace**: `mfi_update`
- **Version**: 1.0.0
