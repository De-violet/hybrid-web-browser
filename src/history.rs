use serde::{Deserialize, Serialize};
use std::fs::{create_dir_all, File, OpenOptions};
use std::io::{BufReader, BufWriter};
use std::path::PathBuf;
use std::sync::mpsc::{sync_channel, SyncSender};
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HistoryEntry {
    pub url: String,
    pub title: String,
    pub timestamp: u64,
}

pub struct HistoryManager {
    entries: Mutex<Vec<HistoryEntry>>,
    tx: SyncSender<Vec<HistoryEntry>>,
}

impl HistoryManager {
    pub fn new() -> Self {
        let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
        let mut dir = PathBuf::from(home);
        dir.push(".config");
        dir.push("falkon-rust");
        let _ = create_dir_all(&dir);

        let mut file_path = dir;
        file_path.push("history.json");

        let mut entries = Vec::new();
        if file_path.exists() {
            if let Ok(file) = File::open(&file_path) {
                let reader = BufReader::new(file);
                if let Ok(loaded) = serde_json::from_reader(reader) {
                    entries = loaded;
                }
            }
        }

        let (tx, rx) = sync_channel::<Vec<HistoryEntry>>(4);
        let save_path = file_path;
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
        }
    }

    pub fn record(&self, url: &str, title: &str) {
        if url.is_empty() || url.starts_with("about:") {
            return;
        }

        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);

        let entry = HistoryEntry {
            url: url.to_string(),
            title: title.to_string(),
            timestamp: now,
        };

        if let Ok(mut entries) = self.entries.lock() {
            // Avoid duplicate consecutive entries
            if let Some(last) = entries.last() {
                if last.url == url {
                    return;
                }
            }
            entries.push(entry);

            // Keep max 1000 items
            if entries.len() > 1000 {
                entries.remove(0);
            }

            let _ = self.tx.try_send(entries.clone());
        }
    }
}

impl Default for HistoryManager {
    fn default() -> Self {
        Self::new()
    }
}
