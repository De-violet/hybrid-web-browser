use attis::filter_lists::{
    count_rules, default_enabled_list_ids, find_list, format_age, is_stale, list_file_path,
    load_cached_lists, read_status, resolve_sources, update_sources, validate_list_content,
    FilterStatus, ListSource, FILTER_LISTS,
};

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

fn sample_list(title: &str, domain: &str) -> String {
    let mut out = format!("[Adblock Plus 2.0]\n! Title: {title}\n! Komentar\n");
    for i in 0..12 {
        out.push_str(&format!("||{domain}{i}.example^\n"));
    }
    out.push_str("##.sample-ad-slot\n");
    out
}

#[test]
fn test_count_rules_skips_comments_and_headers() {
    let content = "[Adblock Plus 2.0]\n! komentar\n\n||ads.example^\n##.ad-slot\n# bukan-rule\n/example.js$script\n";
    // ||..., ##..., dan /... dihitung; header, komentar, baris kosong, dan
    // baris '#' tunggal tidak dihitung.
    assert_eq!(count_rules(content), 3);
}

#[test]
fn test_validate_list_content() {
    assert!(validate_list_content(&sample_list("Test", "ads")).is_ok());
    assert!(validate_list_content("<!DOCTYPE html><html></html>").is_err());
    assert!(validate_list_content("<html><body>error</body></html>").is_err());
    assert!(validate_list_content("").is_err());
    assert!(validate_list_content("||only-one.example^\n").is_err());
}

#[test]
fn test_builtin_lists_are_unique_and_https() {
    let mut ids = std::collections::HashSet::new();
    for def in FILTER_LISTS {
        assert!(ids.insert(def.id), "ID duplikat: {}", def.id);
        assert!(!def.urls.is_empty());
        for url in def.urls {
            assert!(url.starts_with("https://"), "URL bukan HTTPS: {url}");
        }
    }
    assert!(find_list("easylist").is_some());
    assert!(find_list("abpindo").is_some());
    assert!(find_list("tidak-ada").is_none());

    let defaults = default_enabled_list_ids();
    assert!(defaults.contains(&"easylist".to_string()));
    assert!(defaults.contains(&"easyprivacy".to_string()));
    assert!(defaults.contains(&"abpindo".to_string()));
    // Daftar opt-in tidak aktif bawaan agar mesin tetap ringan.
    assert!(!defaults.contains(&"ublock-badware".to_string()));
}

#[test]
fn test_resolve_sources_filters_unknown_and_insecure() {
    let sources = resolve_sources(
        &["easylist".to_string(), "tidak-ada".to_string()],
        &[
            "https://example.com/custom.txt".to_string(),
            "http://example.com/tidak-aman.txt".to_string(),
            "https://exa mple.com/spasi.txt".to_string(),
        ],
    );
    assert_eq!(sources.len(), 2);
    assert_eq!(sources[0].id, "easylist");
    assert_eq!(sources[1].id, "custom-1");
}

#[test]
fn test_format_age_and_staleness() {
    assert_eq!(format_age(1000, 1030), "baru saja");
    assert_eq!(format_age(1000, 1000 + 5 * 60), "5 menit lalu");
    assert_eq!(format_age(1000, 1000 + 3 * 3600), "3 jam lalu");
    assert_eq!(format_age(1000, 1000 + 2 * 86400), "2 hari lalu");

    let dir = unique_temp_dir("stale");
    let missing = dir.join("tidak-ada.txt");
    assert!(is_stale(&missing, 24));
    assert!(is_stale(&missing, 0));
    let fresh = dir.join("fresh.txt");
    std::fs::write(&fresh, "x").unwrap();
    assert!(!is_stale(&fresh, 24));
    assert!(!is_stale(&fresh, 0));
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_update_sources_with_local_files_and_cache_fallback() {
    let dir = unique_temp_dir("update");
    let src_dir = dir.join("sources");
    std::fs::create_dir_all(&src_dir).unwrap();
    let list_a = src_dir.join("a.txt");
    let list_b = src_dir.join("b.txt");
    std::fs::write(&list_a, sample_list("A", "a-ads")).unwrap();
    std::fs::write(&list_b, sample_list("B", "b-ads")).unwrap();

    let sources = vec![
        ListSource {
            id: "test-a".to_string(),
            name: "Test A".to_string(),
            urls: vec![format!("file://{}", list_a.display())],
        },
        ListSource {
            id: "test-b".to_string(),
            name: "Test B".to_string(),
            urls: vec![format!("file://{}", list_b.display())],
        },
    ];

    let status = update_sources(&dir, &sources, true, 24);
    assert_eq!(status.lists.len(), 2);
    assert_eq!(status.total_rules, 26);
    assert!(status.lists.iter().all(|l| l.error.is_none()));
    assert!(list_file_path(&dir, "test-a").exists());
    assert!(list_file_path(&dir, "test-b").exists());

    // Status tersimpan dan bisa dibaca kembali.
    let saved: FilterStatus = read_status(&dir).expect("status.json harus ada");
    assert_eq!(saved.total_rules, 26);

    // Cache bisa dimuat kembali.
    let cached = load_cached_lists(&dir, &[], &[]);
    assert!(cached.is_empty(), "tanpa ID aktif tidak ada yang dimuat");

    // Unduhan kedua tanpa force memakai cache walau sumber sudah dihapus.
    std::fs::remove_file(&list_a).unwrap();
    std::fs::remove_file(&list_b).unwrap();
    let status2 = update_sources(&dir, &sources, false, 24);
    assert_eq!(status2.total_rules, 26);
    assert!(status2.lists.iter().all(|l| l.error.is_none()));

    // Force update dengan sumber hilang: cache lama dipertahankan + error dicatat.
    let status3 = update_sources(&dir, &sources, true, 24);
    assert_eq!(status3.total_rules, 26);
    assert!(status3.lists.iter().all(|l| l.error.is_some()));

    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_update_lists_without_cache_records_error() {
    let dir = unique_temp_dir("nocache");
    let sources = vec![ListSource {
        id: "hilang".to_string(),
        name: "Hilang".to_string(),
        urls: vec!["file:///definitely/not/here/list.txt".to_string()],
    }];
    let status = update_sources(&dir, &sources, true, 24);
    assert_eq!(status.total_rules, 0);
    assert_eq!(status.lists.len(), 1);
    assert!(status.lists[0].error.is_some());
    assert!(!list_file_path(&dir, "hilang").exists());
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn test_update_lists_resolves_builtin_ids_without_network() {
    // update_lists() sendiri tidak dipanggil di sini agar test tidak
    // menyentuh jaringan; yang diuji adalah resolve sumbernya.
    let sources = resolve_sources(&["easylist".to_string()], &[]);
    assert_eq!(sources.len(), 1);
    assert_eq!(sources[0].id, "easylist");
    assert!(sources[0].urls[0].contains("easylist"));
}
