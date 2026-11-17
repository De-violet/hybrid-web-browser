# Changelog

All notable changes to Attis are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Rolling `continuous` prerelease: every push to `main` now publishes a
  development build with the latest Linux and Windows assets, so a fresh
  release is always available without waiting for a version tag. Tagged
  releases (`v*`) work as before.
- Violet default theme: a dark violet-tinted preset (accent `#8b5cf6`)
  is now the default, with a matching preset card in Settings. All
  previous presets remain available.

### Changed

- Renamed from Falkon-Rust to Attis: package, binary, library, internal
  page scheme (`attis:home`, `attis:settings`), configuration directory
  (`~/.config/attis/`), desktop entry, and CI artifacts. Configuration
  and data from earlier installs are not migrated automatically; copy
  them from the previous config directory if needed.
- Documentation (README, CHANGELOG) is now written in English.

## [0.2.0] - 2026-11-17

### Added

- **Adblock filter-list updates**: EasyList, EasyPrivacy, uBlock Origin
  lists (filters, unbreak, badware, privacy), and ABPindo (Indonesian
  ads) can now be downloaded and stored in `filter_lists/` inside the
  configuration directory, then loaded automatically at startup.
  Periodic updates run in the background without blocking the browser
  window from opening. Without downloaded lists, the browser keeps
  running with the built-in offline rules. The `ublock-badware` and
  `ublock-privacy` lists are opt-in (off by default) to keep the engine
  lightweight.
- New CLI flags: `--update-filters` (download all active lists, then
  exit), `--filter-status` (show filter cache status), `--no-restore`
  (skip session restore for one run), and `--version`.
- **Session restore**: open non-private tabs are saved to `session.json`
  and restored when the browser is opened again. It can be disabled via
  `restore_session: false` in `config.json` or from the Settings page.
  Private tabs are never saved.
- **Per-domain adblock allowlist** (`adblock_allowlist` in `config.json`
  and from the Settings page): requests from allowlisted domains are not
  blocked and no cosmetic CSS is injected.
- Configurable RAM-saving settings: `suspend_enabled`,
  `suspend_after_seconds`, `suspend_many_tabs_after_seconds`,
  `suspend_many_tabs_threshold`, and `http_cache_size_mb`, all changeable
  from the Settings page (General & Privacy tabs) without editing JSON.
- `renderer_process_limit` option in `config.json`: caps the number of
  Chromium renderer processes via the `--renderer-process-limit` flag
  (0 = Qt default). Useful on low-RAM machines with many tabs; opt-in.
- `LICENSE` (GPL-3.0), this file (`CHANGELOG.md`), package metadata in
  `Cargo.toml`, and Dependabot for Cargo and GitHub Actions dependency
  updates.
- `test` job in CI: `cargo fmt --check`, `cargo clippy`, and
  `cargo test` on Linux. The build/release job now waits for the test
  job to finish.
- Tests for the allowlist, the cosmetic CSS cache, the filter updater,
  session restore, and legacy `config.json` compatibility.

### Changed

- Package version aligned to `0.2.0` (previously `Cargo.toml` still said
  `0.1.0` while release tags were already at `v0.1.7`).
- The adblock engine now counts active rules and uses a bounded cosmetic
  CSS cache (max. 64 entries) so frequently revisited pages do not
  recompute selectors every time.
- All Rust code formatted with `cargo fmt` for consistency.

### Fixed

- Built-in rule `|| sentry.io/api/*` was written with a space after `||`
  so it never matched; it is now `||sentry.io/api/*$third-party`.
- Rust FFI functions taking raw pointers are now marked `unsafe` with
  `# Safety` documentation, so `cargo clippy` passes on the latest
  stable toolchain (previously 22 `not_unsafe_ptr_arg_deref` errors).
- `run_smart_memory_cleanup` now checks for null `view`/`page` before
  accessing them.
- The built-in filter lists are now documented honestly: the offline
  built-in rules are only ~70 core rules; full aggressive blocking
  requires downloaded lists (`--update-filters` or the button in
  Settings).

## [0.1.0] - 2026-11-15

- Initial release: a minimalist Rust + Qt6 WebEngine browser with an
  adblock engine, Smart Memory suspension, private mode, 6 theme
  presets, an omnibox with bang shortcuts, bookmarks, history, and a
  download manager.

[Unreleased]: https://github.com/De-violet/web-browser/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/De-violet/web-browser/compare/v0.1.7...v0.2.0
[0.1.0]: https://github.com/De-violet/web-browser/releases/tag/v0.1.0
