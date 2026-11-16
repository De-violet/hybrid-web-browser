use std::ffi::{CStr, CString};
use std::os::raw::c_char;

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
    let accent_color = sanitize_css_token(accent, "#89b4fa");
    let fg_color = sanitize_css_token(fg, "#cdd6f4");

    START_PAGE_TPL
        .replace("@FG@", fg_color)
        .replace("@ACCENT@", accent_color)
        .replace("@CLOCK_DISP@", clock_disp)
        .replace("@SEARCH_DISP@", search_disp)
        .replace("@TILES_DISP@", tiles_disp)
        .replace("@SEARCH_ACTION@", search_action)
        .replace("@ENGINE_LABEL@", engine_label)
}

/// Menghasilkan HTML halaman falkon:settings (2-kolom Tab Internal)
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

    let z_08 = if (zoom - 0.8).abs() < 0.05 { "selected" } else { "" };
    let z_09 = if (zoom - 0.9).abs() < 0.05 { "selected" } else { "" };
    let z_10 = if (zoom - 1.0).abs() < 0.05 { "selected" } else { "" };
    let z_11 = if (zoom - 1.1).abs() < 0.05 { "selected" } else { "" };
    let z_125 = if (zoom - 1.25).abs() < 0.05 { "selected" } else { "" };
    let z_15 = if (zoom - 1.5).abs() < 0.05 { "selected" } else { "" };

    let fg_color = sanitize_css_token(fg, "#cdd6f4");
    let accent_color = sanitize_css_token(accent, "#89b4fa");
    let bg_color = sanitize_css_token(bg, "#181825");
    let surface_color = sanitize_css_token(surface, "#1e1e2e");
    let border_color = sanitize_css_token(border, "#313244");

    let safe_homepage = escape_html(homepage);
    let safe_custom_qss = escape_html(custom_qss);

    SETTINGS_PAGE_TPL
        .replace("@FG@", fg_color)
        .replace("@ACCENT@", accent_color)
        .replace("@BG@", bg_color)
        .replace("@SURFACE@", surface_color)
        .replace("@BORDER@", border_color)
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
}

// ---------------- C FFI Exports ----------------

#[no_mangle]
pub extern "C" fn rust_get_start_page(
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

#[no_mangle]
pub extern "C" fn rust_get_settings_page(
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
        "dark"
    } else {
        unsafe { CStr::from_ptr(theme).to_str().unwrap_or("dark") }
    };
    let bg_str = if bg.is_null() {
        "#181825"
    } else {
        unsafe { CStr::from_ptr(bg).to_str().unwrap_or("#181825") }
    };
    let surface_str = if surface.is_null() {
        "#1e1e2e"
    } else {
        unsafe { CStr::from_ptr(surface).to_str().unwrap_or("#1e1e2e") }
    };
    let fg_str = if fg.is_null() {
        "#cdd6f4"
    } else {
        unsafe { CStr::from_ptr(fg).to_str().unwrap_or("#cdd6f4") }
    };
    let accent_str = if accent.is_null() {
        "#89b4fa"
    } else {
        unsafe { CStr::from_ptr(accent).to_str().unwrap_or("#89b4fa") }
    };
    let border_str = if border.is_null() {
        "#313244"
    } else {
        unsafe { CStr::from_ptr(border).to_str().unwrap_or("#313244") }
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
        "falkon:home"
    } else {
        unsafe { CStr::from_ptr(homepage).to_str().unwrap_or("falkon:home") }
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

#[no_mangle]
pub extern "C" fn rust_free_c_str(ptr: *mut c_char) {
    if !ptr.is_null() {
        unsafe {
            let _ = CString::from_raw(ptr);
        }
    }
}
