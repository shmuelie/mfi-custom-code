# mFi REST Server

HTTP REST API server for mFi devices built on top of the mFi API and the
[civetweb](https://github.com/civetweb/civetweb) embedded HTTP server.

## Usage

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

## Dependencies

### Internal

- mfi
- shmuelie-shared
- mfi-update

### External

- [CLI11](https://github.com/CLIUtils/CLI11) — fetched via CMake `FetchContent`
- [civetweb](https://github.com/civetweb/civetweb) — HTTP server; Buildroot package on device, `FetchContent` on host
- [nlohmann_json](https://github.com/nlohmann/json) — request parsing

## Details

- **Language**: C++20
- **Version**: 1.2.2
