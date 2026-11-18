use serde::{Deserialize, Serialize};
use std::fs::{create_dir_all, read_to_string, File};
use std::io::Write;
use std::path::{Path, PathBuf};

fn default_homepage() -> String {
    "attis:home".to_string()
}
fn default_search_engine() -> String {
    "https://www.google.com/search?q=%s".to_string()
}
fn default_theme() -> String {
    "violet".to_string()
}
fn default_download_dir() -> String {
    if let Ok(xdg_dl) = std::env::var("XDG_DOWNLOAD_DIR") {
        if !xdg_dl.trim().is_empty() {
            return xdg_dl;
        }
    }
    let home = std::env::var("HOME")
        .or_else(|_| std::env::var("USERPROFILE"))
        .unwrap_or_else(|_| ".".to_string());
    PathBuf::from(home)
        .join("Downloads")
        .to_string_lossy()
        .to_string()
}
fn default_user_agent() -> String {
    "".to_string()
}
fn default_tab_position() -> String {
    "top".to_string()
}
fn default_true() -> bool {
    true
}
fn default_zoom() -> f32 {
    1.0
}
fn default_window_width() -> i32 {
    1280
}
fn default_window_height() -> i32 {
    800
}
fn default_bg_color() -> String {
    "#16102a".to_string()
}
fn default_fg_color() -> String {
    "#ddd6fe".to_string()
}
fn default_accent_color() -> String {
    "#8b5cf6".to_string()
}
fn default_surface_color() -> String {
    "#1e1738".to_string()
}
fn default_border_color() -> String {
    "#3f2d6b".to_string()
}
fn default_bg_opacity() -> f32 {
    0.88
}
fn default_ui_density() -> String {
    "comfortable".to_string()
}
fn default_custom_rules() -> Vec<String> {
    vec!["||analytics.*^".to_string(), "||telemetry.*^".to_string()]
}
fn default_filter_lists() -> Vec<String> {
    crate::filter_lists::default_enabled_list_ids()
}
fn default_filter_interval() -> u64 {
    crate::filter_lists::DEFAULT_UPDATE_INTERVAL_HOURS
}
fn default_suspend_after() -> u64 {
    120
}
fn default_suspend_many_after() -> u64 {
    45
}
fn default_suspend_threshold() -> usize {
    3
}
fn default_cache_mb() -> u32 {
    20
}

