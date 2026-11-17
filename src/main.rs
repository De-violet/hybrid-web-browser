use attis::adblock_engine::AdBlockManager;
use attis::bridge::{c_to_str, BrowserApp};
use attis::config::ConfigManager;
use attis::filter_lists::{
    is_stale, list_file_path, load_cached_lists, resolve_sources, status_summary, update_lists,
    FilterStatus,
};
use attis::history::HistoryManager;
use attis::session::{load_session, save_session, SessionData};
use attis::tabs::TabManager;
use std::env;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicPtr, AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

static ADBLOCK: OnceLock<AdBlockManager> = OnceLock::new();
static TAB_MGR: OnceLock<Mutex<TabManager>> = OnceLock::new();
static HISTORY_MGR: OnceLock<HistoryManager> = OnceLock::new();
static BROWSER_PTR: AtomicPtr<BrowserApp> = AtomicPtr::new(std::ptr::null_mut());
static CONFIG_DIR: OnceLock<PathBuf> = OnceLock::new();
static RESTORE_ENABLED: AtomicBool = AtomicBool::new(true);
static LAST_SESSION_SAVE: AtomicU64 = AtomicU64::new(0);

/// Konfigurasi updater filter untuk thread latar belakang / FFI C++.
struct FilterUpdateConfig {
    dir: PathBuf,
    enabled_lists: Vec<String>,
    custom_urls: Vec<String>,
    interval_hours: u64,
}

static FILTER_CFG: OnceLock<FilterUpdateConfig> = OnceLock::new();

/// Interval minimum antar penyimpanan sesi dari callback tab (detik).
/// Mencegah penulisan file berulang saat banyak navigasi terjadi cepat.
const SESSION_SAVE_MIN_INTERVAL_SECS: u64 = 5;

fn now_unix() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// Simpan sesi dari snapshot TabManager. Di-throttle kecuali `force`.
fn save_session_snapshot(force: bool) {
    if !RESTORE_ENABLED.load(Ordering::Relaxed) {
        return;
    }
    let Some(dir) = CONFIG_DIR.get() else {
        return;
    };
    let now = now_unix();
    if !force {
        let last = LAST_SESSION_SAVE.load(Ordering::Relaxed);
        if now.saturating_sub(last) < SESSION_SAVE_MIN_INTERVAL_SECS {
            return;
        }
    }
    let Some(mgr_lock) = TAB_MGR.get() else {
        return;
    };
    if let Ok(mgr) = mgr_lock.lock() {
        let _ = save_session(dir, &mgr.all_tabs(), mgr.active_index());
        LAST_SESSION_SAVE.store(now, Ordering::Relaxed);
    }
}

/// Apakah ada daftar filter yang perlu diperbarui (hilang/kedaluwarsa).
fn needs_filter_update(cfg: &FilterUpdateConfig) -> bool {
    resolve_sources(&cfg.enabled_lists, &cfg.custom_urls)
        .iter()
        .any(|src| is_stale(&list_file_path(&cfg.dir, &src.id), cfg.interval_hours))
}

/// Jalankan pembaruan filter di thread latar belakang, lalu muat ulang mesin.
/// Aman dipanggil dari FFI C++ (tombol di Pengaturan) maupun saat startup.
fn spawn_filter_update(force: bool) {
    let Some(cfg) = FILTER_CFG.get() else {
        return;
    };
    let dir = cfg.dir.clone();
    let enabled = cfg.enabled_lists.clone();
    let custom_urls = cfg.custom_urls.clone();
    let interval = cfg.interval_hours;
    let _ = std::thread::Builder::new()
        .name("filter-updater".to_string())
        .spawn(move || {
            let _status = update_lists(&dir, &enabled, &custom_urls, force, interval);
            let contents = load_cached_lists(&dir, &enabled, &custom_urls);
            if let Some(mgr) = ADBLOCK.get() {
                mgr.reload_lists(contents);
            }
        });
}

#[no_mangle]
pub extern "C" fn rust_set_adblock_enabled(enabled: bool) {
    if let Some(mgr) = ADBLOCK.get() {
        mgr.set_enabled(enabled);
    }
}

/// Dipanggil C++ dari Pengaturan: perbarui filter di latar belakang.
#[no_mangle]
pub extern "C" fn rust_update_filters_async() {
    spawn_filter_update(true);
}

