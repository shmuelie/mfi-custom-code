# mFi Home Assistant energy companion

An MQTT companion for the mFi devices exposed by
[`mfi-mqtt-client`](https://github.com/shmuelie/mfi-custom-code/tree/main/mfi-mqtt-client).
It derives one cumulative energy sensor per eligible power-measuring port.
Home Assistant's MQTT integration continues to own the original sensors,
relays, discovery, availability, connection, and credentials.

**Status: 0.1.0 is an unpublished MVP.** Distribution preparation is local only.
The planned public HACS repository is `shmuelie/mfi-home-assistant`; its existence,
publication access, and install/update behavior have not been verified. The
development source remains
[`shmuelie/mfi-custom-code`](https://github.com/shmuelie/mfi-custom-code).
Do not add the C++ monorepo to HACS: HACS selects releases repository-wide;
tag prefixes and an asset filename cannot filter out C++ releases.

The distribution baseline is source-only: tests copy the current runtime,
release metadata, and license into a temporary local Git repository, commit
only that copy, and run the builder CLI against its exact source snapshot.
Additional synthetic fixtures exercise exclusions and filesystem safeguards.
The checks cover the allowlisted prepared tree, flat ZIP layout, version
consistency, source commit provenance, and checksums. Preparing a reviewed
release artifact still requires an explicit commit of the real source.
These local checks do not establish HACS acceptance: no live HACS install,
update, reinstall, or downgrade has been verified.

## Requirements and rollout gates

Use **Home Assistant 2026.9.2 or newer** with its MQTT integration configured.
The tested minimum is pinned through
`pytest-homeassistant-custom-component==0.13.365`; newer HA releases require
their own matching test-plugin validation as they diverge. Development requires
Python **3.14.2 or newer in the 3.14 series**. The companion has no third-party
runtime requirements beyond Home Assistant.

Supported sources are MQTT-owned sensors with power device class, measurement
state class, and W or kW units, belonging to the selected mFi device. Initial
recognized boards are model IDs `58952` (eight ports) and `58993` (one port).
An unrecognized model requires explicit selection and eligible power sources;
the Ubiquiti manufacturer name alone is not proof of compatibility.

Production installation is gated on separately approved, read-only audits:

- Verify the deployed MQTT client and effective freshness settings. Client
  2.0.0 documents non-retained numeric power, a default 60-second refresh, and
  180-second expiration. The discovery firmware version is not the client
  version. Entity metadata cannot prove that the publisher is ready.
- Inventory existing Integral, utility-meter, and template helpers, their
  statistics, and Energy dashboard references. Exclude ports with existing
  helpers until coexistence or a separate history-migration plan is chosen.
  Copying today's total does not migrate statistics history.

These audits are deferred; no live HA or device access is requested here.
Legacy retained power can replay old samples even after a publisher upgrade.
Follow the separately authorized
[MQTT freshness migration runbook](https://github.com/shmuelie/mfi-custom-code/blob/main/docs/mqtt-freshness-migration.md).
Installing this companion does not clear retained messages, upgrade devices,
change broker settings, or authorize deployment.

## Installation after release acceptance

Once a complete release has been approved and published, add
`https://github.com/shmuelie/mfi-home-assistant` in HACS **Custom repositories**
as type **Integration**. Install mFi, restart Home Assistant, and use
**Settings > Devices & services > Add integration > mFi**.

Select an existing MQTT source device, review its detected ports, and exclude
initial power sources that should not receive new counters. Confirm both the
verified publisher freshness prerequisite and the decision to start new
energy counters; this does not import existing helper totals or history.
Add each physical device once. Enabled, non-excluded eligible ports are
enrolled automatically; existing user-disabled sources remain disabled.
The companion appears as a separate `<device> Energy` device, not a transfer
of MQTT device ownership. Existing MQTT entity IDs, automations, topics, and
relay behavior are unchanged.

For an isolated, local rehearsal only, copy the prepared
`repository/custom_components/mfi/` directory into the test HA configuration's
`custom_components/mfi/`, then restart that instance. Alternatively, extract
the flat `mfi.zip` **into** that integration directory, not into
`custom_components/`. Do not apply either procedure to a live installation
without rollout approval. Back up the complete HA configuration first.

## Energy behavior and operations

The energy sensors report estimated consumption in kWh since configuration,
not a hardware lifetime meter or a billing-grade measurement. Left-hand
integration uses the preceding power reading, with a 60-second reporting
cadence so steady loads continue to accumulate. Units are normalized from W
or kW. The energy device class, `total` state class, and absence of `last_reset`
support long-term statistics; users select sensors in the Energy dashboard's
individual-device configuration themselves.

Unknown, unavailable, restored, nonfinite, negative, or unsupported-unit power
cannot establish a live sampling baseline. Saved totals survive valid
restarts, but no energy is invented for downtime or unavailable gaps.
Home Assistant entity renames preserve source bindings. Physical hostname or
port-label changes can change MQTT identities: retain orphaned totals and use
the explicit repair/rebind flow rather than guessing replacements by name.
The integration's options menu offers **exclude**, **rebind**, **ignore**, and
**Recover after an upstream device identity change**. A confirmed rebind
preserves the energy total and ignores the old registry source so it is not
enrolled again. Do not delete a companion merely to resolve a missing source.
Unavailable or disabled source identities still take part in ambiguity detection,
even when their counters are excluded. A new MQTT identity competing with one
of those bindings requires explicit reconciliation, not a new enabled counter.

If a hostname change creates a replacement MQTT device identity, choose
**Recover after an upstream device identity change**, confirm that it is the
same physical device, and select the replacement MQTT device. That device
cannot already belong to another companion or have a pending Add Integration
flow. Cancel that pending setup before using recovery. Creation and recovery
coordinate destination ownership, including rechecking it after checkpoint
writes; a stale confirmation never replaces an established companion.
Existing energy sensor IDs and
totals are preserved, but each counter stays unavailable until explicitly
mapped to its replacement power source using **Rebind**. Recovery does not
automatically match ports by label or infer energy across the gap.

MQTT availability is the freshness authority. Identical numeric readings need
not change HA state timestamps, so those timestamps are not a second timeout.
There is an upstream accuracy limit: at 100 W, a nominal 180-second expiration
can add up to 0.005 kWh after the last received sample. Delayed callbacks can
add scheduling error; broker-delayed payloads have no original sample timestamp.
This companion cannot eliminate those limits or prove a new post-reconnect
sample from availability alone.

Totals and bindings are stored in HA's private
`.storage/mfi_energy.<storage_id>` checkpoints. Metadata and energy snapshots
share a serialized writer using atomic `save_json` writes rather than treating
`Store.async_save()` as a commit acknowledgement. Repeated cancellation does not
release the file-operation lock while an executor write or removal is running.
Options are acknowledged against the snapshot containing that change: if it
commits, a later telemetry/metadata save failure can suspend reporting but does
not undo the committed option or report that option as unsuccessful.
Exclusions and rebindings are staged separately from the effective source
bindings. Until a change commits, valid samples continue under the previous
accounting policy and no new sources are automatically enrolled. A failed save
therefore neither loses/adds energy from a rejected policy nor leaves new
provisional bindings behind. Unignoring a source also waits for commitment; a
failed save leaves it ignored and can be retried after storage recovers.
Closing or canceling an in-flight options request does not interrupt its atomic
checkpoint; the request may still commit before the next operation can begin.

Stopping or reloading the integration rejects waiting configuration requests and
settles in-flight changes before the final checkpoint. Old runtime objects
cannot write after their replacement starts. If Configure reports that the
runtime changed, reopen it once the integration has loaded; do not retry using
the stale form.

Back up the entire HA configuration, including config entries and these files,
as a consistent set.
Do not edit, delete, or initialize missing checkpoints to zero. A missing,
corrupt, unsupported-version, or unwritable checkpoint needs a visible repair
and backup recovery decision. Reported totals advance only after a successful
save; a crash can still lose the unsaved interval, and storage faults can
extend that interval.

If the final checkpoint write fails while unloading or reloading the
integration, unload returns failure and Home Assistant marks the entry
`failed_unload`. The runtime resumes source tracking and save retries to
preserve pending totals in memory rather than silently discarding them.
Recover the disk/storage problem first and allow a successful checkpoint retry;
then restart Home Assistant to recover from `failed_unload`. A simple integration
reload is not sufficient in that state. Restarting before pending totals are
saved can lose them; do not remove the entry or delete its checkpoint as a
workaround.

For troubleshooting, check MQTT source availability and units, the selected
source mapping/exclusions, and HA Repairs/logs. Share only reviewed, redacted
diagnostics; never broker credentials or raw private checkpoints. Removing
the companion must not remove MQTT entities, retained discovery, or Recorder
history. Daily/monthly counters, tariffs, cost, aggregate strip totals,
export/net energy, and history migration are outside this MVP.

## Prepare a distribution locally

The stdlib builder needs Python and local Git, not a token or network access.
Run it in a checkout of the development monorepo, after all required runtime
and release metadata files have been reviewed and committed:

```bash
python home-assistant/build_distribution.py \
  --source-commit "$(git rev-parse HEAD)" \
  --version 0.1.0
```

The SHA must be a complete commit object ID, not `HEAD`, a branch, a tag object,
or an abbreviation. The version is an explicit expected stable version, not a
value silently inferred from a possibly stale manifest. The committed manifest
and newest versioned changelog heading must both match it.

Output is confined to `build/home-assistant/0.1.0-<first-12-SHA-characters>/`:

```text
repository/
  README.md
  CHANGELOG.md
  LICENSE
  hacs.json
  custom_components/mfi/
    manifest.json
    __init__.py
    ...allowlisted runtime modules...
    translations/en.json
    brand/icon.png
mfi.zip
provenance.json
SHA256SUMS
```

`mfi.zip` contains `manifest.json`, modules, `translations/`, `brand/`, and `LICENSE`
directly at the archive root. It has no `mfi/` or `custom_components/mfi/`
wrapper. The prepared tagged source tree, not just the ZIP asset, is required
for HACS. The committed root MIT license is copied verbatim to `repository/LICENSE`
and the packaged integration's `LICENSE`, so the standalone ZIP carries the
copyright and permission notice too.

The builder reads committed Git blobs, never working-tree or staged changes,
and does not execute integration code. It requires every explicitly allowlisted
MVP runtime file. Update and review the allowlist when adding modules,
translations, or packaged assets. Other tracked runtime-tree files are reported
in provenance as excluded; untracked files, caches, tests, C++ binaries,
credentials files, SVG masters, generators, and build outputs are not packaged.
This is an inclusion boundary, not a secret scanner: reviewers must still
inspect allowed Python, JSON, documentation, and image contents for secrets.

Symlinks/submodules within selected source paths, symlink output ancestors, and
any existing output path are rejected. There is no output override, cleanup,
force, overwrite, staging, commit, or publication mode. Failure after creation
can leave a partial output directory: inspect and remove only that exact
generated directory yourself before retrying. Do not run concurrently with a
process that renames output directories. ZIP ordering, timestamps, modes, and
provenance are stable; identical inputs on the same Python/zlib implementation
produce identical artifacts.

`provenance.json` records the source commit, intended destination/version/tag,
excluded tracked paths, and SHA-256 hashes of every prepared file and the ZIP.
`SHA256SUMS` also covers provenance itself. Inside the output directory run:

```bash
sha256sum --check SHA256SUMS
```

Checksums establish integrity, not a trusted signature or proof of publication.
The integration release tag would be `v0.1.0`; it is not a monorepo C++ tag.
Before any separately approved publication, verify that the actual destination
tag, tagged manifest, and ZIP manifest all have that same version.

## Development and validation boundaries

From the monorepo, use the isolated Python test environment:

```bash
python -m pip install --group test -c requirements-ha.lock
python -m pytest tests/home_assistant
ruff check custom_components/mfi tests/home_assistant home-assistant/build_distribution.py
mypy custom_components/mfi
```

`requirements-ha.lock` constrains the test group and its installed dependencies
to the exact versions captured from the isolated HA test environment.
The distribution tests create temporary local Git repositories. The dedicated
HA workflow does not alter the CMake build. It builds a CI artifact, not a GitHub
release or a destination repository. Its automatic token has read-only contents
permission and checkout credentials are not persisted.

The builder checks local archive/layout/metadata consistency. **This is not
hassfest or HACS acceptance.** Source references reviewed for the release
contract are HA 2026.9.2 at
`33c3e0cca60e73a8c4970ee677d75b8bc6464cdf`, HACS 2.0.5 at
`c0dfd8b44297c3673c21973e2539375a53687a9c`, and the HACS bundled-brand validator
at `adb7d83e33d24325535fb43b8226572405143757`. These are inspection references,
not claims of executed tests.

External release acceptance is pending:

- Run hassfest on the **prepared** `repository/custom_components/mfi` tree
  with a reviewed, pinned validator source/image. The inspected hassfest
  action at `26d56ed1a1cbfeabf59feb7b26e368e13f47ef52` invokes an unpinned
  `ghcr.io/home-assistant/hassfest` image, so pinning that action alone does not
  pin the validator. This local MVP does not silently run that mutable image.
- After authorized destination creation/publication, run HACS integration
  validation using a bundled-icon-aware pinned revision and the explicit
  `REPOSITORY_REF`. That action reads GitHub repository/release data; pointing
  it at an unpushed prepared tree cannot validate that tree. Record the
  actually executed action revision/container digest and HACS version.
- Rehearse HACS install, upgrade, reinstall, and schema-compatible downgrade
  in isolated HA. Verify retained totals/bindings, correct minimum HA version,
  and no C++ release leakage. Never downgrade to a version that cannot read
  the stored schema or rewrite a published version.

Only after separate approval should a protected, narrowly credentialed
publication process copy the reviewed tree, create its independent tag, attach
`mfi.zip` to a draft release, include source SHA and archive checksum in the
release notes, and publish the complete release. No such process, credential,
repository creation, or remote write is implemented here.

## Branding and license

The bundled 256 x 256 transparent PNG is original geometric artwork: a power
plug within a home, not a copied Ubiquiti or Home Assistant logo. It uses the
repository's MIT license. In the monorepo the SVG master lives at
`home-assistant/assets/icon.svg` to keep distribution tooling separate from
the C++ project assets; regenerate it with
`python home-assistant/generate_icon.py`. The stdlib generator renders only
the simple SVG elements actually used, and refuses to replace a different
existing icon without deliberate review/removal.

HA 2026.9.2 supports bundled custom-integration branding. Some HACS surfaces
still use the brands CDN; a local icon does not guarantee images everywhere.
