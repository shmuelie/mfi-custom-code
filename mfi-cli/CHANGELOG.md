# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

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
