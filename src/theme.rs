use std::ffi::{CStr, CString};
use std::os::raw::c_char;

/// Menghasilkan Stylesheet QSS Browser Modern
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

    let tab_padding = if is_compact { "7px 16px" } else { "11px 24px" };
    let tab_min_w = if is_compact { "110px" } else { "140px" };
    let url_padding = if is_compact { "7px 16px" } else { "10px 20px" };
    let url_font_size = if is_compact { "13px" } else { "14px" };

    let hover_overlay = "rgba(255, 255, 255, 0.08)";
    let active_hover_overlay = "rgba(255, 255, 255, 0.14)";

    // Parse hex bg to rgba string
    let bg_clean = bg.trim_start_matches('#');
    let (r, g, b) = if bg_clean.len() == 6 {
        (
            u8::from_str_radix(&bg_clean[0..2], 16).unwrap_or(24),
            u8::from_str_radix(&bg_clean[2..4], 16).unwrap_or(24),
            u8::from_str_radix(&bg_clean[4..6], 16).unwrap_or(37),
        )
    } else {
        (24, 24, 37)
    };
    let bg_rgba = format!("rgba({}, {}, {}, {:.2})", r, g, b, op);

    let base_qss = format!(r#"
QMainWindow {{ background-color: {bg_rgba}; }}
QWidget {{ color: {fg}; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Ubuntu, sans-serif; font-size: 13px; }}
QWidget#tabStrip {{ background-color: {bg_rgba}; }}
QTabBar {{ background: transparent; border: none; }}
QTabBar::tab {{
    background-color: transparent;
    color: {fg};
    border: none;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
    padding: {tab_padding};
    margin-right: 4px;
    min-width: {tab_min_w};
    max-width: 260px;
    font-size: 13px;
    font-weight: 500;
}}
QTabBar::tab:hover:!selected {{
    background-color: {hover_overlay};
    color: {fg};
}}
QTabBar::tab:selected {{
    background-color: {surface};
    color: {accent};
    font-weight: 600;
    border-top: 1px solid rgba(255, 255, 255, 0.16);
    border-left: 1px solid rgba(255, 255, 255, 0.08);
    border-right: 1px solid rgba(255, 255, 255, 0.08);
}}
QTabBar::close-button {{
    subcontrol-position: right;
    margin-right: 6px;
    border-radius: 4px;
    width: 14px;
    height: 14px;
    background: transparent;
    image: url({close_icon});
}}
QTabBar::close-button:hover {{
    image: url({close_hover});
    background-color: {active_hover_overlay};
}}
QPushButton#newTabBtn {{
    background: transparent;
    color: {fg};
    border: none;
    border-radius: 8px;
    font-size: 18px;
    font-weight: normal;
}}
QPushButton#newTabBtn:hover {{
    background-color: {hover_overlay};
}}
QPushButton#winMinBtn, QPushButton#winMaxBtn, QPushButton#winCloseBtn {{
    background: transparent;
    color: {fg};
    border: none;
    border-radius: 6px;
    font-size: 13px;
    font-weight: bold;
}}
QPushButton#winMinBtn:hover, QPushButton#winMaxBtn:hover {{
    background-color: {hover_overlay};
}}
QPushButton#winCloseBtn:hover {{
    background-color: #e81123;
    color: #ffffff;
}}
QWidget#navBar {{
    background-color: {surface};
    border-bottom: 1px solid {border};
}}
QPushButton#iconBtn, QPushButton#engineBtn {{
    background-color: transparent;
    color: {fg};
    border: none;
    border-radius: 8px;
    padding: 4px;
    min-width: 32px;
    min-height: 32px;
}}
QPushButton#iconBtn:hover, QPushButton#engineBtn:hover {{
    background-color: {hover_overlay};
}}
QLineEdit#urlBar {{
    background-color: rgba(0, 0, 0, 0.28);
    color: {fg};
    border: 1px solid {border};
    border-radius: 18px;
    padding: {url_padding};
    font-size: {url_font_size};
    selection-background-color: {accent};
    selection-color: #11111b;
}}
QLineEdit#urlBar:focus {{
    border: 1px solid {accent};
    background-color: rgba(0, 0, 0, 0.45);
}}
QLabel#badge {{
    background-color: {accent};
    color: #11111b;
    border-radius: 8px;
    padding: 2px 7px;
    font-size: 11px;
    font-weight: bold;
}}
QMenu {{
    background-color: {surface};
    color: {fg};
    border: 1px solid {border};
    border-radius: 10px;
    padding: 6px;
}}
QMenu::item {{
    padding: 7px 24px 7px 12px;
    border-radius: 6px;
}}
QMenu::item:selected {{
    background-color: {hover_overlay};
    color: {accent};
}}
QMenu::separator {{
    height: 1px;
    background: {border};
    margin: 4px 6px;
}}
"#);

    if custom_qss.is_empty() {
        base_qss
    } else {
        format!("{}\n\n/* Custom User QSS */\n{}", base_qss, custom_qss)
    }
}

// ---------------- C FFI Export ----------------

#[no_mangle]
pub extern "C" fn rust_get_theme_qss(
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
    let bg_str = if bg.is_null() { "#181825" } else { unsafe { CStr::from_ptr(bg).to_str().unwrap_or("#181825") } };
    let surface_str = if surface.is_null() { "#1e1e2e" } else { unsafe { CStr::from_ptr(surface).to_str().unwrap_or("#1e1e2e") } };
    let fg_str = if fg.is_null() { "#cdd6f4" } else { unsafe { CStr::from_ptr(fg).to_str().unwrap_or("#cdd6f4") } };
    let accent_str = if accent.is_null() { "#89b4fa" } else { unsafe { CStr::from_ptr(accent).to_str().unwrap_or("#89b4fa") } };
    let border_str = if border.is_null() { "#313244" } else { unsafe { CStr::from_ptr(border).to_str().unwrap_or("#313244") } };
    let density_str = if density.is_null() { "comfortable" } else { unsafe { CStr::from_ptr(density).to_str().unwrap_or("comfortable") } };
    let qss_str = if custom_qss.is_null() { "" } else { unsafe { CStr::from_ptr(custom_qss).to_str().unwrap_or("") } };
    let icon_str = if close_icon.is_null() { "" } else { unsafe { CStr::from_ptr(close_icon).to_str().unwrap_or("") } };
    let hover_str = if close_hover.is_null() { "" } else { unsafe { CStr::from_ptr(close_hover).to_str().unwrap_or("") } };

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
    CString::new(qss).map(|c| c.into_raw()).unwrap_or(std::ptr::null_mut())
}
