# mfi-update

Self-update support for the mFi tools: fetch the latest release for a given tool
from GitHub Releases and atomically replace the running binary in place.

Used by `mfi-cli`, `mfi-mqtt-client`, and `mfi-rest-server`. See
[Updating](../docs/updating.md) for the end-user behavior and configuration.

## Overview

- **`semver`** — parse/compare `MAJOR.MINOR.PATCH` (true numeric ordering).
- **`config`** — repo, proxy, insecure, bin dir, interval, enable; resolves proxy
  from `http_proxy`/`https_proxy` when unset.
- **`downloader`** — builds and runs a `wget`/`curl` command (proxy +
  `--no-check-certificate`), so TLS is the firmware's, not ours.
- **`release`** — parse GitHub `matching-refs` and release JSON to find the latest
  tag and the asset download URL for a tool.
- **`updater`** — orchestrates check → download → ELF-validate → atomic rename →
  `execv` re-exec, with separate `prepare()`/`apply()` operations and the compatible
  synchronous `check_and_apply()` wrapper.
- **`periodic_updater`** — retains synchronous interval-based checks (REST).
- **`background_updater`** — prepares on one worker while the MQTT-owning loop
  continues polling and handling commands. It shares the periodic schedule and
  option resolution with the synchronous API.

## Background ownership and deadlines

`make_background_updater(enabled, interval_seconds, repo, proxy, insecure,
tool_name, current_version_text, argv)` returns a `unique_ptr`, or null when
disabled/the current version cannot be parsed, matching the periodic factory.
The first `tick()` establishes the interval baseline. Subsequent ticks start at
most one job and return immediately while preparation is pending. A completed
job is joined before `tick()` returns `update_result::ready`; the coordinator
then owns its unique same-directory staged file. `ready` is emitted once, and
no further job starts until that artifact is consumed or discarded.

The owner must publish offline and complete its bounded disconnect **before**
calling `apply_ready(should_cancel)`. No worker renames a target or executes a
new image. Application consumes the artifact even on failure. The owner decides
whether to reconnect through a fresh measurement epoch when application returns.
`request_stop()` is nonblocking/thread-safe, cancels preparation, and permanently
disables future jobs; it is **not** a signal handler API. The main loop must
translate its `volatile sig_atomic_t` flag into this atomic request. All other
coordinator operations, including `stop()`/destruction, belong to the owner
thread. `stop()` cancels, joins, and discards staged results.

Preparation has one absolute monotonic **120-second** budget covering both
metadata requests, download, validation and result handoff. Nonblocking pipe
reads and `waitid(WNOWAIT)`/`waitpid(WNOHANG)` are polled at most 20 ms apart.
Cancellation/timeout allows up to **5 additional seconds** for owned-child
TERM/KILL escalation and reaping; it does not wait for the remaining preparation
budget. Stderr is suppressed to avoid exposing URLs or proxy credentials.
Metadata capture is limited to 1 MiB. Failed/partial artifacts and all owned
descriptors are released, including on exceptions.

The downloader resolves executable paths and constructs argv/environment before
fork; the child uses only async-signal-safe operations before `execve`, suitable
for multithreaded MIPS/uClibc callers. Each downloader gets its own process group.
Only that group/direct child is signalled/reaped; unrelated children are untouched.
As with any userspace deadline, scheduling, filesystem operations, and a process
stuck in uninterruptible kernel I/O cannot be given a hard real-time guarantee.
If SIGKILL cannot be followed by reaping within the allowance, the library emits
`cleanup_failed` and a credential-free diagnostic; the background coordinator
disables further updates rather than accumulating jobs. Kernel intervention may
still be needed in this exceptional case.

## Apply and termination boundary

Before rename, cancellation returns `cancelled` and leaves the target unchanged.
After rename, cancellation or exec failure returns `replaced_not_restarted`:
the installed file changed, but no new image was started. During the final
transition, SIGTERM/SIGINT are blocked on the applying thread, their process-wide
dispositions become default, and pending signals plus the caller's flag are
checked before and after rename. The final commit point unblocks those signals
with default dispositions immediately before exec. A signal racing that point
terminates the old or new image rather than disappearing into a reset handler;
the new image does not inherit blocked termination signals. On a returning path,
the original mask/handlers are restored. Other threads must not change these
signal dispositions concurrently. `should_cancel` must be nonthrowing and prompt.
Ordinary atomic cancellation is honored through the final commit check; requests
after that irreversible commit point cannot revoke exec.

## Outcomes and testing seams

| Outcome | Meaning |
|---|---|
| `disabled` | Updates disabled. |
| `no_downloader` | No usable wget/curl on PATH. |
| `up_to_date` | No newer semantic version. |
| `check_failed` | Metadata request or release parsing failed. |
| `download_failed` | Download, staging, permissions or ELF validation failed. |
| `ready` | Owned artifact prepared; no replacement yet. |
| `updated` | Apply hook reported success; actual successful exec never returns. |
| `cancelled` | Stopped before replacement. |
| `timed_out` | Shared preparation budget expired. |
| `apply_failed` | Replacement failed, target unchanged. |
| `replaced_not_restarted` | Replacement happened but restart failed/was cancelled. |
| `preparation_failed` | Unexpected worker/hook exception or worker creation failure. |
| `cleanup_failed` | Kernel did not permit child reaping within the cleanup allowance. |

The legacy sync wrapper keeps its old apply-failure classification
(`download_failed`). Its existing fetch/download/apply hooks remain supported.
New context-aware fetch/download hooks must honor `context.interrupted()`; legacy
hooks must return promptly. Arbitrary blocking user hooks cannot be preempted by
C++ thread cancellation. `set_downloader(downloader{kind, config, executable})`
selects a local fake executable without changing installed tools or PATH.
`set_target_path()` isolates staging/replacement in tests. `preparation_limits`
and `tick(time_point)` allow short real deadlines and simulated schedules.
An injected apply hook owns its own side effects and must not bypass cancellation;
production application uses the synchronized built-in path.

`tests/test_background_update.cpp` covers coordination/ownership;
`tests/test_update_process.cpp` covers actual subprocess deadlines, cleanup and
signals. Actual exec tests run only in disposable processes with private targets.

## Dependencies

### External

- [nlohmann_json](https://github.com/nlohmann/json) — GitHub API JSON parsing
- CMake `Threads::Threads` — background preparation and POSIX signal masks

## Details

- **Language**: C++20
- **Namespace**: `mfi_update`
- **Version**: 1.0.0
