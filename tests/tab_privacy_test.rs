use attis::history::HistoryManager;
use attis::session::tabs_to_session;
use attis::tabs::TabManager;

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

/// Mirrors the gating decision `rust_tab_callback` (in the binary crate,
/// not callable from integration tests) makes with the new `is_private`
/// flag: sync the tab, and record history only for non-private tabs.
/// Session saving consults the same `TabManager` state via
/// `tabs_to_session`.
fn handle_tab_event(
    mgr: &mut TabManager,
    hist: &HistoryManager,
    index: usize,
    url: &str,
    title: &str,
    is_private: bool,
) {
    let private = mgr.sync_tab(index, url, title, is_private);
    if !private {
        hist.record(url, title);
    }
}

#[test]
fn private_tab_sequence_excluded_from_session_and_history() {
    let dir = unique_temp_dir("privacy_seq");
    let hist = HistoryManager::with_path(dir.join("history.json"));
    let mut mgr = TabManager::new();

    handle_tab_event(&mut mgr, &hist, 0, "https://example.com", "Example", false);
    handle_tab_event(
        &mut mgr,
        &hist,
        1,
        "https://secret.example/",
        "Secret",
        true,
    );

    let entries = hist.list();
    assert_eq!(entries.len(), 1, "private visit must not be recorded");
    assert_eq!(entries[0].url, "https://example.com");

    let session = tabs_to_session(&mgr.all_tabs(), 0).expect("one restorable tab");
    assert_eq!(session.tabs.len(), 1);
    assert_eq!(session.tabs[0].url, "https://example.com");
}

#[test]
fn private_tab_created_via_add_with_mode_excluded() {
    let mut mgr = TabManager::new();
    mgr.add_with_mode("https://secret.example/", true);
    mgr.add_with_mode("https://example.com", false);

    let session = tabs_to_session(&mgr.all_tabs(), 1).expect("one restorable tab");
    assert_eq!(session.tabs.len(), 1);
    assert_eq!(session.tabs[0].url, "https://example.com");
}

#[test]
fn sync_tab_with_private_flag_marks_tab_private() {
    let mut mgr = TabManager::new();
    assert!(mgr.sync_tab(0, "https://secret.example/", "Secret", true));
    assert!(mgr.get(0).unwrap().is_private);
    assert!(!mgr.sync_tab(1, "https://example.com", "Example", false));
    assert!(!mgr.get(1).unwrap().is_private);
}

#[test]
fn sync_tab_corrects_padding_privacy() {
    let mut mgr = TabManager::new();
    // A callback for index 1 pads index 0 as non-private (unknown yet).
    mgr.sync_tab(1, "https://secret.example/", "Secret", true);
    assert!(!mgr.get(0).unwrap().is_private);
    assert!(mgr.get(1).unwrap().is_private);
    // The padded tab's own callback corrects it to private.
    mgr.sync_tab(0, "https://other-secret.example/", "Other", true);
    assert!(mgr.get(0).unwrap().is_private);
    // Nothing restorable remains.
    assert!(tabs_to_session(&mgr.all_tabs(), 0).is_none());
}

#[test]
fn sync_tab_overwrites_stale_privacy() {
    let mut mgr = TabManager::new();
    mgr.add("https://example.com");
    assert!(!mgr.get(0).unwrap().is_private);
    assert!(mgr.sync_tab(0, "https://example.com", "Example", true));
    assert!(mgr.get(0).unwrap().is_private);
    assert!(tabs_to_session(&mgr.all_tabs(), 0).is_none());
}

#[test]
fn normal_tabs_still_recorded_around_private_tab() {
    let dir = unique_temp_dir("privacy_mixed");
    let hist = HistoryManager::with_path(dir.join("history.json"));
    let mut mgr = TabManager::new();

    handle_tab_event(&mut mgr, &hist, 0, "https://a.example/", "A", false);
    handle_tab_event(&mut mgr, &hist, 1, "https://secret.example/", "S", true);
    handle_tab_event(&mut mgr, &hist, 2, "https://b.example/", "B", false);

    let entries = hist.list();
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].url, "https://a.example/");
    assert_eq!(entries[1].url, "https://b.example/");

    let session = tabs_to_session(&mgr.all_tabs(), 2).expect("two restorable tabs");
    assert_eq!(session.tabs.len(), 2);
    assert_eq!(session.tabs[0].url, "https://a.example/");
    assert_eq!(session.tabs[1].url, "https://b.example/");
}
