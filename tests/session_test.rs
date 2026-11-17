use falkon_rust::session::{
    clear_session, is_restorable_url, load_session, save_session, session_path, tabs_to_session,
    MAX_SESSION_TABS,
};
use falkon_rust::tabs::{TabItem, TabManager};

fn unique_temp_dir(name: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "falkon_test_{}_{}_{}",
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

fn tab(url: &str, title: &str, is_private: bool) -> TabItem {
    TabItem {
        id: 0,
        url: url.to_string(),
        title: title.to_string(),
        is_private,
    }
}

#[test]
fn test_is_restorable_url() {
    assert!(is_restorable_url("https://example.com"));
    assert!(is_restorable_url("http://example.com/x"));
    assert!(!is_restorable_url("falkon:home"));
    assert!(!is_restorable_url("falkon:settings"));
    assert!(!is_restorable_url("about:blank"));
    assert!(!is_restorable_url("data:text/html,hi"));
    assert!(!is_restorable_url(""));
    assert!(!is_restorable_url("javascript:alert(1)"));
}

#[test]
fn test_tabs_to_session_filters_private_and_internal() {
    let tabs = vec![
        tab("https://a.example", "A", false),
        tab("https://secret.example", "Secret", true),
        tab("falkon:home", "Home", false),
        tab("about:blank", "", false),
        tab("https://b.example/page", "B", false),
    ];
    let session = tabs_to_session(&tabs, 4).expect("harus ada sesi");
    assert_eq!(session.tabs.len(), 2);
    assert_eq!(session.tabs[0].url, "https://a.example");
    assert_eq!(session.tabs[1].url, "https://b.example/page");
    // Indeks aktif 4 (tab B) dipetakan ke posisi 1 setelah filtering.
    assert_eq!(session.active_index, 1);
    assert!(!session.tabs.iter().any(|t| t.url.contains("secret")));

    // Hanya tab privat/internal -> tidak ada sesi.
    let only_private = vec![tab("https://secret.example", "S", true)];
    assert!(tabs_to_session(&only_private, 0).is_none());
    assert!(tabs_to_session(&[], 0).is_none());
}

#[test]
fn test_save_load_roundtrip() {
    let dir = unique_temp_dir("session");
    let mut mgr = TabManager::new();
    mgr.add_with_mode("https://a.example", false);
    mgr.add_with_mode("https://b.example", false);
    mgr.add_with_mode("https://privat.example", true);
    mgr.set_active(1);

    let saved = save_session(&dir, &mgr.all_tabs(), mgr.active_index()).unwrap();
    assert!(saved);
    assert!(session_path(&dir).exists());

    let loaded = load_session(&dir).expect("sesi harus bisa dimuat");
    assert_eq!(loaded.tabs.len(), 2);
    assert_eq!(loaded.tabs[0].url, "https://a.example");
    assert_eq!(loaded.tabs[1].url, "https://b.example");
    assert_eq!(loaded.active_index, 1);

    // Menyimpan sesi kosong (hanya privat) tidak menimpa file lama.
    let only_private = vec![tab("https://privat.example", "P", true)];
    let saved2 = save_session(&dir, &only_private, 0).unwrap();
    assert!(!saved2);
    assert!(load_session(&dir).is_some());

    clear_session(&dir);
    assert!(load_session(&dir).is_none());
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_load_session_rejects_corrupt_and_clamps_index() {
    let dir = unique_temp_dir("session_bad");
    std::fs::write(session_path(&dir), "bukan json {{").unwrap();
    assert!(load_session(&dir).is_none());

    std::fs::write(
        session_path(&dir),
        r#"{"version":1,"tabs":[{"url":"falkon:home","title":"H"}],"active_index":0,"saved_at":0}"#,
    )
    .unwrap();
    assert!(load_session(&dir).is_none());

    std::fs::write(
        session_path(&dir),
        r#"{"version":1,"tabs":[{"url":"https://a.example","title":"A"}],"active_index":99,"saved_at":0}"#,
    )
    .unwrap();
    let loaded = load_session(&dir).expect("sesi valid harus dimuat");
    assert_eq!(loaded.active_index, 0);
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_session_tab_cap() {
    let tabs: Vec<TabItem> = (0..MAX_SESSION_TABS + 10)
        .map(|i| tab(&format!("https://site{i}.example"), "T", false))
        .collect();
    let session = tabs_to_session(&tabs, 0).unwrap();
    assert_eq!(session.tabs.len(), MAX_SESSION_TABS);
}
