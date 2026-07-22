# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

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
