# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.1] - 2026-03-29

### Changed

- Reduced runtime resource usage for embedded devices.

## [1.0.0] - 2026-03-29

### Added

- Initial release.
- `mfi::board` abstraction for mFi hardware metadata: model name, short name, sysid, hostname, firmware version, and sensor enumeration.
- `mfi::sensor` support for per-port readings: power, current, voltage, power factor, and relay state control.
- Cached sensor metadata lookup for `name()` and `label()`.
- `mfi::led` control with color modes: off, blue, yellow, both, and alternate.
- LED blink frequency get/set support.
- INI-style config helpers via `mfi::config`: read all key/value pairs, read a single key, and read with default fallback.
