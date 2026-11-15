#[derive(Debug, Clone)]
pub struct TabItem {
    pub id: usize,
    pub url: String,
    pub title: String,
}

pub struct TabManager {
    tabs: Vec<TabItem>,
    active_index: usize,
    counter: usize,
}

impl TabManager {
    pub fn new() -> Self {
        Self {
            tabs: Vec::new(),
            active_index: 0,
            counter: 0,
        }
    }

    pub fn add(&mut self, url: &str) -> usize {
        self.counter += 1;
        let tab = TabItem {
            id: self.counter,
            url: url.to_string(),
            title: "New Tab".to_string(),
        };
        self.tabs.push(tab);
        self.active_index = self.tabs.len() - 1;
        self.active_index
    }

    pub fn remove(&mut self, index: usize) {
        if index < self.tabs.len() {
            self.tabs.remove(index);
            if self.tabs.is_empty() {
                self.active_index = 0;
            } else if self.active_index >= self.tabs.len() {
                self.active_index = self.tabs.len() - 1;
            }
        }
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

    pub fn active_tab(&self) -> Option<&TabItem> {
        self.tabs.get(self.active_index)
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
