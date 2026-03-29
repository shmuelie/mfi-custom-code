# HASS MQTT Device

Fork of [KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device) — a C++ library for creating Home Assistant MQTT devices with auto-discovery. Supports switches, sensors, lights (on/off and dimmable), and number inputs.

## Overview

This library handles the MQTT auto-discovery protocol used by Home Assistant. It manages device registration, state publishing, and command subscriptions so that consuming code only needs to define device capabilities.

Supported entity types:

- **Switch** — on/off control
- **Sensor** — numeric/string state reporting
- **On/Off Light** — simple light toggle
- **Dimmable Light** — light with brightness control
- **Number** — numeric input with min/max/step

## Dependencies

### External

- [libmosquitto](https://mosquitto.org/) — MQTT client library
- [nlohmann_json](https://github.com/nlohmann/json) — JSON serialization
- [spdlog](https://github.com/gabime/spdlog) — Logging

## Details

- **Language**: C++20
- **Version**: 1.0.0
