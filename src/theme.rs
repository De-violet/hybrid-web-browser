use std::ffi::{CStr, CString};
use std::os::raw::c_char;

/// Parse warna `#RRGGBB` menjadi komponen RGB.
///
/// Mengembalikan `None` untuk input apa pun yang tidak persis 6 digit hex
/// ASCII — termasuk string non-ASCII, terlalu pendek/panjang, kosong, atau
/// mengandung karakter non-hex — sehingga pemanggil tidak pernah panic.
/// Slicing per byte hanya dilakukan setelah seluruh byte dipastikan digit
/// hex ASCII (satu byte per karakter), jadi indeks byte selalu berada di
/// batas karakter UTF-8.
fn parse_hex_rgb(input: &str) -> Option<(u8, u8, u8)> {
    let hex = input.trim().trim_start_matches('#');
    let bytes = hex.as_bytes();
    if bytes.len() != 6 || !bytes.iter().all(|b| b.is_ascii_hexdigit()) {
        return None;
    }
    let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
    let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
    let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
    Some((r, g, b))
}

/// Validasi ketat token warna untuk QSS: hanya `#RGB` atau `#RRGGBB`
/// (digit hex ASCII) yang diterima; nilai lain diganti fallback. Tanpa
/// ini, token warna yang dibuat-buat bisa menyuntik aturan QSS tambahan
/// (mis. menyembunyikan URL bar atau indikator keamanan).
fn sanitize_color_token<'a>(input: &'a str, fallback: &'a str) -> &'a str {
    let trimmed = input.trim();
    let Some(hex) = trimmed.strip_prefix('#') else {
        return fallback;
    };
    if matches!(hex.len(), 3 | 6) && hex.bytes().all(|b| b.is_ascii_hexdigit()) {
        trimmed
    } else {
        fallback
    }
}

