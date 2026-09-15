# mFi Home Assistant companion integration plan

Status: MVP 0.1.0 implemented locally; not deployed or published. Research
completed September 14, 2026, against Home Assistant 2026.9.2 and HACS 2.0.5,
with current HACS validation behavior checked separately.
The selected architecture is a companion to Home Assistant's existing MQTT
integration, not a replacement.

## Local implementation status

`custom_components/mfi/` now implements the device-selection flow, per-port
energy, atomic checkpoint generations, availability tracking, source registry
reconciliation, exclusions, source rebinding, device-identity recovery, Repairs,
and diagnostics. Development instructions and the local-only distribution
builder are in [home-assistant/](../home-assistant/).

Automated coverage exercises actual HA MQTT sensor/expiration code with a
mocked transport, Recorder energy sum statistics, cancellation-safe checkpoint
ordering, and packaging of the actual runtime snapshot in a temporary Git
repository. The integration and distribution artifact have not been exercised
against a live HACS installation, a real broker/device rollout, or the
destination repository. HACS/hassfest release acceptance remains a release
gate, not a claimed outcome of local tests.

The supported local development baseline is locked in `requirements-ha.lock`.
On a final-checkpoint failure during unloading, HA enters `failed_unload` and
cannot simply retry unloading. The runtime retains pending totals and resumes
checkpoint retries; fix storage, allow a successful save, then restart HA.
This HA lifecycle limitation is documented in the operations guide.

## Confirmed decisions and remaining audit

| Item | Decision |
|---|---|
| Architecture | MQTT companion; preserve existing MQTT entities |
| Supported Home Assistant | 2026.9.2 and newer; no older-version compatibility requirement |
| First-release distribution | HACS custom-repository installation is required at launch |
| Release isolation | Develop in this monorepo; publish reviewed integration-only snapshots to a separate distribution repository |
| Distribution destination | `shmuelie/mfi-home-assistant`, public for HACS; naming it does not authorize creation or publication |
| Existing energy helpers/history | Unknown; audit deferred until implementation is ready, before deciding on new counters, exclusions, or migration |
| Deployed publisher readiness | Unknown; read-only readiness audit deferred until implementation is ready, before rollout |

The setup audit requires separately provided access or redacted configuration
exports; no live Home Assistant or device inspection has been performed.
Inventory existing Integral/utility-meter/template energy entities, their source
ports, and Energy dashboard references. Preserve them while the user decides on
coexistence or migration; do not assume that starting at zero is acceptable.
The MVP supports excluding ports with existing helpers. If history migration is
chosen, define a separate, reviewed mapping and statistics migration before
touching those entities; copying their current number is not history migration.

For publisher readiness, verify the actual executable version and effective
refresh/expiration settings, inspect discovery availability configuration, and
check for retained numeric power replay with a read-only subscriber. Record
upgrade or cleanup requirements without deploying, restarting, publishing, or
deleting anything. Any required rollout follows the separately approved
freshness migration runbook. These audits gate production rollout, not isolated
implementation work. The user explicitly deferred both audits until
implementation is ready; do not request access or collect evidence now.

No additional product decision is needed to implement the isolated MVP.
Deployment-specific evidence and authorization to create/publish the
distribution repository remain prerequisites for rollout. The destination
repository is selected; its existence and publication access have not been
verified.

## Outcome and scope

Create `custom_components/mfi/` so users can select an existing mFi MQTT device
and automatically receive one cumulative energy sensor for every eligible
power-measuring port. No per-port YAML or manually created Integral helpers
should be required.

MQTT continues to own power, current, voltage, relay control, discovery,
availability, broker credentials, and reconnection. The companion owns only
derived energy, source bindings, and its configuration lifecycle. Existing
entity IDs, automations, and MQTT topics remain unchanged.

Initial scope is devices exposed by this repository's `mfi-mqtt-client`, not
every Ubiquiti product or a stock mFi controller. Energy is estimated consumption
since the companion was configured, not a hardware lifetime meter or a
billing-grade measurement.

