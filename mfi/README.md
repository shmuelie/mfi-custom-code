# mFi API

C++ library wrapping the mFi file-system-based hardware interface. Provides classes for reading sensor data (power, current, voltage, power factor), controlling relays, managing the device LED, and reading board/device configuration.

## Overview

The mFi API abstracts the sysfs-like file interface exposed by Ubiquiti mFi devices into a clean C++20 API. Key classes include:

- **`mfi::sensor`** — Read power, current, voltage, and power factor from device ports.
- **`mfi::config`** — Access device configuration.
- **`mfi::board`** — Query board-level information.
- **`mfi::led`** — Control the device status LED.

## Checked measurements

`sensor::power_checked()`, `current_checked()`, `voltage_checked()`, and
`power_factor_checked()` return `sensor_read_result`, a variant of `double` and
`sensor_read_error`. They require a complete finite numeric reading (surrounding
whitespace is allowed), bound input size, and distinguish opening/reading errors
from invalid data. Locale-independent parsing rejects both overflow and
underflow instead of letting an out-of-range number become zero.
`mfi::describe(error)` supplies a diagnostic reason; callers
add their sensor/measurement context.

These APIs do not substitute zero for a failed read. Signed finite readings are
returned as read; the MQTT power consumer applies its consumption-only
negative-value policy and quantization checks.

The existing `power()`, `current()`, `voltage()`, and `power_factor()` getters
retain their legacy behavior for compatibility, including zero on missing
files. Do not use those getters to establish successful-read freshness.

`sensor::relay_checked()` returns `relay_read_result` (`bool` or
`sensor_read_error`), accepting only complete finite numeric 0/1 readings.
Missing or malformed data is not OFF. `relay_checked(bool)` performs a checked
write without creating a missing hardware path and returns an optional
`sensor_write_error` (`open_failed` or `write_failed`): empty means the one-byte
write and close succeeded. Successful writing alone does
not confirm the hardware state: read back before acknowledging a relay command.
Legacy `relay()`/`relay(bool)` behavior is unchanged.

## Dependencies

- No external dependencies.

## Details

- **Language**: C++20
- **Namespace**: `mfi`
- **Version**: 1.0.1
