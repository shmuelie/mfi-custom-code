# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.2.0] - 2026-07-20

### Fixed

- Fixed unbounded memory growth that froze the device over long runtimes. High-
  frequency function state is now published at QoS 0 (retained), so libmosquitto's
  outgoing queue can no longer grow without bound when the broker lags or is
  unreachable. Discovery and availability messages remain QoS 1.

### Added

- `publishMessage()` on `MQTTConnector` and `DeviceBase` now accepts optional
  `qos` and `retain` arguments (defaulting to QoS 1, retained).

### Changed

- `SensorFunction<T>::update()` rounds floating-point readings to the entity's
  `suggested_display_precision` before change-detection, suppressing publishes
  caused by sub-precision sensor jitter.

## [1.1.0] - 2026-03-29

### Changed

- Replaced `dynamic_pointer_cast` with `static_pointer_cast` for RTTI-disabled builds.
- Reduced runtime resource usage for embedded devices.

### Fixed

- Fixed device freeze caused by `std::regex` usage and excessive logger allocations.

## [1.0.0] - 2026-03-29

### Added

- Initial release.
- Home Assistant MQTT auto-discovery framework.
- Reusable base classes for MQTT devices (`DeviceBase`) and functions (`FunctionBase`).
- Device/entity type support: switches, on/off lights, dimmable lights, numbers, and sensors.
- Templated sensor functions with configurable Home Assistant sensor attributes.
- Helper factory for sensor attributes, including temperature sensors.
- MQTT connector with discovery payloads, status updates, message processing, availability/LWT, device registration, and topic subscription management.
