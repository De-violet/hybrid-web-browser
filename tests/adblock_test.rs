use falkon_rust::adblock_engine::AdBlockManager;
use falkon_rust::config::ConfigManager;
use falkon_rust::history::HistoryManager;

#[test]
fn test_builtin_rules_block_tracking() {
    let mgr = AdBlockManager::new();

    // Tracking / Ads must be blocked
    assert!(mgr.check_url("https://google-analytics.com/analytics.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://doubleclick.net/pagead/ads.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://adservice.google.com/adsid/integrator.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://criteo.com/delivery/ld.js", 3, "https://example.com"));

    // Legitimate content must NOT be blocked
    assert!(!mgr.check_url("https://wikipedia.org/wiki/Rust", 0, "https://wikipedia.org"));
    assert!(!mgr.check_url("https://rust-lang.org/learn", 0, "https://rust-lang.org"));
    assert!(!mgr.check_url("https://duckduckgo.com/assets/logo.svg", 4, "https://duckduckgo.com"));

    assert!(mgr.blocked_count() >= 4);
}

#[test]
fn test_custom_rule_loading() {
    let mgr = AdBlockManager::new();

    let rule_file = "/tmp/test_ublock_rules.txt";
    std::fs::write(rule_file, "||bad-domain.xyz^\n/tracking-pixel.gif$image\n").unwrap();

    let loaded = mgr.load_rule_file(rule_file).expect("Failed to load rules");
    assert!(loaded >= 2);

    assert!(mgr.check_url("https://bad-domain.xyz/script.js", 3, "https://example.com"));
    assert!(mgr.check_url("https://example.com/tracking-pixel.gif", 4, "https://example.com"));
    assert!(!mgr.check_url("https://good-domain.xyz/script.js", 3, "https://example.com"));

    let _ = std::fs::remove_file(rule_file);
}

#[test]
fn test_history_recording() {
    let history = HistoryManager::new();
    history.record("https://duckduckgo.com", "DuckDuckGo");
    history.record("https://rust-lang.org", "Rust Programming Language");
}

#[test]
fn test_config_manager_defaults() {
    let cfg = ConfigManager::load();
    assert!(!cfg.config.homepage.is_empty());
    assert!(!cfg.config.search_engine.is_empty());
    assert!(cfg.config.enable_adblock);
    assert!(!cfg.config.bg_color.is_empty());
    assert!(!cfg.config.fg_color.is_empty());
    assert!(!cfg.config.accent_color.is_empty());
    assert!(!cfg.config.surface_color.is_empty());
    assert!(!cfg.config.border_color.is_empty());
    assert!(cfg.config.bg_opacity > 0.0);
    assert!(!cfg.config.ui_density.is_empty());
    assert!(cfg.config.show_home_search);
    assert!(cfg.config.show_home_tiles);
    assert!(cfg.config.show_home_clock);
}
