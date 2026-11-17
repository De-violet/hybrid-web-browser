//! Pemulihan sesi: menyimpan tab non-privat yang terbuka ke `session.json`
//! dan memuatnya kembali saat browser dibuka.
//!
//! Privasi diutamakan: tab privat tidak pernah ditulis ke disk, dan hanya
//! URL `http(s)` yang dipulihkan (halaman internal, `about:blank`, dan
//! `data:` dilewati). Jika tidak ada tab yang bisa dipulihkan, file sesi
//! lama dibiarkan apa adanya agar sesi privat tidak menghapus sesi normal.

use crate::tabs::TabItem;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// Batas tab yang disimpan agar file sesi tetap kecil dan startup tetap cepat.
pub const MAX_SESSION_TABS: usize = 50;

const SESSION_VERSION: u32 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SessionTab {
    pub url: String,
    pub title: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SessionData {
    pub version: u32,
    pub tabs: Vec<SessionTab>,
    pub active_index: usize,
    pub saved_at: u64,
}

pub fn session_path(config_dir: &Path) -> PathBuf {
    config_dir.join("session.json")
}

fn now_unix() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// Hanya URL web biasa yang boleh dipulihkan.
pub fn is_restorable_url(url: &str) -> bool {
    let trimmed = url.trim();
    trimmed.starts_with("http://") || trimmed.starts_with("https://")
}

/// Ubah snapshot tab menjadi data sesi, atau `None` jika tidak ada tab yang
/// bisa dipulihkan. Indeks tab aktif dipetakan ke posisi setelah filtering.
pub fn tabs_to_session(tabs: &[TabItem], active_index: usize) -> Option<SessionData> {
    let mut kept: Vec<(usize, SessionTab)> = Vec::new();
    for (i, tab) in tabs.iter().enumerate() {
        if tab.is_private || !is_restorable_url(&tab.url) {
            continue;
        }
        kept.push((
            i,
            SessionTab {
                url: tab.url.trim().to_string(),
                title: tab.title.clone(),
            },
        ));
        if kept.len() >= MAX_SESSION_TABS {
            break;
        }
    }
    if kept.is_empty() {
        return None;
    }
    let mapped_active = kept
        .iter()
        .position(|(orig, _)| *orig == active_index)
        .unwrap_or_else(|| active_index.min(kept.len() - 1));
    Some(SessionData {
        version: SESSION_VERSION,
        tabs: kept.into_iter().map(|(_, t)| t).collect(),
        active_index: mapped_active,
        saved_at: now_unix(),
    })
}

/// Simpan sesi secara atomik. Mengembalikan `Ok(true)` jika tersimpan,
/// `Ok(false)` jika tidak ada tab yang bisa dipulihkan (file dibiarkan).
pub fn save_session(
    config_dir: &Path,
    tabs: &[TabItem],
    active_index: usize,
) -> Result<bool, String> {
    let Some(data) = tabs_to_session(tabs, active_index) else {
        return Ok(false);
    };
    let path = session_path(config_dir);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let json = serde_json::to_string_pretty(&data).map_err(|e| e.to_string())?;
    let tmp_path = path.with_extension(format!("tmp.{}", std::process::id()));
    fs::write(&tmp_path, &json).map_err(|e| e.to_string())?;
    if let Err(e) = fs::rename(&tmp_path, &path) {
        let _ = fs::remove_file(&tmp_path);
        return Err(e.to_string());
    }
    Ok(true)
}

/// Muat sesi yang tersimpan. File rusak/kosong/tanpa tab valid -> `None`.
pub fn load_session(config_dir: &Path) -> Option<SessionData> {
    let content = fs::read_to_string(session_path(config_dir)).ok()?;
    let mut data: SessionData = serde_json::from_str(&content).ok()?;
    data.tabs.retain(|t| is_restorable_url(&t.url));
    if data.tabs.is_empty() {
        return None;
    }
    data.tabs.truncate(MAX_SESSION_TABS);
    if data.active_index >= data.tabs.len() {
        data.active_index = data.tabs.len() - 1;
    }
    Some(data)
}

/// Hapus file sesi yang tersimpan (mis. saat pengguna menonaktifkan restore).
pub fn clear_session(config_dir: &Path) {
    let _ = fs::remove_file(session_path(config_dir));
}
