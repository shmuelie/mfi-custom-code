# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Background updater with one preparation worker, nonblocking completion polling,
  shared periodic scheduling, and main-thread-only application.
- Owned staged artifacts, split prepare/apply APIs, atomic cancellation, and a
  single 120-second preparation deadline with a separate 5-second child cleanup
  allowance. Added typed ready, cancellation, timeout, apply/restart and cleanup
  failure outcomes.
- Context-aware hooks, local executable/target seams, and no-network coordinator,
  real subprocess, cleanup, deadline and disposable-exec signal regressions.

### Changed

- Downloader pipes and child waits are cancellation/deadline-aware. Forked children
  use only async-signal-safe setup before exec; metadata memory is bounded and
  downloader diagnostics cannot disclose proxy credentials.
- Staging uses unique same-directory files with automatic cleanup instead of a
  shared `.new` path. Synchronous updater/periodic APIs and existing hooks remain
  supported.
- Application synchronizes SIGTERM/SIGINT at the rename/exec boundary and reports
  replacement without restart separately. Added public `Threads::Threads` linkage.

## [1.0.0] - 2026-07-21

### Added

- Initial release.
- Semantic-version parsing and comparison (`semver`).
- Update configuration with proxy/repo/insecure/bin-dir/interval and
  environment-based proxy resolution (`config`).
- `wget`/`curl` downloader command builder and fork/exec runner (`downloader`).
- GitHub `matching-refs` and release JSON parsing to resolve the latest tag and
  asset URL for a tool (`release`).
- `updater`: check, download, ELF validation, atomic in-place replace, and
  `execv` re-exec, with injectable seams for testing.
- `periodic_updater`: interval-driven checks for long-running tools.
