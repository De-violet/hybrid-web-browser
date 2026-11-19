use attis::config::{BrowserConfig, ConfigManager};
use attis::pages::settings_extras_from_dir;

fn unique_temp_dir(name: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "attis_test_{}_{}_{}",
        name,
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0)
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn test_new_config_defaults() {
    let cfg = BrowserConfig::default();
    assert!(cfg.restore_session);
    assert!(cfg.suspend_enabled);
    assert_eq!(cfg.suspend_after_seconds, 120);
    assert_eq!(cfg.suspend_many_tabs_after_seconds, 45);
    assert_eq!(cfg.suspend_many_tabs_threshold, 3);
    assert_eq!(cfg.http_cache_size_mb, 20);
    assert_eq!(cfg.renderer_process_limit, 0);
    assert!(cfg.filter_auto_update);
    assert_eq!(cfg.filter_update_interval_hours, 24);
    assert!(cfg.filter_lists.contains(&"easylist".to_string()));
    assert!(cfg.filter_lists.contains(&"abpindo".to_string()));
    assert!(cfg.custom_filter_urls.is_empty());
    assert!(cfg.adblock_allowlist.is_empty());
}

#[test]
fn test_legacy_config_loads_with_new_defaults() {
    let dir = unique_temp_dir("legacy_cfg");
    // config.json gaya lama: hanya sebagian kunci, tanpa kunci baru.
    std::fs::write(
        dir.join("config.json"),
        r#"{"homepage":"https://example.com","theme":"nord","default_zoom":1.25}"#,
    )
    .unwrap();

    let loaded = ConfigManager::load_from_dir(&dir);
    assert_eq!(loaded.config.homepage, "https://example.com");
    assert_eq!(loaded.config.theme, "nord");
    assert!((loaded.config.default_zoom - 1.25).abs() < f32::EPSILON);
    // Kunci baru memakai nilai bawaan, bukan gagal parse.
    assert!(loaded.config.restore_session);
    assert_eq!(loaded.config.suspend_after_seconds, 120);
    assert_eq!(loaded.config.http_cache_size_mb, 20);
    assert!(loaded.config.filter_auto_update);

    // Roundtrip simpan/muat mempertahankan nilai.
    loaded.save().unwrap();
    let reloaded = ConfigManager::load_from_dir(&dir);
    assert_eq!(reloaded.config.homepage, "https://example.com");
    assert!(reloaded.config.restore_session);
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_renderer_process_limit_clamped() {
    let mut cfg = BrowserConfig::default();
    // 0 = otomatis -> 4.
    assert_eq!(cfg.renderer_process_limit_clamped(), 4);
    cfg.renderer_process_limit = 1;
    assert_eq!(cfg.renderer_process_limit_clamped(), 1);
    cfg.renderer_process_limit = 4;
    assert_eq!(cfg.renderer_process_limit_clamped(), 4);
    cfg.renderer_process_limit = 999;
    assert_eq!(cfg.renderer_process_limit_clamped(), 64);
}

#[test]
fn test_download_dir_for_ffi() {
    // Nilai default -> string kosong (C++ memakai folder Downloads
    // sistem, sehingga folder yang dipindah pengguna tetap dihormati).
    let cfg = BrowserConfig::default();
    assert_eq!(cfg.download_dir_for_ffi(), "");
    // Nilai kustom dipertahankan apa adanya.
    let cfg = BrowserConfig {
        download_dir: "D:\\Unduhan".to_string(),
        ..BrowserConfig::default()
    };
    assert_eq!(cfg.download_dir_for_ffi(), "D:\\Unduhan");
    // String kosong (mis. ditulis C++ sebelumnya) tetap kosong.
    let cfg = BrowserConfig {
        download_dir: String::new(),
        ..BrowserConfig::default()
    };
    assert_eq!(cfg.download_dir_for_ffi(), "");
}

#[test]
fn test_settings_extras_from_dir() {
    let dir = unique_temp_dir("extras");
    // Tanpa config.json: nilai bawaan + status "belum pernah".
    let defaults = settings_extras_from_dir(&dir);
    assert!(defaults.restore_session);
    assert!(defaults.suspend_enabled);
    assert_eq!(defaults.suspend_after_seconds, 120);
    assert!(defaults.filter_status.contains("Never"));

    std::fs::write(
        dir.join("config.json"),
        r#"{
            "restore_session": false,
            "suspend_enabled": false,
            "suspend_after_seconds": 300,
            "suspend_many_tabs_after_seconds": 60,
            "suspend_many_tabs_threshold": 5,
            "http_cache_size_mb": 64,
            "filter_auto_update": false,
            "adblock_allowlist": ["Example.COM", "kerja.id"]
        }"#,
    )
    .unwrap();
    let extras = settings_extras_from_dir(&dir);
    assert!(!extras.restore_session);
    assert!(!extras.suspend_enabled);
    assert_eq!(extras.suspend_after_seconds, 300);
    assert_eq!(extras.suspend_many_after_seconds, 60);
    assert_eq!(extras.suspend_many_threshold, 5);
    assert_eq!(extras.http_cache_size_mb, 64);
    assert!(!extras.filter_auto_update);
    assert_eq!(extras.allowlist, "Example.COM, kerja.id");
    let _ = std::fs::remove_dir_all(&dir);
}