pub fn resolve_config_dir() -> PathBuf {
    if let Ok(custom_dir) = std::env::var("ATTIS_CONFIG_DIR") {
        if !custom_dir.trim().is_empty() {
            return PathBuf::from(custom_dir);
        }
    }
    if let Ok(xdg_cfg) = std::env::var("XDG_CONFIG_HOME") {
        if !xdg_cfg.trim().is_empty() {
            return PathBuf::from(xdg_cfg).join("attis");
        }
    }
    let home = std::env::var("HOME")
        .or_else(|_| std::env::var("USERPROFILE"))
        .unwrap_or_else(|_| ".".to_string());
    PathBuf::from(home).join(".config").join("attis")
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BrowserConfig {
    #[serde(default = "default_homepage")]
    pub homepage: String,

    #[serde(default = "default_search_engine")]
    pub search_engine: String,

    #[serde(default = "default_theme")]
    pub theme: String, // "violet" (default), "dark", "catppuccin", "light", "minimal", "nord", "gruvbox", "custom"

    #[serde(default = "default_bg_color")]
    pub bg_color: String,

    #[serde(default = "default_fg_color")]
    pub fg_color: String,

    #[serde(default = "default_accent_color")]
    pub accent_color: String,

    #[serde(default = "default_surface_color")]
    pub surface_color: String,

    #[serde(default = "default_border_color")]
    pub border_color: String,

    #[serde(default = "default_bg_opacity")]
    pub bg_opacity: f32,

    #[serde(default = "default_ui_density")]
    pub ui_density: String,

    #[serde(default = "default_true")]
    pub show_home_search: bool,

    #[serde(default = "default_true")]
    pub show_home_tiles: bool,

    #[serde(default = "default_true")]
    pub show_home_clock: bool,

    #[serde(default = "default_tab_position")]
    pub tab_position: String, // "top", "bottom", "hidden"

    #[serde(default = "default_true")]
    pub show_nav_buttons: bool,

    #[serde(default = "default_true")]
    pub show_adblock_badge: bool,

    #[serde(default = "default_true")]
    pub show_new_tab_button: bool,

    #[serde(default = "default_zoom")]
    pub default_zoom: f32,

    #[serde(default = "default_download_dir")]
    pub download_dir: String,

    #[serde(default = "default_true")]
    pub enable_adblock: bool,

    #[serde(default = "default_true")]
    pub enable_cosmetic_filter: bool,

    #[serde(default = "default_user_agent")]
    pub user_agent: String,

    #[serde(default = "default_window_width")]
    pub window_width: i32,

    #[serde(default = "default_window_height")]
    pub window_height: i32,

    #[serde(default = "default_custom_rules")]
    pub custom_rules: Vec<String>,

    // --- Daftar filter adblock ---
    /// ID daftar filter yang aktif (lihat `filter_lists::FILTER_LISTS`).
    #[serde(default = "default_filter_lists")]
    pub filter_lists: Vec<String>,

    /// Perbarui daftar filter yang kedaluwarsa di latar belakang saat startup.
    #[serde(default = "default_true")]
    pub filter_auto_update: bool,

    /// Umur cache (jam) sebelum daftar dianggap kedaluwarsa. 0 = manual saja.
    #[serde(default = "default_filter_interval")]
    pub filter_update_interval_hours: u64,

    /// URL daftar filter tambahan milik pengguna (harus `https://`).
    #[serde(default)]
    pub custom_filter_urls: Vec<String>,

    /// Domain yang dikecualikan dari pemblokiran (mis. `contoh.com`).
    #[serde(default)]
    pub adblock_allowlist: Vec<String>,

    // --- Sesi & hemat RAM ---
    /// Pulihkan tab non-privat dari sesi sebelumnya saat startup.
    #[serde(default = "default_true")]
    pub restore_session: bool,

    /// Aktifkan Smart Memory suspension untuk tab latar yang tidak aktif.
    #[serde(default = "default_true")]
    pub suspend_enabled: bool,

    /// Tab latar disuspensi setelah tidak aktif selama ini (detik).
    #[serde(default = "default_suspend_after")]
    pub suspend_after_seconds: u64,

    /// Ambang lebih cepat saat jumlah tab melebihi `suspend_many_tabs_threshold`.
    #[serde(default = "default_suspend_many_after")]
    pub suspend_many_tabs_after_seconds: u64,

    /// Jumlah tab di atas ambang ini memakai `suspend_many_tabs_after_seconds`.
    #[serde(default = "default_suspend_threshold")]
    pub suspend_many_tabs_threshold: usize,

    /// Batas HTTP cache memori WebEngine (MiB).
    #[serde(default = "default_cache_mb")]
    pub http_cache_size_mb: u32,

    /// Batas proses renderer Chromium (0 = otomatis, memakai 4).
    /// Nilai eksplisit dipakai apa adanya, di-clamp ke 64.
    #[serde(default)]
    pub renderer_process_limit: u32,
}

impl BrowserConfig {
    /// Batas proses renderer yang efektif: 0 (otomatis) dipetakan ke 4,
    /// nilai lain di-clamp ke rentang 1-64.
    pub fn renderer_process_limit_clamped(&self) -> u32 {
        match self.renderer_process_limit {
            0 => 4,
            n => n.min(64),
        }
    }

    /// Direktori unduhan yang diteruskan ke C++ lewat FFI. String kosong
    /// berarti "pakai folder Downloads sistem" (C++ memakai
    /// `QStandardPaths::DownloadLocation` bila menerima string kosong),
    /// sehingga folder Downloads yang dipindah pengguna tetap dihormati.
    /// Nilai default maupun string kosong keduanya dipetakan ke "";
    /// nilai kustom dipertahankan apa adanya.
    pub fn download_dir_for_ffi(&self) -> &str {
        if self.download_dir.is_empty() || self.download_dir == default_download_dir() {
            ""
        } else {
            &self.download_dir
        }
    }
}

impl Default for BrowserConfig {
    fn default() -> Self {
        Self {
            homepage: default_homepage(),
            search_engine: default_search_engine(),
            theme: default_theme(),
            bg_color: default_bg_color(),
            fg_color: default_fg_color(),
            accent_color: default_accent_color(),
            surface_color: default_surface_color(),
            border_color: default_border_color(),
            bg_opacity: default_bg_opacity(),
            ui_density: default_ui_density(),
            show_home_search: true,
            show_home_tiles: true,
            show_home_clock: true,
            tab_position: default_tab_position(),
            show_nav_buttons: true,
            show_adblock_badge: true,
            show_new_tab_button: true,
            default_zoom: 1.0,
            download_dir: default_download_dir(),
            enable_adblock: true,
            enable_cosmetic_filter: true,
            user_agent: default_user_agent(),
            window_width: default_window_width(),
            window_height: default_window_height(),
            custom_rules: default_custom_rules(),
            filter_lists: default_filter_lists(),
            filter_auto_update: true,
            filter_update_interval_hours: default_filter_interval(),
            custom_filter_urls: Vec::new(),
            adblock_allowlist: Vec::new(),
            restore_session: true,
            suspend_enabled: true,
            suspend_after_seconds: default_suspend_after(),
            suspend_many_tabs_after_seconds: default_suspend_many_after(),
            suspend_many_tabs_threshold: default_suspend_threshold(),
            http_cache_size_mb: default_cache_mb(),
            renderer_process_limit: 0,
        }
    }
}

pub struct ConfigManager {
    pub dir: PathBuf,
    pub config: BrowserConfig,
    pub user_chrome_qss: Option<String>,
    pub user_style_css: Option<String>,
    pub user_script_js: Option<String>,
}

impl ConfigManager {
    pub fn load() -> Self {
        Self::load_from_dir(resolve_config_dir())
    }

    pub fn load_from_dir<P: AsRef<Path>>(dir_path: P) -> Self {
        let dir = dir_path.as_ref().to_path_buf();
        let _ = create_dir_all(&dir);

        let config_file = dir.join("config.json");
        let config = if config_file.exists() {
            match read_to_string(&config_file) {
                Ok(content) => serde_json::from_str(&content).unwrap_or_default(),
                Err(_) => BrowserConfig::default(),
            }
        } else {
            let def = BrowserConfig::default();
            if let Ok(mut f) = File::create(&config_file) {
                let _ = f.write_all(
                    serde_json::to_string_pretty(&def)
                        .unwrap_or_default()
                        .as_bytes(),
                );
            }
            def
        };

        let qss_file = dir.join("user_chrome.qss");
        let user_chrome_qss = if qss_file.exists() {
            read_to_string(qss_file).ok()
        } else {
            None
        };

        let user_style_css = read_to_string(dir.join("user_style.css")).ok();
        let user_script_js = read_to_string(dir.join("user_script.js")).ok();

        Self {
            dir,
            config,
            user_chrome_qss,
            user_style_css,
            user_script_js,
        }
    }

    pub fn save(&self) -> Result<(), String> {
        let _ = create_dir_all(&self.dir);
        let config_file = self.dir.join("config.json");
        let json = serde_json::to_string_pretty(&self.config).map_err(|e| e.to_string())?;
        let mut f = File::create(&config_file).map_err(|e| e.to_string())?;
        f.write_all(json.as_bytes()).map_err(|e| e.to_string())
    }
}
