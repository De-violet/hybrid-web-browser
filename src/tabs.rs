#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TabItem {
    pub id: usize,
    pub url: String,
    pub title: String,
    pub is_private: bool,
}

pub struct TabManager {
    tabs: Vec<TabItem>,
    closed_tabs: Vec<TabItem>,
    active_index: usize,
    counter: usize,
}

impl TabManager {
    pub fn new() -> Self {
        Self {
            tabs: Vec::new(),
            closed_tabs: Vec::new(),
            active_index: 0,
            counter: 0,
        }
    }

    pub fn add(&mut self, url: &str) -> usize {
        self.add_with_mode(url, false)
    }

    pub fn add_with_mode(&mut self, url: &str, is_private: bool) -> usize {
        self.counter += 1;
        let tab = TabItem {
            id: self.counter,
            url: url.to_string(),
            title: if is_private {
                "Private Tab".to_string()
            } else {
                "New Tab".to_string()
            },
            is_private,
        };
        self.tabs.push(tab);
        self.active_index = self.tabs.len() - 1;
        self.active_index
    }

    pub fn remove(&mut self, index: usize) -> Option<TabItem> {
        if index < self.tabs.len() {
            let removed = self.tabs.remove(index);
            if !removed.is_private
                && !removed.url.is_empty()
                && removed.url != "attis:home"
                && removed.url != "about:blank"
            {
                self.closed_tabs.push(removed.clone());
                if self.closed_tabs.len() > 25 {
                    self.closed_tabs.remove(0);
                }
            }
            if self.tabs.is_empty() {
                self.active_index = 0;
            } else if self.active_index >= self.tabs.len() {
                self.active_index = self.tabs.len() - 1;
            }
            Some(removed)
        } else {
            None
        }
    }

    pub fn move_tab(&mut self, from: usize, to: usize) {
        if from < self.tabs.len() && to < self.tabs.len() && from != to {
            let item = self.tabs.remove(from);
            self.tabs.insert(to, item);
            if self.active_index == from {
                self.active_index = to;
            }
        }
    }

    pub fn pop_recently_closed(&mut self) -> Option<TabItem> {
        self.closed_tabs.pop()
    }

    pub fn update(&mut self, index: usize, url: &str, title: &str) {
        if let Some(tab) = self.tabs.get_mut(index) {
            if !url.is_empty() {
                tab.url = url.to_string();
            }
            if !title.is_empty() {
                tab.title = title.to_string();
            }
        }
    }

    /// Sinkronisasi state tab dari callback C++.
    ///
    /// Slot yang belum dikenal diisi sebagai tab non-privat (privasinya
    /// belum diketahui; callback tab tersebut nanti mengoreksinya), lalu
    /// URL/judul DAN status privat tab target diperbarui dari nilai yang
    /// dikirim C++. Mengembalikan status privat tab target setelah
    /// sinkronisasi; pemanggil memakai nilai ini untuk memutuskan apakah
    /// navigasi boleh dicatat ke history dan sesi.
    pub fn sync_tab(&mut self, index: usize, url: &str, title: &str, is_private: bool) -> bool {
        while self.tabs.len() <= index {
            self.add("");
        }
        if let Some(tab) = self.tabs.get_mut(index) {
            if !url.is_empty() {
                tab.url = url.to_string();
            }
            if !title.is_empty() {
                tab.title = title.to_string();
            }
            tab.is_private = is_private;
        }
        self.tabs
            .get(index)
            .map(|tab| tab.is_private)
            .unwrap_or(is_private)
    }

    pub fn set_active(&mut self, index: usize) {
        if index < self.tabs.len() {
            self.active_index = index;
        }
    }

    pub fn active_tab(&self) -> Option<&TabItem> {
        self.tabs.get(self.active_index)
    }

    pub fn get(&self, index: usize) -> Option<&TabItem> {
        self.tabs.get(index)
    }

    /// Snapshot semua tab (untuk penyimpanan sesi).
    pub fn all_tabs(&self) -> Vec<TabItem> {
        self.tabs.clone()
    }

    /// Indeks tab aktif saat ini.
    pub fn active_index(&self) -> usize {
        self.active_index
    }

    pub fn count(&self) -> usize {
        self.tabs.len()
    }
}

impl Default for TabManager {
    fn default() -> Self {
        Self::new()
    }
}
