# Deploying to mFi Devices

The mFi persistent flash storage is only 64 KB — far too small for the binaries
themselves. The recommended approach is to store binaries in `/tmp` (RAM disk)
and use a small boot script in persistent storage to download them on startup.

## Binary Size Optimization

Release builds (`mips-release`) use `-Os`, LTO, `-fno-rtti`,
`-fno-unwind-tables`, `-ffunction-sections`, `-fdata-sections`,
`--gc-sections`, `-fmerge-all-constants`, `-fvisibility=hidden`, and
`-Wl,--exclude-libs,ALL` to minimize binary size. Typical stripped sizes
for MIPS static builds:

| Binary | On disk | In memory (text+data+bss) |
|--------|---------|---------------------------|
| `mfi-mqtt-client` | 1.14 MB | 1.16 MB |
| `mfi-rest-server` | 741 KB | 768 KB |
| `mfi-cli` | 692 KB | 718 KB |

For further on-disk savings, enable [UPX](https://upx.github.io/) compression
at build time (off by default):

```bash
cmake --preset mips-release -DMFI_UPX=ON
cmake --build --preset mips-release
```

This automatically runs `upx --best` on each executable after linking.
UPX-compressed binaries are self-extracting — they decompress into memory at
startup with no additional tooling needed on the device. This reduces download
time over the network and storage on the HTTP server hosting them. In-memory
size is unchanged.

## Deployment via Boot Script

1. Build with `mips-release` preset.
2. Host the binaries on a local HTTP server or NAS accessible from the device.
3. Create `/var/etc/persistent/rc.poststart` to download and run on boot:

```sh
#!/bin/sh

BINARY_URL="http://your-server/mfi"
INSTALL_DIR="/tmp/mfi-bin"

mkdir -p "$INSTALL_DIR"

# Download binaries
wget -q -O "$INSTALL_DIR/mfi-mqtt-client" "$BINARY_URL/mfi-mqtt-client"
chmod +x "$INSTALL_DIR/mfi-mqtt-client"

# Start the MQTT client
"$INSTALL_DIR/mfi-mqtt-client" --config /var/etc/persistent/mqtt.conf &
```

4. Place your configuration file at `/var/etc/persistent/mqtt.conf` (this is
   small enough for persistent storage).
5. Run `save` on the device to persist the script and config across reboots.

## Direct SCP Deployment (Non-Persistent)

For testing, you can SCP binaries directly to `/tmp`:

```bash
scp build/mips-release/mfi-mqtt-client/mfi-mqtt-client admin@mfi-device:/tmp/
ssh admin@mfi-device '/tmp/mfi-mqtt-client --config /var/etc/persistent/mqtt.conf &'
```

Note: `/tmp` is cleared on reboot, so this approach requires re-copying after
each restart.
