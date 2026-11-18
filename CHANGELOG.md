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

- The Rust engine and the C++ bridge now share a single configuration
  directory on every platform: the Rust side passes its resolved
  config directory to the bridge at startup instead of the bridge
  deriving a second one from Qt. On Windows the two sides previously
  used different directories (`%USERPROFILE%\.config\attis` vs
  `%APPDATA%\attis`), so almost every setting reset to defaults on
  each launch, the history dialog was always empty, and custom
  user-agent never applied. On the first run after upgrading,
  `bookmarks.json` and `user_chrome.qss` are copied once from the old
  Windows location when not already present; `config.json` is not
  migrated, so Windows settings are applied fresh once.
- Release builds now use link-time optimization, a single codegen
  unit, and stripped symbols (`[profile.release]`), and the build
  script rebuilds when the Qt location environment variables change.
- The README now documents the configuration directory per platform.
- Renamed from Falkon-Rust to Attis: package, binary, library, internal
  page scheme (`attis:home`, `attis:settings`), configuration directory
  (`~/.config/attis/`), desktop entry, and CI artifacts. Configuration
  and data from earlier installs are not migrated automatically; copy
  them from the previous config directory if needed.
- Documentation (README, CHANGELOG) is now written in English.
- Session restore is now lazy: only the active tab is loaded at
  startup; restored tabs are created suspended, show their saved title
  and URL immediately, and load the first time they are activated.
  This avoids loading all restored tabs at once on startup.
- The adblock engine is now built once at startup: custom rules from
  `custom_rules.txt` are merged before the build instead of triggering
  a second full rebuild. The serialized engine is cached in
  `filter_lists/` with hash validation over the list contents and
  custom rules, so later startups skip re-parsing the filter lists
  when nothing changed. A missing or corrupt cache falls back to a
  full parse transparently, and the cache is rewritten after filter
  updates.

### Fixed

- On Windows, launching Attis no longer opens a black console window
  next to the browser: the binary now uses the Windows GUI subsystem,
  while the CLI modes (`--check`, `--version`, `--filter-status`,
  `--update-filters`, `--rules`, `--help`) attach to the parent
  console (or allocate one) with UTF-8 output before printing.
- Fixed the `ResourceType` mapping for WebSocket requests: the code
  mapped value `20` to `websocket`, but per the Qt 6.7 header `20` is
  a navigation-preload subframe and WebSocket is `254`, so
  `$websocket` filter rules never matched. The mapping is now a
  tested pure function.
- Tab close-button icons no longer break when the config directory
  path contains spaces (e.g. a Windows username with a space): the
  icon paths in the generated stylesheet are now quoted in `url("...")`.
- A moved Downloads folder is now honored: when the download
  directory setting is still the default, downloads use the system
  Downloads location instead of a hardcoded `$HOME/Downloads`.
- **Security: settings actions can no longer be triggered by websites.**
  Navigations to `attis-set://` URLs are now only honored when they
  originate in the main frame of an internal `attis:` page (the start
  and settings pages). Subframe navigations, navigations from web
  pages, and URLs typed into the address bar are ignored. The unused
  `attis-action` URL alias has been removed.
- **Privacy: private tabs no longer leak into history and session
  restore.** The tab callback now carries the tab's private flag from
  the C++ bridge, so private tabs opened from the UI (menu, shortcut,
  or popups) are excluded from `history.json` and `session.json`
  instead of being recorded and restored as normal tabs.
- Fixed a crash when a theme background color contained non-ASCII
  characters: hex color parsing now validates each character before
  parsing instead of slicing the string by byte offsets.
- Theme color values are now strictly validated (`#RGB` / `#RRGGBB`
  only) before being inserted into stylesheets, in both the Rust
  theme generator and the C++ dialog stylesheets, so a crafted color
  value cannot inject additional stylesheet rules.
- Hovering a link no longer makes the page jump: the hover-URL/toast
  bar is now a floating overlay anchored to the bottom-left of the
  window instead of a row in the main layout, so showing it cannot
  resize the web view. The overlay is click-through and only displays
  the URL.
- The custom minimize/maximize/close buttons are now only shown on
  Linux, where tiling compositors such as Hyprland draw no title-bar
  buttons. On Windows and macOS the native window frame already
  provides them, so the custom buttons no longer appear doubled.
- `renderer_process_limit` in `config.json` is now honored: the C++
  bridge previously overwrote the Chromium flags environment variable
  with a hardcoded `--renderer-process-limit=2`, so the effective
  limit was always 2 regardless of the setting. The bridge now merges
  its flags with the ones set from configuration (or the shell), and
  the renderer limit is owned by the Rust side. A value of `0`
  (default) now applies an automatic limit of 4.

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