/// Menghasilkan Stylesheet QSS Browser Modern (Digunakan langsung oleh C++ Web Bridge)
pub fn generate_qss(
    bg: &str,
    surface: &str,
    fg: &str,
    accent: &str,
    border: &str,
    opacity: f32,
    density: &str,
    custom_qss: &str,
    close_icon: &str,
    close_hover: &str,
) -> String {
    let op = opacity.clamp(0.2, 1.0);
    let is_compact = density == "compact";

    let tab_padding = if is_compact { "6px 14px" } else { "9px 20px" };
    let tab_min_w = if is_compact { "105px" } else { "135px" };
    let url_padding = if is_compact { "5px 14px" } else { "7px 16px" };
    let url_font_size = if is_compact { "13px" } else { "13.5px" };

    // Parse hex bg to RGB + lightness detection. Input tidak valid
    // (mis. string non-ASCII) memakai warna default, tidak pernah panic.
    let (r, g, b) = parse_hex_rgb(bg).unwrap_or((18, 18, 20));
    let luminance = (0.299 * (r as f32) + 0.587 * (g as f32) + 0.114 * (b as f32)) as u32;
    let is_light = luminance > 140;

    let hover_overlay = if is_light {
        "rgba(0, 0, 0, 0.06)"
    } else {
        "rgba(255, 255, 255, 0.08)"
    };
    let active_hover_overlay = if is_light {
        "rgba(0, 0, 0, 0.11)"
    } else {
        "rgba(255, 255, 255, 0.14)"
    };
    let url_bg = if is_light {
        "rgba(0, 0, 0, 0.05)"
    } else {
        "rgba(0, 0, 0, 0.36)"
    };
    let url_hover_bg = if is_light {
        "rgba(0, 0, 0, 0.08)"
    } else {
        "rgba(0, 0, 0, 0.46)"
    };
    let url_focus_bg = if is_light {
        "#ffffff"
    } else {
        "rgba(12, 12, 20, 0.85)"
    };

    let bg_rgba = format!("rgba({}, {}, {}, {:.2})", r, g, b, op);
    let surface_col = sanitize_color_token(surface, "#1c1c1f");
    let fg_col = sanitize_color_token(fg, "#e8e8ea");
    let accent_col = sanitize_color_token(accent, "#8b5cf6");
    let border_col = sanitize_color_token(border, "#2e2e33");

    let base_qss = format!(
        r#"
QMainWindow {{ background-color: {bg_rgba}; }}
QWidget {{ color: {fg_col}; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Ubuntu, sans-serif; font-size: 13px; }}
QWidget#tabStrip {{ background-color: {bg_rgba}; }}
QTabBar {{ background: transparent; border: none; }}
QTabBar::tab {{
    background-color: transparent;
    color: {fg_col};
    border: 1px solid transparent;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: {tab_padding};
    margin-right: 4px;
    min-width: {tab_min_w};
    max-width: 240px;
    font-size: 12.5px;
    font-weight: 500;
}}
QTabBar::tab:hover:!selected {{
    background-color: {hover_overlay};
    color: {fg_col};
}}
QTabBar::tab:selected {{
    background-color: {surface_col};
    color: {accent_col};
    font-weight: 600;
    border-top: 1px solid {border_col};
    border-left: 1px solid {border_col};
    border-right: 1px solid {border_col};
}}
QTabBar::close-button {{
    subcontrol-position: right;
    margin-right: 6px;
    border-radius: 4px;
    width: 14px;
    height: 14px;
    background: transparent;
    image: url("{close_icon}");
}}
QTabBar::close-button:hover {{
    image: url("{close_hover}");
    background-color: {active_hover_overlay};
}}
QPushButton#newTabBtn {{
    background: transparent;
    color: {fg_col};
    border: none;
    border-radius: 6px;
    font-size: 18px;
    font-weight: 500;
}}
QPushButton#newTabBtn:hover {{
    background-color: {hover_overlay};
}}
QPushButton#winMinBtn, QPushButton#winMaxBtn, QPushButton#winCloseBtn {{
    background: transparent;
    color: {fg_col};
    border: none;
    border-radius: 4px;
    font-size: 13px;
    font-weight: 600;
}}
QPushButton#winMinBtn:hover, QPushButton#winMaxBtn:hover {{
    background-color: {hover_overlay};
}}
QPushButton#winCloseBtn:hover {{
    background-color: #e81123;
    color: #ffffff;
}}
QWidget#navBar {{
    background-color: {surface_col};
    border-bottom: 1px solid {border_col};
}}
QWidget#findBar, QWidget#statusBarWidget {{
    background-color: {surface_col};
    border-top: 1px solid {border_col};
}}
QPushButton#iconBtn, QPushButton#engineBtn {{
    background-color: transparent;
    color: {fg_col};
    border: none;
    border-radius: 6px;
    padding: 4px;
    font-size: 15px;
}}
QPushButton#iconBtn::menu-indicator, QPushButton::menu-indicator {{
    image: none;
    width: 0px;
}}
QPushButton#iconBtn:hover, QPushButton#engineBtn:hover {{
    background-color: {hover_overlay};
}}
QPushButton#iconBtn:pressed, QPushButton#engineBtn:pressed {{
    background-color: {active_hover_overlay};
}}
QPushButton#iconBtn:disabled {{
    color: {border_col};
}}
QLineEdit#urlBar, QLineEdit#findInput {{
    background-color: {url_bg};
    color: {fg_col};
    border: 1px solid {border_col};
    border-radius: 8px;
    padding: {url_padding};
    font-size: {url_font_size};
    selection-background-color: {accent_col};
    selection-color: #11111b;
}}
QLineEdit#urlBar:hover, QLineEdit#findInput:hover {{
    background-color: {url_hover_bg};
}}
QLineEdit#urlBar:focus, QLineEdit#findInput:focus {{
    border: 1px solid {accent_col};
    background-color: {url_focus_bg};
}}
QLabel#adblockBadge {{
    background-color: rgba(166, 227, 161, 0.14);
    color: #a6e3a1;
    border: 1px solid rgba(166, 227, 161, 0.30);
    border-radius: 8px;
    padding: 4px 11px;
    font-size: 11px;
    font-weight: 600;
}}
QLabel#privateBadge {{
    background-color: rgba(203, 166, 247, 0.16);
    color: #cba6f7;
    border: 1px solid rgba(203, 166, 247, 0.35);
    border-radius: 8px;
    padding: 4px 10px;
    font-size: 11px;
    font-weight: 600;
}}
QProgressBar#loadProgress {{
    background-color: transparent;
    border: none;
    max-height: 2px;
    min-height: 2px;
}}
QProgressBar#loadProgress::chunk {{
    background-color: {accent_col};
}}
QMenu {{
    background-color: {surface_col};
    color: {fg_col};
    border: 1px solid {border_col};
    border-radius: 8px;
    padding: 6px;
}}
QMenu::item {{
    padding: 6px 24px 6px 12px;
    border-radius: 6px;
}}
QMenu::shortcut {{
    color: {fg_col};
    padding-left: 20px;
}}
QMenu::item:selected {{
    background-color: {hover_overlay};
    color: {accent_col};
}}
QMenu::separator {{
    height: 1px;
    background: {border_col};
    margin: 4px 6px;
}}
"#
    );

    if custom_qss.trim().is_empty() {
        base_qss
    } else {
        format!("{}\n\n/* Custom User QSS */\n{}", base_qss, custom_qss)
    }
}

