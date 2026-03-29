# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-03-29

### Added

- Initial release.
- Home Assistant MQTT auto-discovery framework.
- Reusable base classes for MQTT devices (`DeviceBase`) and functions (`FunctionBase`).
- Device/entity type support: switches, on/off lights, dimmable lights, numbers, and sensors.
- Templated sensor functions with configurable Home Assistant sensor attributes.
- Helper factory for sensor attributes, including temperature sensors.
- MQTT connector with discovery payloads, status updates, message processing, availability/LWT, device registration, and topic subscription management.
