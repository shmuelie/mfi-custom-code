# Copilot Instructions

## Build & Test

This is a CMake C++20 project. All presets require Linux (native or remote).

```bash
# Configure + build (local debug)
cmake --preset local-debug
cmake --build --preset local-debug

# Run all tests
ctest --test-dir build/local-debug --output-on-failure

# Run a single test by name
ctest --test-dir build/local-debug -R "split: basic" --output-on-failure

# Cross-compile for mFi devices (MIPS)
cmake --preset mips-release
cmake --build --preset mips-release
```

Tests use Catch2 v3 with `catch_discover_tests()`, so CTest `-R` matches individual `TEST_CASE` names.

Host builds require: `cmake`, `build-essential`, `pkg-config`, `libmosquitto-dev`, `nlohmann-json3-dev`, `libspdlog-dev`.

## Architecture

Three executables share a common set of static libraries:

- **`mfi`** — Hardware abstraction (sensors, relays, LEDs, board config) over the mFi file-system interface. No external dependencies.
- **`hass_mqtt_device`** — Home Assistant MQTT auto-discovery. Depends on libmosquitto, nlohmann_json, spdlog.
- **`mgpp`** — C++ RAII wrapper around [Mongoose](https://mongoose.ws/) HTTP server. Depends on shmuelie-shared.
- **`shmuelie-shared`** — String helpers (split, join, number parsing). No external dependencies.

Executables:
- **`mfi-mqtt-client`** → mfi + hass_mqtt_device + shmuelie-shared
- **`mfi-rest-server`** → mfi + mgpp + shmuelie-shared
- **`mfi-cli`** → mfi + shmuelie-shared

All executables use CLI11 for argument parsing. CLI11, mongoose, and Catch2 are fetched via `FetchContent`; other dependencies come from system packages.

The `br2/` directory is a Buildroot external tree for cross-compiling to the mFi's MIPS 24Kc (Atheros AR9331). When `MFI_CROSS_COMPILE=ON`, CMake drives Buildroot to produce the MIPS toolchain automatically. The `buildroot` git submodule provides the Buildroot source. Busybox is disabled in the defconfig (uclibc lacks `utmpx.h`); `UCLIBC_SUSV3_LEGACY` is enabled for GCC 14 `<cstdio>` compatibility.

Tests live in `tests/` and are only built for local (non-cross-compile) targets.

## Code Conventions

- **Naming**: `snake_case` for classes, functions, methods, namespaces. Private members prefixed with `_` (e.g., `_name`, `_sensors`). Executables use `kebab-case` (e.g., `mfi-mqtt-client`).
- **Headers**: Always `#pragma once`, never `#ifndef` guards.
- **C++ style**: Heavy use of `std::optional`, `std::string_view`, `noexcept`, `const&` parameters, `final` on leaf classes. RAII for all resource management. RTTI is disabled (`-fno-rtti`) in release builds — use `static_pointer_cast` instead of `dynamic_pointer_cast`.
- **Namespaces**: `mfi`, `mg`, `shmuelie`, `mfi_cli`, `mfi_server`, `mfi_mqtt_client`.
- **CMake targets**: Named via variables in the root CMakeLists.txt (e.g., `MFI_API_TARGET_NAME`, `MGPP_TARGET_NAME`). Use these variables when linking, not raw target names.
- **Version info**: Use `add_version_info(<target>)` from `cmake/AddProjectVersionData.cmake` to stamp version macros into a target.
- **UPX compression**: Release executables are compressed with `mfi_upx_compress(<target>)`. Controlled by `MFI_UPX` (defaults ON for release builds).
