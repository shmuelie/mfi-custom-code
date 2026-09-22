# Native mFi Home Assistant integration plan

Status: implemented locally alongside the 0.1.0 energy companion; not deployed
or published. Research baseline: HA 2026.9.2, with MQTT
and device-registry behavior also inspected in 2026.9.3. This plan supersedes the
companion-only direction. Existing entries remain in companion mode until
explicit migration.

## Implementation and validation status

The C++ client now supports explicit legacy/native modes, one-time persistent
ID setup, and a read-only migration export. The HA integration implements native
descriptor discovery, port child devices, direct power integration, and
single-attempt correlated relay controls using the configured MQTT client.
Administrative migration actions are wired to a domain-level journal
coordinator; setup blocks unfinished migrations and prepared destinations
cannot be claimed by another discovery flow.

Local coverage includes host C++ tests, the existing legacy and new native
loopback-broker smoke tests, cross-language validation of real publisher packets
against the HA parser, native/companion HA tests, and real persistent registry
restart/rollback probes. A full activation/rollback test uses the actual mFi
runtime and HA MQTT code with mocked broker transport, preserving entity IDs,
user overrides, and latest energy totals.

These checks do not constitute MIPS hardware/flash, production HA/broker, or
live HACS acceptance. The implementation remains unpublished and those rollout
gates remain separately authorized. See the
[operations guide](../home-assistant/) for exact actions and prerequisites.

## Decisions and outcome

| Item | Decision |
|---|---|
| Entity ownership | mFi owns power, current, voltage, relay, and calculated energy |
| Transport | Reuse Home Assistant's MQTT integration and configured connection; no second broker client or credential store |
| Device model | One mFi-owned parent per physical strip, with one native child per physical port |
| Port renaming | Home Assistant only; do not write the physical device's port label |
| Power entity disabled | Continue enabled energy calculation from valid native reports; Power entity enablement is independent |
| Device ID provisioning | Provide an idempotent setup command that generates and persists a UUID once, then reuses it |
| Publisher requirement | Require an upgraded `mfi-mqtt-client` with native mFi discovery; preserve an explicit legacy mode |
| Existing installations | Keep operating in companion mode until an explicitly confirmed ownership migration |
| Migration window | User approved planning a short, coordinated unload of HA's shared MQTT integration; other MQTT entities are temporarily unavailable, but the broker stays running |
| Compatibility | Retain HA 2026.9.2 as the minimum, subject to native-child lifecycle acceptance on that release |
| Distribution | Continue developing here and prepare releases for `shmuelie/mfi-home-assistant`; no repository creation, push, publication, or deployment is authorized by this plan |

The desired result:

```text
mPower Pro                          mFi parent: hardware identity and firmware
  Desk lamp                         Port 1 child: editable name, area, labels
    Power                           W, measurement
    Current                         A, measurement
    Voltage                         V, measurement
    Relay                           switch
    Energy                          kWh, total
  Computer                          Port 2 child
    ...
```

Renaming a child changes its entities' generated display-name prefix without
changing their unique IDs, MQTT topics, or existing entity IDs. Explicit user
entity-name overrides remain overrides. Device rename is a Home Assistant
customization, not a request to rewrite a hardware port label.

Children inherit the parent's area unless overridden; labels are explicit, not
inherited. Parent action targeting includes children; child targeting includes
only that port. Parent disabling/deletion cascades. Hardware metadata belongs
on the parent, not repeated as properties on each child.

## Why own the entities

Native child devices already exist in the selected HA baseline. The blocker in
the companion approach is MQTT discovery: its schema has no native parent
reference, and its registration paths create main devices. Manually moving
MQTT entities is undone by registration on reload; converting their devices
behind MQTT's back conflicts with later main-device registration.

mFi-owned `SensorEntity` and `SwitchEntity` implementations can return native
`ChildDeviceInfo` directly, without modifying or monkey-patching HA Core.
All devices belong to the same mFi config entry, satisfying the parent/child
ownership invariant. MQTT supplies subscriptions, connection state, and
publishing, not the entity definitions.

Correction to the original companion plan: a helper entity can attach to an
MQTT-owned device using `entity.device_entry` without transferring device
ownership. This was verified and is used by HA's Integral helper. It does not,
however, make MQTT discovery create native children. The native design removes
that discovery limitation by giving mFi ownership of all five entities.

