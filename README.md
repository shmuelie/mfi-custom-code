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

### mFi API (`mfi`)

C++ library wrapping the mFi file-system-based hardware interface. Provides
classes for reading sensor data (power, current, voltage, power factor),
controlling relays, managing the device LED, and reading board/device
configuration.

### HASS MQTT Device (`hass_mqtt_device`)

Fork of [KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device) — a
C++ library for creating Home Assistant MQTT devices with auto-discovery. Supports
switches, sensors, lights (on/off and dimmable), and number inputs. Uses
[Mosquitto](https://mosquitto.org/) for MQTT connectivity.

### mFi MQTT Client (`mfi-mqtt-client`)

MQTT client that exposes mFi device ports to
[Home Assistant](https://www.home-assistant.io/) via MQTT auto-discovery. Each
port is published as a set of sensor entities (power, current, voltage) and a
switch entity (relay control). Only publishes updates when values change to
minimize MQTT traffic.

```
MQTT Client for Ubiquiti's mFi Devices
Usage: ./mfi-mqtt-client [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  --config :FILE              Configuration file to load options from
  --server TEXT REQUIRED      The MQTT server to connect to
  --port UINT [1883]          The port to use when connecting to the MQTT server
  --username TEXT REQUIRED    The username to use when connecting to the MQTT server
  --password TEXT REQUIRED    The password to use when connecting to the MQTT server
  --polling-rate UINT:UINT in [0 - 4294967295] [1000]
                              The polling rate in milliseconds
  --log-level ENUM:value in {trace->0,debug->1,info->2,warn->3,error->4,critical->5,off->6} [2]
                              The log level to use
```

#### Configuration File

The client accepts a configuration file in TOML or INI format:

```toml
server = "mqtt.example.com"
port = 1883
username = "username"
password = "password"
polling_rate = 1000
log_level = 2
```

### mFi REST Server (`mfi-rest-server`)

HTTP REST API server for the mFi devices built on top of the mFi API and
Mongoose.

```
REST API for Ubiquiti's mFi Devices
Usage: ./mfi-rest-server [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  -i,--ip TEXT [0.0.0.0]      The IP address to listen on
  -p,--port UINT [8000]       The port to listen on
  -l,--log-level UINT [0]     The log level to use
```

### mFi CLI (`mfi-cli`)

CLI tool for inspecting mFi device state. Mostly exists to test the mFi API.

```
CLI tool for Ubiquiti's mFi Devices
Usage: ./mfi-cli [OPTIONS] SUBCOMMAND

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit

Subcommands:
  info                        Display information about the mFi device
```

### Mongoose C++ (`mgpp`)

C++ wrapper around [Mongoose](https://mongoose.ws/) providing RAII-based resource
management for the HTTP server, connections, and timers.

### Shared Utilities (`shmuelie-shared`)

String helper functions (split, join, number parsing) shared across projects.

### Buildroot Config (`br2`)

External tree for [Buildroot](https://buildroot.org/) to cross-compile for the
mFi MIPS architecture. Thanks to
[cracauer/mFI-mPower-updated-sshd](https://github.com/cracauer/mFI-mPower-updated-sshd)
for helping figure this out.

## Building

Uses CMake to build/configure. All projects use
[CLI11](https://github.com/CLIUtils/CLI11) for command-line parsing.

### Required Packages

- `bc`
- `binutils`
- `build-essential`
- `bzip2`
- `cmake`
- `diffutils`
- `findutils`
- `rsync`
- `unzip`

### Optional Packages

Required to build for the host system. Not needed for cross-compilation — the
Buildroot submodule handles that.

- `libmosquitto-dev`
- `nlohmann-json3-dev`
- `pkg-config`
- `libspdlog-dev`
- `catch2` (for running tests)

### CMake Presets

`CMakePresets.json` provides four presets:

| Preset | Target | Build Type |
|--------|--------|------------|
| `local-debug` | Host system | Debug |
| `local-release` | Host system | Release |
| `mips-debug` | mFi device (MIPS) | Debug |
| `mips-release` | mFi device (MIPS) | MinSizeRel |

When `MFI_CROSS_COMPILE` is `ON`, CMake configure will also run `make` on the
Buildroot submodule to build the MIPS cross-compilation toolchain.

```bash
# Configure and build for local development
cmake --preset local-debug
cmake --build --preset local-debug

# Cross-compile for mFi devices
cmake --preset mips-release
cmake --build --preset mips-release
```

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2) and are only built for
local (non-cross-compile) targets. After building:

```bash
cd build/local-debug
ctest --output-on-failure
```

## Deploying to mFi Devices

Copy the cross-compiled binaries to the device and configure autostart:

1. Build with `mips-release` preset.
2. Copy binaries from `install/mips-release/` to the device via SCP.
3. Create a config file and place it on the device.
4. Add a startup entry to `/var/etc/persistent/rc.poststart`.
5. Run `save` on the device to persist across reboots.

## Useful mFi Notes

- `/var/etc/persistent/` is the home directory when you SSH in and the
  "persistent" folder.
- The `save` alias will save the persistent folder's data to flash memory, so it
  will be there after a reboot. The flash memory is only 128KB, split in half for
  backup — 64KB of usable space.
- If `/var/etc/persistent/profile` exists it will be run on login.
- If `/var/etc/persistent/rc.poststart` exists it will be run after the device
  starts up. NOTE: the firmware waits 3 minutes after startup before running
  your script.