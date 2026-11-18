#ifndef WEB_BRIDGE_H
#define WEB_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* BrowserHandle;

typedef bool (*RustBlockCallback)(const char* url, int resource_type, const char* source_url);
typedef void (*RustTabCallback)(int tab_index, const char* url, const char* title, int is_private);
typedef void (*RustTabCloseCallback)(int tab_index);
typedef void (*RustTabMoveCallback)(int from_index, int to_index);

BrowserHandle browser_create(const char* title, int width, int height, const char* config_dir);
void browser_set_block_callback(RustBlockCallback cb);
void browser_set_tab_callback(BrowserHandle handle, RustTabCallback cb);
void browser_set_tab_close_callback(BrowserHandle handle, RustTabCloseCallback cb);
void browser_set_tab_move_callback(BrowserHandle handle, RustTabMoveCallback cb);
int browser_add_tab(BrowserHandle handle, const char* url);
int browser_add_private_tab(BrowserHandle handle, const char* url);
int browser_add_suspended_tab(BrowserHandle handle, const char* url, const char* title);
void browser_close_tab(BrowserHandle handle, int index);
void browser_switch_tab(BrowserHandle handle, int index);
void browser_navigate(BrowserHandle handle, const char* url);
void browser_back(BrowserHandle handle);
void browser_forward(BrowserHandle handle);
void browser_reload(BrowserHandle handle);
void browser_update_adblock_badge(BrowserHandle handle, uint64_t blocked_count);

// UI & UX Customization API
void browser_configure_ui_ux(
    BrowserHandle handle,
    int tab_position, // 0 = Top, 1 = Bottom, 2 = Hidden
    bool show_nav_buttons,
    bool show_adblock_badge,
    bool show_new_tab_button,
    float default_zoom
);
void browser_set_theme(BrowserHandle handle, const char* theme_name, const char* custom_qss);
void browser_apply_stylesheet(BrowserHandle handle, const char* qss);
void browser_set_colors(BrowserHandle handle, const char* bg_color, const char* fg_color, const char* accent_color);
void browser_set_search_engine(BrowserHandle handle, const char* template_url);
void browser_set_download_dir(BrowserHandle handle, const char* dir);
void browser_set_user_agent(BrowserHandle handle, const char* ua);
void browser_set_custom_user_style(BrowserHandle handle, const char* css);
void browser_set_custom_user_script(BrowserHandle handle, const char* js);

int browser_run(BrowserHandle handle);
void browser_destroy(BrowserHandle handle);

#ifdef __cplusplus
}
#endif

#endif // WEB_BRIDGE_H