## Stable identities and publisher modes

Add `--ha-mode legacy|native`, defaulting to `legacy` for compatibility.
No upgrade automatically switches a running installation to native ownership.
In native mode:

- Require a provisioned `device_id` configuration value, also accepted through
  `--device-id`: a persistent 32-character lowercase UUID without separators.
  Provide a setup command that generates a random UUID once and safely persists
  it to the selected device configuration. Re-running setup reuses a valid
  existing ID; ordinary startup never generates or replaces one. Reject
  malformed IDs and conflicting configured/command-line values with an
  actionable error. Store it in persistent configuration, not volatile storage;
  failure to persist is a failed setup, not permission to run with a temporary
  ID. Preserve unrelated configuration and never expose broker credentials.
  Provisioning alone does not enable native mode or contact the broker.
  Never derive identity from hostname, model ID, or a port label.
  The present board API has no verified hardware serial/MAC getter; do not
  silently substitute one or discover identity through HA's ARP cache.
- Identify ports using the hardware API's numeric `sensor.id()`, starting at 1.
  Model IDs `58952` and `58993` currently enumerate eight and one ports,
  respectively. Advertise actual capabilities; do not manufacture missing
  ports or infer physical identity from labels.
- Use `<device_id>:port:<port_id>` for child identity and append the measurement
  role for new entity unique IDs. Identity and display names are independent.
- Publish native discovery only. Do not also emit `homeassistant/.../config`
  payloads for these entities, and do not listen on legacy relay-command topics.
- Keep legacy mode's wire contract unchanged. Native mode does not automatically
  purge legacy retained records; cleanup belongs to the migration operation.

Provide a read-only migration-map export in the upgraded client while it is
still in legacy mode. It records physical port IDs, current legacy role unique
IDs, discovery/state/command topics, and the selected native device ID. Compare
this with actual HA/broker inventory. Stale labels or ambiguous records require
user resolution, never a guess or wildcard cleanup.

Changing a provisioned device ID is a replacement/recovery operation, not a
rename. Duplicate live device IDs or conflicting publisher sessions must raise
an actionable conflict and disable control rather than oscillating ownership.

## Native MQTT protocol v1

Use a separate namespace, with a strict, versioned JSON contract and bounded
payload sizes. All topics below are derived from the validated device and
physical port IDs; do not accept arbitrary command topics or templates from
discovery. Native discovery is a descriptor, not a second implementation of
HA's generic discovery schema.

| Topic | Payload purpose | Publication policy |
|---|---|---|
| `mfi/<device_id>/config` | Versioned descriptor and port capabilities | Retained, QoS 1; reannounce on connection and descriptor change |
| `mfi/<device_id>/availability` | Device transport state and publisher session | Retained, QoS 1, including Last Will |
| `mfi/<device_id>/port/<port_id>/state` | One polling-cycle report for that port | Non-retained, QoS 0; changes/errors promptly, unchanged refresh at 60 seconds by default |
| `mfi/<device_id>/port/<port_id>/set` | Explicit ON/OFF command for that port | Non-retained, QoS 0; one publish attempt, no application retry or reconnect replay |

Example descriptor shape, with the remaining physical ports included similarly:

```json
{
  "schema_version": 1,
  "device_id": "0123456789abcdef0123456789abcdef",
  "name": "mPower Pro",
  "manufacturer": "Ubiquiti Networks",
  "model_id": "58952",
  "model": "mPower Pro",
  "firmware_version": "reported board version",
  "publisher_version": "reported client version",
  "refresh_interval": 60,
  "expire_after": 180,
  "ports": [
    {
      "id": 1,
      "name": "Port 1",
      "capabilities": ["power", "current", "voltage", "relay"]
    }
  ]
}
```

Reject duplicate port IDs, unsupported schemas, invalid topic/ID combinations,
invalid intervals, and conflicting descriptors. Preserve the existing
validation that expiration allows at least three refresh intervals and polling
is no slower than the refresh interval. A missing/removed descriptor never
silently deletes entity history; make the device unavailable and require
confirmed removal or a capability migration.

### Session, samples, and availability

Generate a new unpredictable session ID before each publisher connection and
install its Last Will before connecting. Availability contains that session ID
and `online`/`offline`. Each port report contains the session ID, a monotonically
increasing sequence number for that session/port, and separate role results:

