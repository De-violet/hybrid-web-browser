use crate::config::resolve_config_dir;
use crate::filter_lists::status_summary;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::path::Path;

const START_PAGE_TPL: &str = include_str!("assets/start_page.html");
const SETTINGS_PAGE_TPL: &str = include_str!("assets/settings_page.html");

pub fn escape_html(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    for c in input.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&#39;"),
            _ => out.push(c),
        }
    }
    out
}

pub fn sanitize_css_token<'a>(input: &'a str, fallback: &'a str) -> &'a str {
    let trimmed = input.trim();
    if trimmed.is_empty()
        || trimmed.contains('<')
        || trimmed.contains('>')
        || trimmed.contains('"')
        || trimmed.contains('\'')
        || trimmed.contains(';')
        || trimmed.contains('{')
        || trimmed.contains('}')
    {
        fallback
    } else {
        trimmed
    }
}

/// Menghasilkan HTML halaman Start / Tab Baru (Zen / Arc Style)
pub fn generate_start_page_html(
    accent: &str,
    fg: &str,
    is_google: bool,
    show_clock: bool,
    show_search: bool,
    show_tiles: bool,
) -> String {
    let clock_disp = if show_clock {
        ""
    } else {
        "display:none !important;"
    };
    let search_disp = if show_search {
        ""
    } else {
        "display:none !important;"
    };
    let tiles_disp = if show_tiles {
        ""
    } else {
        "display:none !important;"
    };
    let search_action = if is_google {
        "https://www.google.com/search"
    } else {
        "https://html.duckduckgo.com/html/"
    };
    let engine_label = if is_google { "Google" } else { "DuckDuckGo" };
    let accent_color = sanitize_css_token(accent, "#8b5cf6");
    let fg_color = sanitize_css_token(fg, "#ddd6fe");

    START_PAGE_TPL
        .replace("@FG@", fg_color)
        .replace("@ACCENT@", accent_color)
        .replace("@CLOCK_DISP@", clock_disp)
        .replace("@SEARCH_DISP@", search_disp)
        .replace("@TILES_DISP@", tiles_disp)
        .replace("@SEARCH_ACTION@", search_action)
        .replace("@ENGINE_LABEL@", engine_label)
}

