use falkon_rust::adblock_engine::AdBlockManager;
use falkon_rust::config::ConfigManager;
use falkon_rust::history::HistoryManager;
use falkon_rust::pages::{escape_html, generate_settings_page_html, generate_start_page_html};
use falkon_rust::tabs::TabManager;
use falkon_rust::theme::generate_qss;

#[test]
fn test_builtin_rules_block_tracking() {
    let mgr = AdBlockManager::new();

    // Tracking / Ads must be blocked (3 = Script, 4 = Image, 0 = MainFrame)
    assert!(mgr.check_url("https://google-analytics.com/analytics.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://doubleclick.net/pagead/ads.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://adservice.google.com/adsid/integrator.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://criteo.com/delivery/ld.js", 3, "https://example.com"));

    // Legitimate content must NOT be blocked
    assert!(!mgr.check_url("https://wikipedia.org/wiki/Rust", 0, "https://wikipedia.org"));
    assert!(!mgr.check_url("https://rust-lang.org/learn", 0, "https://rust-lang.org"));
    assert!(!mgr.check_url("https://duckduckgo.com/assets/logo.svg", 4, "https://duckduckgo.com"));

    assert!(mgr.blocked_count() >= 4);

    // Disable adblock toggle
    mgr.set_enabled(false);
    assert!(!mgr.check_url("https://doubleclick.net/pagead/ads.js", 3, "https://example.com"));
    mgr.set_enabled(true);
}

#[test]
fn test_custom_rule_loading_and_cosmetic_css() {
    let mgr = AdBlockManager::new();

    let rule_file = std::env::temp_dir().join("test_falkon_ublock_rules.txt");
    std::fs::write(&rule_file, "||bad-domain.xyz^\n/tracking-pixel.gif$image\n").unwrap();

    let loaded = mgr.load_rule_file(&rule_file).expect("Failed to load rules");
    assert_eq!(loaded, 2);

    assert!(mgr.check_url("https://bad-domain.xyz/script.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://example.com/tracking-pixel.gif", 4, "https://example.com"));
    assert!(!mgr.check_url("https://good-domain.xyz/script.js", 3, "https://example.com"));

    let css = mgr.cosmetic_css_for_url("https://example.com");
    assert!(css.contains("display: none !important;"));

    let _ = std::fs::remove_file(&rule_file);
}

#[test]
fn test_history_recording_isolated() {
    let hist_path = std::env::temp_dir().join("test_falkon_history.json");
    let _ = std::fs::remove_file(&hist_path);

    let history = HistoryManager::with_path(&hist_path);
    history.record("https://duckduckgo.com", "DuckDuckGo");
    history.record("https://rust-lang.org", "Rust Programming Language");
    history.record("falkon:home", "New Tab"); // internal page should be ignored

    let items = history.list();
    assert_eq!(items.len(), 2);
    assert_eq!(items[0].url, "https://duckduckgo.com");
    assert_eq!(items[1].url, "https://rust-lang.org");

    history.clear();
    assert!(history.list().is_empty());
    let _ = std::fs::remove_file(&hist_path);
}

#[test]
fn test_config_manager_isolated() {
    let cfg_dir = std::env::temp_dir().join("test_falkon_cfg_dir");
    let _ = std::fs::remove_dir_all(&cfg_dir);

    let cfg = ConfigManager::load_from_dir(&cfg_dir);
    assert_eq!(cfg.config.homepage, "falkon:home");
    assert!(cfg.config.enable_adblock);
    assert!(!cfg.config.bg_color.is_empty());

    let _ = std::fs::remove_dir_all(&cfg_dir);
}

#[test]
fn test_tab_manager_move_and_restore() {
    let mut tabs = TabManager::new();
    tabs.add("https://rust-lang.org");
    tabs.add("https://crates.io");
    tabs.move_tab(0, 1);
    assert_eq!(tabs.get(1).unwrap().url, "https://rust-lang.org");

    let closed = tabs.remove(1).unwrap();
    assert_eq!(closed.url, "https://rust-lang.org");
    let restored = tabs.pop_recently_closed().unwrap();
    assert_eq!(restored.url, "https://rust-lang.org");
}

#[test]
fn test_pages_and_theme_generation() {
    assert_eq!(escape_html("<script>\"x\"</script>"), "&lt;script&gt;&quot;x&quot;&lt;/script&gt;");

    let start_html = generate_start_page_html("#89b4fa", "#cdd6f4", true, true, true, true);
    assert!(start_html.contains("https://www.google.com/search"));

    let settings_html = generate_settings_page_html(
        "catppuccin",
        "#1e1e2e",
        "#181825",
        "#cdd6f4",
        "#cba6f7",
        "#45475a",
        0.9,
        "comfortable",
        "top",
        1.0,
        true,
        true,
        true,
        true,
        "/* qss */",
        "falkon:home",
    );
    assert!(settings_html.contains("falkon-set://"));

    let qss = generate_qss("#eff1f5", "#e6e9ef", "#4c4f69", "#1e66f5", "#ccd0da", 0.95, "compact", "", "a.svg", "b.svg");
    assert!(qss.contains("QMainWindow"));
}