## Existing foundation

| Repository evidence | Consequence for the integration |
|---|---|
| `mfi-mqtt-client/src/mfi_mqtt_client/port.cpp` creates power in W, current in A, voltage in V, and a relay per port | Discover existing entities; do not recreate these platforms |
| `mfi-mqtt-client/README.md` documents client 2.0.0 power freshness | Require the refreshed, expiring power contract for supported energy calculation |
| Power normally refreshes every 60 seconds and expires after 180 seconds; both are configurable | Constant power must continue accumulating without depending on numeric state changes |
| Power is non-retained; shared and per-channel availability are retained and combined with `all` | Honor MQTT's availability decisions rather than treating cached values as fresh |
| `device.cpp` publishes Ubiquiti manufacturer and board model metadata | Use metadata and MQTT ownership to identify candidate devices |
| Device identity comes from hostname and source identities include port labels | Do not promise that changing a physical label or hostname preserves source identity |
| `device.cpp` obtains `sw_version` from the board firmware version | Do not mistake discovery's software version for the MQTT client version |
| `mfi/src/mfi/board.cpp` enumerates eight-port board `0xe648` and one-port board `0xe671`; discovery renders model IDs as decimal strings `58952` and `58993` | Use these verified IDs for initial matching and count checks; enumerate sources dynamically rather than manufacturing entities from an expected count |

Changing to non-retained power does not delete legacy retained samples. Existing
brokers must follow the separately authorized
[MQTT freshness migration](mqtt-freshness-migration.md). Installing the companion
must never publish cleanup messages or change the device's configuration.

## Architecture and setup

```text
mFi hardware -> mfi-mqtt-client -> MQTT broker -> Home Assistant MQTT
                                                    |
                                         existing power entities
                                                    |
                                      mFi energy companion
                                                    |
                                     per-port kWh entities
                                                    |
                                    Recorder / Energy dashboard
```

1. Add a Python custom integration with domain `mfi`, `config_flow: true`,
   `dependencies: ["mqtt"]`, `iot_class: "local_push"`, and
   `integration_type: "device"`. Use one config entry per selected source device.
   Include a custom-integration version, documentation, issue tracker,
   codeowners, and `requirements: []`. Set the flow unique ID from the MQTT
   config-entry ID and source device-registry ID, not a hostname. Abort duplicate
   flows before creating storage or entities.
2. Provide an Add Integration flow that lists candidate devices already owned
   by MQTT. Match the manufacturer's metadata and verified mFi model/model-ID
   fixtures; manufacturer alone is insufficient. Allow explicit selection for
   an unrecognized model only after validating its eligible power entities.
3. Show the detected ports and explain the required freshness contract and
   legacy retained-state cleanup. Require confirmation of that prerequisite;
   entity registry metadata alone cannot prove the publisher version or that
   the broker has been cleaned. Direct users to MQTT setup when it is missing.
4. Discover sources from entity/device registries and sensor metadata:
   `sensor` domain, `mqtt` platform, selected source device, `power` device
   class, `measurement` state class, and a supported power unit. Support W and
   kW with explicit conversion. Never infer eligibility from `_power` suffixes.
5. Automatically create energy sensors for all eligible enabled, non-excluded
   sources after setup confirmation.
   Reconcile registry changes and late-arriving metadata so adding a port does
   not require reinstalling the companion. Preserve user-disabled entities;
   report disabled power sources rather than enabling them. Preserve exclusion
   choices across reloads and later reconciliation.
6. Use Home Assistant state/event helpers to observe the existing sources.
   Subscribe only to relevant entities and registry events, with a timer for
   constant-power accumulation. Do not read MQTT's private runtime structures,
   create another broker connection, or subclass MQTT's entity implementations.
7. Handle MQTT loading after the companion and sources temporarily disappearing
   without deleting configuration. Set up the companion in a waiting state
   when appropriate, then reconcile when the sources become available.

### Concrete Home Assistant API boundaries

