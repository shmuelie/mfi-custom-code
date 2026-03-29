# mFi CLI

CLI tool for inspecting mFi device state. Primarily used to test the mFi API.

## Usage

```
CLI tool for Ubiquiti's mFi Devices
Usage: ./mfi-cli [OPTIONS] SUBCOMMAND

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit

Subcommands:
  info                        Display information about the mFi device
```

## Dependencies

### Internal

- mfi
- shmuelie-shared

### External

- [CLI11](https://github.com/CLIUtils/CLI11) — fetched via CMake `FetchContent`

## Details

- **Language**: C++20
- **Version**: 1.0.0