/// Menghasilkan HTML halaman attis:settings (2-kolom Tab Internal)
#[allow(clippy::too_many_arguments)]
pub fn generate_settings_page_html(
    theme: &str,
    bg: &str,
    surface: &str,
    fg: &str,
    accent: &str,
    border: &str,
    opacity: f32,
    density: &str,
    tab_pos: &str,
    zoom: f32,
    is_google: bool,
    show_clock: bool,
    show_search: bool,
    show_tiles: bool,
    custom_qss: &str,
    homepage: &str,
) -> String {
    let cur_theme = theme.to_lowercase();
    let theme_violet = if cur_theme == "violet" {
        "selected"
    } else {
        ""
    };
    let theme_dark = if cur_theme == "dark" || cur_theme == "dark_glass" {
        "selected"
    } else {
        ""
    };
    let theme_catppuccin = if cur_theme == "catppuccin" {
        "selected"
    } else {
        ""
    };
    let theme_light = if cur_theme == "light" { "selected" } else { "" };
    let theme_minimal = if cur_theme == "minimal" {
        "selected"
    } else {
        ""
    };
    let theme_nord = if cur_theme == "nord" { "selected" } else { "" };
    let theme_gruvbox = if cur_theme == "gruvbox" {
        "selected"
    } else {
        ""
    };

    let opt_top = if tab_pos == "top" { "selected" } else { "" };
    let opt_bottom = if tab_pos == "bottom" { "selected" } else { "" };
    let opt_hidden = if tab_pos == "hidden" { "selected" } else { "" };

    let opt_comfortable = if density != "compact" { "selected" } else { "" };
    let opt_compact = if density == "compact" { "selected" } else { "" };

    let engine_google = if is_google { "selected" } else { "" };
    let engine_ddg = if !is_google { "selected" } else { "" };

    let chk_clock = if show_clock { "checked" } else { "" };
    let chk_search = if show_search { "checked" } else { "" };
    let chk_tiles = if show_tiles { "checked" } else { "" };

    let opacity_pct = (opacity.clamp(0.2, 1.0) * 100.0).round() as i32;

    let z_08 = if (zoom - 0.8).abs() < 0.05 {
        "selected"
    } else {
        ""
    };
    let z_09 = if (zoom - 0.9).abs() < 0.05 {
        "selected"
    } else {
        ""
    };
    let z_10 = if (zoom - 1.0).abs() < 0.05 {
        "selected"
    } else {
        ""
    };
    let z_11 = if (zoom - 1.1).abs() < 0.05 {
        "selected"
    } else {
        ""
    };
    let z_125 = if (zoom - 1.25).abs() < 0.05 {
        "selected"
    } else {
        ""
    };
    let z_15 = if (zoom - 1.5).abs() < 0.05 {
        "selected"
    } else {
        ""
    };

    let fg_color = sanitize_css_token(fg, "#ddd6fe");
    let accent_color = sanitize_css_token(accent, "#8b5cf6");
    let bg_color = sanitize_css_token(bg, "#16102a");
    let surface_color = sanitize_css_token(surface, "#1e1738");
    let border_color = sanitize_css_token(border, "#3f2d6b");

    let safe_homepage = escape_html(homepage);
    let safe_custom_qss = escape_html(custom_qss);
    let extras = settings_extras();
    let chk_filter_auto = if extras.filter_auto_update {
        "checked"
    } else {
        ""
    };
    let chk_restore = if extras.restore_session {
        "checked"
    } else {
        ""
    };
    let chk_suspend = if extras.suspend_enabled {
        "checked"
    } else {
        ""
    };
    let safe_filter_status = escape_html(&extras.filter_status);
    let safe_allowlist = escape_html(&extras.allowlist);

    SETTINGS_PAGE_TPL
        .replace("@FG@", fg_color)
        .replace("@ACCENT@", accent_color)
        .replace("@BG@", bg_color)
        .replace("@SURFACE@", surface_color)
        .replace("@BORDER@", border_color)
        .replace("@THEME_VIOLET@", theme_violet)
        .replace("@THEME_DARK@", theme_dark)
        .replace("@THEME_CATPPUCCIN@", theme_catppuccin)
        .replace("@THEME_LIGHT@", theme_light)
        .replace("@THEME_MINIMAL@", theme_minimal)
        .replace("@THEME_NORD@", theme_nord)
        .replace("@THEME_GRUVBOX@", theme_gruvbox)
        .replace("@OPT_TOP@", opt_top)
        .replace("@OPT_BOTTOM@", opt_bottom)
        .replace("@OPT_HIDDEN@", opt_hidden)
        .replace("@OPT_COMFORTABLE@", opt_comfortable)
        .replace("@OPT_COMPACT@", opt_compact)
        .replace("@ENGINE_GOOGLE@", engine_google)
        .replace("@ENGINE_DDG@", engine_ddg)
        .replace("@CHK_CLOCK@", chk_clock)
        .replace("@CHK_SEARCH@", chk_search)
        .replace("@CHK_TILES@", chk_tiles)
        .replace("@OPACITY_PCT@", &opacity_pct.to_string())
        .replace("@ZOOM_08@", z_08)
        .replace("@ZOOM_09@", z_09)
        .replace("@ZOOM_10@", z_10)
        .replace("@ZOOM_11@", z_11)
        .replace("@ZOOM_125@", z_125)
        .replace("@ZOOM_15@", z_15)
        .replace("@HOMEPAGE@", &safe_homepage)
        .replace("@CUSTOM_QSS@", &safe_custom_qss)
        .replace("@FILTER_STATUS@", &safe_filter_status)
        .replace("@CHK_FILTER_AUTO@", chk_filter_auto)
        .replace("@CHK_RESTORE@", chk_restore)
        .replace("@CHK_SUSPEND@", chk_suspend)
        .replace("@SUSPEND_SECS@", &extras.suspend_after_seconds.to_string())
        .replace(
            "@SUSPEND_MANY_SECS@",
            &extras.suspend_many_after_seconds.to_string(),
        )
        .replace(
            "@SUSPEND_THRESHOLD@",
            &extras.suspend_many_threshold.to_string(),
        )
        .replace("@CACHE_MB@", &extras.http_cache_size_mb.to_string())
        .replace("@ALLOWLIST@", &safe_allowlist)
}

/// Nilai pengaturan tambahan untuk halaman Pengaturan, dibaca langsung dari
/// `config.json` dan status filter agar tidak perlu mengubah signature FFI.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SettingsExtras {
    pub filter_status: String,
    pub filter_auto_update: bool,
    pub restore_session: bool,
    pub suspend_enabled: bool,
    pub suspend_after_seconds: u64,
    pub suspend_many_after_seconds: u64,
    pub suspend_many_threshold: usize,
    pub http_cache_size_mb: u32,
    pub allowlist: String,
}

impl Default for SettingsExtras {
    fn default() -> Self {
        Self {
            filter_status: String::new(),
            filter_auto_update: true,
            restore_session: true,
            suspend_enabled: true,
            suspend_after_seconds: 120,
            suspend_many_after_seconds: 45,
            suspend_many_threshold: 3,
            http_cache_size_mb: 20,
            allowlist: String::new(),
        }
    }
}

pub fn settings_extras() -> SettingsExtras {
    settings_extras_from_dir(&resolve_config_dir())
}

