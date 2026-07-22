# Self-Updating

The `mfi-cli`, `mfi-mqtt-client`, and `mfi-rest-server` tools can update
themselves from this repository's GitHub Releases, replacing their own binary in
place. This mirrors — and is compatible with — the shell-based updater in the
[mfi-env](https://github.com/shmuelie/mfi-env) project.

## How it works

1. The tool resolves the latest release for its own name via the GitHub API:
   `git/matching-refs/tags/<tool>/`, then picks the highest **semantic version**
   (a true `MAJOR.MINOR.PATCH` comparison, so `v1.10.0` beats `v1.9.0`).
2. If that version is newer than the running one (from the tool's own
   `--version`), it reads the release's asset named exactly `<tool>` and its
   `browser_download_url`.
3. It shells out to the device's `wget` (or `curl`) to download the asset — so
   TLS, proxying, and certificate handling are the firmware's, not ours. No TLS
   library is linked into the binaries.
4. The download is validated (non-empty, ELF magic), then atomically `rename()`d
   over the running binary and the process re-`execv`s itself. Because the target
   lives in **persistent** storage (`/var/etc/persistent/bin` by default), the
   update survives reboots.

- **Long-running tools** (`mfi-mqtt-client`, `mfi-rest-server`) check
  periodically from their main loop (default: once every 24 hours). The first
  check is deferred one full interval, since the boot downloader already fetched
  the latest at startup.
- **`mfi-cli`** is short-lived, so it exposes an explicit `update` subcommand
  instead of a periodic check.

## Configuration

All tools accept the same knobs; values resolve with the precedence
**CLI option > environment (`http_proxy`/`https_proxy`) > built-in default**.

| Setting | Default | mqtt-client / rest-server flag | mfi-cli `update` flag |
|---|---|---|---|
| Enable | on | `--update` / `--no-update` | *(the subcommand itself)* |
| Interval (s) | `86400` | `--update-interval` | *n/a (one-shot)* |
| Repo `owner/name` | `shmuelie/mfi-custom-code` | `--update-repo` | `--repo` |
| Proxy `host:port` | *(env or none)* | `--update-proxy` | `--proxy` |
| Skip TLS verify | on | `--update-insecure` / `--update-check-cert` | `--no-check-cert` |
| Binary directory | `/var/etc/persistent/bin` | *(auto: `/proc/self/exe`)* | `--bin-dir` |

TLS verification is skipped by default because the device's CA store is limited
(the same reason mfi-env passes `--no-check-certificate`).

## Examples

```sh
# Check for an update without applying it
mfi-cli update --check

# Update mfi-cli now, through a proxy
mfi-cli update --proxy proxy.local:8888

# Run the MQTT client with self-update disabled
mfi-mqtt-client --config /var/etc/persistent/mqtt.ini --no-update

# Update from a fork/mirror
mfi-rest-server --update-repo myfork/mfi-custom-code
```

## Requirements & caveats

- The device must have a `wget` (preferred) or `curl` that can reach
  `api.github.com` over HTTPS. If none is found, the tool logs once and keeps
  running — it never fails because of updates.
- Updates run unauthenticated against the GitHub API, which is rate-limited to
  60 requests/hour per IP; the conservative default interval stays well under it.
- The self-update replaces the **running** binary in persistent storage. If you
  deploy via the mfi-env boot `downloader`, both paths target the same
  `/var/etc/persistent/bin/<tool>`, so they stay consistent.

## Relationship to mfi-env

[mfi-env](https://github.com/shmuelie/mfi-env) drives updates externally: a boot
`rc.poststart.d/downloader` fetches the latest release at startup, and a host
`update.sh` checks/updates over SSH. The in-binary self-update described here
uses the **same** API endpoints, asset naming, and `/var/etc/persistent/bin`
path, so it complements those scripts rather than conflicting with them — it just
lets a device keep itself current between reboots without an external trigger.