// ---------------- C FFI Export ----------------

/// # Safety
/// Semua argumen pointer harus null atau menunjuk string C valid yang
/// null-terminated. Hanya dipanggil dari C++ bridge.
#[no_mangle]
pub unsafe extern "C" fn rust_get_theme_qss(
    bg: *const c_char,
    surface: *const c_char,
    fg: *const c_char,
    accent: *const c_char,
    border: *const c_char,
    opacity: f32,
    density: *const c_char,
    custom_qss: *const c_char,
    close_icon: *const c_char,
    close_hover: *const c_char,
) -> *mut c_char {
    let bg_str = if bg.is_null() {
        "#121214"
    } else {
        unsafe { CStr::from_ptr(bg).to_str().unwrap_or("#121214") }
    };
    let surface_str = if surface.is_null() {
        "#1c1c1f"
    } else {
        unsafe { CStr::from_ptr(surface).to_str().unwrap_or("#1c1c1f") }
    };
    let fg_str = if fg.is_null() {
        "#e8e8ea"
    } else {
        unsafe { CStr::from_ptr(fg).to_str().unwrap_or("#e8e8ea") }
    };
    let accent_str = if accent.is_null() {
        "#8b5cf6"
    } else {
        unsafe { CStr::from_ptr(accent).to_str().unwrap_or("#8b5cf6") }
    };
    let border_str = if border.is_null() {
        "#2e2e33"
    } else {
        unsafe { CStr::from_ptr(border).to_str().unwrap_or("#2e2e33") }
    };
    let density_str = if density.is_null() {
        "comfortable"
    } else {
        unsafe { CStr::from_ptr(density).to_str().unwrap_or("comfortable") }
    };
    let qss_str = if custom_qss.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(custom_qss).to_str().unwrap_or("") }
    };
    let icon_str = if close_icon.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(close_icon).to_str().unwrap_or("") }
    };
    let hover_str = if close_hover.is_null() {
        ""
    } else {
        unsafe { CStr::from_ptr(close_hover).to_str().unwrap_or("") }
    };

    let qss = generate_qss(
        bg_str,
        surface_str,
        fg_str,
        accent_str,
        border_str,
        opacity,
        density_str,
        qss_str,
        icon_str,
        hover_str,
    );
    CString::new(qss)
        .map(|c| c.into_raw())
        .unwrap_or(std::ptr::null_mut())
}
