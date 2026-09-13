# MQTT power freshness migration

This is a client/broker compatibility runbook, not authorization to deploy,
restart devices, publish releases, or delete broker records. It does not require
Home Assistant server configuration.

## Why cleanup is separate

An ordinary non-retained publication does not overwrite or delete a broker's
existing retained record. The new client therefore cannot make previously
retained power trustworthy merely by changing future publication flags.

New numeric power is non-retained. Discovery and shared/per-channel availability
remain retained. Per-channel availability has the same topic base as the existing
state topic and an `availability` suffix; its JSON payload is `online` or
`offline` under the `availability` key. New discovery combines both availability
topics using `availability_mode: all`.

## Rehearsal and approved migration

1. Rehearse first against an isolated broker seeded with legacy retained
   discovery, numeric power, shared availability, and unrelated current/voltage
   and relay records. Prove that a new non-retained sample leaves a seeded
   retained numeric record intact.
2. Prepare an explicit allowlist of numeric power state topics from the actual
   discovery messages. Do not infer topic names from labels or use wildcards.
   Record the old/new binaries and configuration for rollback.
3. Before an authorized deployment/cleanup, coordinate the old publisher, client
   IDs, boot downloader, and in-process/external updaters so an older process
   cannot immediately repopulate the records or replace the candidate binary.
4. Start the updated publisher under the approved rollout procedure. Its
   retained offline gates must exist before shared online; a failed channel
   stays offline. A recovering channel requires accepted current-connection
   numeric publication followed by its acknowledged online transition.
5. With separate approval, clear only the allowlisted numeric state records
   using zero-byte retained publications. Export the old records for audit
   first. Do not remove discovery, shared/channel availability, relay state or
   commands, or current/voltage records. The application does not do this
   automatically.
6. Use a new clean-session subscriber to confirm there is no retained numeric
   power replay and that current numeric samples are non-retained. Verify
   persistence behavior after broker restart in the isolated rehearsal.
7. Include a failed-read case and a subscriber absent during invalidation.
   Retained channel offline must remain observable after resubscription and
   broker restart, even if the subscriber saved an earlier numeric reading.

## Failure and rollback

Do not claim immediate offline delivery if the broker cannot acknowledge it:
the client logs the failure and expiration/Last Will remain the fallback.
Consumer expiration measures receipt time, not original device sample age.

Restoring an old binary can restore retained numeric publishing and replace the
combined-availability discovery contract. That rollback does **not** preserve
the new freshness guarantees. Do not replay saved old numeric records as fresh
measurements or silently purge the new channel-availability topics. Document any
separately approved cleanup of obsolete topics as its own allowlisted action.

State topics and discovery identities are not renamed by this change. Physical
label changes can still alter the existing label-derived identifiers; do not
combine a label/identifier migration with this freshness rollout.
