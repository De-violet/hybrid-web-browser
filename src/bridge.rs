use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

pub type BrowserHandle = *mut c_void;

pub type RustBlockCallback = extern "C" fn(*const c_char, c_int, *const c_char) -> bool;
pub type RustTabCallback = extern "C" fn(c_int, *const c_char, *const c_char, c_int);
pub type RustTabCloseCallback = extern "C" fn(c_int);
pub type RustTabMoveCallback = extern "C" fn(c_int, c_int);

extern "C" {
    pub fn browser_create(
        title: *const c_char,
        width: c_int,
        height: c_int,
        config_dir: *const c_char,
    ) -> BrowserHandle;
    pub fn browser_set_block_callback(cb: RustBlockCallback);
    pub fn browser_set_tab_callback(handle: BrowserHandle, cb: RustTabCallback);
    pub fn browser_set_tab_close_callback(handle: BrowserHandle, cb: RustTabCloseCallback);
    pub fn browser_set_tab_move_callback(handle: BrowserHandle, cb: RustTabMoveCallback);
    pub fn browser_add_tab(handle: BrowserHandle, raw_url: *const c_char) -> c_int;
    pub fn browser_add_private_tab(handle: BrowserHandle, raw_url: *const c_char) -> c_int;
    pub fn browser_add_suspended_tab(
        handle: BrowserHandle,
        raw_url: *const c_char,
        title: *const c_char,
    ) -> c_int;
    pub fn browser_close_tab(handle: BrowserHandle, index: c_int);
    pub fn browser_switch_tab(handle: BrowserHandle, index: c_int);
    pub fn browser_navigate(handle: BrowserHandle, raw_url: *const c_char);
    pub fn browser_back(handle: BrowserHandle);
    pub fn browser_forward(handle: BrowserHandle);
    pub fn browser_reload(handle: BrowserHandle);
    pub fn browser_update_adblock_badge(handle: BrowserHandle, blocked_count: u64);

    pub fn browser_configure_ui_ux(
        handle: BrowserHandle,
        tab_position: c_int,
        show_nav_buttons: bool,
        show_adblock_badge: bool,
        show_new_tab_button: bool,
        default_zoom: f32,
    );
    pub fn browser_set_theme(
        handle: BrowserHandle,
        theme_name: *const c_char,
        custom_qss: *const c_char,
    );
    pub fn browser_set_colors(
        handle: BrowserHandle,
        bg_color: *const c_char,
        fg_color: *const c_char,
        accent_color: *const c_char,
    );
    pub fn browser_apply_stylesheet(handle: BrowserHandle, qss: *const c_char);
    pub fn browser_set_search_engine(handle: BrowserHandle, template_url: *const c_char);
    pub fn browser_set_download_dir(handle: BrowserHandle, dir: *const c_char);
    pub fn browser_set_user_agent(handle: BrowserHandle, ua: *const c_char);
    pub fn browser_set_custom_user_style(handle: BrowserHandle, css: *const c_char);
    pub fn browser_set_custom_user_script(handle: BrowserHandle, js: *const c_char);

    pub fn browser_run(handle: BrowserHandle) -> c_int;
    pub fn browser_destroy(handle: BrowserHandle);
}

pub struct BrowserApp {
    handle: BrowserHandle,
    blocked_count: Arc<AtomicU64>,
}

impl BrowserApp {
    /// `config_dir` adalah direktori hasil `resolve_config_dir()`: C++
    /// memakai direktori yang sama untuk semua file config (config.json,
    /// history, bookmark, QSS, profil), sehingga kedua sisi tidak lagi
    /// terbelah di Windows. String kosong membuat C++ fallback ke
    /// `QStandardPaths` (defensif).
    pub fn new(
        title: &str,
        width: i32,
        height: i32,
        block_cb: RustBlockCallback,
        config_dir: &Path,
    ) -> Self {
        unsafe {
            browser_set_block_callback(block_cb);
        }

        let c_title = CString::new(title).unwrap_or_else(|_| CString::new("Attis").unwrap());
        let dir_string = config_dir.to_string_lossy();
        let c_dir = CString::new(dir_string.as_ref()).unwrap_or_else(|_| CString::new("").unwrap());
        let handle = unsafe { browser_create(c_title.as_ptr(), width, height, c_dir.as_ptr()) };

        Self {
            handle,
            blocked_count: Arc::new(AtomicU64::new(0)),
        }
    }

    pub fn set_tab_close_callback(&self, cb: RustTabCloseCallback) {
        unsafe {
            browser_set_tab_close_callback(self.handle, cb);
        }
    }

    pub fn set_tab_move_callback(&self, cb: RustTabMoveCallback) {
        unsafe {
            browser_set_tab_move_callback(self.handle, cb);
        }
    }