| Concern | Implementation decision for HA 2026.9.2 |
|---|---|
| Runtime ownership | Typed config-entry `runtime_data` holds one source manager, accumulator set, checkpoint writer, and unsubscribe callbacks |
| Platforms | Await `hass.config_entries.async_forward_entry_setups(entry, [Platform.SENSOR])`; implement awaited unload and `async_unload_platforms` |
| Source enumeration | Use `entity_registry.async_entries_for_device(..., include_disabled_entities=True)` and the owning device/config-entry metadata; obtain power class/unit/state class from registry capabilities and live states |
| Source changes | `async_track_state_change_event` for selected sources; handle attribute changes and `new_state is None`, not just numeric changes |
| Unchanged reports | `async_track_state_report_event` may supplement processing, but is not a required heartbeat or freshness proof |
| New entities | Filter entity/device-registry update events and `async_track_state_added_domain(..., "sensor", ...)`; debounce reconciliation and subscribe before the initial scan |
| Clock | One `async_track_time_interval` per selected device at 60 seconds; account using `hass.loop.time()`, not its wall-clock callback argument |
| Cleanup | Register listener/timer disposers with `entry.async_on_unload`; explicitly await the checkpoint writer during orderly unload/stop rather than relying on background-task cancellation |
| Errors | Translated flow errors and Repairs for missing sources, invalid checkpoints, and storage faults; transition-based diagnostic logging |

There is no raw MQTT subscription in the companion MVP, so neither broker
credentials nor `mqtt.async_wait_for_mqtt_client` is needed in the normal source
tracking path. The MQTT dependency orders integration setup, not availability of
every MQTT entity: late source reconciliation is still required. Do not add a
broad manifest MQTT discovery subscription or instantiate a polling coordinator.

Device presentation needs explicit care: Home Assistant 2026.9.2 scopes device
ownership and identifiers to one config entry. Reusing an MQTT identifier does
not merge an mFi-owned device into the MQTT record. Create one clearly named
energy-companion device per config entry and expose its source-device reference
in configuration and diagnostics. Do not move MQTT entities, use deprecated
ownership-transfer shims, or misrepresent the source as a routing hub with
`via_device_id`. Verify presentation against the pinned supported HA releases.

Adding another physical device requires adding/selecting that device once;
automatic creation applies to all its ports, including subsequently discovered
ports. Broker-wide enrollment without confirmation is not part of the MVP.

## Identity and lifecycle

- Assign each energy sensor a persistent port-binding ID, with a unique ID such
  as `<companion-entry-id>_<binding-id>_energy`. Store the source entity registry
  ID and its MQTT unique ID as binding metadata, not its editable entity ID as
  the only lookup key.
- A Home Assistant entity rename must update the reference without changing the
  energy sensor's identity or accumulated total. Match registry changes before
  creating new bindings to avoid duplicate counters.
- Physical label/hostname changes can produce new upstream unique IDs. Preserve
  orphaned totals and surface a repair/rebind workflow; never guess a
  replacement by display name. User-confirmed rebinding preserves the energy
  sensor ID and starts a new sampling baseline without bridging the gap.
- For a genuinely new source, automatically create one new binding. When an old
  source disappears and a new one appears ambiguously, require reconciliation
  rather than silently creating a replacement for the missing port.
- Old retained discovery can leave an obsolete source registered after a
  physical label change. A registry entry still existing is not proof that it
  is a different physical port. Use the verified board port count as a guard,
  retain missing/unavailable bindings, and stop automatic enrollment when new
  sources compete with orphaned bindings or would exceed the device's port
  count. Count excluded/disabled candidate sources too; they must not make a
  duplicate discovery look like an unused physical port. Show an explicit
  mapping/ignore choice. For an unrecognized model,
  require confirmation of an ambiguous mapping rather than guessing a count.
  Stable physical port IDs are not in today's discovery payload; transparent
  firmware-label migration cannot be guaranteed without a future protocol
  extension.