pub fn settings_extras_from_dir(config_dir: &Path) -> SettingsExtras {
    let mut extras = SettingsExtras {
        filter_status: status_summary(config_dir),
        ..Default::default()
    };
    let Ok(content) = std::fs::read_to_string(config_dir.join("config.json")) else {
        return extras;
    };
    let Ok(value) = serde_json::from_str::<serde_json::Value>(&content) else {
        return extras;
    };
    if let Some(v) = value.get("filter_auto_update").and_then(|v| v.as_bool()) {
        extras.filter_auto_update = v;
    }
    if let Some(v) = value.get("restore_session").and_then(|v| v.as_bool()) {
        extras.restore_session = v;
    }
    if let Some(v) = value.get("suspend_enabled").and_then(|v| v.as_bool()) {
        extras.suspend_enabled = v;
    }
    if let Some(v) = value.get("suspend_after_seconds").and_then(|v| v.as_u64()) {
        extras.suspend_after_seconds = v;
    }
    if let Some(v) = value
        .get("suspend_many_tabs_after_seconds")
        .and_then(|v| v.as_u64())
    {
        extras.suspend_many_after_seconds = v;
    }
    if let Some(v) = value
        .get("suspend_many_tabs_threshold")
        .and_then(|v| v.as_u64())
    {
        extras.suspend_many_threshold = v as usize;
    }
    if let Some(v) = value.get("http_cache_size_mb").and_then(|v| v.as_u64()) {
        extras.http_cache_size_mb = v as u32;
    }
    if let Some(list) = value.get("adblock_allowlist").and_then(|v| v.as_array()) {
        let domains: Vec<&str> = list.iter().filter_map(|v| v.as_str()).collect();
        extras.allowlist = domains.join(", ");
    }
    extras
}

// ---------------- C FFI Exports ----------------

/// # Safety
/// Semua argumen pointer harus null atau menunjuk string C valid yang
/// null-terminated. Hanya dipanggil dari C++ bridge.
#[no_mangle]
pub unsafe extern "C" fn rust_get_start_page(
    accent: *const c_char,
    fg: *const c_char,
    is_google: bool,
    show_clock: bool,
    show_search: bool,
    show_tiles: bool,
) -> *mut c_char {
    let accent_str = if accent.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(accent).to_str().unwrap_or("") }
    };
    let fg_str = if fg.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(fg).to_str().unwrap_or("") }
    };

    let html = generate_start_page_html(
        accent_str,
        fg_str,
        is_google,
        show_clock,
        show_search,
        show_tiles,
    );
    CString::new(html)
        .map(|c| c.into_raw())
        .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// Semua argumen pointer harus null atau menunjuk string C valid yang
/// null-terminated. Hanya dipanggil dari C++ bridge.
#[no_mangle]
pub unsafe extern "C" fn rust_get_settings_page(
    theme: *const c_char,
    bg: *const c_char,
    surface: *const c_char,
    fg: *const c_char,
    accent: *const c_char,
    border: *const c_char,
    opacity: f32,
    density: *const c_char,
    tab_pos: *const c_char,
    zoom: f32,
    is_google: bool,
    show_clock: bool,
    show_search: bool,
    show_tiles: bool,
    custom_qss: *const c_char,
    homepage: *const c_char,
) -> *mut c_char {
    let theme_str = if theme.is_null() {
        "violet"
    } else {
        unsafe { CStr::from_ptr(theme).to_str().unwrap_or("violet") }
    };
    let bg_str = if bg.is_null() {
        "#16102a"
    } else {
        unsafe { CStr::from_ptr(bg).to_str().unwrap_or("#16102a") }
    };
    let surface_str = if surface.is_null() {
        "#1e1738"
    } else {
        unsafe { CStr::from_ptr(surface).to_str().unwrap_or("#1e1738") }
    };
    let fg_str = if fg.is_null() {
        "#ddd6fe"
    } else {
        unsafe { CStr::from_ptr(fg).to_str().unwrap_or("#ddd6fe") }
    };
    let accent_str = if accent.is_null() {
        "#8b5cf6"
    } else {
        unsafe { CStr::from_ptr(accent).to_str().unwrap_or("#8b5cf6") }
    };
    let border_str = if border.is_null() {
        "#3f2d6b"
    } else {
        unsafe { CStr::from_ptr(border).to_str().unwrap_or("#3f2d6b") }
    };
    let density_str = if density.is_null() {
        "comfortable"
    } else {
        unsafe { CStr::from_ptr(density).to_str().unwrap_or("comfortable") }
    };
    let tab_pos_str = if tab_pos.is_null() {
        "top"
    } else {
        unsafe { CStr::from_ptr(tab_pos).to_str().unwrap_or("top") }
    };
    let qss_str = if custom_qss.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(custom_qss).to_str().unwrap_or("") }
    };
    let homepage_str = if homepage.is_null() {
        "attis:home"
    } else {
        unsafe { CStr::from_ptr(homepage).to_str().unwrap_or("attis:home") }
    };

    let html = generate_settings_page_html(
        theme_str,
        bg_str,
        surface_str,
        fg_str,
        accent_str,
        border_str,
        opacity,
        density_str,
        tab_pos_str,
        zoom,
        is_google,
        show_clock,
        show_search,
        show_tiles,
        qss_str,
        homepage_str,
    );
    CString::new(html)
        .map(|c| c.into_raw())
        .unwrap_or(std::ptr::null_mut())
}

/// # Safety
/// `ptr` harus null atau pointer yang sebelumnya dikembalikan oleh salah
/// satu fungsi FFI Rust di crate ini (dan belum pernah dibebaskan).
#[no_mangle]
pub unsafe extern "C" fn rust_free_c_str(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe {
            let _ = CString::from_raw(ptr);
        }
    }
}