    pub fn configure_ui_ux(
        &self,
        tab_position: i32,
        show_nav: bool,
        show_badge: bool,
        show_new_tab: bool,
        zoom: f32,
    ) {
        unsafe {
            browser_configure_ui_ux(
                self.handle,
                tab_position,
                show_nav,
                show_badge,
                show_new_tab,
                zoom,
            );
        }
    }

    pub fn set_theme(&self, theme_name: &str, custom_qss: Option<&str>) {
        let c_theme = CString::new(theme_name).unwrap_or_else(|_| CString::new("violet").unwrap());
        let c_qss = custom_qss.and_then(|q| CString::new(q).ok());
        unsafe {
            browser_set_theme(
                self.handle,
                c_theme.as_ptr(),
                c_qss.as_ref().map_or(std::ptr::null(), |s| s.as_ptr()),
            );
        }
    }

    pub fn set_colors(&self, bg: &str, fg: &str, accent: &str) {
        let c_bg = CString::new(bg).unwrap_or_else(|_| CString::new("#121214").unwrap());
        let c_fg = CString::new(fg).unwrap_or_else(|_| CString::new("#e8e8ea").unwrap());
        let c_accent = CString::new(accent).unwrap_or_else(|_| CString::new("#8b5cf6").unwrap());
        unsafe {
            browser_set_colors(self.handle, c_bg.as_ptr(), c_fg.as_ptr(), c_accent.as_ptr());
        }
    }

    pub fn set_tab_callback(&self, cb: RustTabCallback) {
        unsafe {
            browser_set_tab_callback(self.handle, cb);
        }
    }

    pub fn add_tab(&self, url: &str) -> i32 {
        let c_url = CString::new(url).unwrap_or_else(|_| CString::new("").unwrap());
        unsafe { browser_add_tab(self.handle, c_url.as_ptr()) }
    }

    pub fn add_private_tab(&self, url: &str) -> i32 {
        let c_url = CString::new(url).unwrap_or_else(|_| CString::new("").unwrap());
        unsafe { browser_add_private_tab(self.handle, c_url.as_ptr()) }
    }

    /// Buat tab restore dalam keadaan tersuspensi (URL/judul tampil, isi
    /// belum dimuat). Tab dimuat saat pertama kali diaktifkan.
    pub fn add_suspended_tab(&self, url: &str, title: &str) -> i32 {
        let c_url = CString::new(url).unwrap_or_else(|_| CString::new("").unwrap());
        let c_title = CString::new(title).unwrap_or_else(|_| CString::new("").unwrap());
        unsafe { browser_add_suspended_tab(self.handle, c_url.as_ptr(), c_title.as_ptr()) }
    }

    pub fn switch_tab(&self, index: i32) {
        unsafe { browser_switch_tab(self.handle, index) }
    }

    pub fn navigate(&self, url: &str) {
        let c_url = CString::new(url).unwrap_or_else(|_| CString::new("").unwrap());
        unsafe { browser_navigate(self.handle, c_url.as_ptr()) }
    }

    pub fn apply_stylesheet(&self, qss: &str) {
        if let Ok(c_qss) = CString::new(qss) {
            unsafe { browser_apply_stylesheet(self.handle, c_qss.as_ptr()) }
        }
    }

    pub fn set_search_engine(&self, template_url: &str) {
        if let Ok(c_url) = CString::new(template_url) {
            unsafe { browser_set_search_engine(self.handle, c_url.as_ptr()) }
        }
    }

    pub fn set_download_dir(&self, dir: &str) {
        if let Ok(c_dir) = CString::new(dir) {
            unsafe { browser_set_download_dir(self.handle, c_dir.as_ptr()) }
        }
    }

    pub fn set_user_agent(&self, ua: &str) {
        if let Ok(c_ua) = CString::new(ua) {
            unsafe { browser_set_user_agent(self.handle, c_ua.as_ptr()) }
        }
    }

    pub fn set_custom_user_style(&self, css: &str) {
        if let Ok(c_css) = CString::new(css) {
            unsafe { browser_set_custom_user_style(self.handle, c_css.as_ptr()) }
        }
    }

    pub fn set_custom_user_script(&self, js: &str) {
        if let Ok(c_js) = CString::new(js) {
            unsafe { browser_set_custom_user_script(self.handle, c_js.as_ptr()) }
        }
    }

    pub fn update_badge(&self, count: u64) {
        self.blocked_count.store(count, Ordering::Relaxed);
        unsafe { browser_update_adblock_badge(self.handle, count) }
    }

    pub fn run(&self) -> i32 {
        unsafe { browser_run(self.handle) }
    }
}

impl Drop for BrowserApp {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                browser_destroy(self.handle);
            }
            self.handle = std::ptr::null_mut();
        }
    }
}

/// Converts a C-string pointer to a Rust `&str`.
///
/// # Safety
/// The caller must ensure that `ptr` is either null or points to a valid, null-terminated C string
/// whose lifetime outlives the returned reference.
pub unsafe fn c_to_str<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    CStr::from_ptr(ptr).to_str().unwrap_or("")
}
