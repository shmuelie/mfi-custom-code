# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Default-enabled device-wide CPU utilization and memory total, available, used,
  and utilization diagnostic sensors on the existing Home Assistant device.
- Independent 10-second system sampling, 60-second successful unchanged refresh,
  and 180-second expiration, with CLI/config timing options and an opt-out.
- Checked proc-based statistics with CPU baseline warm-up, documented legacy
  memory availability estimation, and independent CPU/memory failure handling.
- Non-retained system telemetry, per-sensor availability, and fresh sampling
  after reconnect without delaying outlet readiness for CPU warm-up.

### Fixed

- Delayed initial offline acknowledgements no longer leave healthy outlets
  unavailable until the next CPU/memory sampling interval on startup or reconnect.

## [2.0.0] - 2026-09-12

### Breaking changes

- Numeric power state is no longer retained, and invalid or stale channels become
  unavailable instead of continuing to expose a cached reading. New subscribers
  may wait for the next successful power refresh.
- Existing retained power records require separately approved, exact-topic
  cleanup; the client does not erase them automatically. See
  [the migration runbook](../docs/mqtt-freshness-migration.md).
- Zero polling intervals and polling slower than the power refresh interval are
  rejected. Expiration must allow at least three refresh intervals.

### Added

- Periodic self-update from GitHub Releases (see docs/updating.md). Enabled by
  default; configurable via `--update`/`--no-update`, `--update-interval`,
  `--update-repo`, `--update-proxy`, and `--update-insecure`.
- Successful unchanged-power refresh (60 seconds) and discovery expiration
  (180 seconds), configurable through `--power-refresh-interval` and
  `--power-expire-after`.
- Retained per-power-channel availability and checked measurement reads with
  independent failure handling.
- Signal-driven, acknowledged offline shutdown with a 5-second deadline.

### Changed

- Numeric power telemetry is non-retained. Existing retained power requires
  separately authorized exact-topic migration; no automatic deletion occurs.
- Update preparation runs in the background with a 120-second shared deadline
  and 5-second downloader cleanup allowance. Only application/re-exec interrupts
  MQTT; termination cancels preparation and suppresses application.
- Polling must be positive and no slower than the power refresh interval.

### Fixed

- Failed reads and quantization overflow no longer appear as valid zero/null.
- Reconnects cannot refresh cached power before new successful readings.

## [1.2.1] - 2026-07-20

### Fixed

- Fixed a long-running freeze on the device caused by unbounded MQTT memory
  growth. Sensor/relay state is now published at QoS 0 (retained), so
  libmosquitto's outgoing queue can no longer grow without bound when the broker
  lags or is unreachable. Sensor readings are also rounded to their display
  precision to reduce publish churn. (via hass_mqtt_device 1.2.0)

## [1.2.0] - 2026-03-30

### Fixed

- Fixed empty MQTT topics caused by registering functions before the device was connected to the broker.
- Fixed hardware I/O using relative paths instead of absolute paths on mFi devices.
- Deduplicated device IDs in MQTT topics — topics now use `home/<hostname>/...` instead of repeating the hostname three times.

## [1.1.1] - 2026-03-30

### Fixed

- Fixed hardware I/O using relative paths instead of absolute paths on mFi devices, causing relay control and potentially all sensor reads to fail.

## [1.1.0] - 2026-03-29

### Added

- Only send MQTT updates when sensor values actually change, reducing traffic.

### Changed

- Replaced `dynamic_pointer_cast` with `static_pointer_cast` for RTTI-disabled builds.
- Reduced runtime resource usage for embedded devices.

### Fixed

- Fixed `--config` file validation failing on mFi devices due to uclibc using the `statx` syscall.
- Fixed device freeze caused by `std::regex` usage and excessive logger allocations.
- Fixed MQTT client crashes from mosquitto resource leaks and missing error handling.

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