```json
{
  "session_id": "opaque connection identifier",
  "sequence": 42,
  "power": {"status": "ok", "value": 100.0},
  "current": {"status": "ok", "value": 0.9},
  "voltage": {"status": "ok", "value": 120.0},
  "relay": {"status": "ok", "value": "ON"}
}
```

An unsuccessful role read is explicit, for example
`"power": {"status": "error", "reason": "invalid_data"}`. It carries no numeric
fallback. Reject nonfinite numbers, booleans masquerading as numbers, and
out-of-range values; power remains nonnegative consumption, never signed net
energy. Use checked measurement APIs; native relay polling also needs a
checked read that distinguishes malformed/missing data from OFF. An invalid
power result suspends that port's energy but does not invalidate another valid
measurement or another port.

Availability requires the HA broker connection, a valid matching publisher
session, shared online state, and a fresh successful role result. Start every
HA setup/reload/reconnect behind a no-live-sample gate. Reject retained numeric
reports, wrong-session reports, and duplicate/out-of-order sequences. These
must not refresh freshness deadlines.

Allow a current-session report to arrive before its online notification, but
do not expose it as available until both requirements are satisfied. Do not
backfill the gated interval. On offline, reconnect, expiry, or invalidation,
settle only the preceding valid interval and clear the power baseline. A later
valid sample starts a new interval.

Update deadlines on every accepted report, including identical values, without
depending on HA state-change events. Expiration and integration timers use
monotonic receiver time. The session/sequence envelope prevents old-session
replay; it does not prove original sample age within a delayed connection.
Document receipt-time accuracy and callback-scheduling limits rather than
claiming billing-grade or hard-real-time accounting.

Use the existing bounded connector queue, acknowledged startup/offline behavior,
transport deadlines, and owning-thread model. Make discovery publication
mode-specific; do not fork the transport or weaken the existing legacy
power-freshness tests.

### Relay controls

Implement `async_turn_on` and `async_turn_off` as explicit idempotent set
operations, not a custom toggle command. Commands include `session_id`,
`request_id`, and `value: "ON"|"OFF"`. The publisher rejects retained commands,
wrong-session commands, malformed payloads, and unknown ports. It must never
interpret arbitrary non-ON input as OFF, as the current legacy handler does.

Publish commands with `qos=0` and `retain=False`, after checking the current
HA broker connection, publisher session, and port availability. Use a fresh
request ID for each user action, make one publish attempt, and never keep an
application queue of commands to send after reconnection. Preserve clean-session
publisher subscriptions, so commands are not deliberately stored for an offline
publisher.

QoS 1 is deliberately not used for commands: HA's underlying Paho client can
retain an outgoing QoS 1 message even when publication reports no connection,
and resend it when only HA reconnects. The publisher may have stayed online, so
its session ID alone does not reject that stale command. In the installed HA
2026.9.2/Paho baseline, disconnected QoS 0 publication fails without entering
that outgoing retransmission queue for both MQTT 3.1.1 and MQTT 5. Preserve this
transport property as an explicit regression gate on supported HA/client
versions; checking connection state before publishing is not by itself enough.

Do not optimistically update the switch. The next fresh relay report confirms
the observed hardware state and echoes the processed request ID; an explicit
error result reports a failed write/read. Republish confirmation even for
unchanged state. Serialize one pending command per port, use a bounded
10-second confirmation timeout, and cancel pending confirmations on
disconnect/unload.
Only a matching request ID and publisher session with a successful hardware
result can complete the action. Surface publish, timeout, and device failures
as HA action errors. A lost QoS 0 command or confirmation can therefore time
out; do not hide that uncertainty with an automatic retry.

Never replay a failed command automatically after reconnect or startup.
Successful transport publication is not hardware confirmation. A disconnect or
timeout cannot retract a command already transmitted or prove it was never
applied; preserve that uncertainty until a fresh hardware report. The guarantee
is no resend on reconnect, not transactional cancellation of physical I/O.

## Home Assistant implementation

