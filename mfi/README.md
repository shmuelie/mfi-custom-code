# mFi API

C++ library wrapping the mFi file-system-based hardware interface. Provides classes for reading sensor data (power, current, voltage, power factor), controlling relays, managing the device LED, and reading board/device configuration.

## Overview

The mFi API abstracts the sysfs-like file interface exposed by Ubiquiti mFi devices into a clean C++20 API. Key classes include:

- **`mfi::sensor`** — Read power, current, voltage, and power factor from device ports.
- **`mfi::config`** — Access device configuration.
- **`mfi::board`** — Query board-level information.
- **`mfi::led`** — Control the device status LED.

## Dependencies

- No external dependencies.

## Details

- **Language**: C++20
- **Namespace**: `mfi`
- **Version**: 1.0.1
