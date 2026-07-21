# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.1.0] - 2026-07-20

### Added

- `sensor` command: `sensor list` and `sensor get <id>` for per-port readings.
- `relay` command: `relay get/set/on/off/toggle <id>` for reading and controlling
  port relays.
- `led` command: `led get` and `led set --color/--frequency` for reading and
  controlling the device LED.
- `config` command: `config list <file>` and `config get <file> <key> [--default]`
  for reading INI-style configuration files.

These expose the remaining libmfi capabilities (relay writes, LED control, config
reading) that were previously only reachable through the API.

## [1.0.1] - 2026-03-30

### Fixed

- Fixed hardware I/O using relative paths instead of absolute paths on mFi devices, causing relay control and potentially all sensor reads to fail.

## [1.0.0] - 2026-03-29

### Added

- Initial release.
- CLI11-based command-line tool for inspecting mFi device state.
- `info` subcommand to display device metadata: device ID, name, short name, hostname, and firmware version.
- `--all` flag to print full per-sensor details including readings and relay state.
- Build-time version info support.
