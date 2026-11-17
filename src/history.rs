use crate::config::resolve_config_dir;
use serde::{Deserialize, Serialize};
use std::fs::{create_dir_all, File, OpenOptions};
use std::io::{BufReader, BufWriter};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{sync_channel, SyncSender};
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct HistoryEntry {
    pub url: String,
    pub title: String,
    pub timestamp: u64,
}

pub struct HistoryManager {
    entries: Mutex<Vec<HistoryEntry>>,
    tx: SyncSender<Vec<HistoryEntry>>,
    file_path: PathBuf,
}

impl HistoryManager {
    pub fn new() -> Self {
        let dir = resolve_config_dir();
        let _ = create_dir_all(&dir);
        Self::with_path(dir.join("history.json"))
    }

    pub fn with_path<P: AsRef<Path>>(path: P) -> Self {
        let file_path = path.as_ref().to_path_buf();
        if let Some(parent) = file_path.parent() {
            let _ = create_dir_all(parent);
        }

        let mut entries = Vec::new();
        if file_path.exists() {
            if let Ok(file) = File::open(&file_path) {
                let reader = BufReader::new(file);
                if let Ok(loaded) = serde_json::from_reader(reader) {
                    entries = loaded;
                }
            }
        }

        let (tx, rx) = sync_channel::<Vec<HistoryEntry>>(16);
        let save_path = file_path.clone();
        std::thread::Builder::new()
            .name("history-writer".into())
            .spawn(move || {
                while let Ok(data) = rx.recv() {
                    if let Ok(file) = OpenOptions::new()
                        .create(true)
                        .write(true)
                        .truncate(true)
                        .open(&save_path)
                    {
                        let writer = BufWriter::new(file);
                        let _ = serde_json::to_writer_pretty(writer, &data);
                    }
                }
            })
            .ok();

        Self {
            entries: Mutex::new(entries),
            tx,
            file_path,
        }
    }

    pub fn record(&self, url: &str, title: &str) {
        let trimmed = url.trim();
        if trimmed.is_empty()
            || trimmed.starts_with("about:")
            || trimmed.starts_with("falkon:")
            || trimmed.starts_with("falkon-set:")
            || trimmed.starts_with("data:")
        {
            return;
        }

        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);

        let clean_title = if title.trim().is_empty() {
            trimmed.to_string()
        } else {
            title.trim().to_string()
        };

        if let Ok(mut entries) = self.entries.lock() {
            if let Some(last) = entries.last_mut() {
                if last.url == trimmed {
                    if !title.trim().is_empty() && last.title != clean_title {
                        last.title = clean_title;
                        last.timestamp = now;
                        let _ = self.tx.try_send(entries.clone());
                    }
                    return;
                }
            }

            entries.push(HistoryEntry {
                url: trimmed.to_string(),
                title: clean_title,
                timestamp: now,
            });

            if entries.len() > 1000 {
                let overflow = entries.len() - 1000;
                entries.drain(0..overflow);
            }

            let _ = self.tx.try_send(entries.clone());
        }
    }

    pub fn clear(&self) {
        if let Ok(mut entries) = self.entries.lock() {
            entries.clear();
            let _ = self.tx.try_send(Vec::new());
        }
        if let Ok(file) = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&self.file_path)
        {
            let _ = std::io::Write::write_all(&mut BufWriter::new(file), b"[]");
        }
    }

    pub fn list(&self) -> Vec<HistoryEntry> {
        self.entries.lock().map(|e| e.clone()).unwrap_or_default()
    }
}

impl Default for HistoryManager {
    fn default() -> Self {
        Self::new()
    }
}
