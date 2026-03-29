# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [1.2.0] - 2025-01-03

### Changed

- Replaced docopt with CLI11 for argument parsing.
- Refactored common code into shared library.
- Use hostname from board for device identification.

### Added

- Improved CMake integration for version and app info.

### Fixed

- Fix argument parsing regression.

## [1.1.0] - 2024-02-11

### Changed

- Renamed from mfi-server to mfi-rest-server.
- Extracted Mongoose C++ wrapper into its own project (mgpp).

## [1.0.0] - 2024-02-03

### Added

- Initial release.
- REST server for mFi devices under `/api/v2/`.
- Endpoints for device info, all sensors, individual sensor lookup/control, and LED status/control.
- JSON responses for board, sensor, and LED data.
- Relay control via `POST /api/v2/sensor/{id}`.
- LED control via `POST /api/v2/led` with color and frequency fields.
- Input validation for IP address and sensor ID values.
- Response version headers.
- CLI options for listen IP, port, and log level.
