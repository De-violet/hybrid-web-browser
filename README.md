# Attis

[![Build and Release](https://github.com/De-violet/web-browser/actions/workflows/build.yml/badge.svg)](https://github.com/De-violet/web-browser/actions/workflows/build.yml)

**Attis** (formerly Falkon-Rust) is a high-performance minimalist web browser that combines **Rust** (a uBlock Origin / EasyList-based ad and tracker blocker, configuration management, asynchronous history, and interface generation) with **Qt6 WebEngine (C++17)** for rendering modern web pages while keeping memory usage low.

Its focus: **low RAM even with many tabs, smooth video streaming, aggressive yet lightweight ad blocking, ease of use, and flexible customization.**

> **Why Attis?** In myth, Attis was the god whose sacrifice brought forth violets — violet and sacrifice in a single name. The default theme is violet.

## Download

Every push to `main` publishes an up-to-date development build as the rolling `continuous` prerelease on the [Releases page](https://github.com/De-violet/web-browser/releases). Stable releases are tagged `v*` (for example `v0.2.0`).

## Key Features

- **Rust-native uBlock Origin engine (`adblock` crate):** Blocks ads and trackers at the network request interceptor level before they touch the network, with automatic per-domain cosmetic CSS filter injection. EasyList, EasyPrivacy, uBlock, and ABPindo filter lists can be updated via `--update-filters` or the button in Settings, then cached locally (see [Filter Lists & Updates](#filter-lists--updates)). A per-domain allowlist exempts sites you choose.
- **Smart Memory Suspension:** Automatically suspends inactive background tabs to save RAM, while protecting tabs that are playing audio/video. Time thresholds, the many-tabs threshold, and the HTTP cache size can be changed in Settings.
- **Session Restore:** Tabs that were open when the browser closed are restored when it is opened again. Private tabs are never saved. Disable it in Settings or run with `--no-restore` for a fresh session.
- **True Private / Incognito Mode:** Uses a separate off-the-record `QWebEngineProfile` that stores no browsing history, disk cache, or persistent cookies (`Ctrl+Shift+N` or `--private`).
- **7 Modern Theme Presets + Custom QSS:** *Violet* (default), *Dark Glass*, *Catppuccin Mocha*, *Nordic Frost*, *Gruvbox Dark*, *Minimal OLED*, and *Catppuccin Latte (Light)*, with a color token editor and `user_chrome.qss`.
- **Smart Omnibox & Bang Shortcuts:** Direct search with `!g` (Google), `!d` (DuckDuckGo), `!gh` (GitHub), `!yt` (YouTube), and `!w` (Wikipedia) prefixes.
- **Complete UI/UX:**
  - Inline find-in-page bar (`Ctrl+F`) with next/previous navigation and a match counter.
  - HTTPS/HTTP security indicator in the address bar, tab favicons, and a page-load progress bar.
  - Restore recently closed tabs (`Ctrl+Shift+T`) and a right-click tab context menu (Duplicate, Suspend, Close Other Tabs).
  - Download manager (`Ctrl+J`), bookmarks (`Ctrl+B` / `Ctrl+D`) with a search box, and history (`Ctrl+H`) with instant search.
  - Full support for `target="_blank"` links, popup windows, HTML5 video fullscreen, and device permission dialogs (camera, microphone, location, notifications).

---

## Build Prerequisites

You need a **Rust toolchain** (`cargo`, `rustc`) and the **Qt6 WebEngine** and **Qt6 SVG** development libraries:

### Arch Linux / Manjaro
```bash
sudo pacman -S base-devel rust pkgconf qt6-base qt6-webengine qt6-svg
```

### Ubuntu / Debian / Linux Mint
```bash
sudo apt update
sudo apt install build-essential cargo rustc pkg-config qt6-base-dev qt6-webengine-dev libqt6svg6-dev
```

### Fedora
```bash
sudo dnf install gcc-c++ cargo rust pkgconf-pkg-config qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtsvg-devel
```

### Windows
Install the Rust toolchain and Visual Studio Build Tools (C++ workload), then install Qt 6.7.2 (MSVC 2019 64-bit) with the `qtwebengine`, `qtwebchannel`, and `qtpositioning` modules plus `qtbase`, `qtdeclarative`, and `qtsvg` — for example with the Qt Online Installer or `aqtinstall`. Set `QT_DIR` to the Qt installation directory before building:

```powershell
$env:QT_DIR = "C:\Qt\6.7.2\msvc2019_64"
cargo build --release
```

---

## How to Build & Run

```bash
# 1. Build in release mode (optimized)
cargo build --release

# 2. Run the application
./target/release/attis

# 3. Run unit & integration tests
cargo test
```

### Command-Line Options (CLI)

| Option | Description |
| :--- | :--- |
| `attis [URL]` | Open a specific URL at startup |
| `--private` / `--incognito` | Open the initial tab in Private Mode |
| `--rules <FILE>` | Load an additional EasyList / uBlock filter rules file |
| `--update-filters` | Download the active filter lists, save them to the cache, then exit (no GUI) |
| `--filter-status` | Show filter-list cache status (no GUI) |
| `--check <URL>` | Test whether a URL is blocked by the adblock engine (no GUI) |
| `--no-restore` | Skip session restore for this run |
| `--version` | Show the application version |
| `--homepage <URL>` | Override the default homepage for this run |
| `--search <TEMPLATE>` | Override the search engine URL (use `%s` for the query) |
| `-h`, `--help` | Show usage help |

---

## Filter Lists & Updates

By default, the browser uses ~70 built-in offline core rules, so it still blocks common trackers without internet access. For full aggressive blocking, download the complete filter lists:

```bash
./target/release/attis --update-filters
# or from Settings (Ctrl+,) → Privacy & AdBlock → Update Filter Lists
```

Lists used (all can be enabled/disabled via `filter_lists` in `config.json`):

| List | Contents |
| :--- | :--- |
| `easylist` | General ads (EasyList) |
| `easyprivacy` | Trackers & privacy (EasyPrivacy) |
| `ublock-filters` | uBlock Origin filters |
| `ublock-unbreak` | Fixes for sites broken by filters |
| `ublock-badware` | Malware/badware domains (opt-in, off by default) |
| `ublock-privacy` | uBlock Origin privacy filters (opt-in, off by default) |
| `abpindo` | Indonesian-language ads (ABPindo) |

Downloaded lists are stored in `filter_lists/` inside the configuration directory and reloaded automatically at startup. If `filter_auto_update` is enabled (default), expired lists (24 hours by default, see `filter_update_interval_hours`) are refreshed in the background without blocking startup; if a download fails, the old cache is kept.

Downloads use `curl` (or `wget` as a fallback; on Windows `curl.exe` then PowerShell), with no extra HTTP dependencies so the binary stays light. Note: scriptlet-based rules (`##+js(...)`) and `$redirect`/`$replace` rules from uBlock lists are not executed by the `adblock` engine — network blocking and cosmetic CSS filtering are what is applied.

## Saving RAM

- **Tab suspension:** background tabs inactive for longer than `suspend_after_seconds` (default 120 seconds; 45 seconds when the open tab count exceeds `suspend_many_tabs_threshold`) are unloaded to `about:blank` until revisited. Tabs that are playing sound are never suspended, so video/music streaming is safe. Disable via `suspend_enabled: false` or in Settings.
- **In-memory HTTP cache:** limited to `http_cache_size_mb` (default 20 MB).
- **Renderer process limit:** set `renderer_process_limit` in `config.json` (for example `4`) to cap Chromium renderer processes on low-RAM machines. `0` (default) applies the automatic limit of 4. Changes apply when the browser is reopened.

---

## Keyboard Shortcuts

| Shortcut | Function |
| :--- | :--- |
| `Ctrl + T` | Open New Tab |
| `Ctrl + Shift + N` | Open Private (Incognito) Tab |
| `Ctrl + W` | Close Active Tab |
| `Ctrl + Shift + T` | Restore Recently Closed Tab |
| `Ctrl + Tab` / `Ctrl + Shift + Tab` | Switch Between Tabs |
| `Ctrl + L` | Focus Address Bar |
| `Ctrl + F` | Open Find-in-Page Bar |
| `Ctrl + D` | Add / Remove Page Bookmark |
| `Ctrl + B` | Open Bookmarks |
| `Ctrl + H` | Open Browsing History |
| `Ctrl + J` | Open Download Manager |
| `Ctrl + ,` | Open Settings Page (`attis:settings`) |
| `Ctrl + M` | Hide / Show Navigation Buttons (Minimal Mode) |
| `Ctrl + +` / `Ctrl + -` / `Ctrl + 0` | Zoom In / Zoom Out / Reset Page Zoom |
| `F11` | Fullscreen Mode |

---

## Configuration Directory Layout

Attis keeps all of its configuration in a single directory, shared by
the Rust engine and the Qt interface:

- Linux: `~/.config/attis/` (or `$XDG_CONFIG_HOME/attis/`). Override
  with the `ATTIS_CONFIG_DIR` environment variable.
- Windows: `%USERPROFILE%\.config\attis\` (override with
  `ATTIS_CONFIG_DIR`). On the first run after upgrading from a version
  before the unified directory, `bookmarks.json` and `user_chrome.qss`
  are copied once from the previous location (`%APPDATA%\attis\`) when
  not already present; `config.json` is not migrated, so settings are
  applied fresh once.

Contents:

- `config.json` — Theme, colors, tab bar position, zoom, homepage preferences, filters, session, and RAM-saving settings.
- `bookmarks.json` — Bookmarked sites.
- `history.json` — Browsing history records.
- `session.json` — Tabs restored at startup (never contains private tabs).
- `filter_lists/` — Downloaded filter-list cache (`*.txt`) and `status.json`.
- `custom_rules.txt` — Additional user uBlock / EasyList filter rules.
- `user_chrome.qss` — Custom QSS stylesheet for the Qt window interface.
- `user_style.css` — Custom CSS injected into every web page.
- `user_script.js` — Custom JavaScript userscript injected when a page finishes loading.

All keys in `config.json` are optional and have defaults, so configuration files from older versions remain readable.

---

## Troubleshooting

- **`Qt6WebEngineWidgets must be installed` during build:** install the Qt6 WebEngine packages for your distro (see above), then run `cargo build --release` again.
- **Filters fail to download (`--update-filters` fails):** make sure `curl` or `wget` is installed and internet access is available. The browser keeps working with the built-in offline rules.
- **Broken site / video won't play:** try disabling adblock for that site by adding it to the allowlist in Settings → Privacy & AdBlock, then reload the page.
- **Tabs suspend too quickly:** raise `suspend_after_seconds` in Settings → General, or turn off Smart Memory.

---

## License

Attis is licensed under the [GPL-3.0 License](LICENSE). The EasyList, EasyPrivacy, uBlock Origin, and ABPindo filter lists are the work of their respective authors under their own licenses.