- Unload must remove listeners, cancel timers, and persist pending totals.
  Reload must not duplicate entities or integrations of the same time interval.
  Removing the companion removes only its resources, never MQTT discovery,
  source entities, broker records, or Recorder history directly.

## Automatic energy calculation

### Calculation and entity contract

Use left-hand rectangular integration, suitable for outlet loads that remain
constant and then change abruptly:

```text
energy_delta_kWh = previous_power_W * elapsed_seconds / 3_600_000
```

On a valid power change, settle the preceding interval using the previous
power, then establish the new power baseline. A 60-second timer also settles
intervals while a source remains valid, so an unchanged load still produces
energy updates. Timer and source callbacks must share one accounting cursor to
prevent double counting.

Use monotonic elapsed time during a running HA process. Use `Decimal` for the
parsed source value, normalized watts, and accumulated kWh; serialize totals as
decimal strings. Convert each measured elapsed duration explicitly rather than
mixing binary floats into Decimal arithmetic. Expose four decimal places of
display precision without repeatedly rounding intermediate increments.

Source callbacks settle time and update in-memory baselines synchronously on
the HA event loop. Persistence uses immutable snapshots in an executor and must
not block those callbacks. Numeric energy reporting is batched at the 60-second
interval; availability changes are immediate. Skip unchanged checkpoints, for
example for an all-zero device, unless binding metadata changed.

| Property | Value |
|---|---|
| Entity name | Source port's display name plus `Energy`, without duplicating `Power` |
| Unit | `kWh` |
| Device class | `energy` |
| State class | `total` |
| `last_reset` | Unset |
| Reset behavior | No scheduled or device-reboot reset |
| Default enabled | Yes, for each eligible enabled, non-excluded power source |

`total` without `last_reset` follows HA's recommendation for a cumulative value
that never resets. `total_increasing` is not needed merely because consumption
is nonnegative. These attributes support long-term statistics and the Energy
dashboard. Users still choose the sensors in the dashboard's individual-device
configuration; the integration must not rewrite dashboard preferences.

Use the built-in Integral integration as the behavioral reference, not an
integration dependency. Its `_IntegrationMethod` and `_Left` implementations
live inside its sensor platform; `IntegrationSensor` also owns its restoration
and helper-specific lifecycle. They are not a reusable public calculation API.
Implement the small left accumulator independently behind an mFi-owned
`SensorEntity`, reusing HA's event, JSON/file, and timer helpers. Do not import or
subclass the Integral platform, or create its helper config entries.

### Availability and gaps

- Accumulate only from finite, nonnegative, available power. Zero W is valid.
  Unknown, unavailable, malformed, negative, nonfinite, or unsupported-unit
  states must suspend calculation and surface a diagnostic reason.
- On a transition to unavailable, settle only the preceding valid interval up
  to that transition, then clear the power/time baseline. Keep the saved total
  internally and mark the energy entity unavailable.
- Recovery establishes a new baseline. Never interpolate across an unavailable
  interval, an integration reload, or Home Assistant downtime.
- Do not equate `last_changed` with freshness: identical numeric readings may
  remain unchanged while MQTT refreshes their expiration. MQTT remains the
  authority for source availability and expiration.
- `last_reported` and state-report events also cannot establish sample age:
  HA's MQTT sensor renews its expiration on message receipt, but the MQTT entity
  can suppress a state write when the tracked attributes did not change.
  Availability-only changes can write an old numeric value too. Keep the
  independent timer and never impose a second timeout based on these fields.
- When starting, do not restore a power reading or an old integration timestamp
  as a live baseline. Start at the current time only after a non-restored,
  valid source state is available (`ATTR_RESTORED` must not be true). A reload
  may use the currently available source at the current time; it must not
  integrate backwards to that state's timestamps.

