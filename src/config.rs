use serde::{Deserialize, Serialize};
use std::fs::{create_dir_all, read_to_string, File};
use std::io::Write;
use std::path::PathBuf;

fn default_homepage() -> String {
    "falkon:home".to_string()
}
fn default_search_engine() -> String {
    "https://www.google.com/search?q=%s".to_string()
}
fn default_theme() -> String {
    "dark".to_string()
}
fn default_download_dir() -> String {
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
    format!("{}/Downloads", home)
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
    "#181825".to_string()
}
fn default_fg_color() -> String {
    "#cdd6f4".to_string()
}
fn default_accent_color() -> String {
    "#89b4fa".to_string()
}
fn default_surface_color() -> String {
    "#1e1e2e".to_string()
}
fn default_border_color() -> String {
    "#313244".to_string()
}
fn default_bg_opacity() -> f32 {
    0.85
}
fn default_ui_density() -> String {
    "comfortable".to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BrowserConfig {
    #[serde(default = "default_homepage")]
    pub homepage: String,

    #[serde(default = "default_search_engine")]
    pub search_engine: String,

    #[serde(default = "default_theme")]
    pub theme: String, // "dark", "light", "nord", "gruvbox", "custom"

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

    #[serde(default)]
    pub custom_rules: Vec<String>,
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
            custom_rules: vec![
                "||analytics.*^".to_string(),
                "||telemetry.*^".to_string(),
            ],
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
        let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
        let mut dir = PathBuf::from(home);
        dir.push(".config");
        dir.push("falkon-rust");
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
                let _ = f.write_all(serde_json::to_string_pretty(&def).unwrap_or_default().as_bytes());
            }
            def
        };

        // Load optional custom style files
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
}
