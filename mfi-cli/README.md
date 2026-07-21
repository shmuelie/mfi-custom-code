# mFi CLI

Command-line tool for inspecting and controlling an mFi device. Exposes the full
libmfi API: board info, sensor readings, relay control, LED control, and
configuration-file reading.

## Usage

```
CLI tool for Ubiquiti's mFi Devices
Usage: ./mfi-cli [OPTIONS] SUBCOMMAND

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit

Subcommands:
  info                        Display information about the mFi device
  sensor                      Query sensor/port readings
  relay                       Read and control port relays
  led                         Read and control the device LED
  config                      Read INI-style configuration files
```

### `info`

```
mfi-cli info [--all]        # board info; --all also lists every sensor
```

### `sensor`

```
mfi-cli sensor list         # readings for every port
mfi-cli sensor get <id>     # readings for one port
```

### `relay`

```
mfi-cli relay get <id>          # print On/Off
mfi-cli relay set <id> <on|off> # set relay state
mfi-cli relay on <id>           # turn on
mfi-cli relay off <id>          # turn off
mfi-cli relay toggle <id>       # invert current state
```

### `led`

```
mfi-cli led get                                  # print color + frequency
mfi-cli led set --color <off|blue|yellow|both|alternate>
mfi-cli led set --frequency <n>                  # alternate frequency
```

`led set` requires at least one of `--color` / `--frequency`.

### `config`

```
mfi-cli config list <file>                    # dump all key=value pairs
mfi-cli config get <file> <key> [--default <v>]
```

## Dependencies

### Internal

- mfi
- shmuelie-shared

### External

- [CLI11](https://github.com/CLIUtils/CLI11) — fetched via CMake `FetchContent`

## Details

- **Language**: C++20
- **Version**: 1.1.0

