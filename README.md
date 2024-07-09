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

### Presets

`CMakePresets.json` has four presets:

- `local-debug` to build for the host system with debug symbols.
- `local-release` to build for the host system with optimizations.
- `mips-debug` to build for the mFi devices with debug symbols.
- `mips-release` to build for the mFi devices with optimizations.

## Projects

`mfi-rest-server` and `mfi-mqtt-client` use
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

### Mongoose C++

`mgpp` is a C++ wrapper around [Mongoose](https://mongoose.ws/).

### mFi Rest Server

`mfi-rest-server` is a HTTP REST API server for the mFi devices built on top of
the mFi API.

```
mFi HTTP RESET Server
Usage: ./mfi-rest-server/mfi-rest-server [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  -i,--ip TEXT [0.0.0.0]      The IP address to listen on
  -p,--port UINT [8000]       The port to listen on
  -l,--log-level UINT [0]     The log level to use
```

### mFi MQTT Client

`mfi-mqtt-client` is a MQTT client for usage with [Home
Assistant](https://www.home-assistant.io/). It uses a fork of
[KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device).

```
mFi MQTT Client
Usage: ./mfi-mqtt-client [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  --server TEXT REQUIRED      The MQTT server to connect to
  --port UINT REQUIRED        The port to use when connecting to the MQTT server
  --username TEXT REQUIRED    The username to use when connecting to the MQTT server
  --password TEXT REQUIRED    The password to use when connecting to the MQTT server
  --polling-rate UINT [1000]  The polling rate in milliseconds
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