| Area | Plan |
|---|---|
| Manifest | Keep domain `mfi`, `dependencies: ["mqtt"]`, `local_push`, and no extra broker library; add native discovery matcher `mfi/+/config` |
| Config flow | Validate native descriptors in `async_step_mqtt`, then confirm one physical device; unique ID is the provisioned device ID |
| Compatibility mode | Add explicit `companion`/`native` mode and config-entry versioning; a schema upgrade alone never transfers ownership or clears topics |
| Parent registry | Register the mFi-owned physical device before its children; keep hardware/model/firmware metadata here |
| Child registry | Register `ChildDeviceInfo` with parent ID, stable port identifier, and display name; same config entry/subentry as parent |
| Entities | Four sensors per port when supported: Power, Current, Voltage, Energy; one Relay `SwitchEntity`; `has_entity_name=True` with role-only names |
| MQTT transport | Wait for `mqtt.async_wait_for_mqtt_client`; use public `mqtt.async_subscribe`, `mqtt.async_publish`, and `mqtt.async_subscribe_connection_status` |
| Runtime | One typed device runtime manages port results, availability deadlines, command confirmations, entity listeners, and energy checkpoints |
| Energy input | Feed validated native power reports directly into the existing accumulator; do not integrate the newly created HA power entity as a second path |
| Lifecycle | Preserve runtime-generation checks, destination/configuration serialization, staged options, committed-only reporting, and cancellation-safe checkpoint ordering |

Subscribe to metadata, availability, and state once per runtime, not separately
per entity. Register connection callbacks before accepting telemetry. Dispose
every subscription/timer and stop commands before final checkpointing. MQTT
reload and late startup must leave entities unavailable and retry cleanly, not
create another runtime or silently use restored samples.

State roles have independent validity even though they share a report topic.
Power/current/voltage use measurement state classes and their proper units;
energy remains consumption-only `kWh`, `total`, with no scheduled reset.
Retain the tested Decimal left integration, independent reporting timer,
durable checkpoints, and no-downtime-backfill rule.

An energy exclusion affects calculated energy only, not the port's native
measurements or relay. Existing user-disabled entities remain disabled. Native
entity enablement does not control hardware polling: an enabled energy entity
uses validated native power reports even if its separate Power entity is hidden
or disabled. Identify this difference from companion source tracking in the
migration preview and preserve any explicit energy exclusion/disable choice.
Native descriptor name updates must not overwrite `name_by_user` or entity name,
icon, area, label, and disabled-state customizations.

New native energy IDs can use the physical device/port identity. Migrated
energy entities retain their existing mFi unique IDs, binding IDs, entity IDs,
storage ID, and totals using an explicit persisted physical-port mapping.
Do not reset IDs merely to make their format uniform.

## Ownership migration

### Why a coordinated maintenance window is necessary

`EntityRegistry.async_update_entity_platform` supports an in-place transfer
between integrations, preserving the registry entry and entity ID, but rejects
loaded entities. Empty MQTT discovery payloads normally remove entities and
their registry entries. Therefore do not tombstone discovery first while HA's
MQTT discovery listeners are active, and do not abuse MQTT's discovery-schema
migration flag as an integration-ownership transfer.

The user selected a maintenance-window approach. Show its shared-MQTT impact
in the preview and require confirmation at execution time. Routine native
setup and operation do not unload the shared MQTT integration.

### Migration transaction

Implement a domain-level administrative migration coordinator, independent of
the entry runtime that it must unload. Serialize migrations on the affected
MQTT config entry as well as the physical device. Persist an integration-owned
versioned journal before mutations. HA registry changes and the checkpoint are
not one atomic transaction; completion must be resumable and idempotent.

1. **Inventory and backup.** Verify an approved publisher/map, exact registry
   entries and topics, port-role mapping, broker ownership, energy checkpoint,
   and user metadata. Inventory MQTT-specific device triggers/actions and
   external helpers. If a companion already exists, record its separate
   `<strip> Energy` device ID, identifiers, config-entry/subentry ownership,
   full restorable user metadata, and entity membership, including disabled
   entities. Inventory automations or dashboards referring to that device ID;
   retiring it requires confirmed remapping where needed, not an assumption
   that preserving entity IDs fixes device references. Export the allowlisted
   retained discovery records and take a consistent HA backup. Refuse ambiguous
   mappings, shared/unexpected parent or companion members, competing native
   entries, or concurrent setup/recovery.
2. **Prepare, without taking ownership.** Upgrade the HA integration with both
   modes available. Upgrade the publisher in legacy mode and provision its
   stable ID. Reuse the existing companion config entry and checkpoint when
   present; otherwise create a pending native entry with no active entities.
   Record old/new identities and the rollback mapping.
