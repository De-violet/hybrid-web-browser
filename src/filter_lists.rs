//! Manajemen daftar filter adblock (EasyList, EasyPrivacy, uBlock, ABPindo).
//!
//! Desainnya sengaja ringan: tidak ada dependensi HTTP baru. Pengunduhan
//! memakai `curl` (cadangan: `wget`, lalu PowerShell di Windows) lewat
//! `std::process::Command`, hasilnya di-cache sebagai file teks di
//! `<config>/filter_lists/`, dan mesin adblock dibangun dari cache tersebut.
//! Jika unduhan gagal, cache lama tetap dipakai sehingga browser tidak
//! pernah kehilangan pemblokiran hanya karena jaringan sedang bermasalah.

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

/// Batas ukuran satu daftar filter (32 MiB). Daftar resmi jauh lebih kecil
/// (EasyList ~3 MiB); batas ini mencegah respons raksasa memakan RAM.
pub const MAX_LIST_BYTES: usize = 32 * 1024 * 1024;

/// Interval pembaruan otomatis bawaan, dalam jam.
pub const DEFAULT_UPDATE_INTERVAL_HOURS: u64 = 24;

/// Timeout unduhan per URL, dalam detik.
const DOWNLOAD_TIMEOUT_SECS: u64 = 60;

/// Satu daftar filter yang bisa diunduh.
#[derive(Debug, Clone, Copy)]
pub struct FilterListDef {
    pub id: &'static str,
    pub name: &'static str,
    /// URL dicoba berurutan sampai satu berhasil.
    pub urls: &'static [&'static str],
    pub enabled_by_default: bool,
}

/// Daftar bawaan. Urutan di sini adalah urutan pemuatan ke mesin adblock.
pub const FILTER_LISTS: &[FilterListDef] = &[
    FilterListDef {
        id: "easylist",
        name: "EasyList",
        urls: &[
            "https://easylist-downloads.adblockplus.org/easylist.txt",
            "https://easylist.to/easylist/easylist.txt",
        ],
        enabled_by_default: true,
    },
    FilterListDef {
        id: "easyprivacy",
        name: "EasyPrivacy",
        urls: &[
            "https://easylist-downloads.adblockplus.org/easyprivacy.txt",
            "https://easylist.to/easylist/easyprivacy.txt",
        ],
        enabled_by_default: true,
    },
    FilterListDef {
        id: "ublock-filters",
        name: "uBlock filters",
        urls: &["https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/filters.txt"],
        enabled_by_default: true,
    },
    FilterListDef {
        id: "ublock-unbreak",
        name: "uBlock unbreak",
        urls: &["https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/unbreak.txt"],
        enabled_by_default: true,
    },
    FilterListDef {
        id: "abpindo",
        name: "ABPindo (Indonesia)",
        urls: &["https://raw.githubusercontent.com/ABPindo/indonesianadblockrules/master/subscriptions/abpindo.txt"],
        enabled_by_default: true,
    },
    FilterListDef {
        id: "ublock-badware",
        name: "uBlock badware",
        urls: &["https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/badware.txt"],
        enabled_by_default: false,
    },
    FilterListDef {
        id: "ublock-privacy",
        name: "uBlock privacy",
        urls: &["https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/privacy.txt"],
        enabled_by_default: false,
    },
];

/// Sumber daftar filter yang sudah di-resolve (bawaan atau kustom pengguna).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ListSource {
    pub id: String,
    pub name: String,
    pub urls: Vec<String>,
}

impl From<&FilterListDef> for ListSource {
    fn from(def: &FilterListDef) -> Self {
        Self {
            id: def.id.to_string(),
            name: def.name.to_string(),
            urls: def.urls.iter().map(|u| u.to_string()).collect(),
        }
    }
}

/// ID daftar yang aktif secara bawaan.
pub fn default_enabled_list_ids() -> Vec<String> {
    FILTER_LISTS
        .iter()
        .filter(|d| d.enabled_by_default)
        .map(|d| d.id.to_string())
        .collect()
}

pub fn find_list(id: &str) -> Option<&'static FilterListDef> {
    FILTER_LISTS.iter().find(|d| d.id == id)
}

/// Resolve ID yang diaktifkan + URL kustom menjadi daftar sumber unduhan.
/// ID yang tidak dikenal diabaikan; URL kustom harus `https://`.
pub fn resolve_sources(enabled_ids: &[String], custom_urls: &[String]) -> Vec<ListSource> {
    let mut sources: Vec<ListSource> = FILTER_LISTS
        .iter()
        .filter(|d| enabled_ids.iter().any(|id| id == d.id))
        .map(ListSource::from)
        .collect();
    for (i, raw_url) in custom_urls.iter().enumerate() {
        let url = raw_url.trim();
        if url.starts_with("https://") && !url.contains(char::is_whitespace) {
            sources.push(ListSource {
                id: format!("custom-{}", i + 1),
                name: format!("Daftar kustom {}", i + 1),
                urls: vec![url.to_string()],
            });
        }
    }
    sources
}

