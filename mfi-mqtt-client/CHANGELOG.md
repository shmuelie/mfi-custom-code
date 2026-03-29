# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- Fixed `--config` file validation failing on mFi devices due to uclibc using the `statx` syscall.

## [1.0.1] - 2025-01-03

### Changed

- Use hostname as unique device ID for more stable entity identification.
- Report the whole board as a single device with many sensors instead of separate devices.

### Added

- Input validators for MQTT connection options.
- Default MQTT broker port (1883).
- Config file support for persistent settings.
- spdlog-based structured logging.

### Fixed

- Fix log format string issues.
- Fix versioning in build metadata.

## [1.0.0] - 2024-02-03

### Added

- Initial release.
- MQTT client that exposes an mFi board to Home Assistant via MQTT auto-discovery.
- Per-port entities for power sensor, current sensor, voltage sensor, and relay switch.
- Automatic publishing of board metadata as device info: manufacturer, model, software version, model ID, and configuration URL.
- MQTT connection handling with polling and message processing.
- Reconnect/backoff handling and availability/LWT support.
- CLI options for broker server, port, username, password, polling rate, and log level.
