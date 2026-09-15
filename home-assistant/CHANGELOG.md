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

Production publisher/history audits, isolated HACS installation and lifecycle
acceptance, creation of the public distribution repository, and publication
remain pending separate evidence and authorization.
