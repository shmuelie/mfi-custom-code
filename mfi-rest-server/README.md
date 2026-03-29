# mFi REST Server

HTTP REST API server for mFi devices built on top of the mFi API and Mongoose.

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
- mgpp
- shmuelie-shared

### External

- [CLI11](https://github.com/CLIUtils/CLI11) — fetched via CMake `FetchContent`

## Details

- **Language**: C++20
- **Version**: 1.2.0