3. **Quiesce HA.** Freeze and durably save the companion, then unload it and the
   shared MQTT entry. Use the public config-entry disable API as a restart
   guard for that MQTT entry, recording its previous disabled state. Verify
   the maintenance state is persisted before destructive steps; returning from
   an unload or an API that schedules a save is not proof of persistence.
   Verify every allowlisted source/energy entity is unloaded. Keep the
   coordinator/journal outside that unloaded runtime. Do not disable/delete
   every MQTT entity individually, edit `.storage` registries by hand, or
   assume a failed unload succeeded.
4. **Cut over the publisher and broker.** Under the separately approved device
   rollout, switch to native mode and prevent an old process/updater from
   reintroducing legacy discovery. While HA MQTT is unloaded, an explicit
   operator-side broker tool clears only the exported, allowlisted legacy
   discovery records. Export first and confirm broker acceptance/absence.
   Never purge a wildcard topic tree, legacy relay commands, or unrelated
   discovery. The native namespace avoids depending on old retained numeric
   records; their separate cleanup remains governed by the existing runbook.
5. **Transfer entities first.** For each unloaded MQTT sensor/switch, use
   `async_update_entity_platform(..., new_platform="mfi",
   new_config_entry_id=<mfi entry>, new_config_subentry_id=None,
   new_unique_id=<stable native role ID>, new_device_id=None)`.
   Preserve the existing entity ID and user overrides. Journal each result.
   Restore pre-window user disabled flags from the inventory, not temporary
   config-entry/device disable flags introduced by maintenance.
   Do not delete/recreate entities to achieve the move.
6. **Transfer the parent second.** Once all affected entities are transferred
   and detached, move the former MQTT strip device to the mFi config entry
   with `DeviceRegistry.async_update_device(new_config_entry_id=...)`, replace
   its identifiers with the native physical identity, and preserve its device
   ID and user metadata. Only then create the native port children.
7. **Attach children and checkpoint.** Assign the transferred native entities
   and existing energy entities to the appropriate children. Preserve energy
   unique IDs and totals; atomically upgrade the checkpoint's mapping/schema
   with an original-schema backup. Reuse the old companion entry rather than
   opening a second writer. Update that entry's mode/identity only through the
   coordinator, not an ordinary discovery callback.
8. **Retire the old companion device.** If the inventory contains the separate
   energy-companion device, re-read it and verify its ID, ownership, and
   identifiers match the journal. Confirm every inventoried energy entity now
   exists on its intended native child with the same entity/registry IDs and
   that the old device has no entities (including disabled ones) or children.
   Refuse removal if any unexpected member or user change makes the snapshot
   stale. Record retirement intent durably, then remove only that empty device
   with the public registry API and journal the outcome. Do not remove the
   reused mFi config entry or its energy checkpoint. HA does not automatically
   delete this empty device while its owning entry still exists.
9. **Resume and verify.** Restore the MQTT entry's prior enabled state, then
   activate the native entry once
   legacy-discovery absence and journal invariants are confirmed. Require live
   native samples before availability. Verify counts, IDs, totals, registry
   ownership, and relay confirmation without issuing an unsolicited physical
   switch operation. Ask separately before any real relay test. End the window
   only after unrelated MQTT entities recover and the journal is finalized.

**Ordering is not optional.** An isolated HA registry probe showed that moving
the parent first can remove its old entities through ownership cleanup. The
entity-transfer/detach, parent-transfer, child-attach sequence preserved entity
registry IDs, entity IDs, user names, and the parent's device ID in the probe.
That was an in-memory unloaded-registry experiment, not an end-to-end broker,
restart, history, or production migration.

The journal records `prepared`, `quiesced`, `broker_clean`,
`entities_transferred`, `parent_transferred`, `children_attached`,
`companion_retirement_pending`, `companion_retired`, and `active` phases, plus
per-entity progress and preconditions. For a fresh installation, mark companion
retirement not applicable rather than creating a dummy device. After an
interruption, reconcile observed registry/broker state with the journal, keep
the native entry inert, and resume or explicitly roll back. An already absent
old companion is accepted only when the prior retirement intent and migrated
entity invariants match; otherwise require recovery. Do not blindly repeat
destructive steps or erase the journal on error.

