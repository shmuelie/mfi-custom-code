# Ubiquiti mFi Custom Code

Providing custom code to run on Ubiquiti's mFi Devices

## Projects

`mfi-rest-server` and `mfi-mqtt-client` use
[docopt.cpp](https://github.com/docopt/docopt.cpp.git) to parse the command line
options.

### Buildroot Config

`br2` is a config file for [Buildroot](https://buildroot.org/) to compile C/C++
to run on the mFi devices. Thanks to
[cracauer/mFI-mPower-updated-sshd](https://github.com/cracauer/mFI-mPower-updated-sshd)
for helping me figure this out.

### mFi API

`mfi` is a C++ API for the mFi devices, wrapping the file system based API they
natively support.

### Mongoose C++

`mgpp` is a C++ wrapper around [Mongoose](https://mongoose.ws/).

### mFi Rest Server

`mfi-rest-server` is a HTTP REST API server for the mFi devices built on top of
the mFi API.

```
mFi HTTP REST SERVER.

  Usage:
    mfi-rest-server [options]
    mfi-rest-server (-h | --help)
    mfi-rest-server --version

  Options:
    -h --help                  Show this screen.
    --version                  Show version.
    -i, --ip IP_ADDRESS        Use IP_ADDRESS to listen on [default: 0.0.0.0].
    -p, --port PORT            Use PORT to listen on [default: 8000].
    -l, --log-level LOG_LEVEL  Logging level 0-4 [default: 0].
```

### mFi MQTT Client

`mfi-mqtt-client` is a MQTT client for usage with [Home
Assistant](https://www.home-assistant.io/). It uses a fork of
[KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device) which has
been tweaked to build as a static library, work as a git submodule, to not build
examples, and to not require Spdlog.

```
mFi MQTT Client.

  Usage:
    mfi-mqtt-client --server SERVER --port PORT --username USER --password PASS
    mfi-mqtt-client (-h | --help)
    mfi-mqtt-client --version

  Options:
    -h --help        Show this screen.
    --version        Show version.
    --server SERVER  The MQTT server to connect to.
    --port PORT      The port to use when connecting to the MQTT server.
    --username USER  The username to use when connecting to the MQTT server.
    --password PASS  The password to use when connecting to the MQTT server.
```

## Building

Uses CMake to build/configure. If `MFI_CROSS_COMPILE` is `ON`, CMake configure
will also run `make` on the Buildroot submodule and configure CMake to use the
cross-compiler for MIPS. The resulting binaries can be copied and run on the mFi
devices.

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