There is a documented accuracy limit: an undetected outage can continue to
look available until MQTT expiration. At 100 W and a 180-second expiration,
that interval can contribute up to 0.005 kWh after the last received sample.
That is the nominal timeout contribution under a responsive event loop, not a
hard real-time bound: delayed HA callbacks can add scheduling error. Stop at the
observed unavailable transition and record its processing time; test nominal
expiration with a controlled clock and delayed scheduling separately. Arbitrary
broker delays remain undetectable because the existing numeric payload carries
no original sample timestamp. A reconnect must not bridge the offline interval,
but source availability alone cannot prove a new post-reconnect sample.
Never claim that a companion eliminates these upstream limitations.

### Persistence

**Do not treat `Store.async_save()` as a commit acknowledgement.** In the pinned
HA source, Store catches and logs serialization/write failures and may defer
writes during shutdown. Merely awaiting it would not satisfy the no-published-
but-unsaved-total requirement.

Use one integration-owned versioned JSON checkpoint per companion, at
`hass.config.path(".storage", "mfi_energy.<storage_id>")`. Write through
`homeassistant.helpers.json.save_json(..., private=True, atomic_writes=True)` in
`hass.async_add_executor_job`; this helper propagates errors and its atomic
writer uses replacement and fsync. This is a deliberate narrow deviation from
Store, not a private-method override. Keep filesystem I/O off the event loop.

1. On final successful flow confirmation, generate a storage ID and bindings,
   and save an initial zero checkpoint before returning the config entry.
   Keep that storage ID in the entry; retries reuse the pending bootstrap
   rather than creating duplicate files. A pre-entry failure must not create a
   success-shaped configured integration. An aborted flow cleans up only its
   own unreferenced bootstrap file, never an established entry's checkpoint.
2. Each checkpoint contains a schema version, storage ID, increasing generation,
   source/binding metadata, and decimal-string totals. Validate its entire shape
   and finite/nonnegative values. Load with HA's JSON loader using an explicit
   missing sentinel such as `None`; a missing file for an existing entry is an
   error, never permission to initialize all counters again.
3. Use a single serialized writer per companion. Snapshot in the event loop,
   save in the executor, then publish only the totals from the successfully
   committed generation. Do not publish mutable newer values accumulated while
   the write was pending. Coalesce another pending save instead of creating an
   unbounded queue.
4. On failure, keep the last committed totals, mark energy reporting unavailable,
   and raise a Repairs issue. Retain unsaved increments in memory while valid
   source tracking continues; retry at the reporting cadence without resetting
   or integrating intervals twice. Recovery publishes a committed snapshot and
   clears the issue. Do not replay stale availability when a pending save ends.
5. On unload or `EVENT_HOMEASSISTANT_STOP`, stop accepting new intervals at a
   defined monotonic boundary and await the final save. An executor write cannot
   safely be canceled merely by canceling its awaiter; prevent a new runtime
   writer from racing an unfinished old one.
6. Restore totals and bindings only, never power or a pre-restart time cursor.
   Do not layer `RestoreSensor` over the checkpoint as a competing authority.
   Missing, corrupt, or unsupported-version data requires recovery from backup
   or an explicit repair decision; never auto-reset a `total` sensor. Downgrades
   must reject an unreadable schema without rewriting it.

Atomic/fsync writes are intentionally limited to one changed snapshot per
device per reporting interval, plus configuration and shutdown boundaries, not
one write per MQTT sample. Measure write latency and HA responsiveness in the
multi-device rehearsal.

Under normal successful writes, an abrupt HA process crash can lose consumption
since the last checkpoint, approximately one reporting interval plus in-flight
write latency. Storage failures can lengthen that gap. A saved-but-not-yet-
published total is safe to expose on restart; it must not be counted again.
Neither fsync nor this design guarantees survival of faulty storage, mismatched
backup restoration, or complete power-loss behavior on every filesystem.

## Proposed files

