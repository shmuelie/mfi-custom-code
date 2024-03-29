# Ubiquiti mFi Custom Code

Providing custom code to run on Ubiquiti's mFi Devices

## Projects

### Buildroot Config

In `br2` is a config file for [Buildroot](https://buildroot.org/) to compile
C/C++ to run on the mFi devices. Thanks to
[cracauer/mFI-mPower-updated-sshd](https://github.com/cracauer/mFI-mPower-updated-sshd)
for helping me figure this out.

### mFi API

In `mfi` is a C++ API for the mFi devices, wrapping the file system based API
they natively support.

### mFi Server

In `mfi-server` is a [Mongoose](https://mongoose.ws/) based HTTP REST API server
for the mFi devices built on top of the mFi API. Also includes a simple C++
wrapper around Mongoose.

```
mFi HTTP REST SERVER.

  Usage:
        mfi-server [options]
        mfi-server (-h | --help)
        mfi-server --version

  Options:
        -h --help                  Show this screen.
        --version                  Show version.
        -i, --ip IP_ADDRESS        Use IP_ADDRESS to listen on [default: 0.0.0.0].
        -p, --port PORT            Use PORT to listen on [default: 8000].
        -l, --log-level LOG_LEVEL  Logging level 0-4 [default: 0].
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