### History, automations, and rollback

- Preserve entity IDs, units, state classes, and energy totals; verify actual
  Recorder continuity across migration rather than assuming ID preservation
  alone proves every history requirement.
- Preserve the old strip device ID when safe. Parent-targeted actions should
  expand to the new children, but MQTT-specific device triggers/actions may
  still reference MQTT behavior and require explicit remapping.
- No automatic helper-history import: the deferred Integral/utility-meter/
  template audit still determines coexistence or a separate migration.
- If the expected parent contains unexpected entities or already has children,
  refuse automatic ownership transfer. A parent with children cannot move
  directly to another config entry.
- Rollback must quiesce both runtimes again, detach all affected entities before
  deleting only the new empty child records, reverse platform/parent ownership
  in a safe order, restore the allowlisted discovery, and deliberately restart
  the legacy publisher. Do not delete children while entities are attached.
- If the companion device was retired, restore it from its journaled
  identifiers/owner using public registration APIs, reapply its saved user
  metadata, and verify its original device ID before reattaching the preserved
  energy entities. HA's deleted-device restoration preserved that ID and user
  name in an isolated registry probe; verify it across restart in acceptance
  tests. If the original ID cannot be restored, stop rollback with an explicit
  repair requirement rather than silently breaking device references or editing
  HA's registry files. Companion-only rollback must leave one restored
  companion device, not another native parent or an empty duplicate.
- After native operation has accumulated energy, carry the latest totals back
  through the saved binding map. Restoring a pre-migration energy file would
  lose consumption and is not a valid rollback.
- Keep compatible old checkpoint readers and a reviewed reverse conversion.
  A binary downgrade alone is not a rollback. Do not replay native relay
  commands, silently reactivate two publishers, or clear newer totals.

## Proposed changes and milestones

| Milestone | Implementation surfaces and exit condition |
|---|---|
| 1. Contract and migration fixtures | Document/schema-test native v1 descriptor, reports, commands, stable identity, and journal; fixtures from one/eight-port boards and legacy discovery; prove child naming and safe unloaded registry transfer |
| 2. Publisher native mode | `mfi-mqtt-client` mode/ID options, one-time persistent ID setup command, descriptor and migration-map export, native port reports/commands; reuse `mfi` checked reads and bounded connector lifecycle; legacy C++/wire tests unchanged |
| 3. HA native runtime | Add `protocol.py`, `mqtt.py`, `entity.py`, and `switch.py`; extend sensor/config flow; parent/child registration, direct power integration, availability and command confirmation |
| 4. Safe ownership migration | Add `migration.py` and journal handling; explicit compatibility mode, maintenance workflow, registry transfer, checkpoint mapping, empty-companion retirement/restoration, crash/retry/rollback; no production automation until rehearsal passes |
| 5. Acceptance and distribution | Independent peer review, supported-HA matrix, isolated broker and HACS rehearsals, migration/rollback guide, publisher/version compatibility table, updated distribution allowlist and release notes |

Retain and adapt `energy.py` and error-reporting `storage.py`. Split native
transport and legacy source tracking rather than adding many mode conditionals
to `source.py`; share only the accounting/persistence/lifecycle mechanisms that
have the same invariants. Add the new runtime files to the distribution
builder's explicit allowlist. Keep C++ and custom-integration versions/releases
independent and preserve the existing no-publication default.

## Acceptance criteria