```text
custom_components/mfi/
  __init__.py          # Config entry setup, runtime ownership, unloading
  manifest.json
  const.py
  config_flow.py       # Device selection, prerequisites, options/rebinding
  source.py            # Registry discovery and source lifecycle tracking
  energy.py            # Pure interval accounting and validity state machine
  sensor.py            # Energy SensorEntity implementation
  storage.py           # Error-reporting atomic checkpoints and schema validation
  repairs.py           # Missing source/storage repair flows
  diagnostics.py       # Source mapping and calculation health, redacted
  brand/icon.png
  strings.json
  translations/en.json
tests/home_assistant/
  conftest.py
  test_config_flow.py
  test_sources.py
  test_energy.py
  test_sensor.py
  test_lifecycle.py
  test_storage.py
  test_mqtt_contract.py
home-assistant/
  hacs.json            # Copied to the distribution repository root
  README.md            # Integration installation and operations guide
  CHANGELOG.md         # Integration-only versions
```

Add Python dependency/test configuration and a separate HA workflow without
changing the CMake build. Keep the integration dependency-free beyond Home
Assistant at runtime. The files under `home-assistant/` are release metadata,
not another runtime package. Distribution copies them to its root; the
monorepo itself is not the HACS installation URL.

## Delivery milestones

| Milestone | Deliverable and exit condition |
|---|---|
| 1. Scaffold and contract fixtures | Use the researched HA/Python/test pins; create registry/MQTT fixtures, config-entry runtime skeleton, and distribution artifact builder; test unchanged-reading expiration and source/device ownership |
| 2. Companion setup | Config flow, source bindings, same-source deduplication, automatic port reconciliation, waiting/recovery states, and safe unload |
| 3. Energy entities | Correct interval accounting, availability boundaries, persistent kWh totals, and Recorder-compatible metadata |
| 4. Lifecycle hardening | Restart/reload, source rename/removal, ambiguous replacement repair, corrupt storage, multiple devices, and disabled-entity behavior |
| 5. Distribution and rollout | After separate publication approval, verify HACS install/update from the distribution repository, CI, operations documentation, readiness/history audit outcome, and independent component changelog/version |

## HACS distribution and release contract

HACS selects releases repository-wide and uses release tags as versions.
Neither an integration tag prefix nor `filename` filters unrelated releases.
The user selected **`shmuelie/mfi-home-assistant`** as the public,
integration-only distribution repository to avoid changing the C++ updater or
coupling component versions. This is the planned HACS custom-repository
destination, not a claim that the repository has been created.

The monorepo remains the development source of truth. Build a reviewed snapshot
from an explicit source commit; copy only `custom_components/mfi/`, the
integration README/changelog, HACS metadata, and applicable license/attribution
files to the distribution tree. Record the source commit and artifact checksum
in the release notes. Use a protected, explicitly approved publication workflow
with a narrowly scoped credential for the destination repository. Repository
creation, credential provisioning, and publication are not authorized by this
plan.

The destination uses ordinary `v1.0.0`, `v1.0.1`, etc. tags with corresponding
manifest versions `1.0.0`, `1.0.1`. Every tagged tree must include exactly one
integration under `custom_components/`, with all its runtime files present;
uploading a ZIP without the tagged source is insufficient.

Copy this metadata to the distribution repository root:

```json
{
  "name": "mFi",
  "content_in_root": false,
  "zip_release": true,
  "filename": "mfi.zip",
  "hide_default_branch": true,
  "homeassistant": "2026.9.2"
}
```

Attach `mfi.zip` containing the **contents** of `custom_components/mfi/` at the
archive root: `manifest.json`, `__init__.py`, other modules, translations, and
`brand/icon.png`. Do not wrap them in `mfi/` or `custom_components/mfi/`; HACS
extracts directly into the destination integration directory. No C++ binaries,
tests, credentials, build outputs, or monorepo metadata belong in that archive.

Build and validate the source/artifact before publication, upload assets to a
draft release, then publish the complete release. Never rewrite a published
version. Assert that the tagged manifest, ZIP manifest, and release tag agree.
Run HACS integration validation and hassfest against the prepared distribution
layout; explicitly select the intended ref using `REPOSITORY_REF` where needed
rather than assuming a tag-triggered action validates that tag.

