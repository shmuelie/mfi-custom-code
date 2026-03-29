# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

*No unreleased changes.*

## [1.0.0] - 2026-03-29

### Added

- Initial release.
- C++ RAII wrapper classes around Mongoose networking primitives: `mg::manager`, `mg::timer`, `mg::server`, `mg::http_server`, `mg::connection`, `mg::http_message`, and `mg::http_response`.
- Event-driven HTTP server support via `eventing_handler`.
- Mongoose event enumeration and logging helpers.
- HTTP URI matching and capture support.
