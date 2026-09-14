# mFi MQTT Client

MQTT client that exposes mFi device ports and device-wide CPU/memory diagnostics via Home Assistant MQTT auto-discovery. Each port publishes power, current, voltage, and relay control. Changes publish promptly; unchanged power also refreshes after successful hardware reads so consumers can distinguish constant loads from failed polling.

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
  --polling-rate UINT:UINT in [1 - 4294967295] [1000]
                              The polling rate in milliseconds
  --power-refresh-interval UINT [60]
                              Successful unchanged-power refresh interval (seconds)
  --power-expire-after UINT [180]
                              Power expiration advertised in discovery (seconds)
  --system-metrics,--no-system-metrics
                              Enable/disable CPU and memory reporting (enabled)
  --system-polling-interval UINT [10]
                              System sampling interval (seconds)
  --system-refresh-interval UINT [60]
                              Successful unchanged-system-metric refresh (seconds)
  --system-expire-after UINT [180]
                              System metric expiration in discovery (seconds)
  --system-proc-root TEXT [/proc]
                              Proc directory for alternate mounts or fixtures
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

The power refresh interval accepts 1-86400 seconds; expiration accepts 1-259200
seconds and must allow at least three refresh intervals. Polling must not be
slower than the refresh interval. Zero polling intervals are rejected. MQTT
service runs independently in short waits, rather than blocking for an entire
hardware polling interval.

## CPU and memory diagnostics

Five diagnostic sensors are enabled by default on the same Home Assistant device
as the outlets. Names and unique IDs follow the existing MQTT conventions.
All values use one decimal place and `state_class: measurement`; memory sizes
also use `device_class: data_size`. Percentage sensors omit `device_class`.

| Sensor | Unit | Meaning |
|---|---|---|
| CPU Utilization | % | Aggregate busy CPU time between successful samples |
| Memory Total | MiB | Kernel-reported usable RAM (`MemTotal`) |
| Memory Available | MiB | RAM available for applications without swapping |
| Memory Used | MiB | Total minus available |
| Memory Utilization | % | Used divided by total, multiplied by 100 |

CPU comes from the aggregate `cpu` line in `/proc/stat`, not the MQTT process or
individual cores. Utilization is normalized to 0-100% across all CPUs. Idle and
I/O wait count as non-busy; guest counters are not added twice. The first sample
establishes a baseline, so CPU remains unavailable until the next successful
sample. Failures, counter regressions, and zero elapsed ticks reset the baseline;
reconnect also requires two new reads. CPU warm-up does not hold the device or
outlets offline, but its initial offline availability must still be acknowledged.
The same pending-readiness rule applies to initial memory sampling: a valid read
before the offline PUBACK is withheld without delaying shared availability.
Diagnostics stay offline until a fresh scheduled read can be published; outlet
availability does not wait for the next system sampling interval.

Memory comes from `/proc/meminfo`; its `kB` values are KiB, converted to MiB by
dividing by 1024. `MemAvailable` is preferred. On older kernels without it,
available RAM is **estimated** as:

```text
MemFree + Buffers + Cached + SReclaimable - Shmem
```

`MemFree`, `Buffers`, and `Cached` are required for this estimate. Optional
`SReclaimable` and `Shmem` contribute zero only when absent. The legacy estimate
is bounded to 0 through `MemTotal` and is not equivalent to the modern kernel's
estimate, especially where older firmware does not report shared memory. Use
of the estimate is logged; malformed fields and arithmetic overflow are errors,
not reasons to substitute zero or silently change methods.

System sampling has its own steady-clock deadline, independent of outlet
polling, and does not launch a process or add a worker thread. Changed rounded
values publish at the next sample (10 seconds by default). Successful unchanged
readings refresh every 60 seconds, plus at most one sampling interval and
scheduling jitter. Discovery advertises 180-second expiration. Numeric state is
QoS 0, **not retained**; discovery and shared/per-sensor availability are retained.
New subscribers wait for a new publication. Cached samples are never replayed
on reconnect. A CPU read failure affects only CPU; a memory read failure affects
the four memory sensors. Failures and recoveries are logged on transition.

