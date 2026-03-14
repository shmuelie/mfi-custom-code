# Ubiquiti mFi Custom Code

Custom firmware applications for Ubiquiti's mFi line of power monitoring and
switching devices. Includes an MQTT client for Home Assistant integration, a REST
API server, and a CLI tool — all built on a shared C++ library that wraps the mFi
hardware interface.

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

The mFi persistent flash storage is only 64 KB — far too small for the binaries
themselves. The recommended approach is to store binaries in `/tmp` (RAM disk)
and use a small boot script in persistent storage to download them on startup.

### Binary Size Optimization

Release builds (`mips-release`) use `-Os`, LTO, `-ffunction-sections`,
`-fdata-sections`, and `--gc-sections` to minimize binary size. Typical stripped
sizes for MIPS static builds:

| Binary | Approximate Size |
|--------|-----------------|
| `mfi-mqtt-client` | ~350 KB |
| `mfi-rest-server` | ~230 KB |
| `mfi-cli` | ~180 KB |

### Deployment via Boot Script

1. Build with `mips-release` preset.
2. Host the binaries on a local HTTP server or NAS accessible from the device.
3. Create `/var/etc/persistent/rc.poststart` to download and run on boot:

```sh
#!/bin/sh

BINARY_URL="http://your-server/mfi"
INSTALL_DIR="/tmp/mfi-bin"

mkdir -p "$INSTALL_DIR"

# Download binaries
wget -q -O "$INSTALL_DIR/mfi-mqtt-client" "$BINARY_URL/mfi-mqtt-client"
chmod +x "$INSTALL_DIR/mfi-mqtt-client"

# Start the MQTT client
"$INSTALL_DIR/mfi-mqtt-client" --config /var/etc/persistent/mqtt.conf &
```

4. Place your configuration file at `/var/etc/persistent/mqtt.conf` (this is
   small enough for persistent storage).
5. Run `save` on the device to persist the script and config across reboots.

### Direct SCP Deployment (Non-Persistent)

For testing, you can SCP binaries directly to `/tmp`:

```bash
scp build/mips-release/mfi-mqtt-client/mfi-mqtt-client admin@mfi-device:/tmp/
ssh admin@mfi-device '/tmp/mfi-mqtt-client --config /var/etc/persistent/mqtt.conf &'
```

Note: `/tmp` is cleared on reboot, so this approach requires re-copying after
each restart.

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