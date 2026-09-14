# HASS MQTT Device

Fork of [KodeZ/hass_mqtt_device](https://github.com/KodeZ/hass_mqtt_device) — a C++ library for creating Home Assistant MQTT devices with auto-discovery. Supports switches, sensors, lights (on/off and dimmable), and number inputs.

## Overview

This library handles the MQTT auto-discovery protocol used by Home Assistant. It manages device registration, state publishing, and command subscriptions so that consuming code only needs to define device capabilities.

Supported entity types:

- **Switch** — on/off control
- **Sensor** — numeric/string state reporting
- **On/Off Light** — simple light toggle
- **Dimmable Light** — light with brightness control
- **Number** — numeric input with min/max/step

## Expiring sensor policy

`SensorFunction<T>` preserves change-only, retained state by default. Passing
`sensor_policy::telemetry()` (or its compatible `sensor_policy::power()` alias)
opts into 60-second successful-read refresh, 180-second
discovery expiration, non-retained numeric telemetry, negative-value rejection,
and retained per-channel validity. Other refresh/expiry intervals may be supplied;
expiry must allow at least three refresh intervals.

Call `update(value)` only with a new successfully read measurement. Its boolean
result reports numeric/processing validity, **not** successful publication.
`update(value, steady_clock_time)` supports deterministic scheduling tests.
Call `invalidate(reason)` on a failed read. Invalidity is logged on transition,
revokes channel readiness, and is published as retained channel offline rather
than an invalid numeric payload. Floating-point quantization checks both scaling
overflow and final finiteness.

The connector performs an acknowledged initial offline handshake and calls
function lifecycle hooks from its normal processing loop, not from a connection
callback. Expiring sensors never publish cached values through `sendStatus()`.
`DeviceBase::sendStatus()` sends function state only; shared availability is
owned by the connector's handshake. Discovery combines shared and per-channel
availability for opted-in sensors, leaving other function discovery unchanged.

`SensorAttributes::entity_category` optionally marks diagnostic sensors.
Unset categories and empty device classes are omitted from discovery; existing
nonempty device classes remain unchanged.

`await_sample()` marks an intentionally pending measurement unavailable without
logging a fault or supplying a numeric value. Its acknowledged offline state
satisfies connection readiness, useful for CPU utilization's two-sample warm-up.
It does not acknowledge transport packets or bypass existing power readiness.
`DeviceBase::beginConnection()` is virtual; overrides must call the base method
before resetting their own sampling baselines and marking pending sensors.
Numeric publication remains gated by the acknowledged initial offline state.

`publishMessage()` returns a `publication` ticket with acceptance, connection
epoch, and sequence information. `publicationState()` reports pending, complete,
or failed: completion means a QoS 1 PUBACK, or completion of a QoS 0 local socket
write, **not delivery to subscribers**. Tickets from an old connection fail.
Disconnected sends are rejected. The connector limits pending publications and
allows at most one outstanding QoS 0 packet per topic; a 5-second outstanding
delivery deadline aborts a stalled connection rather than replaying its queue.

All connector/device/function operations must remain on one owning thread.
Callers retain ownership of registered devices: connector registration uses weak
references to avoid a device/connector ownership cycle. Keep a `shared_ptr` for
each device for its full active lifetime.

Use `shutdown()` for bounded offline publication and acknowledgement before
disconnecting; it defaults to 5 seconds and reports whether offline was
acknowledged (or the connector was already disconnected). A failed flush logs an
error and preserves the Last Will by closing without a clean DISCONNECT.

## Dependencies

### External

- [libmosquitto](https://mosquitto.org/) — MQTT client library
- [nlohmann_json](https://github.com/nlohmann/json) — JSON serialization
- [spdlog](https://github.com/gabime/spdlog) — Logging

## Details

- **Language**: C++20
- **Version**: 1.2.0