Bundle an original or appropriately licensed `brand/icon.png`: local custom
integration branding is supported by the selected HA baseline. Use a HACS
validator revision supporting bundled icons; older action validators expected
central-brand registration. Some HACS UI surfaces still use the brands CDN,
so do not promise that a bundled icon covers every HACS image.

Record tested HACS and action revisions/container digests in release tooling.
Research checked HACS 2.0.5 plus a newer validation revision; source inspection
does not substitute for a real HACS install. Verify install, upgrade, reinstall,
and schema-compatible downgrade in isolated HA. Users add the **distribution**
repository as type Integration; catalog inclusion is not required. Subsequent
C++ releases must have no effect on the HACS version feed.

## Verification and acceptance criteria

Use Python **3.14.2 or newer within the supported 3.14 series** for the minimum
version job: HA 2026.9.2 explicitly requires `>=3.14.2`. Pin
`pytest-homeassistant-custom-component==0.13.365`, whose published metadata pins
`homeassistant==2026.9.2`, and lock its compatible test dependencies. Enable the
`enable_custom_integrations` fixture and `asyncio_mode = auto`.

Exercise HA 2026.9.2 and the current supported stable release in separate CI
environments as they diverge, pairing each with its matching test-plugin
version. Do not force a newer HA through the baseline plugin's exact pin.
Use an injectable monotonic clock for accumulator tests and HA's timer/MQTT
fixtures for lifecycle tests. Add Ruff and mypy checks scoped to the Python
component without changing the CMake job.

Include a real isolated-broker rehearsal for availability/retention and a real
HACS installation before release: the implemented mocked-transport tests and
local artifact checks do not prove either physical wire freshness or live HACS
installation. Those environment-level rehearsals remain pending.

| Scenario | Required result |
|---|---|
| One-port and eight-port devices | Exactly one energy entity per eligible enabled, non-excluded power source, without manual helpers |
| Excluded or user-disabled source | Reconciliation does not enable it or create an unwanted replacement counter |
| 100 W for one hour, unchanged numeric value with valid refreshes | 0.1 kWh increase; unrounded calculation error below 0.000001 kWh |
| 100 W for 30 minutes, then 200 W for 30 minutes | 0.15 kWh increase using the left method |
| 0 W for an hour | No increase and no false invalidation |
| 0.1 kW input | Same result as 100 W |
| Source event and timer at the same boundary | Each elapsed interval counted once |
| Missing refreshes at 100 W with 180-second expiration, controlled scheduler | MQTT makes the source unavailable at 180 seconds; no more than 0.005 kWh after the last sample, within numeric test tolerance |
| Identical MQTT payloads suppress HA state writes | Expiration remains renewed and the independent timer still accumulates 0.1 kWh per hour at 100 W |
| Wall-clock jump or delayed HA callback | Wall-clock changes cannot create or subtract energy; delayed outage handling is measured and documented rather than claiming a hard expiry-time guarantee |
| Explicit offline, invalid sample, or one failed port | Affected energy source suspends; unrelated ports continue |
| Ten-minute unavailable gap followed by recovery | No energy attributed to the gap; total does not reset |
| Restart/reload with a saved total and hours of downtime | Saved total restored; no downtime integration or duplicate listeners |
| Persisted total is unavailable or corrupt | Visible repair/error; no success-shaped zero reset |
| Checkpoint write fails | No larger energy state published before a successful checkpoint |
| MQTT changes or offline transition while checkpoint is pending | Publish only that snapshot's committed total, with current availability; no lost increments, stale online state, or overlapping writer |
| Crash after save and before state publication | Restore the committed generation without integrating its intervals again |
| Corrupt/missing checkpoint, bootstrap failure, or unsupported downgrade | No counter reset, no overwritten evidence, and an actionable setup/repair error |
| HA source entity renamed | Same energy unique ID and total, automatically rebound reference |
| Physical port label changes upstream identity | Ambiguity is surfaced; confirmed rebind retains history |
| Old label-derived retained discovery remains after rename | Count/orphan safeguards prevent silently assigning competing counters to the same physical port |
| New source, repeated registry events, and duplicate setup | Exactly one binding per source; no duplicate energy sensors |
| Companion removed | Existing MQTT entities and relay operation remain unchanged |
| Legacy retained numeric power | Rehearsal demonstrates replay risk; installation does not claim cleanup or erase broker records |
| Recorder and Energy dashboard | Correct kWh statistics without resets/unit errors; sensor is selectable for individual-device energy |
| Existing energy helpers/history discovered during audit | No helper or history changed before the user selects coexistence or a reviewed migration |
| Publisher readiness unknown | Audit reports evidence and missing prerequisites; no automatic device upgrade or broker cleanup |
| HACS custom repository | Installs only the integration runtime files, enforces the minimum HA version, and upgrades without losing bindings/totals |
| Integration release artifact | ZIP has runtime files at its root; tag, source, archive manifest versions and recorded source checksum/provenance agree |
| New C++ release in the development repository | No change in the distribution repository's HACS available version |

