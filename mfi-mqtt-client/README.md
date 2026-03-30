# mFi MQTT Client

MQTT client that exposes mFi device ports to Home Assistant via MQTT auto-discovery. Each port is published as a set of sensor entities (power, current, voltage) and a switch entity (relay control). Only publishes updates when values change to minimize MQTT traffic.

## Usage

```
MQTT Client for Ubiquiti's mFi Devices
Usage: ./mfi-mqtt-client [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  --config :FILE              Configuration file to load options from
  --server TEXT REQUIRED      The MQTT server to connect to
  --port UINT [1883]          The port to use when connecting to the MQTT server
  --username TEXT REQUIRED    The username to use when connecting to the MQTT server
  --password TEXT REQUIRED    The password to use when connecting to the MQTT server
  --polling-rate UINT:UINT in [0 - 4294967295] [1000]
                              The polling rate in milliseconds
  --log-level ENUM:value in {trace->0,debug->1,info->2,warn->3,error->4,critical->5,off->6} [2]
                              The log level to use
```

## Configuration

Options can be provided via a TOML or INI configuration file passed with `--config`:

```toml
server = "mqtt.example.com"
port = 1883
username = "username"
password = "password"
polling_rate = 1000
log_level = 2
```

## Dependencies

### Internal

- mfi
- hass_mqtt_device
- shmuelie-shared

### External

- [CLI11](https://github.com/CLIUtils/CLI11) — fetched via CMake `FetchContent`

## Details

- **Language**: C++20
- **Version**: 1.2.0
