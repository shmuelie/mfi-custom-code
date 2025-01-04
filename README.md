# Ubiquiti mFi Custom Code

Providing custom code to run on Ubiquiti's mFi Devices

## Building

Uses CMake to build/configure. If `MFI_CROSS_COMPILE` is `ON`, CMake configure
will also run `make` on the Buildroot submodule and configure CMake to use the
cross-compiler for MIPS. The resulting binaries can be copied and run on the mFi
devices.

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

These packages are required to build for the host system. They are not required
to build for the mFi devices, as the Buildroot submodule will handle that.

- `libmosquitto-dev`
- `nlohmann-json3-dev`
- `pkg-config`
- `libspdlog-dev`

### Presets

`CMakePresets.json` has four presets:

- `local-debug` to build for the host system with debug symbols.
- `local-release` to build for the host system with optimizations.
- `mips-debug` to build for the mFi devices with debug symbols.
- `mips-release` to build for the mFi devices with optimizations.

## Projects

`mfi-clid`, `mfi-rest-server`, and `mfi-mqtt-client` use
[CLI11](https://github.com/CLIUtils/CLI11) to parse the command line options.

### Shared Code

`shmuelie-shared` is a shared library for code that is used in multiple projects.

### Buildroot Config

`br2` is an external tree for [Buildroot](https://buildroot.org/) to compile
C/C++ to run on the mFi devices. Thanks to
[cracauer/mFI-mPower-updated-sshd](https://github.com/cracauer/mFI-mPower-updated-sshd)
for helping me figure this out.

The `CMakeLists.txt` in the folder will load the Buildroot configuration and run
`make` to build the toolchain. If the toolchain builds sucessfully, it will
configure cmake to use the toolchain.

### mFi API

`mfi` is a C++ API for the mFi devices, wrapping the file system based API they
natively support.

### mFi CLI

`mfi-cli` is a CLI tool for Ubiquiti's mFi Devices, mostly exists to test the
API.

```
CLI tool for Ubiquiti's mFi Devices
Usage: ./mfi-cli [OPTIONS] SUBCOMMAND

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit

Subcommands:
  info                        Display information about the mFi device
```

#### Info Command

```
Display information about the mFi device
Usage: ./mfi-cli info [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  -a,--all [0]                Display all information
```

### Mongoose C++

`mgpp` is a C++ wrapper around [Mongoose](https://mongoose.ws/).

### mFi Rest Server

`mfi-rest-server` is a HTTP REST API server for the mFi devices built on top of
the mFi API.

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

### HASS MQTT Device

`hass-mqtt-device` is a fork of
[KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device), a C++
library for creating Home Assistant MQTT devices. The fork is used to support
rapid changes. The changes will eventually(?) backported to the original
repository.

### mFi MQTT Client

`mfi-mqtt-client` is a MQTT client for usage with [Home
Assistant](https://www.home-assistant.io/). It uses a fork of
[KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device).

```
MQTT Client for Ubiquiti's mFi Devices
Usage: ./mfi-mqtt-client [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  --config                    Configuration file to load options from
  --server TEXT REQUIRED      The MQTT server to connect to
  --port UINT [1883]          The port to use when connecting to the MQTT server
  --username TEXT REQUIRED    The username to use when connecting to the MQTT server
  --password TEXT REQUIRED    The password to use when connecting to the MQTT server
  --polling-rate UINT [1000]  The polling rate in milliseconds
  --log-level ENUM:value in {trace->0,debug->1,info->2,warn->3,error->4,critical->5,off->6} OR {0,1,2,3,4,5,6} [2]
                              The log level to use
```

The configuration file is in in TOML or INI formats:

#### TOML Format

```toml
server = "mqtt.example.com"
port = 1883
username = "username"
password = "password"
polling_rate = 1000
log_level = 2
```

#### INI Format

```ini
server = "mqtt.example.com"
port = 1883
username = "username"
password = "password"
polling_rate = 1000
log_level = 2
```

## Useful mFi Stuff:

- `/var/etc/persistent/` is the home directory when you SSH in and the
  "persistent" folder.
- The `save` alias will save the persistent folders data to flash memory, so it
  will be there after a reboot. The flash memory is only 128KB, with that split
  in half to have a backup so 64KB of space to save stuff.
- If `/var/etc/persistent/profile` exists it will be run on login.
- If `/var/etc/persistent/rc.poststart` exists it will be run after the device
  starts up. NOTE, the firmware waits 3 minutes after startup before running
  your script.