Daily/monthly utility meters, tariffs, cost calculation, aggregate strip totals,
export/net-energy support, device-side metering, and ownership migration from
MQTT are outside the MVP. Users can layer HA's existing helpers on the cumulative
per-port sensors where needed.

## References

Primary source pins: HA 2026.9.2 commit
`33c3e0cca60e73a8c4970ee677d75b8bc6464cdf`; HACS 2.0.5 commit
`c0dfd8b44297c3673c21973e2539375a53687a9c`; newer HACS validation
revision `adb7d83e33d24325535fb43b8226572405143757`.

- [Integration manifests and MQTT dependencies](https://developers.home-assistant.io/docs/creating_integration_manifest/#mqtt)
- [Device ownership and registry APIs](https://developers.home-assistant.io/docs/device_registry_index/)
- [HA 2026.9.2 device registry](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/device_registry.py)
- [HA 2026.9.2 source event helpers](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/event.py)
- [HA 2026.9.2 entity registry identity and rename events](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/entity_registry.py)
- [Entity registry and unique IDs](https://developers.home-assistant.io/docs/entity_registry_index/)
- [Sensor state classes, restoration, and statistics](https://developers.home-assistant.io/docs/core/entity/sensor/)
- [Integral helper, left integration, and timed updates](https://www.home-assistant.io/integrations/integration/)
- [Integral platform's internal calculation and lifecycle](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/components/integration/sensor.py)
- [MQTT expiration renewal](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/components/mqtt/sensor.py#L287-L301)
- [MQTT unchanged-state write suppression](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/components/mqtt/entity.py#L1707-L1733)
- [Store write-error handling](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/storage.py#L569-L589)
- [Error-propagating JSON save helper](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/json.py)
- [Atomic/fsync writer and WriteError](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/util/file.py)
- [HA Python/dependency requirements](https://github.com/home-assistant/core/blob/2026.9.2/pyproject.toml)
- [Matching custom-integration test package metadata](https://pypi.org/pypi/pytest-homeassistant-custom-component/0.13.365/json)
- [Energy dashboard requirements](https://www.home-assistant.io/docs/energy/faq/)
- [HACS integration repository requirements](https://www.hacs.xyz/docs/publish/integration/)
- [HACS metadata options](https://www.hacs.xyz/docs/publish/start/#hacsjson)
- [HACS release selection](https://github.com/hacs/integration/blob/c0dfd8b44297c3673c21973e2539375a53687a9c/custom_components/hacs/repositories/base.py#L1100-L1124)
- [HACS release ZIP extraction](https://github.com/hacs/integration/blob/c0dfd8b44297c3673c21973e2539375a53687a9c/custom_components/hacs/repositories/base.py#L559-L603)
- [HACS current bundled-brand validation](https://github.com/hacs/integration/blob/adb7d83e33d24325535fb43b8226572405143757/custom_components/hacs/validate/brands.py)
- [HACS validation ref selection](https://github.com/hacs/integration/blob/adb7d83e33d24325535fb43b8226572405143757/action/action.py#L124-L160)