/// Dipanggil C++ dari Pengaturan: terapkan allowlist (CSV domain) ke mesin.
/// Persistensi ke `config.json` ditangani sisi C++.
///
/// # Safety
/// `csv` harus null atau menunjuk string C valid yang null-terminated.
#[no_mangle]
pub unsafe extern "C" fn rust_set_allowlist(csv: *const c_char) {
    let csv_str = if csv.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(csv).to_str().unwrap_or("") }
    };
    if let Some(mgr) = ADBLOCK.get() {
        let domains: Vec<String> = csv_str
            .split(',')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect();
        mgr.set_allowlist(domains);
    }
}

/// # Safety
/// `url` harus null atau menunjuk string C valid yang null-terminated.
#[no_mangle]
pub unsafe extern "C" fn rust_get_cosmetic_css(url: *const c_char) -> *mut c_char {
    let url_str = if url.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(url).to_str().unwrap_or("") }
    };
    if url_str.is_empty() {
        return std::ptr::null_mut();
    }
    if let Some(mgr) = ADBLOCK.get() {
        let css = mgr.cosmetic_css_for_url(url_str);
        if !css.is_empty() {
            return CString::new(css)
                .map(|c| c.into_raw())
                .unwrap_or(std::ptr::null_mut());
        }
    }
    std::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_clear_history() {
    if let Some(hist) = HISTORY_MGR.get() {
        hist.clear();
    }
}

extern "C" fn rust_block_callback(
    url: *const c_char,
    resource_type: c_int,
    source_url: *const c_char,
) -> bool {
    let url_str = unsafe { c_to_str(url) };
    let source_str = unsafe { c_to_str(source_url) };

    if let Some(mgr) = ADBLOCK.get() {
        let blocked = mgr.check_url(url_str, resource_type, source_str);
        if blocked {
            let count = mgr.blocked_count();
            let app_ptr = BROWSER_PTR.load(Ordering::Acquire);
            if !app_ptr.is_null() {
                unsafe {
                    (*app_ptr).update_badge(count);
                }
            }
        }
        blocked
    } else {
        false
    }
}

extern "C" fn rust_tab_callback(tab_index: c_int, url: *const c_char, title: *const c_char) {
    let url_str = unsafe { c_to_str(url) };
    let title_str = unsafe { c_to_str(title) };

    let mut is_private = false;
    if let Some(mgr_lock) = TAB_MGR.get() {
        if let Ok(mut mgr) = mgr_lock.lock() {
            while mgr.count() <= tab_index as usize {
                mgr.add("");
            }
            mgr.update(tab_index as usize, url_str, title_str);
            if let Some(tab) = mgr.get(tab_index as usize) {
                is_private = tab.is_private;
            }
        }
    }

    if !is_private {
        if let Some(hist) = HISTORY_MGR.get() {
            hist.record(url_str, title_str);
        }
        save_session_snapshot(false);
    }
}

extern "C" fn rust_tab_close_callback(tab_index: c_int) {
    if let Some(mgr_lock) = TAB_MGR.get() {
        if let Ok(mut mgr) = mgr_lock.lock() {
            mgr.remove(tab_index as usize);
        }
    }
    save_session_snapshot(false);
}

extern "C" fn rust_tab_move_callback(from_index: c_int, to_index: c_int) {
    if let Some(mgr_lock) = TAB_MGR.get() {
        if let Ok(mut mgr) = mgr_lock.lock() {
            mgr.move_tab(from_index as usize, to_index as usize);
        }
    }
    save_session_snapshot(false);
}

fn print_usage() {
    println!("Attis — High-Performance Minimalist Browser with uBlock Origin & Smart Memory");
    println!("Usage: attis [OPTIONS] [URL]");
    println!();
    println!("Options:");
    println!("  --rules <FILE>          Load custom uBlock/EasyList filter rules");
    println!("  --update-filters        Download active filter lists to cache, then exit (no GUI)");
    println!("  --filter-status         Show cached filter-list status, then exit (no GUI)");
    println!("  --check <URL>           Headless verification: test if URL is blocked by uBlock");
    println!("  --homepage <URL>        Override default homepage");
    println!("  --search <TEMPLATE>     Override search template (e.g. 'https://search.brave.com/search?q=%s')");
    println!("  --private               Open initial tab in Private / Incognito mode");
    println!("  --no-restore            Skip session restore for this run");
    println!("  --version               Show version and exit");
    println!("  -h, --help              Show this help message");
}

fn print_filter_status(status: &FilterStatus) {
    println!(
        "Filter lists: {} active, {} rules total",
        status.lists.len(),
        status.total_rules
    );
    for list in &status.lists {
        match &list.error {
            Some(err) => println!(
                "  - {} ({}): {} rules (cache kept, update failed: {})",
                list.name, list.id, list.rule_count, err
            ),
            None => println!("  - {} ({}): {} rules", list.name, list.id, list.rule_count),
        }
    }
}

