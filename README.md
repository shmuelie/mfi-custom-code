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

### mFi CLI

In `mfi-cli` is a simple CLI program for testing the mFi API.

### mFi Server

In `mfi-server` is a [Mongoose](https://mongoose.ws/) based HTTP REST API server
for the mFi devices built on top of the mFi API. Also includes a simple C++
wrapper around Mongoose.

## Building

Uses CMake to build/configure. If `MFI_CROSS_COMPILE` is `ON`, CMake configure
will also run `make` on the Buildroot submodule and configure CMake to use the
cross-compiler for MIPS. The resulting binaries can be copied and run on the mFi
devices.

## Useful mFi Stuff:

- `/var/etc/persistent/` is the home directory when you SSH in and the
  "persistent" folder.
- The `save` alias will save the persistent folders data to flash memory, so it
  will be there after a reboot.
- If `/var/etc/persistent/profile` exists it will be run on login.
- If `/var/etc/persistent/rc.poststart` exists it will be run after the device
  starts up.