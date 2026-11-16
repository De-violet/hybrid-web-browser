use falkon_rust::adblock_engine::AdBlockManager;
use falkon_rust::bridge::{c_to_str, BrowserApp};
use falkon_rust::config::ConfigManager;
use falkon_rust::history::HistoryManager;
use falkon_rust::tabs::TabManager;
use std::env;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::atomic::{AtomicPtr, Ordering};
use std::sync::{Mutex, OnceLock};

static ADBLOCK: OnceLock<AdBlockManager> = OnceLock::new();
static TAB_MGR: OnceLock<Mutex<TabManager>> = OnceLock::new();
static HISTORY_MGR: OnceLock<HistoryManager> = OnceLock::new();
static BROWSER_PTR: AtomicPtr<BrowserApp> = AtomicPtr::new(std::ptr::null_mut());

#[no_mangle]
pub extern "C" fn rust_set_adblock_enabled(enabled: bool) {
    if let Some(mgr) = ADBLOCK.get() {
        mgr.set_enabled(enabled);
    }
}

#[no_mangle]
pub extern "C" fn rust_get_cosmetic_css(url: *const c_char) -> *mut c_char {
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
    }
}

extern "C" fn rust_tab_close_callback(tab_index: c_int) {
    if let Some(mgr_lock) = TAB_MGR.get() {
        if let Ok(mut mgr) = mgr_lock.lock() {
            mgr.remove(tab_index as usize);
        }
    }
}

extern "C" fn rust_tab_move_callback(from_index: c_int, to_index: c_int) {
    if let Some(mgr_lock) = TAB_MGR.get() {
        if let Ok(mut mgr) = mgr_lock.lock() {
            mgr.move_tab(from_index as usize, to_index as usize);
        }
    }
}

fn print_usage() {
    println!("Falkon-Rust — High-Performance Minimalist Browser with uBlock Origin & Smart Memory");
    println!("Usage: falkon-rust [OPTIONS] [URL]");
    println!();
    println!("Options:");
    println!("  --rules <FILE>          Load custom uBlock/EasyList filter rules");
    println!("  --check <URL>           Headless verification: test if URL is blocked by uBlock");
    println!("  --homepage <URL>        Override default homepage");
    println!("  --search <TEMPLATE>     Override search template (e.g. 'https://search.brave.com/search?q=%s')");
    println!("  --private               Open initial tab in Private / Incognito mode");
    println!("  -h, --help              Show this help message");
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let cfg_mgr = ConfigManager::load();
    let adblock = AdBlockManager::with_custom_rules(
        &cfg_mgr.config.custom_rules,
        cfg_mgr.config.enable_adblock,
    );

    // Load custom rules from config directory if present
    let custom_rules_file = cfg_mgr.dir.join("custom_rules.txt");
    if custom_rules_file.exists() {
        let _ = adblock.load_rule_file(&custom_rules_file);
    }

    let mut initial_url = cfg_mgr.config.homepage.clone();
    let mut search_override: Option<String> = None;
    let mut check_url: Option<String> = None;
    let mut start_private = false;
    let mut i = 1;

    while i < args.len() {
        match args[i].as_str() {
            "-h" | "--help" => {
                print_usage();
                return;
            }
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
            "--private" | "--incognito" => {
                start_private = true;
            }
            url if !url.starts_with('-') => {
                initial_url = url.to_string();
            }
            _ => {}
        }
        i += 1;
    }

    if let Some(target_url) = check_url {
        let is_blocked = adblock.check_url(&target_url, 3, "https://example.com");
        println!(
            "Check '{}': {}",
            target_url,
            if is_blocked { "BLOCKED" } else { "ALLOWED" }
        );
        return;
    }

    let _ = ADBLOCK.set(adblock);
    let _ = HISTORY_MGR.set(HistoryManager::new());

    let mut tab_manager = TabManager::new();
    tab_manager.add_with_mode(&initial_url, start_private);
    let _ = TAB_MGR.set(Mutex::new(tab_manager));

    let app = BrowserApp::new(
        "Falkon-Rust",
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
        if start_private {
            (*app_raw).add_private_tab(&initial_url);
        } else {
            (*app_raw).add_tab(&initial_url);
        }
        let exit_code = (*app_raw).run();
        // Reset global pointer SEBELUM menjatuhkan BrowserApp agar IO thread tidak mengakses dangling pointer
        BROWSER_PTR.store(std::ptr::null_mut(), Ordering::SeqCst);
        let _ = Box::from_raw(app_raw);
        std::process::exit(exit_code);
    }
}
