# Mongoose C++ (mgpp)

C++ wrapper around [Mongoose](https://mongoose.ws/) providing RAII-based resource management for the HTTP server, connections, and timers.

## Overview

mgpp exposes a C++20 interface over the Mongoose embedded networking library. Key abstractions:

- **`mg::http_server`** — RAII HTTP server with request routing.
- **`mg::connection`** — Managed connection wrapper.
- **`mg::http_response`** / **`mg::http_message`** — Request/response helpers.
- **`mg::timer`** — Periodic timer support.
- **`mg::manager`** — Event loop manager.

## Dependencies

### External

- [Mongoose](https://mongoose.ws/) — fetched via CMake `FetchContent`

### Internal

- shmuelie-shared

## Details

- **Language**: C++20
- **Namespace**: `mg`
- **Version**: 1.0.0
