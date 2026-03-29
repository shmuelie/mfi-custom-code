# Ubiquiti mFi Custom Code

Custom firmware applications for Ubiquiti's mFi line of power monitoring and
switching devices. Includes an MQTT client for Home Assistant integration, a REST
API server, and a CLI tool — all built on a shared C++ library that wraps the mFi
hardware interface.

## Architecture

```
mfi-mqtt-client ─┬─ hass_mqtt_device (Home Assistant MQTT discovery)
                 ├─ mfi (hardware abstraction: sensors, relays, LEDs)
                 └─ shmuelie-shared (string utilities)

mfi-rest-server ─┬─ mgpp (Mongoose C++ wrapper)
                 ├─ mfi
                 └─ shmuelie-shared

mfi-cli ─────────┬─ mfi
                 └─ shmuelie-shared
```

## Projects

### Executables

| Project | Description | Version |
|---------|-------------|---------|
| [mfi-mqtt-client](mfi-mqtt-client/) | MQTT client with Home Assistant auto-discovery | 1.1.0 |
| [mfi-rest-server](mfi-rest-server/) | HTTP REST API server | 1.2.0 |
| [mfi-cli](mfi-cli/) | CLI tool for device inspection | 1.0.0 |

### Libraries

| Project | Description | Version |
|---------|-------------|---------|
| [mfi](mfi/) | Hardware abstraction for sensors, relays, LEDs | 1.0.0 |
| [hass_mqtt_device](hass_mqtt_device/) | Home Assistant MQTT device library | 1.0.0 |
| [mgpp](mgpp/) | C++ wrapper around Mongoose | 1.0.0 |
| [shmuelie-shared](shmuelie-shared/) | String helper functions | 1.0.0 |

## Alternatives

There are several community shell-script projects for controlling mFi devices.
This project differs in being a compiled C++ solution with a proper hardware
abstraction layer, native MQTT, and Home Assistant auto-discovery.

| Feature | mfi-custom-code | [mpower-tools](https://github.com/magcode/mpower-tools) | [mFi-tools](https://github.com/maletazul/mFi-tools) |
|---|---|---|---|
| Language | C++ | Shell scripts | Shell scripts |
| MQTT | Native (libmosquitto) | Via `mosquitto_pub` | Via `mosquitto_pub` |
| Home Assistant | Auto-discovery (switches + sensors) | Manual YAML config | Manual YAML config |
| REST API | Yes (Mongoose) | No | No |
| CLI tool | Yes | No | No |
| Sensor data | Power, current, voltage, power factor | Power, energy | Power, energy |
| Change-only updates | Yes (reduces MQTT traffic) | No (polls on interval) | No (polls on interval) |
| Hardware abstraction | C++ library | Direct file I/O | Direct file I/O |
| Cross-compilation | Buildroot toolchain | Not needed | Not needed |
| Unit tests | Catch2 | None | None |
| Install complexity | Build + deploy binary | Copy scripts | Copy scripts |

## Documentation

- [Building](docs/building.md) — packages, CMake presets, build commands, testing
- [Deploying](docs/deploying.md) — binary sizes, UPX, boot scripts, SCP
- [mFi Notes](docs/mfi-notes.md) — persistent storage, save, boot hooks
- [cfgmtd](docs/cfgmtd.md) — flash configuration utility internals