/// Terapkan batas proses renderer lewat variabel lingkungan Chromium.
/// Harus dipanggil SEBELUM QApplication/WebEngine dibuat. Nilai 0 = default.
/// Nilai yang sudah disetel pengguna di environment tidak ditimpa.
fn apply_renderer_process_limit(limit: u32) {
    if limit == 0 {
        return;
    }
    let existing = env::var("QTWEBENGINE_CHROMIUM_FLAGS").unwrap_or_default();
    if existing.contains("--renderer-process-limit") {
        return;
    }
    let flag = format!("--renderer-process-limit={limit}");
    let merged = if existing.trim().is_empty() {
        flag
    } else {
        format!("{existing} {flag}")
    };
    env::set_var("QTWEBENGINE_CHROMIUM_FLAGS", merged);
}

fn main() {
    let args: Vec<String> = env::args().collect();

    // Help/version ditangani sebelum memuat konfigurasi/mesin agar instan.
    if args.iter().any(|a| a == "-h" || a == "--help") {
        print_usage();
        return;
    }
    if args.iter().any(|a| a == "--version") {
        println!("attis {}", env!("CARGO_PKG_VERSION"));
        return;
    }

    let cfg_mgr = ConfigManager::load();
    let _ = CONFIG_DIR.set(cfg_mgr.dir.clone());
    RESTORE_ENABLED.store(cfg_mgr.config.restore_session, Ordering::Relaxed);
    let _ = FILTER_CFG.set(FilterUpdateConfig {
        dir: cfg_mgr.dir.clone(),
        enabled_lists: cfg_mgr.config.filter_lists.clone(),
        custom_urls: cfg_mgr.config.custom_filter_urls.clone(),
        interval_hours: cfg_mgr.config.filter_update_interval_hours,
    });

    // Muat cache daftar filter yang sudah terunduh (jika ada). Tanpa cache,
    // mesin tetap berjalan dengan rule bawaan offline.
    let cached_lists = load_cached_lists(
        &cfg_mgr.dir,
        &cfg_mgr.config.filter_lists,
        &cfg_mgr.config.custom_filter_urls,
    );
    let adblock = AdBlockManager::with_lists(
        &cfg_mgr.config.custom_rules,
        cfg_mgr.config.enable_adblock,
        &cached_lists,
        &cfg_mgr.config.adblock_allowlist,
    );

    // Load custom rules from config directory if present
    let custom_rules_file = cfg_mgr.dir.join("custom_rules.txt");
    if custom_rules_file.exists() {
        let _ = adblock.load_rule_file(&custom_rules_file);
    }

    let mut initial_url = cfg_mgr.config.homepage.clone();
    let mut explicit_url: Option<String> = None;
    let mut search_override: Option<String> = None;
    let mut check_url: Option<String> = None;
    let mut start_private = false;
    let mut no_restore = false;
    let mut update_filters = false;
    let mut filter_status_only = false;
    let mut i = 1;

    while i < args.len() {
        match args[i].as_str() {
            "--rules" => {
                if i + 1 < args.len() {
                    match adblock.load_rule_file(&args[i + 1]) {
                        Ok(n) => println!("Loaded {} rules from {}", n, args[i + 1]),
                        Err(e) => eprintln!("Failed to load rules from {}: {}", args[i + 1], e),
                    }
                    i += 1;
                }
            }
            "--homepage" => {
                if i + 1 < args.len() {
                    initial_url = args[i + 1].clone();
                    i += 1;
                }
            }
            "--search" => {
                if i + 1 < args.len() {
                    search_override = Some(args[i + 1].clone());
                    i += 1;
                }
            }
            "--check" => {
                if i + 1 < args.len() {
                    check_url = Some(args[i + 1].clone());
                    i += 1;
                }
            }
            "--update-filters" => {
                update_filters = true;
            }
            "--filter-status" | "--filters" => {
                filter_status_only = true;
            }
            "--no-restore" => {
                no_restore = true;
            }
            "--private" | "--incognito" => {
                start_private = true;
            }
            url if !url.starts_with('-') => {
                explicit_url = Some(url.to_string());
                initial_url = url.to_string();
            }
            _ => {}
        }
        i += 1;
    }

    if filter_status_only {
        println!("{}", status_summary(&cfg_mgr.dir));
        println!("Active rules in engine: {}", adblock.rule_count());
        return;
    }

    if update_filters {
        let status = update_lists(
            &cfg_mgr.dir,
            &cfg_mgr.config.filter_lists,
            &cfg_mgr.config.custom_filter_urls,
            true,
            cfg_mgr.config.filter_update_interval_hours,
        );
        print_filter_status(&status);
        return;
    }

    if let Some(target_url) = check_url {
        let is_blocked = adblock.check_url(&target_url, 3, "https://example.com");
        println!(
            "Check '{}': {} ({} active rules)",
            target_url,
            if is_blocked { "BLOCKED" } else { "ALLOWED" },
            adblock.rule_count()
        );
        return;
    }

    apply_renderer_process_limit(cfg_mgr.config.renderer_process_limit_clamped());

    let _ = ADBLOCK.set(adblock);
    let _ = HISTORY_MGR.set(HistoryManager::new());

    // Pembaruan filter otomatis berjalan di latar belakang agar tidak
    // memperlambat startup; mesin dimuat ulang setelah unduhan selesai.
    if cfg_mgr.config.filter_auto_update {
        if let Some(cfg) = FILTER_CFG.get() {
            if needs_filter_update(cfg) {
                spawn_filter_update(false);
            }
        }
    }

    // Pemulihan sesi: hanya saat tidak ada URL eksplisit, bukan privat,
    // dan tidak diminta --no-restore.
    let saved_session: Option<SessionData> = if cfg_mgr.config.restore_session
        && !no_restore
        && !start_private
        && explicit_url.is_none()
    {
        load_session(&cfg_mgr.dir)
    } else {
        None
    };

    let mut tab_manager = TabManager::new();
    if let Some(ref session) = saved_session {
        for tab in &session.tabs {
            tab_manager.add_with_mode(&tab.url, false);
        }
        tab_manager.set_active(session.active_index);
    } else {
        tab_manager.add_with_mode(&initial_url, start_private);
    }
    let _ = TAB_MGR.set(Mutex::new(tab_manager));

    let app = BrowserApp::new(
        "Attis",
        cfg_mgr.config.window_width,
        cfg_mgr.config.window_height,
        rust_block_callback,
    );
    app.set_tab_callback(rust_tab_callback);
    app.set_tab_close_callback(rust_tab_close_callback);
    app.set_tab_move_callback(rust_tab_move_callback);

    let tab_pos = match cfg_mgr.config.tab_position.to_lowercase().as_str() {
        "bottom" => 1,
        "hidden" => 2,
        _ => 0,
    };
    app.configure_ui_ux(
        tab_pos,
        cfg_mgr.config.show_nav_buttons,
        cfg_mgr.config.show_adblock_badge,
        cfg_mgr.config.show_new_tab_button,
        cfg_mgr.config.default_zoom,
    );
    let custom_qss = cfg_mgr.user_chrome_qss.as_deref();
    app.set_theme(&cfg_mgr.config.theme, custom_qss);
    if cfg_mgr.config.theme.eq_ignore_ascii_case("custom") {
        app.set_colors(
            &cfg_mgr.config.bg_color,
            &cfg_mgr.config.fg_color,
            &cfg_mgr.config.accent_color,
        );
    }

    let search_engine = search_override.unwrap_or(cfg_mgr.config.search_engine);
    app.set_search_engine(&search_engine);
    app.set_download_dir(&cfg_mgr.config.download_dir);
    if !cfg_mgr.config.user_agent.is_empty() {
        app.set_user_agent(&cfg_mgr.config.user_agent);
    }
    if let Some(ref css) = cfg_mgr.user_style_css {
        app.set_custom_user_style(css);
    }
    if let Some(ref js) = cfg_mgr.user_script_js {
        app.set_custom_user_script(js);
    }

    let app_box = Box::new(app);
    let app_raw = Box::into_raw(app_box);
    BROWSER_PTR.store(app_raw, Ordering::Release);

    unsafe {
        if let Some(ref session) = saved_session {
            for tab in &session.tabs {
                (*app_raw).add_tab(&tab.url);
            }
            (*app_raw).switch_tab(session.active_index as i32);
        } else if start_private {
            (*app_raw).add_private_tab(&initial_url);
        } else {
            (*app_raw).add_tab(&initial_url);
        }
        let exit_code = (*app_raw).run();
        // Simpan sesi terakhir sebelum keluar (tab privat tidak ikut).
        save_session_snapshot(true);
        // Reset global pointer SEBELUM menjatuhkan BrowserApp agar IO thread tidak mengakses dangling pointer
        BROWSER_PTR.store(std::ptr::null_mut(), Ordering::SeqCst);
        let _ = Box::from_raw(app_raw);
        std::process::exit(exit_code);
    }
}
