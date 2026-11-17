use attis::adblock_engine::{normalize_domain, AdBlockManager};
use attis::config::ConfigManager;
use attis::history::HistoryManager;
use attis::pages::{escape_html, generate_settings_page_html, generate_start_page_html};
use attis::tabs::TabManager;
use attis::theme::generate_qss;

#[test]
fn test_builtin_rules_block_tracking() {
    let mgr = AdBlockManager::new();

    // Tracking / Ads must be blocked (3 = Script, 4 = Image, 0 = MainFrame)
    assert!(mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://example.com"
    ));
    assert!(mgr.check_url(
        "https://doubleclick.net/pagead/ads.js",
        3,
        "https://example.com"
    ));
    assert!(mgr.check_url(
        "https://adservice.google.com/adsid/integrator.js",
        3,
        "https://example.com"
    ));
    assert!(mgr.check_url(
        "https://criteo.com/delivery/ld.js",
        3,
        "https://example.com"
    ));

    // Legitimate content must NOT be blocked
    assert!(!mgr.check_url(
        "https://wikipedia.org/wiki/Rust",
        0,
        "https://wikipedia.org"
    ));
    assert!(!mgr.check_url("https://rust-lang.org/learn", 0, "https://rust-lang.org"));
    assert!(!mgr.check_url(
        "https://duckduckgo.com/assets/logo.svg",
        4,
        "https://duckduckgo.com"
    ));

    assert!(mgr.blocked_count() >= 4);

    // Disable adblock toggle
    mgr.set_enabled(false);
    assert!(!mgr.check_url(
        "https://doubleclick.net/pagead/ads.js",
        3,
        "https://example.com"
    ));
    mgr.set_enabled(true);
}

#[test]
fn test_custom_rule_loading_and_cosmetic_css() {
    let mgr = AdBlockManager::new();

    let rule_file = std::env::temp_dir().join("test_attis_ublock_rules.txt");
    std::fs::write(&rule_file, "||bad-domain.xyz^\n/tracking-pixel.gif$image\n").unwrap();

    let loaded = mgr
        .load_rule_file(&rule_file)
        .expect("Failed to load rules");
    assert_eq!(loaded, 2);

    assert!(mgr.check_url("https://bad-domain.xyz/script.js", 3, "https://example.com"));
    assert!(mgr.check_url(
        "https://example.com/tracking-pixel.gif",
        4,
        "https://example.com"
    ));
    assert!(!mgr.check_url(
        "https://good-domain.xyz/script.js",
        3,
        "https://example.com"
    ));

    let css = mgr.cosmetic_css_for_url("https://example.com");
    assert!(css.contains("display: none !important;"));

    let _ = std::fs::remove_file(&rule_file);
}

#[test]
fn test_history_recording_isolated() {
    let hist_path = std::env::temp_dir().join("test_attis_history.json");
    let _ = std::fs::remove_file(&hist_path);

    let history = HistoryManager::with_path(&hist_path);
    history.record("https://duckduckgo.com", "DuckDuckGo");
    history.record("https://rust-lang.org", "Rust Programming Language");
    history.record("attis:home", "New Tab"); // internal page should be ignored

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
    let cfg_dir = std::env::temp_dir().join("test_attis_cfg_dir");
    let _ = std::fs::remove_dir_all(&cfg_dir);

    let cfg = ConfigManager::load_from_dir(&cfg_dir);
    assert_eq!(cfg.config.homepage, "attis:home");
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
    assert_eq!(
        escape_html("<script>\"x\"</script>"),
        "&lt;script&gt;&quot;x&quot;&lt;/script&gt;"
    );

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
        "attis:home",
    );
    assert!(settings_html.contains("attis-set://"));

    let qss = generate_qss(
        "#eff1f5", "#e6e9ef", "#4c4f69", "#1e66f5", "#ccd0da", 0.95, "compact", "", "a.svg",
        "b.svg",
    );
    assert!(qss.contains("QMainWindow"));
}

#[test]
fn test_normalize_domain() {
    assert_eq!(
        normalize_domain("https://WWW.Example.com/path?q=1"),
        Some("example.com".to_string())
    );
    assert_eq!(
        normalize_domain("sub.example.com:8080"),
        Some("sub.example.com".to_string())
    );
    assert_eq!(
        normalize_domain("example.com."),
        Some("example.com".to_string())
    );
    assert_eq!(normalize_domain("  "), None);
    assert_eq!(normalize_domain("exa mple.com"), None);
    assert_eq!(normalize_domain(""), None);
}

#[test]
fn test_allowlist_exempts_listed_domain_and_subdomains() {
    let mgr = AdBlockManager::with_lists(
        &[],
        true,
        &[],
        &["Example.com".to_string(), "kerja.id".to_string()],
    );
    assert_eq!(mgr.allowlist(), vec!["example.com", "kerja.id"]);

    // Tracker dari domain yang masuk allowlist tidak diblokir.
    assert!(!mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://example.com/page"
    ));
    assert!(!mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://sub.example.com/page"
    ));
    assert!(!mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://www.example.com/page"
    ));
    // Domain lain tetap diblokir; suffix yang mirip bukan subdomain.
    assert!(mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://other-site.org/page"
    ));
    assert!(mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://notexample.com/page"
    ));
    // CSS kosmetik juga tidak diinjeksikan di domain allowlist.
    assert!(mgr.cosmetic_css_for_url("https://example.com/").is_empty());
    assert!(!mgr
        .cosmetic_css_for_url("https://other-site.org/")
        .is_empty());

    // Mengganti allowlist berlaku langsung.
    mgr.set_allowlist(vec!["other-site.org".to_string()]);
    assert!(mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://example.com/page"
    ));
    assert!(!mgr.check_url(
        "https://google-analytics.com/analytics.js",
        3,
        "https://other-site.org/page"
    ));
}

#[test]
fn test_reload_lists_and_rule_count() {
    let builtin_only = AdBlockManager::new();
    let builtin_count = builtin_only.rule_count();
    assert!(builtin_count > 50);
    assert!(!builtin_only.check_url(
        "https://ads-test.example/banner.js",
        3,
        "https://example.com"
    ));

    let mgr = AdBlockManager::with_lists(
        &[],
        true,
        &["||ads-test.example^\n##.test-ad-slot\n".to_string()],
        &[],
    );
    assert!(mgr.rule_count() > builtin_count);
    assert!(mgr.check_url(
        "https://ads-test.example/banner.js",
        3,
        "https://example.com"
    ));

    // Reload dengan daftar kosong mengembalikan mesin ke rule bawaan.
    mgr.reload_lists(vec![]);
    assert_eq!(mgr.rule_count(), builtin_count);
    assert!(!mgr.check_url(
        "https://ads-test.example/banner.js",
        3,
        "https://example.com"
    ));
}

#[test]
fn test_builtin_sentry_rule_blocks_third_party() {
    let mgr = AdBlockManager::new();
    assert!(mgr.check_url(
        "https://sentry.io/api/123/envelope",
        13,
        "https://example.com"
    ));
}

#[test]
fn test_cosmetic_css_cache_is_consistent() {
    let mgr = AdBlockManager::new();
    let first = mgr.cosmetic_css_for_url("https://example.com/some-page");
    let second = mgr.cosmetic_css_for_url("https://example.com/some-page");
    assert!(!first.is_empty());
    assert_eq!(first, second);
}
