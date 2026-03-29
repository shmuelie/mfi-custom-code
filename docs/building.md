# Building

Uses CMake to build/configure. All projects use
[CLI11](https://github.com/CLIUtils/CLI11) for command-line parsing.
CLI11, [Mongoose](https://mongoose.ws/), and
[Catch2](https://github.com/catchorg/Catch2) are fetched via CMake's
`FetchContent`; other dependencies come from system packages.

## Required Packages

- `bc`
- `binutils`
- `build-essential`
- `bzip2`
- `cmake`
- `diffutils`
- `findutils`
- `rsync`
- `unzip`

## Optional Packages

Required to build for the host system. Not needed for cross-compilation — the
Buildroot submodule handles that.

- `libmosquitto-dev`
- `nlohmann-json3-dev`
- `pkg-config`
- `libspdlog-dev`

## CMake Presets

`CMakePresets.json` provides four presets:

| Preset | Target | Build Type |
|--------|--------|------------|
| `local-debug` | Host system | Debug |
| `local-release` | Host system | Release |
| `mips-debug` | mFi device (MIPS) | Debug |
| `mips-release` | mFi device (MIPS) | MinSizeRel |

When `MFI_CROSS_COMPILE` is `ON`, CMake configure will also run `make` on the
Buildroot submodule to build the MIPS cross-compilation toolchain.

```bash
# Configure and build for local development
cmake --preset local-debug
cmake --build --preset local-debug

# Cross-compile for mFi devices
cmake --preset mips-release
cmake --build --preset mips-release
```

## Testing

Tests use [Catch2](https://github.com/catchorg/Catch2) and are only built for
local (non-cross-compile) targets. After building:

```bash
cd build/local-debug
ctest --output-on-failure
```