| Scenario | Required result |
|---|---|
| Fresh one/eight-port setup | One parent, exactly one child per advertised physical port, supported measurement/relay entities, and one enabled energy entity per non-excluded valid power port |
| Child renamed | Generated role names update together; entity IDs, unique IDs, topics, totals, and explicit name overrides do not change |
| Child area/labels and parent targeting | Correct area inheritance/override, no implied label inheritance, parent targets all children, one child targets only that port |
| Device or port label/hostname changes | Same physical IDs and entities; no guessed rebinding or duplicate counters |
| Device ID provisioning and reboot | Setup generates and persists one ID; repeated setup, restart, and upgrade reuse it; corrupt/conflicting/unwritable configuration fails explicitly without replacing identity or enabling native mode |
| Power entity disabled | An independently enabled, non-excluded energy entity continues accumulating from valid native power; no synthetic zero or lost interval due only to Power entity disablement |
| Descriptor/state ordering and replay | Retained config is accepted; retained numeric reports and wrong-session/out-of-order reports never start accounting or reset expiry |
| Constant 100 W for an hour | 0.1 kWh within existing arithmetic tolerance, including unchanged report refreshes |
| Invalid power with valid current/voltage | Power and energy unavailable; other valid roles and unrelated ports continue |
| Expiry, disconnect, restart, session replacement | No downtime integration, old cached command execution, duplicate listeners, or stale-runtime writes |
| Relay ON/OFF, including unchanged state | Exactly one validated non-retained QoS 0 publish attempt; only a matching request/session and fresh hardware result confirms success |
| Publish failure, timeout, rejected/retained command | Actionable error; no invented success, automatic replay, or malformed-value-to-OFF conversion |
| HA-only broker disconnect, publisher stays online | For MQTT 3.1.1 and 5, neither a rejected publication nor an interrupted QoS 0 send is queued for resend when HA reconnects with the publisher session unchanged; commands already transmitted remain explicitly indeterminate until reported |
| Lost command or confirmation | Bounded action error without an automatic retry; no optimistic relay state or assumption that timeout means OFF/not applied |
| Existing user-disabled/overridden entities | Migration preserves disabled flags, names, icons, areas, labels, and energy exclusions |
| Legacy-to-native cutover | Same entity/registry IDs and parent ID; new child IDs; one entity owner; energy/history continuous except the explicit unavailable window |
| Companion-to-native topology | Exactly one native main device for the strip and the expected port children; every original entity preserved, no empty `<strip> Energy` device and no duplicate parent |
| Companion retirement precondition fails | Unexpected/disabled entities, children, changed ownership or stale metadata stop retirement without removing the device, config entry, or checkpoint |
| Crash during companion retirement and rollback | Journal safely resumes an already performed removal; rollback restores original companion ID/customizations and energy membership or reports a repair blocker without silently substituting another device |
| Parent-first transfer prevention | Guard rejects the unsafe order before any source entity can be removed |
| Interruption after every journal phase | Resume/rollback is idempotent; no missing counters, lost overrides, wildcard cleanup, or two active owners |
| Rollback after native consumption | Latest totals retained; legacy ownership/discovery restored only after native sources are quiesced |
| Shared MQTT maintenance | Impact preview/confirmation, broker remains up, unrelated retained topics untouched, unrelated HA MQTT entities recover |
| Distribution and versions | Legacy mode still works; incompatible native schema is refused; package includes all runtime modules; HACS release gates remain enforced |

## Remaining rollout prerequisites

The architecture, upgraded-publisher requirement, and maintenance-window
strategy are selected. No further product decision blocks local implementation.
The live publisher/history audits, actual device-ID provisioning, physical
port mapping confirmation, migration-window scheduling, credentials, device
rollout, repository creation, and publication remain separately authorized
rollout work. This plan performs none of them.

## Verified references

- [HA native child-device contracts](https://developers.home-assistant.io/docs/device_registry_index/#child-devices)
- [HA 2026.9.2 device registry and ownership transfer](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/device_registry.py)
- [Unloaded entity platform migration API](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/helpers/entity_registry.py#L2075-L2115)
- [Public config-entry maintenance disable API](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/config_entries.py#L2510-L2542)
- [MQTT discovery removal behavior](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/components/mqtt/entity.py#L1042-L1083)
- [MQTT public transport functions](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/components/mqtt/client.py)
- [Paho 2.1.0 publish implementation and QoS-specific queuing](https://github.com/eclipse-paho/paho.mqtt.python/blob/v2.1.0/src/paho/mqtt/client.py#L1770-L1818)
- [Paho reconnect reset of outgoing messages](https://github.com/eclipse-paho/paho.mqtt.python/blob/v2.1.0/src/paho/mqtt/client.py#L3712-L3740)
- [MQTT connection-status subscription](https://github.com/home-assistant/core/blob/2026.9.2/homeassistant/components/mqtt/__init__.py#L676-L688)
- [Entity naming and device prefixes](https://developers.home-assistant.io/docs/core/entity/#entity-naming)
- [Helper attachment without device ownership transfer](https://github.com/home-assistant/core/blob/2026.9.3/homeassistant/helpers/device.py#L25-L55)