/// Direktori cache daftar filter di dalam direktori konfigurasi.
pub fn lists_dir(config_dir: &Path) -> PathBuf {
    config_dir.join("filter_lists")
}

/// Nama file cache yang aman untuk sebuah ID (hanya huruf, angka, `-`, `_`).
pub fn sanitize_list_id(id: &str) -> String {
    let cleaned: String = id
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    if cleaned.is_empty() {
        "list".to_string()
    } else {
        cleaned
    }
}

pub fn list_file_path(config_dir: &Path, id: &str) -> PathBuf {
    lists_dir(config_dir).join(format!("{}.txt", sanitize_list_id(id)))
}

pub fn status_path(config_dir: &Path) -> PathBuf {
    lists_dir(config_dir).join("status.json")
}

pub fn now_unix() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn file_mtime_unix(path: &Path) -> u64 {
    fs::metadata(path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// Apakah file cache sudah kedaluwarsa. File yang belum ada selalu dianggap
/// kedaluwarsa (harus diunduh); `interval_hours == 0` berarti file yang
/// sudah ada tidak pernah kedaluwarsa (hanya pembaruan manual).
pub fn is_stale(path: &Path, interval_hours: u64) -> bool {
    if !path.exists() {
        return true;
    }
    if interval_hours == 0 {
        return false;
    }
    let age = now_unix().saturating_sub(file_mtime_unix(path));
    age > interval_hours.saturating_mul(3600)
}

/// Hitung rule efektif: baris non-kosong yang bukan komentar (`!...`),
/// header (`[Adblock Plus ...]`), atau komentar `#` (kecuali `##` kosmetik).
pub fn count_rules(content: &str) -> usize {
    content
        .lines()
        .filter(|line| {
            let t = line.trim();
            if t.is_empty() || t.starts_with('!') || t.starts_with('[') {
                return false;
            }
            if t.starts_with('#') && !t.starts_with("##") {
                return false;
            }
            true
        })
        .count()
}

/// Validasi isi daftar yang diunduh: tolak halaman error HTML, file kosong,
/// dan file dengan rule terlalu sedikit (kemungkinan unduhan rusak).
/// Mengembalikan jumlah rule jika valid.
pub fn validate_list_content(content: &str) -> Result<usize, String> {
    let head: String = content.trim_start().chars().take(200).collect();
    let head_lower = head.to_lowercase();
    if head_lower.starts_with("<!doctype")
        || head_lower.starts_with("<html")
        || head_lower.starts_with("<head")
    {
        return Err("unduhan tampak seperti halaman HTML, bukan daftar filter".to_string());
    }
    let rules = count_rules(content);
    if rules < 10 {
        return Err(format!(
            "hanya {rules} rule ditemukan, daftar tampak rusak/tidak lengkap"
        ));
    }
    Ok(rules)
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct ListStatus {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub rule_count: usize,
    #[serde(default)]
    pub bytes: u64,
    #[serde(default)]
    pub updated_at: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct FilterStatus {
    #[serde(default)]
    pub updated_at: u64,
    #[serde(default)]
    pub total_rules: usize,
    #[serde(default)]
    pub lists: Vec<ListStatus>,
}

pub fn read_status(config_dir: &Path) -> Option<FilterStatus> {
    let content = fs::read_to_string(status_path(config_dir)).ok()?;
    serde_json::from_str(&content).ok()
}

pub fn write_status(config_dir: &Path, status: &FilterStatus) -> Result<(), String> {
    let dir = lists_dir(config_dir);
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let json = serde_json::to_string_pretty(status).map_err(|e| e.to_string())?;
    write_file_atomic(&status_path(config_dir), &json)
}

fn write_file_atomic(path: &Path, content: &str) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let tmp_path = path.with_extension(format!("tmp.{}", std::process::id()));
    fs::write(&tmp_path, content).map_err(|e| e.to_string())?;
    if let Err(e) = fs::rename(&tmp_path, path) {
        let _ = fs::remove_file(&tmp_path);
        return Err(e.to_string());
    }
    Ok(())
}

/// Format an age in human-friendly language, e.g. "5 minutes ago".
pub fn format_age(updated_at: u64, now: u64) -> String {
    let age = now.saturating_sub(updated_at);
    if age < 60 {
        "just now".to_string()
    } else if age < 3600 {
        format!("{} minutes ago", age / 60)
    } else if age < 86400 {
        format!("{} hours ago", age / 3600)
    } else {
        format!("{} days ago", age / 86400)
    }
}

/// Status summary for the CLI and the Settings page.
pub fn status_summary(config_dir: &Path) -> String {
    match read_status(config_dir) {
        Some(status) if !status.lists.is_empty() => {
            let when = if status.updated_at > 0 {
                format_age(status.updated_at, now_unix())
            } else {
                "unknown".to_string()
            };
            let failed = status.lists.iter().filter(|l| l.error.is_some()).count();
            let mut out = format!(
                "{} lists active, {} rules, updated {}",
                status.lists.len(),
                status.total_rules,
                when
            );
            if failed > 0 {
                out.push_str(&format!(" ({failed} failed to update, using cached lists)"));
            }
            out
        }
        _ => "Never updated — click Update Filter Lists or run --update-filters".to_string(),
    }
}

/// Muat isi cache daftar yang sudah terunduh, sesuai urutan sumber.
/// File yang belum ada dilewati (mesin tetap berjalan dengan rule bawaan).
pub fn load_cached_lists(
    config_dir: &Path,
    enabled_ids: &[String],
    custom_urls: &[String],
) -> Vec<String> {
    resolve_sources(enabled_ids, custom_urls)
        .iter()
        .filter_map(|src| fs::read_to_string(list_file_path(config_dir, &src.id)).ok())
        .filter(|content| !content.trim().is_empty())
        .collect()
}

// ---------- Pengunduhan (tanpa dependensi HTTP) ----------

/// Hasil percobaan satu program pengunduh: `None` berarti program tidak
/// ditemukan sehingga program berikutnya boleh dicoba.
fn run_downloader(program: &str, args: &[String]) -> Option<Result<String, String>> {
    let output = match Command::new(program).args(args).output() {
        Ok(output) => output,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return None,
        Err(e) => return Some(Err(format!("{program}: {e}"))),
    };
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        let snippet: String = stderr.trim().chars().take(200).collect();
        return Some(Err(format!(
            "{program} gagal (status {}): {snippet}",
            output.status
        )));
    }
    if output.stdout.is_empty() {
        return Some(Err(format!("{program} mengembalikan respons kosong")));
    }
    if output.stdout.len() > MAX_LIST_BYTES {
        return Some(Err(format!(
            "{program} mengembalikan respons melebihi batas {MAX_LIST_BYTES} byte"
        )));
    }
    Some(Ok(String::from_utf8_lossy(&output.stdout).into_owned()))
}

fn try_curl(url: &str, timeout_secs: u64) -> Option<Result<String, String>> {
    run_downloader(
        "curl",
        &[
            "-fsSL".to_string(),
            "--max-time".to_string(),
            timeout_secs.to_string(),
            "--max-filesize".to_string(),
            MAX_LIST_BYTES.to_string(),
            "--retry".to_string(),
            "1".to_string(),
            url.to_string(),
        ],
    )
}

fn try_wget(url: &str, timeout_secs: u64) -> Option<Result<String, String>> {
    run_downloader(
        "wget",
        &[
            "-qO-".to_string(),
            format!("--timeout={timeout_secs}"),
            "--tries=2".to_string(),
            "--max-redirect=5".to_string(),
            url.to_string(),
        ],
    )
}

#[cfg(target_os = "windows")]
fn try_powershell(url: &str, timeout_secs: u64) -> Option<Result<String, String>> {
    // Tolak URL dengan karakter yang bisa merusak quoting PowerShell.
    if url.contains('\'') || url.contains('"') || url.contains(char::is_whitespace) {
        return Some(Err(
            "URL tidak aman untuk PowerShell (mengandung spasi/quote)".to_string(),
        ));
    }
    let script = format!(
        "Invoke-WebRequest -Uri '{url}' -UseBasicParsing -TimeoutSec {timeout_secs} | Select-Object -ExpandProperty Content"
    );
    run_downloader(
        "powershell",
        &[
            "-NoProfile".to_string(),
            "-NonInteractive".to_string(),
            "-Command".to_string(),
            script,
        ],
    )
}

/// Unduh teks dari URL memakai program eksternal yang tersedia.
/// Mengembalikan error gabungan jika semua program gagal/tidak ada.
pub fn download_text(url: &str, timeout_secs: u64) -> Result<String, String> {
    let mut errors: Vec<String> = Vec::new();
    for attempt in [try_curl(url, timeout_secs), try_wget(url, timeout_secs)] {
        match attempt {
            Some(Ok(text)) => return Ok(text),
            Some(Err(e)) => errors.push(e),
            None => {}
        }
    }
    #[cfg(target_os = "windows")]
    match try_powershell(url, timeout_secs) {
        Some(Ok(text)) => return Ok(text),
        Some(Err(e)) => errors.push(e),
        None => {}
    }
    if errors.is_empty() {
        Err(
            "tidak ada program pengunduh (curl/wget) yang ditemukan; instal curl untuk memperbarui filter"
                .to_string(),
        )
    } else {
        Err(errors.join("; "))
    }
}

/// Perbarui sumber daftar filter: unduh yang kedaluwarsa/hilang (atau semua
/// jika `force`), tulis atomik, dan pertahankan cache lama saat gagal.
/// Selalu menulis `status.json` terbaru dan mengembalikannya.
pub fn update_sources(
    config_dir: &Path,
    sources: &[ListSource],
    force: bool,
    interval_hours: u64,
) -> FilterStatus {
    let dir = lists_dir(config_dir);
    let _ = fs::create_dir_all(&dir);
    let now = now_unix();
    let mut statuses: Vec<ListStatus> = Vec::new();
    let mut total_rules = 0usize;

    for src in sources {
        let path = list_file_path(config_dir, &src.id);
        let needs_update = force || is_stale(&path, interval_hours);

        if !needs_update {
            match fs::read_to_string(&path) {
                Ok(content) => {
                    let rules = count_rules(&content);
                    total_rules += rules;
                    statuses.push(ListStatus {
                        id: src.id.clone(),
                        name: src.name.clone(),
                        rule_count: rules,
                        bytes: content.len() as u64,
                        updated_at: file_mtime_unix(&path),
                        error: None,
                    });
                }
                Err(e) => statuses.push(ListStatus {
                    id: src.id.clone(),
                    name: src.name.clone(),
                    error: Some(format!("cache tidak bisa dibaca: {e}")),
                    ..Default::default()
                }),
            }
            continue;
        }

        let mut downloaded: Option<(String, usize)> = None;
        let mut last_error: Option<String> = None;
        for url in &src.urls {
            match download_text(url, DOWNLOAD_TIMEOUT_SECS) {
                Ok(text) => match validate_list_content(&text) {
                    Ok(rules) => {
                        downloaded = Some((text, rules));
                        break;
                    }
                    Err(e) => last_error = Some(format!("{url}: {e}")),
                },
                Err(e) => last_error = Some(format!("{url}: {e}")),
            }
        }

        match downloaded {
            Some((text, rules)) => {
                let bytes = text.len() as u64;
                match write_file_atomic(&path, &text) {
                    Ok(()) => {
                        total_rules += rules;
                        statuses.push(ListStatus {
                            id: src.id.clone(),
                            name: src.name.clone(),
                            rule_count: rules,
                            bytes,
                            updated_at: now,
                            error: None,
                        });
                    }
                    Err(e) => {
                        // Gagal menulis: pertahankan cache lama jika ada.
                        push_cached_fallback(
                            &path,
                            src,
                            &mut statuses,
                            &mut total_rules,
                            format!("gagal menyimpan cache: {e}"),
                        );
                    }
                }
            }
            None => {
                let err =
                    last_error.unwrap_or_else(|| "tidak ada URL yang berhasil diunduh".to_string());
                push_cached_fallback(&path, src, &mut statuses, &mut total_rules, err);
            }
        }
    }

    let status = FilterStatus {
        updated_at: now,
        total_rules,
        lists: statuses,
    };
    let _ = write_status(config_dir, &status);
    status
}

fn push_cached_fallback(
    path: &Path,
    src: &ListSource,
    statuses: &mut Vec<ListStatus>,
    total_rules: &mut usize,
    error: String,
) {
    match fs::read_to_string(path) {
        Ok(old) => {
            let rules = count_rules(&old);
            *total_rules += rules;
            statuses.push(ListStatus {
                id: src.id.clone(),
                name: src.name.clone(),
                rule_count: rules,
                bytes: old.len() as u64,
                updated_at: file_mtime_unix(path),
                error: Some(error),
            });
        }
        Err(_) => statuses.push(ListStatus {
            id: src.id.clone(),
            name: src.name.clone(),
            error: Some(error),
            ..Default::default()
        }),
    }
}

/// Perbarui daftar bawaan yang diaktifkan + URL kustom pengguna.
pub fn update_lists(
    config_dir: &Path,
    enabled_ids: &[String],
    custom_urls: &[String],
    force: bool,
    interval_hours: u64,
) -> FilterStatus {
    let sources = resolve_sources(enabled_ids, custom_urls);
    update_sources(config_dir, &sources, force, interval_hours)
}
