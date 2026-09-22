# mFi Home Assistant companion changelog

Versions here belong only to the `mfi` Home Assistant companion, independently
of this monorepo's C++ executables. No version listed as unreleased has been
published to HACS.

## [0.1.0] - Unreleased

Initial, locally developed energy-companion MVP:

- Derive cumulative per-port kWh from existing mFi MQTT power entities without
  replacing MQTT discovery, power/current/voltage sensors, or relay controls.
- Select one source device per entry, confirm publisher freshness prerequisites,
  and exclude sources whose existing energy helpers should remain authoritative.
- Preserve energy totals and source bindings with integration-owned checkpoints;
  expose storage and source-reconciliation failures instead of resetting totals.
- Prepare integration-only snapshots and flat HACS release archives from an
  explicit source commit, with independent version checks, provenance, checksums,
  original bundled branding, and local distribution tests.

### Added

- Opt-in native mFi discovery with mFi-owned measurement and relay entities,
  one parent and stable native child devices per physical port.
- Session/sequence freshness, rejected retained telemetry, and direct energy
  accounting independent of the Power entity's enabled state.
- Single-attempt QoS 0 relay commands with correlated hardware confirmation
  and explicit timeout/disconnection failures.
- Preserve companion mode on config-entry schema upgrades; changing entity
  ownership remains an explicit, separately approved migration.
- Administrator-only inventory, quiesce, transfer, activation, and rollback
  actions with durable journals, persisted maintenance checks, preserved
  entity/registry/energy identities, and retirement/restoration of the old
  empty companion device.

### Fixed

- Coordinate setup and device recovery so stale confirmations or concurrent
  recovery cannot replace another companion or remove its saved energy.
- Preserve file-operation ordering through repeated task cancellation.
- Acknowledge each options change against its own committed snapshot instead of
  rolling it back after an unrelated later save failure.
- Preserve excluded/disabled source identity safeguards during replacement
  discovery, including partially discovered multi-port devices.
- Drain in-flight configuration before shutdown and reject queued or retired
  runtime operations so they cannot overwrite newer counters after reload.
- Keep unignored sources out of enrollment until their metadata commits, leaving
  a consistent, retryable checkpoint when an unignore save fails.
- Stage rebindings and exclusions until their own checkpoint succeeds. Failed
  changes cannot enroll extra ambiguous counters or alter energy accounting.
- Keep canceled options serialized until their checkpoint acknowledgement so a
  later request cannot replace their in-flight metadata.

Production publisher/history audits, isolated HACS installation and lifecycle
acceptance, creation of the public distribution repository, and publication
remain pending separate evidence and authorization.