CLI/config timing intervals are in seconds. Sampling and refresh accept
1-86400, expiration accepts 1-259200, sampling must not exceed refresh, and
expiration must allow at least three refresh intervals. These settings are
independent of power freshness. Configuration uses the option names:

```toml
system-metrics = true
system-polling-interval = 10
system-refresh-interval = 60
system-expire-after = 180
```

Use `--no-system-metrics` or `system-metrics = false` to opt out. Disabling
reporting does not delete existing retained discovery; previously discovered
diagnostics expire and can be removed separately in Home Assistant. It does not
change the outlet entities. `/proc` is the default even for host builds;
`--system-proc-root` allows an alternate proc mount or synthetic fixture directory.
No process-specific metrics, swap, temperature, or per-core sensors are included.

## Power freshness and availability

Power is published as `{"value": <watts>}` at QoS 0, **not retained**. Each
publication represents a successful read of that channel, including unchanged
100 W or 0 W loads. Discovery advertises `expire_after: 180` by default; normal
unchanged refresh is 60 seconds plus at most a polling cycle and scheduling
jitter. Changed quantized readings do not wait for the refresh interval.

Power discovery combines retained shared transport availability with retained
per-channel availability using `availability_mode: all`. The new channel topic
is `<existing sensor topic base>/availability` and uses the same
`{"availability":"online"}` / `{"availability":"offline"}` payload convention.
An unreadable, malformed, nonfinite, negative, or overflowing power value marks
that channel offline; it is never converted into zero or JSON null. Failures
are logged on transition, with recovery notifications, and do not stop other
measurements or outlets.

After reconnect, each channel starts behind an acknowledged offline gate.
Cached power is never replayed. A channel becomes online only after a new valid
read has been accepted for numeric publication and its availability transition
has completed. Accepted QoS 0 publication is **not** proof of subscriber receipt;
QoS 1 acknowledgements confirm broker acceptance, not subscriber processing.
Expiration remains the fallback for crashes, network loss, or stalled polling.

Current, voltage, relay state, and discovery retain their existing retention
policy. State topics, unique IDs, labels, units, and relay commands are unchanged.
A new subscriber may wait for the next successful power refresh; MQTT discovery
does not require server-side configuration changes.

The connector rejects disconnected/backlogged publications rather than building
an unbounded queue. Uncompleted MQTT publications have a 5-second transport
deadline, after which the connection and its old queued state are discarded.
Expiry measures receiver receipt time, not original sample age; arbitrary broker
delays cannot be detected without original timestamps.

## Shutdown and updates

SIGINT/SIGTERM request orderly shutdown. The client publishes retained shared
offline, services MQTT for up to 5 seconds for its PUBACK, and then disconnects.
If offline cannot be acknowledged, it logs the failure and closes without a
clean MQTT DISCONNECT so the broker can still use the Last Will.

Update checking/downloading runs in one background preparation job, keeping
polling and relay servicing active. Preparation has a shared 120-second deadline
and up to 5 additional seconds for owned downloader-child cleanup. Only applying
a prepared update briefly goes offline and replaces/re-execs on the MQTT-owning
thread. Termination cancels pending preparation and prevents a late result from
restarting the process. See [Self-Updating](../docs/updating.md).

## Existing retained power messages

Changing future publications to non-retained does **not** erase old retained
power. Do not consider an existing broker migrated until those exact legacy
records have been cleared through a separately approved operation. The client
does not purge them automatically. See the
[MQTT retained-state migration runbook](../docs/mqtt-freshness-migration.md).

## Dependencies

### Internal

- mfi
- hass_mqtt_device
- shmuelie-shared
- mfi-update

### External

- [CLI11](https://github.com/CLIUtils/CLI11) — fetched via CMake `FetchContent`

## Details

- **Language**: C++20
- **Version**: 2.0.0
