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