#include "web_bridge.h"

#include <QString>
#include <QStringList>
#include <QApplication>
#include <QColor>
#include <QMainWindow>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineFindTextResult>
#include <QWebEngineSettings>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineDownloadRequest>
#include <QWebEngineHistory>
#include <QShortcut>
#include <QKeySequence>
#include <QStandardPaths>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QLabel>
#include <QProgressBar>
#include <QMenu>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QEvent>
#include <QWidgetAction>
#include <QListWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QAction>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <QUrl>
#include <QUrlQuery>
#include <vector>

#ifdef Q_OS_WIN
// Windows-only: dark title bars via DWM. NOMINMAX keeps windows.h from
// defining min/max macros that would break <algorithm>.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

extern "C" {
    char* rust_get_start_page(const char* accent, const char* fg, const char* bg, const char* surface, const char* border, bool is_google, bool show_clock, bool show_search, bool show_tiles);
    char* rust_get_settings_page(
        const char* theme, const char* bg, const char* surface, const char* fg, const char* accent, const char* border,
        float opacity, const char* density, const char* tab_pos, float zoom, bool is_google,
        bool show_clock, bool show_search, bool show_tiles, const char* custom_qss, const char* homepage
    );
    char* rust_get_theme_qss(
        const char* bg, const char* surface, const char* fg, const char* accent, const char* border,
        float opacity, const char* density, const char* custom_qss, const char* close_icon, const char* close_hover
    );
    char* rust_get_cosmetic_css(const char* url);
    void rust_set_adblock_enabled(bool enabled);
    void rust_update_filters_async();
    void rust_set_allowlist(const char* csv);
    void rust_clear_history();
    void rust_free_c_str(char* ptr);
}

static RustBlockCallback g_block_cb = nullptr;

// Unified configuration directory, supplied once by the Rust side
// (resolve_config_dir()) via browser_create. Both sides must read and
// write the SAME config.json / history.json / bookmarks.json: on
// Windows the Rust directory (%USERPROFILE%\.config\attis) differs
// from Qt's ConfigLocation (%APPDATA%\attis), so deriving the
// directory here from QStandardPaths split the configuration in two
// and almost every setting reset to defaults on each launch. Empty
// until browser_create runs; the accessor falls back to the legacy
// QStandardPaths location when no directory was supplied (null/empty).
static QString g_config_dir;

static QString attis_config_dir() {
    if (!g_config_dir.isEmpty()) {
        return g_config_dir;
    }
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/attis";
}

// One-time migration of user data from the legacy C++ config directory
// (%APPDATA%\attis on Windows) to the unified directory supplied by
// Rust. Each file is copied only if it exists in the source and not
// yet in the target; sources are never overwritten or deleted.
// Skipped entirely when both directories are the same (always true on
// Linux, where both sides already used ~/.config/attis). config.json
// is deliberately NOT migrated: the Rust-side file becomes
// authoritative, so settings are applied fresh once after upgrading.
static void migrate_legacy_config_dir(const QString &newDir) {
    QString legacyDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/attis";
    QString cleanNew = QDir::cleanPath(QDir(newDir).absolutePath());
    QString cleanLegacy = QDir::cleanPath(QDir(legacyDir).absolutePath());
#ifdef Q_OS_WIN
    if (cleanNew.compare(cleanLegacy, Qt::CaseInsensitive) == 0) {
        return;
    }
#else
    if (cleanNew == cleanLegacy) {
        return;
    }
#endif
    static const char *kMigratedFiles[] = {
        "bookmarks.json", "user_chrome.qss", "tab_close.svg", "tab_close_hover.svg"
    };
    QDir().mkpath(newDir);
    for (const char *name : kMigratedFiles) {
        QString src = legacyDir + "/" + QString::fromLatin1(name);
        QString dst = newDir + "/" + QString::fromLatin1(name);
        if (QFile::exists(src) && !QFile::exists(dst)) {
            QFile::copy(src, dst);
        }
    }
}

class BridgeInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    explicit BridgeInterceptor(QObject *parent = nullptr) : QWebEngineUrlRequestInterceptor(parent) {}

    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        if (!g_block_cb) {
            return;
        }

        QByteArray urlBytes = info.requestUrl().toString().toUtf8();
        QByteArray partyBytes = info.firstPartyUrl().toString().toUtf8();
        int resType = static_cast<int>(info.resourceType());

        if (g_block_cb(urlBytes.constData(), resType, partyBytes.constData())) {
            info.block(true);
        }
    }
};

struct TabMeta {
    QWebEngineView *view = nullptr;
    QString savedUrl;
    QString savedTitle;
    qint64 lastActive = 0;
    bool isSuspended = false;
    bool isPrivate = false;
};

struct ClosedTabEntry {
    QString url;
    QString title;
};

struct DownloadItemMeta {
    QString fileName;
    QString stateText;
    qint64 receivedBytes = 0;
    qint64 totalBytes = 0;
};

struct BrowserContext {
    QApplication *app = nullptr;
    QMainWindow *window = nullptr;
    QVBoxLayout *mainLayout = nullptr;
    QWidget *tabRowWidget = nullptr;
    QTabBar *tabBar = nullptr;
    QStackedWidget *viewStack = nullptr;
    QWidget *navBar = nullptr;
    QProgressBar *loadProgressBar = nullptr;
    QPushButton *backBtn = nullptr;
    QPushButton *forwardBtn = nullptr;
    QPushButton *reloadBtn = nullptr;
    QAction *securityAction = nullptr;
    QAction *engineAction = nullptr;
    QAction *bookmarkAction = nullptr;
    QLineEdit *urlBar = nullptr;
    QPushButton *newTabBtn = nullptr;
    QPushButton *winMinBtn = nullptr;
    QPushButton *winMaxBtn = nullptr;
    QPushButton *winCloseBtn = nullptr;
    QPushButton *menuBtn = nullptr;
    QLabel *privateBadge = nullptr;
    QLabel *adblockBadge = nullptr;

    // Inline Find Bar
    QWidget *findBar = nullptr;
    QLineEdit *findInput = nullptr;
    QLabel *findStatusLabel = nullptr;

    // Bottom Status / Toast Bar
    QWidget *statusBarWidget = nullptr;
    QLabel *statusHoverLabel = nullptr;
    QLabel *statusToastLabel = nullptr;

    QWebEngineProfile *privateProfile = nullptr;
    BridgeInterceptor *interceptor = nullptr;
    RustTabCallback tabCallback = nullptr;
    RustTabCloseCallback tabCloseCallback = nullptr;
    RustTabMoveCallback tabMoveCallback = nullptr;
    QTimer *memoryTimer = nullptr;

    std::vector<TabMeta> tabs;
    std::vector<ClosedTabEntry> closedTabs;
    std::vector<DownloadItemMeta> downloads;

    bool useGoogleSearch = true;
    QString customSearchEngine;
    QString downloadDir;
    QString customChromeQss;   // QSS untuk antarmuka Qt
    QString customPageCss;     // CSS untuk injeksi halaman web (user_style.css)
    QString customUserScript;  // JS untuk injeksi halaman web (user_script.js)
    QString customUserAgent;
    float currentZoom = 1.0f;
    bool ownApp = false;
    bool showNewTabButton = true;
    bool enableAdblock = true;
    bool enableCosmeticFilter = true;
    QString currentTheme = "violet";
    QString bgColor = "#121214";
    QString fgColor = "#e8e8ea";
    QString accentColor = "#8b5cf6";
    QString surfaceColor = "#1c1c1f";
    QString borderColor = "#2e2e33";
    float bgOpacity = 1.0f;
    QString uiDensity = "comfortable";
    QString tabPosition = "top";
    bool showHomeSearch = true;
    bool showHomeTiles = true;
    bool showHomeClock = true;

    // Hemat RAM & sesi (dapat diubah dari Pengaturan, tersimpan di config.json)
    bool suspendEnabled = true;
    int suspendAfterSecs = 120;
    int suspendManySecs = 45;
    int suspendManyThreshold = 3;
    int httpCacheSizeMb = 20;
    bool restoreSession = true;
    bool filterAutoUpdate = true;
    // Start maximized on first run; the real state is saved on exit and
    // restored from config.json, so the window always fits the screen.
    bool windowMaximized = true;
};

static int create_tab_internal(BrowserContext *ctx, const char* raw_url, bool is_private);
static void show_status_toast(BrowserContext *ctx, const QString &msg, int timeoutMs = 3000);

class BrowserPage : public QWebEnginePage {
public:
    explicit BrowserPage(BrowserContext *ctx, QWebEngineProfile *profile, QObject *parent = nullptr)
        : QWebEnginePage(profile, parent), m_ctx(ctx) {}

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;
    QWebEnginePage* createWindow(WebWindowType type) override;

private:
    BrowserContext *m_ctx = nullptr;
};

void browser_set_block_callback(RustBlockCallback cb) {
    g_block_cb = cb;
}

void browser_set_tab_callback(BrowserHandle handle, RustTabCallback cb) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx) {
        ctx->tabCallback = cb;
    }
}

void browser_set_tab_close_callback(BrowserHandle handle, RustTabCloseCallback cb) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx) {
        ctx->tabCloseCallback = cb;
    }
}

void browser_set_tab_move_callback(BrowserHandle handle, RustTabMoveCallback cb) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx) {
        ctx->tabMoveCallback = cb;
    }
}

static void update_tab_bar_placement(BrowserContext *ctx) {
    if (!ctx || !ctx->mainLayout || !ctx->tabRowWidget || !ctx->navBar || !ctx->viewStack) return;

    if (ctx->tabPosition == "hidden") {
        ctx->tabRowWidget->hide();
        return;
    }

    ctx->tabRowWidget->show();
    if (ctx->tabBar) {
        ctx->tabBar->show();
        ctx->tabBar->setShape(ctx->tabPosition == "bottom" ? QTabBar::RoundedSouth : QTabBar::RoundedNorth);
    }
    if (ctx->newTabBtn) {
        ctx->newTabBtn->setVisible(ctx->showNewTabButton);
    }

    ctx->mainLayout->removeWidget(ctx->tabRowWidget);
    if (ctx->tabPosition == "bottom") {
        int stackIdx = ctx->mainLayout->indexOf(ctx->viewStack);
        ctx->mainLayout->insertWidget(stackIdx + 1, ctx->tabRowWidget);
    } else {
        ctx->mainLayout->insertWidget(0, ctx->tabRowWidget);
    }
}

// Strict color validation for stylesheets: only `#RGB` / `#RRGGBB` hex
// colors are accepted; anything else falls back to the given default so
// a crafted color value can never inject extra QSS rules. The Rust side
// validates the same way when generating the main theme stylesheet;
// this helper covers the dialog stylesheets formatted here in C++ and
// the places that store color values from settings actions, config
// files, and FFI calls.
static QString sanitize_qss_color(const QString &value, const QString &fallback) {
    QString v = value.trimmed();
    if (v.size() == 4 || v.size() == 7) {
        if (v.at(0) == '#') {
            bool allHex = true;
            for (int i = 1; i < v.size(); ++i) {
                QChar c = v.at(i);
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    allHex = false;
                    break;
                }
            }
            if (allHex) {
                return v;
            }
        }
    }
    return fallback;
}

#ifdef Q_OS_WIN
// True when a #rrggbb color is dark (relative luminance below 0.5).
static bool is_dark_hex_color(const QString &hex) {
    QColor c(hex);
    if (!c.isValid()) return true;
    double lum = (0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue()) / 255.0;
    return lum < 0.5;
}

// Ask Windows to draw a dark (or light) title bar for this window.
// DWMWA_USE_IMMERSIVE_DARK_MODE needs Windows 10 1809+; on older builds
// the call fails silently and the system title bar stays as-is.
static void apply_dark_title_bar(QWidget *widget, bool dark) {
    if (!widget) return;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd) return;
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}
#endif

static void apply_theme_qss(BrowserContext *ctx) {
    if (!ctx || !ctx->window) return;

    QString closeSvg = attis_config_dir() + "/tab_close.svg";
    QString closeHoverSvg = attis_config_dir() + "/tab_close_hover.svg";

    char* rawQss = rust_get_theme_qss(
        ctx->bgColor.toUtf8().constData(),
        ctx->surfaceColor.toUtf8().constData(),
        ctx->fgColor.toUtf8().constData(),
        ctx->accentColor.toUtf8().constData(),
        ctx->borderColor.toUtf8().constData(),
        ctx->bgOpacity,
        ctx->uiDensity.toUtf8().constData(),
        ctx->customChromeQss.toUtf8().constData(),
        closeSvg.toUtf8().constData(),
        closeHoverSvg.toUtf8().constData()
    );

    if (rawQss) {
        QString qss = QString::fromUtf8(rawQss);
        rust_free_c_str(rawQss);
        ctx->window->setStyleSheet(qss);
    }

#ifdef Q_OS_WIN
    // Keep the native title bar in step with the theme, so a dark theme
    // does not end up with a white title bar (and vice versa).
    apply_dark_title_bar(ctx->window, is_dark_hex_color(ctx->bgColor));
#endif

    update_tab_bar_placement(ctx);
}

void browser_set_theme(BrowserHandle handle, const char* theme_name, const char* custom_qss) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->window) return;

    if (custom_qss) {
        ctx->customChromeQss = QString::fromUtf8(custom_qss);
    }

    if (theme_name) {
        QString t = QString::fromUtf8(theme_name).toLower();
        ctx->currentTheme = t;
        if (t == "violet") {
            ctx->bgColor = "#121214"; ctx->surfaceColor = "#1c1c1f"; ctx->fgColor = "#e8e8ea"; ctx->accentColor = "#8b5cf6"; ctx->borderColor = "#2e2e33"; ctx->bgOpacity = 1.0f;
        } else if (t == "dark_glass" || t == "dark") {
            ctx->bgColor = "#11111b"; ctx->surfaceColor = "#181825"; ctx->fgColor = "#cdd6f4"; ctx->accentColor = "#89b4fa"; ctx->borderColor = "#313244"; ctx->bgOpacity = 0.88f;
        } else if (t == "catppuccin") {
            ctx->bgColor = "#1e1e2e"; ctx->surfaceColor = "#181825"; ctx->fgColor = "#cdd6f4"; ctx->accentColor = "#cba6f7"; ctx->borderColor = "#45475a"; ctx->bgOpacity = 0.92f;
        } else if (t == "light") {
            ctx->bgColor = "#eff1f5"; ctx->surfaceColor = "#e6e9ef"; ctx->fgColor = "#4c4f69"; ctx->accentColor = "#1e66f5"; ctx->borderColor = "#ccd0da"; ctx->bgOpacity = 0.96f;
        } else if (t == "minimal") {
            ctx->bgColor = "#121212"; ctx->surfaceColor = "#1c1c1c"; ctx->fgColor = "#e0e0e0"; ctx->accentColor = "#ffffff"; ctx->borderColor = "#2c2c2c"; ctx->bgOpacity = 0.92f;
        } else if (t == "nord") {
            ctx->bgColor = "#2e3440"; ctx->surfaceColor = "#242933"; ctx->fgColor = "#eceff4"; ctx->accentColor = "#88c0d0"; ctx->borderColor = "#3b4252"; ctx->bgOpacity = 0.92f;
        } else if (t == "gruvbox") {
            ctx->bgColor = "#282828"; ctx->surfaceColor = "#1d2021"; ctx->fgColor = "#ebdbb2"; ctx->accentColor = "#fe8019"; ctx->borderColor = "#3c3836"; ctx->bgOpacity = 0.92f;
        }
    }

    apply_theme_qss(ctx);
}

void browser_set_colors(BrowserHandle handle, const char* bg_color, const char* fg_color, const char* accent_color) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx) return;
    if (bg_color && bg_color[0] != '\0') ctx->bgColor = sanitize_qss_color(QString::fromUtf8(bg_color), ctx->bgColor);
    if (fg_color && fg_color[0] != '\0') ctx->fgColor = sanitize_qss_color(QString::fromUtf8(fg_color), ctx->fgColor);
    if (accent_color && accent_color[0] != '\0') ctx->accentColor = sanitize_qss_color(QString::fromUtf8(accent_color), ctx->accentColor);
    apply_theme_qss(ctx);
}

void browser_configure_ui_ux(
    BrowserHandle handle,
    int tab_position,
    bool show_nav_buttons,
    bool show_adblock_badge,
    bool show_new_tab_button,
    float default_zoom
) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx) return;

    if (tab_position == 1) {
        ctx->tabPosition = "bottom";
    } else if (tab_position == 2) {
        ctx->tabPosition = "hidden";
    } else {
        ctx->tabPosition = "top";
    }
    ctx->showNewTabButton = show_new_tab_button;

    apply_theme_qss(ctx);

    if (ctx->backBtn) ctx->backBtn->setVisible(show_nav_buttons);
    if (ctx->forwardBtn) ctx->forwardBtn->setVisible(show_nav_buttons);
    if (ctx->reloadBtn) ctx->reloadBtn->setVisible(show_nav_buttons);
    if (ctx->adblockBadge) ctx->adblockBadge->setVisible(show_adblock_badge);
    if (ctx->newTabBtn) ctx->newTabBtn->setVisible(show_new_tab_button && ctx->tabPosition != "hidden");

    if (default_zoom > 0.2f && default_zoom < 5.0f) {
        ctx->currentZoom = default_zoom;
        if (ctx->viewStack) {
            for (int i = 0; i < ctx->viewStack->count(); ++i) {
                auto *v = qobject_cast<QWebEngineView*>(ctx->viewStack->widget(i));
                if (v) v->setZoomFactor(ctx->currentZoom);
            }
        }
    }
}

void browser_set_search_engine(BrowserHandle handle, const char* template_url) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && template_url) {
        QString tpl = QString::fromUtf8(template_url).trimmed();
        ctx->customSearchEngine = tpl;
        if (tpl.contains("duckduckgo", Qt::CaseInsensitive)) {
            ctx->useGoogleSearch = false;
        } else if (tpl.contains("google", Qt::CaseInsensitive)) {
            ctx->useGoogleSearch = true;
        }
    }
}

void browser_set_download_dir(BrowserHandle handle, const char* dir) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && dir) {
        ctx->downloadDir = QString::fromUtf8(dir);
    }
}

void browser_set_user_agent(BrowserHandle handle, const char* ua) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ua && ua[0] != '\0') {
        QString uaStr = QString::fromUtf8(ua);
        if (ctx) ctx->customUserAgent = uaStr;
        QWebEngineProfile::defaultProfile()->setHttpUserAgent(uaStr);
        if (ctx && ctx->privateProfile) {
            ctx->privateProfile->setHttpUserAgent(uaStr);
        }
    }
}

void browser_apply_stylesheet(BrowserHandle handle, const char* qss) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && qss) {
        ctx->customChromeQss = QString::fromUtf8(qss);
        apply_theme_qss(ctx);
    }
}

void browser_set_custom_user_style(BrowserHandle handle, const char* css) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && css) {
        ctx->customPageCss = QString::fromUtf8(css);
    }
}

void browser_set_custom_user_script(BrowserHandle handle, const char* js) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && js) {
        ctx->customUserScript = QString::fromUtf8(js);
    }
}

static bool is_url_bookmarked(const QString &url);
static void update_bookmark_button(BrowserContext *ctx, bool isBookmarked);
static void update_security_indicator(BrowserContext *ctx, const QString &url);

static void update_nav_buttons_state(BrowserContext *ctx) {
    if (!ctx || !ctx->viewStack) return;
    auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (view && view->history()) {
        if (ctx->backBtn) ctx->backBtn->setEnabled(view->history()->canGoBack());
        if (ctx->forwardBtn) ctx->forwardBtn->setEnabled(view->history()->canGoForward());
    }
}

static void update_url_bar_for_current_tab(BrowserContext *ctx) {
    if (!ctx || !ctx->tabBar || !ctx->urlBar || !ctx->viewStack) return;
    int idx = ctx->tabBar->currentIndex();
    QString currentUrl;
    if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
        if (ctx->privateBadge) {
            ctx->privateBadge->setVisible(ctx->tabs[idx].isPrivate);
        }
        if (ctx->tabs[idx].isSuspended) {
            currentUrl = ctx->tabs[idx].savedUrl;
            ctx->urlBar->setText(currentUrl);
            update_bookmark_button(ctx, is_url_bookmarked(currentUrl));
            update_security_indicator(ctx, currentUrl);
            update_nav_buttons_state(ctx);
            return;
        }
    }
    auto *currentView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (currentView) {
        currentUrl = currentView->url().toString();
        if (currentUrl == "attis:home") {
            ctx->urlBar->setText("");
            ctx->urlBar->setPlaceholderText(ctx->useGoogleSearch
                ? "Search Google or type a URL... (Ctrl+L)"
                : "Search DuckDuckGo or type a URL... (Ctrl+L)");
            update_bookmark_button(ctx, false);
        } else if (currentUrl == "attis:settings") {
            ctx->urlBar->setText("attis:settings");
            update_bookmark_button(ctx, false);
        } else {
            ctx->urlBar->setText(currentUrl);
            update_bookmark_button(ctx, is_url_bookmarked(currentUrl));
        }
        update_security_indicator(ctx, currentUrl);
        update_nav_buttons_state(ctx);
    }
}

static void adjust_zoom(BrowserContext *ctx, float delta) {
    if (!ctx || !ctx->viewStack) return;
    auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (view) {
        float z = view->zoomFactor() + delta;
        if (z < 0.25f) z = 0.25f;
        if (z > 3.0f) z = 3.0f;
        ctx->currentZoom = z;
        view->setZoomFactor(z);
        show_status_toast(ctx, QString("Zoom: %1%").arg(static_cast<int>(z * 100.0f)), 1500);
    }
}

// Smart Memory: discard/suspend inactive background tabs unless playing audio/video.
// Ambang waktu & jumlah tab dapat diubah dari Pengaturan (config.json).
static void run_smart_memory_cleanup(BrowserContext *ctx) {
    if (!ctx || !ctx->tabBar || !ctx->viewStack || ctx->tabs.size() <= 1) return;
    if (!ctx->suspendEnabled) return;

    int activeIdx = ctx->tabBar->currentIndex();
    qint64 now = QDateTime::currentSecsSinceEpoch();
    int suspendedCount = 0;

    for (size_t i = 0; i < ctx->tabs.size(); ++i) {
        if (static_cast<int>(i) == activeIdx) {
            ctx->tabs[i].lastActive = now;
            continue;
        }

        auto &meta = ctx->tabs[i];
        if (meta.isSuspended) continue;
        if (!meta.view || !meta.view->page()) continue;

        // Streaming audio/video protection: NEVER suspend playing media
        if (meta.view->page()->recentlyAudible()) {
            continue;
        }

        // Suspend lebih cepat saat tab terbuka melebihi ambang (banyak tab).
        bool manyTabs = static_cast<int>(ctx->tabs.size()) > ctx->suspendManyThreshold;
        qint64 threshold = manyTabs ? ctx->suspendManySecs : ctx->suspendAfterSecs;
        if (now - meta.lastActive > threshold) {
            QString currentUrl = meta.view->url().toString();
            if (!currentUrl.isEmpty() && currentUrl != "about:blank" && currentUrl != "attis:home" && currentUrl != "attis:settings") {
                meta.savedUrl = currentUrl;
                meta.savedTitle = ctx->tabBar->tabText(static_cast<int>(i));
                meta.isSuspended = true;

                meta.view->load(QUrl("about:blank"));
                ctx->tabBar->setTabText(static_cast<int>(i), "[Suspended] " + meta.savedTitle);
                suspendedCount++;
            }
        }
    }

    if (suspendedCount > 0) {
        show_status_toast(ctx, QString("Smart Memory: suspended %1 tab(s) to save RAM").arg(suspendedCount), 2500);
    }
}

static const char* GOOGLE_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'>"
    "<path fill='#EA4335' d='M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z'/>"
    "<path fill='#4285F4' d='M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z'/>"
    "<path fill='#FBBC05' d='M10.53 28.59c-.48-1.45-.76-2.99-.76-4.59s.27-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z'/>"
    "<path fill='#34A853' d='M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z'/>"
    "</svg>";

static const char* DDG_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 48'>"
    "<circle cx='24' cy='24' r='23' fill='#DE5833'/>"
    "<path fill='#FFF' d='M34 23c-1.5-3.5-5-5-10-5s-8 2-9 5c-1 3 0 7 3 9 3 2 9 3 13 1 4-2 4-7 3-10z'/>"
    "<circle cx='20' cy='22' r='2' fill='#222'/>"
    "<path fill='#F9A03F' d='M15 25c-3 1-5 4-4 6 1 1 4 0 6-2l-2-4z'/>"
    "<path fill='#4BA3E3' d='M22 34c2 0 5-1 7-3l-2-2c-2 1-4 1-5 1v4z'/>"
    "</svg>";

static const char* LOCK_HTTPS_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#a6e3a1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<rect x='3' y='11' width='18' height='11' rx='2' ry='2'/>"
    "<path d='M7 11V7a5 5 0 0 1 10 0v4'/>"
    "</svg>";

static const char* UNLOCK_HTTP_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#f38ba8' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<rect x='3' y='11' width='18' height='11' rx='2' ry='2'/>"
    "<path d='M7 11V7a5 5 0 0 1 9.9-1'/>"
    "</svg>";

static const char* INTERNAL_PAGE_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#8b5cf6' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<circle cx='12' cy='12' r='10'/>"
    "<line x1='12' y1='16' x2='12' y2='12'/>"
    "<line x1='12' y1='8' x2='12.01' y2='8'/>"
    "</svg>";

static void update_security_indicator(BrowserContext *ctx, const QString &url) {
    if (!ctx || !ctx->securityAction) return;
    QPixmap px;
    if (url.startsWith("https://")) {
        px.loadFromData(reinterpret_cast<const uchar*>(LOCK_HTTPS_SVG), static_cast<uint>(strlen(LOCK_HTTPS_SVG)), "SVG");
        ctx->securityAction->setIcon(QIcon(px));
        ctx->securityAction->setToolTip("Secure Connection (Encrypted HTTPS)");
    } else if (url.startsWith("http://")) {
        px.loadFromData(reinterpret_cast<const uchar*>(UNLOCK_HTTP_SVG), static_cast<uint>(strlen(UNLOCK_HTTP_SVG)), "SVG");
        ctx->securityAction->setIcon(QIcon(px));
        ctx->securityAction->setToolTip("Unencrypted Connection (HTTP)");
    } else {
        px.loadFromData(reinterpret_cast<const uchar*>(INTERNAL_PAGE_SVG), static_cast<uint>(strlen(INTERNAL_PAGE_SVG)), "SVG");
        ctx->securityAction->setIcon(QIcon(px));
        ctx->securityAction->setToolTip("Attis Internal Page");
    }
}

static void update_search_engine_button(BrowserContext *ctx) {
    if (!ctx) return;
    QPixmap px;
    if (ctx->useGoogleSearch) {
        px.loadFromData(reinterpret_cast<const uchar*>(GOOGLE_SVG), static_cast<uint>(strlen(GOOGLE_SVG)), "SVG");
        if (ctx->engineAction) {
            ctx->engineAction->setIcon(QIcon(px));
            ctx->engineAction->setToolTip("Google Search aktif (Klik untuk ganti ke DuckDuckGo)");
        }
    } else {
        px.loadFromData(reinterpret_cast<const uchar*>(DDG_SVG), static_cast<uint>(strlen(DDG_SVG)), "SVG");
        if (ctx->engineAction) {
            ctx->engineAction->setIcon(QIcon(px));
            ctx->engineAction->setToolTip("DuckDuckGo aktif (Klik untuk ganti ke Google)");
        }
    }
}

static QString generate_start_page_html(BrowserContext *ctx) {
    if (!ctx) return QString();
    char* raw = rust_get_start_page(
        ctx->accentColor.toUtf8().constData(),
        ctx->fgColor.toUtf8().constData(),
        ctx->bgColor.toUtf8().constData(),
        ctx->surfaceColor.toUtf8().constData(),
        ctx->borderColor.toUtf8().constData(),
        ctx->useGoogleSearch,
        ctx->showHomeClock,
        ctx->showHomeSearch,
        ctx->showHomeTiles
    );
    if (!raw) return QString();
    QString res = QString::fromUtf8(raw);
    rust_free_c_str(raw);
    return res;
}

static QString generate_dialog_stylesheet(const QString &bg, const QString &fg, const QString &accent) {
    QString safeBg = sanitize_qss_color(bg, "#121214");
    QString safeFg = sanitize_qss_color(fg, "#e8e8ea");
    QString safeAccent = sanitize_qss_color(accent, "#8b5cf6");
    QColor bgCol(safeBg);
    bool isLight = bgCol.isValid() && bgCol.lightnessF() > 0.5;
    QString inputBg = isLight ? "rgba(0, 0, 0, 0.04)" : "rgba(255, 255, 255, 0.06)";
    QString borderCol = isLight ? "rgba(0, 0, 0, 0.15)" : "rgba(255, 255, 255, 0.15)";
    QString btnBg = isLight ? "rgba(0, 0, 0, 0.07)" : "rgba(255, 255, 255, 0.08)";
    QString btnHover = isLight ? "rgba(0, 0, 0, 0.12)" : "rgba(255, 255, 255, 0.14)";

    return QString(R"(
QDialog {
    background-color: %1;
    color: %2;
}
QLabel {
    color: %2;
}
QLineEdit, QComboBox, QDoubleSpinBox {
    background-color: %4;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 6px 10px;
}
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus {
    border: 1px solid %3;
}
QComboBox QAbstractItemView {
    background-color: %1;
    color: %2;
    selection-background-color: %3;
    selection-color: %1;
    border: 1px solid %5;
}
QCheckBox {
    color: %2;
}
QPushButton {
    background-color: %6;
    color: %2;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 6px 14px;
    min-width: 70px;
}
QPushButton:hover {
    background-color: %7;
}
QPushButton:pressed {
    background-color: %3;
    color: %1;
}
QListWidget {
    background-color: %4;
    color: %2;
    border: 1px solid %5;
    border-radius: 8px;
    padding: 4px;
}
QListWidget::item {
    padding: 7px 10px;
    border-radius: 6px;
}
QListWidget::item:hover {
    background-color: %6;
}
QListWidget::item:selected {
    background-color: %3;
    color: %1;
}
)").arg(safeBg, safeFg, safeAccent, inputBg, borderCol, btnBg, btnHover);
}

static const char* STAR_EMPTY_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='#cdd6f4' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<polygon points='12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26'/>"
    "</svg>";

static const char* STAR_FILLED_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='#f9e2af' stroke='#f9e2af' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>"
    "<polygon points='12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26'/>"
    "</svg>";

static bool is_url_bookmarked(const QString &url) {
    if (url.isEmpty() || url.startsWith("attis:") || url.startsWith("about:")) return false;
    QString bmPath = attis_config_dir() + "/bookmarks.json";
    QFile f(bmPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return false;
    for (const auto &val : doc.array()) {
        if (val.toObject()["url"].toString() == url) {
            return true;
        }
    }
    return false;
}

static void update_bookmark_button(BrowserContext *ctx, bool isBookmarked) {
    if (!ctx || !ctx->bookmarkAction) return;
    QPixmap px;
    const char *svg = isBookmarked ? STAR_FILLED_SVG : STAR_EMPTY_SVG;
    px.loadFromData(reinterpret_cast<const uchar*>(svg), static_cast<uint>(strlen(svg)), "SVG");
    ctx->bookmarkAction->setIcon(QIcon(px));
    ctx->bookmarkAction->setToolTip(isBookmarked ? "Remove from Bookmarks (Ctrl+D)" : "Add to Bookmarks (Ctrl+D)");
}

static void toggle_bookmark_current_page(BrowserContext *ctx) {
    if (!ctx || !ctx->viewStack) return;
    auto *curView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (!curView) return;
    QString url = curView->url().toString();
    QString title = curView->title();
    if (title.isEmpty()) title = url;
    if (url.isEmpty() || url.startsWith("attis:") || url.startsWith("about:")) return;

    QString bmPath = attis_config_dir() + "/bookmarks.json";
    QFile f(bmPath);
    QJsonArray arr;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) arr = doc.array();
        f.close();
    }

    bool found = false;
    QJsonArray newArr;
    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        if (obj["url"].toString() == url) {
            found = true;
        } else {
            newArr.append(obj);
        }
    }

    if (!found) {
        QJsonObject item;
        item["title"] = title;
        item["url"] = url;
        newArr.append(item);
    }

    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(newArr).toJson());
        f.close();
    }

    update_bookmark_button(ctx, !found);
    show_status_toast(ctx, found ? "Dihapus dari Bookmark" : "Disimpan ke Bookmark", 2000);
}

static void show_bookmarks_dialog(BrowserContext *ctx) {
    if (!ctx || !ctx->window) return;
    QDialog dlg(ctx->window);
    dlg.setWindowTitle("Bookmarks");
#ifdef Q_OS_WIN
    apply_dark_title_bar(&dlg, is_dark_hex_color(ctx->bgColor));
#endif
    dlg.resize(520, 380);
    dlg.setStyleSheet(generate_dialog_stylesheet(ctx->bgColor, ctx->fgColor, ctx->accentColor));

    auto *layout = new QVBoxLayout(&dlg);
    auto *searchBox = new QLineEdit(&dlg);
    searchBox->setPlaceholderText("Search bookmark titles or URLs...");
    layout->addWidget(searchBox);

    auto *listWidget = new QListWidget(&dlg);

    QString bmPath = attis_config_dir() + "/bookmarks.json";
    QFile f(bmPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            for (const auto &val : doc.array()) {
                QJsonObject obj = val.toObject();
                QString title = obj["title"].toString();
                QString url = obj["url"].toString();
                auto *item = new QListWidgetItem(QString("%1\n%2").arg(title, url), listWidget);
                item->setData(Qt::UserRole, url);
            }
        }
    }

    layout->addWidget(listWidget);

    QObject::connect(searchBox, &QLineEdit::textChanged, [listWidget](const QString &q) {
        for (int i = 0; i < listWidget->count(); ++i) {
            auto *it = listWidget->item(i);
            it->setHidden(!q.isEmpty() && !it->text().contains(q, Qt::CaseInsensitive));
        }
    });

    auto *btnBox = new QHBoxLayout();
    auto *openBtn = new QPushButton("Open", &dlg);
    auto *newTabBtn = new QPushButton("Open in New Tab", &dlg);
    auto *delBtn = new QPushButton("Delete", &dlg);
    auto *closeBtn = new QPushButton("Close", &dlg);
    btnBox->addWidget(openBtn);
    btnBox->addWidget(newTabBtn);
    btnBox->addWidget(delBtn);
    btnBox->addStretch();
    btnBox->addWidget(closeBtn);
    layout->addLayout(btnBox);

    auto openCurrent = [&]() {
        auto *item = listWidget->currentItem();
        if (item) {
            QString url = item->data(Qt::UserRole).toString();
            browser_navigate(ctx, url.toUtf8().constData());
            dlg.accept();
        }
    };

    QObject::connect(openBtn, &QPushButton::clicked, openCurrent);
    QObject::connect(listWidget, &QListWidget::itemDoubleClicked, openCurrent);
    QObject::connect(newTabBtn, &QPushButton::clicked, [&]() {
        auto *item = listWidget->currentItem();
        if (item) {
            QString url = item->data(Qt::UserRole).toString();
            browser_add_tab(ctx, url.toUtf8().constData());
            dlg.accept();
        }
    });

    QObject::connect(delBtn, &QPushButton::clicked, [&]() {
        int row = listWidget->currentRow();
        if (row < 0) return;
        delete listWidget->takeItem(row);

        QJsonArray newArr;
        for (int i = 0; i < listWidget->count(); ++i) {
            auto *item = listWidget->item(i);
            QString text = item->text();
            QString url = item->data(Qt::UserRole).toString();
            QString title = text.section('\n', 0, 0);
            QJsonObject obj;
            obj["title"] = title;
            obj["url"] = url;
            newArr.append(obj);
        }
        QFile wf(bmPath);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            wf.write(QJsonDocument(newArr).toJson());
        }
        update_url_bar_for_current_tab(ctx);
    });

    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

static void show_history_dialog(BrowserContext *ctx) {
    if (!ctx || !ctx->window) return;
    QDialog dlg(ctx->window);
    dlg.setWindowTitle("Browsing History");
#ifdef Q_OS_WIN
    apply_dark_title_bar(&dlg, is_dark_hex_color(ctx->bgColor));
#endif
    dlg.resize(560, 420);
    dlg.setStyleSheet(generate_dialog_stylesheet(ctx->bgColor, ctx->fgColor, ctx->accentColor));

    auto *layout = new QVBoxLayout(&dlg);
    auto *searchBox = new QLineEdit(&dlg);
    searchBox->setPlaceholderText("Search browsing history...");
    layout->addWidget(searchBox);

    auto *listWidget = new QListWidget(&dlg);

    QString histPath = attis_config_dir() + "/history.json";
    QFile f(histPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (int i = arr.size() - 1; i >= 0; --i) {
                QJsonObject obj = arr[i].toObject();
                QString title = obj["title"].toString();
                QString url = obj["url"].toString();
                qint64 ts = static_cast<qint64>(obj["timestamp"].toDouble(0));
                QString timeStr = ts > 0
                    ? QDateTime::fromSecsSinceEpoch(ts).toString("dd MMM HH:mm")
                    : "";
                QString header = timeStr.isEmpty() ? (title.isEmpty() ? url : title) : QString("[%1] %2").arg(timeStr, title.isEmpty() ? url : title);
                auto *item = new QListWidgetItem(QString("%1\n%2").arg(header, url), listWidget);
                item->setData(Qt::UserRole, url);
            }
        }
    }

    layout->addWidget(listWidget);

    QObject::connect(searchBox, &QLineEdit::textChanged, [listWidget](const QString &q) {
        for (int i = 0; i < listWidget->count(); ++i) {
            auto *it = listWidget->item(i);
            it->setHidden(!q.isEmpty() && !it->text().contains(q, Qt::CaseInsensitive));
        }
    });

    auto *btnBox = new QHBoxLayout();
    auto *openBtn = new QPushButton("Open", &dlg);
    auto *newTabBtn = new QPushButton("Open in New Tab", &dlg);
    auto *clearBtn = new QPushButton("Clear All", &dlg);
    auto *closeBtn = new QPushButton("Close", &dlg);
    btnBox->addWidget(openBtn);
    btnBox->addWidget(newTabBtn);
    btnBox->addWidget(clearBtn);
    btnBox->addStretch();
    btnBox->addWidget(closeBtn);
    layout->addLayout(btnBox);

    auto openCurrent = [&]() {
        auto *item = listWidget->currentItem();
        if (item) {
            QString url = item->data(Qt::UserRole).toString();
            browser_navigate(ctx, url.toUtf8().constData());
            dlg.accept();
        }
    };

    QObject::connect(openBtn, &QPushButton::clicked, openCurrent);
    QObject::connect(listWidget, &QListWidget::itemDoubleClicked, openCurrent);
    QObject::connect(newTabBtn, &QPushButton::clicked, [&]() {
        auto *item = listWidget->currentItem();
        if (item) {
            QString url = item->data(Qt::UserRole).toString();
            browser_add_tab(ctx, url.toUtf8().constData());
            dlg.accept();
        }
    });

    QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
        listWidget->clear();
        rust_clear_history();
        show_status_toast(ctx, "Browsing history cleared", 2000);
    });

    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

static void toggle_find_bar(BrowserContext *ctx, bool show) {
    if (!ctx || !ctx->findBar || !ctx->findInput) return;
    if (show) {
        ctx->findBar->show();
        ctx->findInput->setFocus();
        ctx->findInput->selectAll();
    } else {
        ctx->findBar->hide();
        if (ctx->viewStack) {
            auto *curView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
            if (curView) curView->findText("");
        }
    }
}

static void trigger_find_text(BrowserContext *ctx, bool backward = false) {
    if (!ctx || !ctx->viewStack || !ctx->findInput) return;
    auto *curView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (!curView) return;
    QString query = ctx->findInput->text();
    if (query.isEmpty()) {
        curView->findText("");
        if (ctx->findStatusLabel) ctx->findStatusLabel->setText("");
        return;
    }
    QWebEnginePage::FindFlags flags;
    if (backward) flags |= QWebEnginePage::FindBackward;
    curView->findText(query, flags, [ctx](const QWebEngineFindTextResult &res) {
        if (ctx->findStatusLabel) {
            ctx->findStatusLabel->setText(QString("%1 / %2").arg(res.activeMatch()).arg(res.numberOfMatches()));
        }
    });
}

static void open_downloads_folder(BrowserContext *ctx) {
    QString targetDir = ctx ? ctx->downloadDir : "";
    if (targetDir.isEmpty()) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (targetDir.isEmpty()) {
            targetDir = QDir::homePath() + "/Downloads";
        }
    }
    QDir().mkpath(targetDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetDir));
}

static void show_downloads_dialog(BrowserContext *ctx) {
    if (!ctx || !ctx->window) return;
    QDialog dlg(ctx->window);
    dlg.setWindowTitle(" Download Manager");
#ifdef Q_OS_WIN
    apply_dark_title_bar(&dlg, is_dark_hex_color(ctx->bgColor));
#endif
    dlg.resize(520, 340);
    dlg.setStyleSheet(generate_dialog_stylesheet(ctx->bgColor, ctx->fgColor, ctx->accentColor));

    auto *layout = new QVBoxLayout(&dlg);
    auto *listWidget = new QListWidget(&dlg);

    if (ctx->downloads.empty()) {
        listWidget->addItem("No downloads in this session yet.");
    } else {
        for (auto it = ctx->downloads.rbegin(); it != ctx->downloads.rend(); ++it) {
            QString kb = it->totalBytes > 0
                ? QString("%1 KB / %2 KB").arg(it->receivedBytes / 1024).arg(it->totalBytes / 1024)
                : QString("%1 KB").arg(it->receivedBytes / 1024);
            listWidget->addItem(QString("%1 — %2 (%3)").arg(it->fileName, it->stateText, kb));
        }
    }
    layout->addWidget(listWidget);

    auto *btnBox = new QHBoxLayout();
    auto *folderBtn = new QPushButton("Open Downloads Folder", &dlg);
    auto *closeBtn = new QPushButton("Close", &dlg);
    btnBox->addWidget(folderBtn);
    btnBox->addStretch();
    btnBox->addWidget(closeBtn);
    layout->addLayout(btnBox);

    QObject::connect(folderBtn, &QPushButton::clicked, [&]() {
        open_downloads_folder(ctx);
        dlg.accept();
    });
    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

static void save_browser_config(BrowserContext *ctx, const QString &homepage = "") {
    if (!ctx) return;
    QString configPath = attis_config_dir() + "/config.json";
    QJsonObject root;
    QFile rf(configPath);
    if (rf.open(QIODevice::ReadOnly)) {
        QJsonDocument d = QJsonDocument::fromJson(rf.readAll());
        if (d.isObject()) root = d.object();
        rf.close();
    }

    root["theme"] = ctx->currentTheme;
    root["tab_position"] = ctx->tabPosition;
    root["search_engine"] = ctx->useGoogleSearch ? "https://www.google.com/search?q=%s" : "https://html.duckduckgo.com/html/?q=%s";
    if (!homepage.isEmpty()) {
        root["homepage"] = homepage;
    } else if (!root.contains("homepage")) {
        root["homepage"] = "attis:home";
    }
    root["show_nav_buttons"] = (ctx->backBtn && ctx->backBtn->isVisible());
    root["show_adblock_badge"] = (ctx->adblockBadge && ctx->adblockBadge->isVisible());
    root["show_new_tab_button"] = ctx->showNewTabButton;
    root["default_zoom"] = ctx->currentZoom;
    root["download_dir"] = ctx->downloadDir;
    root["enable_adblock"] = ctx->enableAdblock;
    root["enable_cosmetic_filter"] = ctx->enableCosmeticFilter;
    if (!ctx->customUserAgent.isEmpty()) {
        root["user_agent"] = ctx->customUserAgent;
    } else if (!root.contains("user_agent")) {
        root["user_agent"] = "";
    }
    root["bg_color"] = ctx->bgColor;
    root["surface_color"] = ctx->surfaceColor;
    root["fg_color"] = ctx->fgColor;
    root["accent_color"] = ctx->accentColor;
    root["border_color"] = ctx->borderColor;
    root["bg_opacity"] = ctx->bgOpacity;
    root["ui_density"] = ctx->uiDensity;
    root["show_home_search"] = ctx->showHomeSearch;
    root["show_home_tiles"] = ctx->showHomeTiles;
    root["show_home_clock"] = ctx->showHomeClock;
    root["suspend_enabled"] = ctx->suspendEnabled;
    root["suspend_after_seconds"] = ctx->suspendAfterSecs;
    root["suspend_many_tabs_after_seconds"] = ctx->suspendManySecs;
    root["suspend_many_tabs_threshold"] = ctx->suspendManyThreshold;
    root["http_cache_size_mb"] = ctx->httpCacheSizeMb;
    root["restore_session"] = ctx->restoreSession;
    root["filter_auto_update"] = ctx->filterAutoUpdate;
    if (ctx->window) {
        // Persist the maximized state, and the restored (non-maximized)
        // size — saving the maximized geometry would pin the window to
        // full-screen size even after the user un-maximizes it.
        root["window_maximized"] = ctx->window->isMaximized();
        QRect normal = ctx->window->normalGeometry();
        root["window_width"] = normal.width();
        root["window_height"] = normal.height();
    }

    QFile wf(configPath);
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        wf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        wf.close();
    }
}

static QString generate_settings_page_html(BrowserContext *ctx);

static void refresh_internal_tabs(BrowserContext *ctx) {
    if (!ctx) return;
    for (auto &t : ctx->tabs) {
        if (!t.view) continue;
        if (t.savedUrl == "attis:settings") {
            t.view->page()->setBackgroundColor(Qt::transparent);
            t.view->setHtml(generate_settings_page_html(ctx), QUrl("attis:settings"));
        } else if (t.savedUrl == "attis:home") {
            t.view->page()->setBackgroundColor(Qt::transparent);
            t.view->setHtml(generate_start_page_html(ctx), QUrl("attis:home"));
        }
    }
}

static QString generate_settings_page_html(BrowserContext *ctx) {
    if (!ctx) return QString();
    QString hp = "attis:home";
    QString configPath = attis_config_dir() + "/config.json";
    QFile f(configPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject() && doc.object().contains("homepage")) {
            hp = doc.object()["homepage"].toString();
        }
    }
    char* raw = rust_get_settings_page(
        ctx->currentTheme.toUtf8().constData(),
        ctx->bgColor.toUtf8().constData(),
        ctx->surfaceColor.toUtf8().constData(),
        ctx->fgColor.toUtf8().constData(),
        ctx->accentColor.toUtf8().constData(),
        ctx->borderColor.toUtf8().constData(),
        ctx->bgOpacity,
        ctx->uiDensity.toUtf8().constData(),
        ctx->tabPosition.toUtf8().constData(),
        ctx->currentZoom,
        ctx->useGoogleSearch,
        ctx->showHomeClock,
        ctx->showHomeSearch,
        ctx->showHomeTiles,
        ctx->customChromeQss.toUtf8().constData(),
        hp.toUtf8().constData()
    );
    if (!raw) return QString();
    QString res = QString::fromUtf8(raw);
    rust_free_c_str(raw);
    return res;
}

static void open_settings_tab(BrowserContext *ctx);

static void save_allowlist_config(BrowserContext *ctx, const QString &csv) {
    if (!ctx) return;
    QString configPath = attis_config_dir() + "/config.json";
    QJsonObject root;
    QFile rf(configPath);
    if (rf.open(QIODevice::ReadOnly)) {
        QJsonDocument d = QJsonDocument::fromJson(rf.readAll());
        if (d.isObject()) root = d.object();
        rf.close();
    }
    QJsonArray arr;
    const QStringList parts = csv.split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString domain = part.trimmed().toLower();
        if (!domain.isEmpty()) arr.append(domain);
    }
    root["adblock_allowlist"] = arr;
    QFile wf(configPath);
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        wf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        wf.close();
    }
}

static void handle_settings_action(BrowserContext *ctx, const QUrl &url) {
    if (!ctx) return;
    QString action = url.host();
    if (action.isEmpty()) {
        action = url.path();
        if (action.startsWith('/')) action = action.mid(1);
    }
    QUrlQuery q(url);

    if (action == "open_settings") {
        open_settings_tab(ctx);
    } else if (action == "theme") {
        QString val = q.queryItemValue("val");
        browser_set_theme(ctx, val.toUtf8().constData(), nullptr);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "accent_color") {
        QString val = q.queryItemValue("val", QUrl::FullyDecoded).trimmed();
        if (!val.isEmpty()) {
            ctx->accentColor = sanitize_qss_color(val, ctx->accentColor);
            apply_theme_qss(ctx);
            save_browser_config(ctx);
            refresh_internal_tabs(ctx);
        }
    } else if (action == "bg_opacity") {
        int pct = q.queryItemValue("val").toInt();
        if (pct >= 20 && pct <= 100) {
            ctx->bgOpacity = pct / 100.0f;
            apply_theme_qss(ctx);
            save_browser_config(ctx);
        }
    } else if (action == "tab_position") {
        QString val = q.queryItemValue("val");
        ctx->tabPosition = val;
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "ui_density") {
        QString val = q.queryItemValue("val");
        ctx->uiDensity = val;
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "home_toggle") {
        QString elem = q.queryItemValue("element");
        bool val = (q.queryItemValue("val") == "1");
        if (elem == "clock") ctx->showHomeClock = val;
        else if (elem == "search") ctx->showHomeSearch = val;
        else if (elem == "tiles") ctx->showHomeTiles = val;
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "tokens") {
        QString bg = q.queryItemValue("bg", QUrl::FullyDecoded).trimmed();
        QString surface = q.queryItemValue("surface", QUrl::FullyDecoded).trimmed();
        QString fg = q.queryItemValue("fg", QUrl::FullyDecoded).trimmed();
        QString accent = q.queryItemValue("accent", QUrl::FullyDecoded).trimmed();
        QString border = q.queryItemValue("border", QUrl::FullyDecoded).trimmed();
        if (!bg.isEmpty()) ctx->bgColor = sanitize_qss_color(bg, ctx->bgColor);
        if (!surface.isEmpty()) ctx->surfaceColor = sanitize_qss_color(surface, ctx->surfaceColor);
        if (!fg.isEmpty()) ctx->fgColor = sanitize_qss_color(fg, ctx->fgColor);
        if (!accent.isEmpty()) ctx->accentColor = sanitize_qss_color(accent, ctx->accentColor);
        if (!border.isEmpty()) ctx->borderColor = sanitize_qss_color(border, ctx->borderColor);
        ctx->currentTheme = "custom";
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "custom_qss") {
        QString qss = q.queryItemValue("qss", QUrl::FullyDecoded);
        ctx->customChromeQss = qss;
        QString userQssPath = attis_config_dir() + "/user_chrome.qss";
        QFile f(userQssPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(qss.toUtf8());
            f.close();
        }
        apply_theme_qss(ctx);
        save_browser_config(ctx);
    } else if (action == "reset_defaults" || action == "reset") {
        ctx->currentTheme = "violet";
        ctx->bgColor = "#121214";
        ctx->surfaceColor = "#1c1c1f";
        ctx->fgColor = "#e8e8ea";
        ctx->accentColor = "#8b5cf6";
        ctx->borderColor = "#2e2e33";
        ctx->bgOpacity = 0.88f;
        ctx->uiDensity = "comfortable";
        ctx->tabPosition = "top";
        ctx->showHomeClock = true;
        ctx->showHomeSearch = true;
        ctx->showHomeTiles = true;
        ctx->suspendEnabled = true;
        ctx->suspendAfterSecs = 120;
        ctx->suspendManySecs = 45;
        ctx->suspendManyThreshold = 3;
        ctx->httpCacheSizeMb = 20;
        ctx->restoreSession = true;
        ctx->filterAutoUpdate = true;
        ctx->customChromeQss = "";
        QString userQssPath = attis_config_dir() + "/user_chrome.qss";
        QFile::remove(userQssPath);
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "engine" || action == "search_engine") {
        QString val = q.queryItemValue("val");
        ctx->useGoogleSearch = (val == "google");
        ctx->customSearchEngine.clear();
        update_search_engine_button(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "nav_buttons") {
        bool show = (q.queryItemValue("val") == "1");
        if (ctx->backBtn) ctx->backBtn->setVisible(show);
        if (ctx->forwardBtn) ctx->forwardBtn->setVisible(show);
        if (ctx->reloadBtn) ctx->reloadBtn->setVisible(show);
        save_browser_config(ctx);
    } else if (action == "adblock_enable") {
        bool en = (q.queryItemValue("val") == "1");
        ctx->enableAdblock = en;
        rust_set_adblock_enabled(en);
        save_browser_config(ctx);
        show_status_toast(ctx, en ? "uBlock Origin Diaktifkan" : "uBlock Origin Dinonaktifkan", 2000);
    } else if (action == "adblock_badge") {
        bool show = (q.queryItemValue("val") == "1");
        if (ctx->adblockBadge) ctx->adblockBadge->setVisible(show);
        save_browser_config(ctx);
    } else if (action == "zoom") {
        float z = q.queryItemValue("val").toFloat();
        if (z >= 0.5f && z <= 3.0f) {
            ctx->currentZoom = z;
            if (ctx->viewStack) {
                for (int i = 0; i < ctx->viewStack->count(); ++i) {
                    auto *v = qobject_cast<QWebEngineView*>(ctx->viewStack->widget(i));
                    if (v) v->setZoomFactor(z);
                }
            }
            save_browser_config(ctx);
        }
    } else if (action == "homepage") {
        QString hp = q.queryItemValue("val", QUrl::FullyDecoded).trimmed();
        save_browser_config(ctx, hp);
        show_status_toast(ctx, "Home page saved", 2000);
    } else if (action == "open_downloads") {
        open_downloads_folder(ctx);
    } else if (action == "clear_history") {
        rust_clear_history();
        show_status_toast(ctx, "History cleared", 2000);
    } else if (action == "clear_cache") {
        QWebEngineProfile::defaultProfile()->clearHttpCache();
        show_status_toast(ctx, "HTTP cache cleared", 2000);
    } else if (action == "filter_update") {
        rust_update_filters_async();
        show_status_toast(ctx, "Updating filter lists in the background...", 2500);
    } else if (action == "filter_auto") {
        ctx->filterAutoUpdate = (q.queryItemValue("val") == "1");
        save_browser_config(ctx);
    } else if (action == "restore_session") {
        ctx->restoreSession = (q.queryItemValue("val") == "1");
        save_browser_config(ctx);
    } else if (action == "suspend_toggle") {
        ctx->suspendEnabled = (q.queryItemValue("val") == "1");
        save_browser_config(ctx);
        show_status_toast(ctx, ctx->suspendEnabled ? "Smart Memory diaktifkan" : "Smart Memory dinonaktifkan", 2000);
    } else if (action == "suspend_timeout") {
        int secs = q.queryItemValue("val").toInt();
        if (secs >= 30 && secs <= 3600) {
            ctx->suspendAfterSecs = secs;
            save_browser_config(ctx);
        }
    } else if (action == "suspend_many") {
        int secs = q.queryItemValue("secs").toInt();
        int threshold = q.queryItemValue("threshold").toInt();
        if (secs >= 15 && secs <= 600) ctx->suspendManySecs = secs;
        if (threshold >= 1 && threshold <= 20) ctx->suspendManyThreshold = threshold;
        save_browser_config(ctx);
    } else if (action == "cache_size") {
        int mb = q.queryItemValue("val").toInt();
        if (mb >= 4 && mb <= 512) {
            ctx->httpCacheSizeMb = mb;
            QWebEngineProfile::defaultProfile()->setHttpCacheMaximumSize(static_cast<qint64>(mb) * 1024 * 1024);
            save_browser_config(ctx);
        }
    } else if (action == "allowlist_save") {
        QString csv = q.queryItemValue("val", QUrl::FullyDecoded).trimmed();
        save_allowlist_config(ctx, csv);
        rust_set_allowlist(csv.toUtf8().constData());
        show_status_toast(ctx, "Adblock allowlist saved", 2000);
    } else if (action == "smart_memory") {
        run_smart_memory_cleanup(ctx);
    }
}

bool BrowserPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) {
    if (url.scheme() == "attis-set") {
        // Privileged settings actions are only honored for navigations
        // that originate in the main frame of our own internal attis:
        // pages (in-page links and script navigation from the start and
        // settings pages). A website must never be able to trigger them,
        // so subframe navigations and navigations from web pages are
        // ignored. The navigation is always rejected, so the page never
        // actually navigates away or shows an error page.
        if (isMainFrame && this->url().scheme() == "attis") {
            handle_settings_action(m_ctx, url);
        }
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

QWebEnginePage* BrowserPage::createWindow(WebWindowType type) {
    (void)type;
    if (!m_ctx) return nullptr;
    bool currentPrivate = false;
    int curIdx = m_ctx->tabBar ? m_ctx->tabBar->currentIndex() : -1;
    if (curIdx >= 0 && curIdx < static_cast<int>(m_ctx->tabs.size())) {
        currentPrivate = m_ctx->tabs[curIdx].isPrivate;
    }
    int idx = create_tab_internal(m_ctx, "about:blank", currentPrivate);
    if (idx >= 0 && idx < static_cast<int>(m_ctx->tabs.size()) && m_ctx->tabs[idx].view) {
        return m_ctx->tabs[idx].view->page();
    }
    return nullptr;
}

static void open_settings_tab(BrowserContext *ctx) {
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return;

    for (size_t i = 0; i < ctx->tabs.size(); ++i) {
        if (ctx->tabs[i].savedUrl == "attis:settings") {
            ctx->tabBar->setCurrentIndex(static_cast<int>(i));
            ctx->viewStack->setCurrentIndex(static_cast<int>(i));
            return;
        }
    }

    int curIdx = ctx->tabBar->currentIndex();
    if (curIdx >= 0 && curIdx < static_cast<int>(ctx->tabs.size())) {
        if (ctx->tabs[curIdx].savedUrl.isEmpty() ||
            ctx->tabs[curIdx].savedUrl == "attis:home" ||
            ctx->tabs[curIdx].savedUrl == "about:blank") {
            browser_navigate(ctx, "attis:settings");
            return;
        }
    }

    browser_add_tab(ctx, "attis:settings");
}

static void show_settings_dialog(BrowserContext *ctx) {
    open_settings_tab(ctx);
}

static void restore_closed_tab(BrowserContext *ctx) {
    if (!ctx || ctx->closedTabs.empty()) {
        show_status_toast(ctx, "No closed tab to restore", 1800);
        return;
    }
    ClosedTabEntry last = ctx->closedTabs.back();
    ctx->closedTabs.pop_back();
    browser_add_tab(ctx, last.url.toUtf8().constData());
    show_status_toast(ctx, QString("Tab restored: %1").arg(last.title), 2000);
}

static void position_status_overlay(BrowserContext *ctx); // defined below, before browser_create

static void show_status_toast(BrowserContext *ctx, const QString &msg, int timeoutMs) {
    if (!ctx || !ctx->statusBarWidget || !ctx->statusToastLabel) return;
    ctx->statusToastLabel->setText(msg);
    position_status_overlay(ctx);
    ctx->statusBarWidget->show();
    QTimer::singleShot(timeoutMs, ctx->statusBarWidget, [ctx, msg]() {
        if (ctx->statusToastLabel && ctx->statusToastLabel->text() == msg) {
            ctx->statusToastLabel->clear();
            if (ctx->statusHoverLabel && ctx->statusHoverLabel->text().isEmpty()) {
                ctx->statusBarWidget->hide();
            }
        }
    });
}

static void setup_download_handler(BrowserContext *ctx, QWebEngineProfile *profile) {
    QObject::connect(profile, &QWebEngineProfile::downloadRequested, [ctx](QWebEngineDownloadRequest *download) {
        QString targetDir = ctx->downloadDir;
        if (targetDir.isEmpty()) {
            targetDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            if (targetDir.isEmpty()) {
                targetDir = QDir::homePath() + "/Downloads";
            }
        }
        QDir().mkpath(targetDir);
        download->setDownloadDirectory(targetDir);
        download->accept();

        DownloadItemMeta meta;
        meta.fileName = download->downloadFileName();
        meta.stateText = "Downloading...";
        meta.receivedBytes = 0;
        meta.totalBytes = download->totalBytes();
        ctx->downloads.push_back(meta);
        size_t dlIdx = ctx->downloads.size() - 1;

        show_status_toast(ctx, QString("⬇️ Downloading: %1").arg(meta.fileName), 3000);

        QObject::connect(download, &QWebEngineDownloadRequest::receivedBytesChanged, [ctx, download, dlIdx]() {
            if (dlIdx < ctx->downloads.size()) {
                ctx->downloads[dlIdx].receivedBytes = download->receivedBytes();
                ctx->downloads[dlIdx].totalBytes = download->totalBytes();
            }
        });

        QObject::connect(download, &QWebEngineDownloadRequest::stateChanged, [ctx, download, dlIdx](QWebEngineDownloadRequest::DownloadState state) {
            if (dlIdx >= ctx->downloads.size()) return;
            if (state == QWebEngineDownloadRequest::DownloadCompleted) {
                ctx->downloads[dlIdx].stateText = "Done";
                show_status_toast(ctx, QString("✅ Download finished: %1").arg(ctx->downloads[dlIdx].fileName), 4000);
            } else if (state == QWebEngineDownloadRequest::DownloadInterrupted) {
                ctx->downloads[dlIdx].stateText = "Failed";
                show_status_toast(ctx, QString("❌ Download failed: %1").arg(ctx->downloads[dlIdx].fileName), 4000);
            } else if (state == QWebEngineDownloadRequest::DownloadCancelled) {
                ctx->downloads[dlIdx].stateText = "Cancelled";
            }
        });
    });
}

// Positions the hover-URL/toast bar as a floating overlay anchored to the
// bottom-left of the central widget. The bar must NOT live in the main
// layout: showing/hiding it there resized the web view, so hovering any
// link made the whole page jump up and down. As an overlay, only the URL
// appears; the layout never moves.
static void position_status_overlay(BrowserContext *ctx) {
    if (!ctx || !ctx->statusBarWidget) return;
    QWidget *parent = ctx->statusBarWidget->parentWidget();
    if (!parent) return;
    const int margin = 8;
    ctx->statusBarWidget->setMaximumWidth(qMax(200, parent->width() - 2 * margin));
    ctx->statusBarWidget->adjustSize();
    const int x = margin;
    const int y = parent->height() - ctx->statusBarWidget->height() - margin;
    ctx->statusBarWidget->move(x, qMax(margin, y));
    ctx->statusBarWidget->raise();
}

// Keeps the status overlay anchored when the central widget resizes.
// No signals/slots, so no Q_OBJECT/moc needed.
class StatusOverlayAnchor : public QObject {
public:
    explicit StatusOverlayAnchor(BrowserContext *ctx)
        : QObject(ctx->statusBarWidget), m_ctx(ctx) {}
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Resize) {
            position_status_overlay(m_ctx);
        }
        return QObject::eventFilter(watched, event);
    }
private:
    BrowserContext *m_ctx;
};

BrowserHandle browser_create(const char* title, int width, int height, const char* config_dir) {
    // The Rust side owns the config directory (resolve_config_dir());
    // record it before anything reads a config file so every C++ path
    // below resolves to the same directory Rust uses. A null or empty
    // value keeps the legacy QStandardPaths fallback (defensive).
    if (config_dir && config_dir[0] != '\0') {
        g_config_dir = QDir::cleanPath(QString::fromUtf8(config_dir));
    }

    // 1. Ultra-Low RAM & Hardware Acceleration Optimization Flags
    // Gabungkan dengan flag yang sudah disetel sisi Rust/shell (mis.
    // --renderer-process-limit dari config); jangan timpa. Flag
    // --renderer-process-limit dimiliki sisi Rust dan tidak lagi
    // di-hardcode di sini.
    const char* kBaseFlags =
        "--enable-low-end-device-mode "
        "--disable-gpu-shader-disk-cache "
        "--disable-gpu-program-cache "
        "--disk-cache-size=20971520 "
        "--media-cache-size=20971520 "
        "--disable-speech-api "
        "--disable-breakpad "
        // Cap the V8 heap per renderer (YouTube's JS heap normally stays
        // well under this) and drop the Cast + Translate components, so a
        // background music tab costs less RAM. --aggressive-cache-discard
        // drops cached page resources early on low-memory systems.
        "--js-flags=--max-old-space-size=512 "
        "--disable-features=MediaRouter,Translate "
        "--aggressive-cache-discard "
        "--enable-features=VaapiVideoDecoder,VaapiVideoDecodeLinuxGL "
        "--enable-accelerated-video-decode";
    QByteArray existingFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").trimmed();
    QByteArray mergedFlags = existingFlags.isEmpty()
        ? QByteArray(kBaseFlags)
        : existingFlags + " " + kBaseFlags;
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", mergedFlags);

    auto *ctx = new BrowserContext();

    if (!qApp) {
        static int argc = 1;
        static char arg0[] = "attis";
        static char* argv[] = { arg0, nullptr };
        ctx->app = new QApplication(argc, argv);
        ctx->ownApp = true;
    } else {
        ctx->app = qApp;
    }

    ctx->window = new QMainWindow();
    ctx->window->setWindowTitle(QString::fromUtf8(title ? title : "Attis"));
    ctx->window->resize(width > 0 ? width : 1280, height > 0 ? height : 800);
    // Never open larger than the screen: a saved 1280x800 window overflows
    // a 1366x768 display, and a small laptop screen must still fit.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QSize avail = screen->availableGeometry().size();
        if (ctx->window->width() > avail.width() || ctx->window->height() > avail.height()) {
            ctx->window->resize(qMin(ctx->window->width(), avail.width()),
                                 qMin(ctx->window->height(), avail.height()));
        }
    }

    QString configPath = attis_config_dir();
    // One-time migration from the legacy directory (no-op on Linux and
    // when the directories already match); must run before any config
    // file is read or written below.
    migrate_legacy_config_dir(configPath);
    QDir().mkpath(configPath + "/cache");
    QDir().mkpath(configPath + "/storage");

    QString closeSvg = configPath + "/tab_close.svg";
    QString closeHoverSvg = configPath + "/tab_close_hover.svg";
    {
        QFile f(closeSvg);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><line x1=\"4\" y1=\"4\" x2=\"12\" y2=\"12\" stroke=\"#808499\" stroke-width=\"1.8\" stroke-linecap=\"round\"/><line x1=\"12\" y1=\"4\" x2=\"4\" y2=\"12\" stroke=\"#808499\" stroke-width=\"1.8\" stroke-linecap=\"round\"/></svg>");
        }
        QFile f2(closeHoverSvg);
        if (f2.open(QIODevice::WriteOnly)) {
            f2.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><line x1=\"4\" y1=\"4\" x2=\"12\" y2=\"12\" stroke=\"#ffffff\" stroke-width=\"2\" stroke-linecap=\"round\"/><line x1=\"12\" y1=\"4\" x2=\"4\" y2=\"12\" stroke=\"#ffffff\" stroke-width=\"2\" stroke-linecap=\"round\"/></svg>");
        }
    }

    // Load config.json into ctx if present
    QString cfgFile = configPath + "/config.json";
    QFile rf(cfgFile);
    if (rf.open(QIODevice::ReadOnly)) {
        QJsonDocument d = QJsonDocument::fromJson(rf.readAll());
        if (d.isObject()) {
            QJsonObject o = d.object();
            if (o.contains("theme")) ctx->currentTheme = o["theme"].toString();
            if (o.contains("bg_color")) ctx->bgColor = sanitize_qss_color(o["bg_color"].toString(), ctx->bgColor);
            if (o.contains("surface_color")) ctx->surfaceColor = sanitize_qss_color(o["surface_color"].toString(), ctx->surfaceColor);
            if (o.contains("fg_color")) ctx->fgColor = sanitize_qss_color(o["fg_color"].toString(), ctx->fgColor);
            if (o.contains("accent_color")) ctx->accentColor = sanitize_qss_color(o["accent_color"].toString(), ctx->accentColor);
            if (o.contains("border_color")) ctx->borderColor = sanitize_qss_color(o["border_color"].toString(), ctx->borderColor);
            if (o.contains("bg_opacity")) ctx->bgOpacity = static_cast<float>(o["bg_opacity"].toDouble(0.88));
            if (o.contains("ui_density")) ctx->uiDensity = o["ui_density"].toString();
            if (o.contains("tab_position")) ctx->tabPosition = o["tab_position"].toString();
            if (o.contains("show_home_search")) ctx->showHomeSearch = o["show_home_search"].toBool(true);
            if (o.contains("show_home_tiles")) ctx->showHomeTiles = o["show_home_tiles"].toBool(true);
            if (o.contains("show_home_clock")) ctx->showHomeClock = o["show_home_clock"].toBool(true);
            if (o.contains("show_new_tab_button")) ctx->showNewTabButton = o["show_new_tab_button"].toBool(true);
            if (o.contains("enable_adblock")) ctx->enableAdblock = o["enable_adblock"].toBool(true);
            if (o.contains("enable_cosmetic_filter")) ctx->enableCosmeticFilter = o["enable_cosmetic_filter"].toBool(true);
            if (o.contains("user_agent")) ctx->customUserAgent = o["user_agent"].toString();
            if (o.contains("search_engine")) {
                QString se = o["search_engine"].toString();
                ctx->customSearchEngine = se;
                ctx->useGoogleSearch = se.contains("google", Qt::CaseInsensitive);
            }
            if (o.contains("download_dir")) ctx->downloadDir = o["download_dir"].toString();
            if (o.contains("suspend_enabled")) ctx->suspendEnabled = o["suspend_enabled"].toBool(true);
            if (o.contains("suspend_after_seconds")) ctx->suspendAfterSecs = std::max(30, std::min(3600, o["suspend_after_seconds"].toInt(120)));
            if (o.contains("suspend_many_tabs_after_seconds")) ctx->suspendManySecs = std::max(15, std::min(600, o["suspend_many_tabs_after_seconds"].toInt(45)));
            if (o.contains("suspend_many_tabs_threshold")) ctx->suspendManyThreshold = std::max(1, std::min(20, o["suspend_many_tabs_threshold"].toInt(3)));
            if (o.contains("http_cache_size_mb")) ctx->httpCacheSizeMb = std::max(4, std::min(512, o["http_cache_size_mb"].toInt(20)));
            if (o.contains("restore_session")) ctx->restoreSession = o["restore_session"].toBool(true);
            if (o.contains("filter_auto_update")) ctx->filterAutoUpdate = o["filter_auto_update"].toBool(true);
            if (o.contains("window_maximized")) ctx->windowMaximized = o["window_maximized"].toBool(true);
        }
        rf.close();
    }

    QString userQssPath = configPath + "/user_chrome.qss";
    QFile qssFile(userQssPath);
    if (qssFile.open(QIODevice::ReadOnly)) {
        ctx->customChromeQss = QString::fromUtf8(qssFile.readAll());
        qssFile.close();
    }

    auto *profile = QWebEngineProfile::defaultProfile();
    profile->setCachePath(configPath + "/cache");
    profile->setPersistentStoragePath(configPath + "/storage");
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setHttpCacheMaximumSize(static_cast<qint64>(ctx->httpCacheSizeMb) * 1024 * 1024);

    // Off-the-record profile for Private / Incognito tabs
    ctx->privateProfile = new QWebEngineProfile(ctx->window);
    ctx->privateProfile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    ctx->privateProfile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    auto configureSettings = [](QWebEngineSettings *settings) {
        settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
        settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);
        settings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
        settings->setAttribute(QWebEngineSettings::WebGLEnabled, true);
        settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
        settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    };
    configureSettings(profile->settings());
    configureSettings(ctx->privateProfile->settings());

    ctx->interceptor = new BridgeInterceptor(ctx->window);
    profile->setUrlRequestInterceptor(ctx->interceptor);
    ctx->privateProfile->setUrlRequestInterceptor(ctx->interceptor);

    setup_download_handler(ctx, profile);
    setup_download_handler(ctx, ctx->privateProfile);

    // Main layout
    auto *centralWidget = new QWidget(ctx->window);
    ctx->mainLayout = new QVBoxLayout(centralWidget);
    ctx->mainLayout->setContentsMargins(0, 0, 0, 0);
    ctx->mainLayout->setSpacing(0);

    // 1. Tab Bar Row
    ctx->tabRowWidget = new QWidget(centralWidget);
    ctx->tabRowWidget->setObjectName("tabStrip");
    auto *tabRowLayout = new QHBoxLayout(ctx->tabRowWidget);
    tabRowLayout->setContentsMargins(12, 8, 12, 0);
    tabRowLayout->setSpacing(6);

    ctx->tabBar = new QTabBar(ctx->tabRowWidget);
    ctx->tabBar->setTabsClosable(true);
    ctx->tabBar->setMovable(true);
    ctx->tabBar->setExpanding(false);
    ctx->tabBar->setElideMode(Qt::ElideRight);
    ctx->tabBar->setContextMenuPolicy(Qt::CustomContextMenu);

    ctx->newTabBtn = new QPushButton("+", ctx->tabRowWidget);
    ctx->newTabBtn->setObjectName("newTabBtn");
    ctx->newTabBtn->setFixedSize(32, 32);
    ctx->newTabBtn->setToolTip("New Tab (Ctrl+T)");
    ctx->newTabBtn->setCursor(Qt::PointingHandCursor);

    tabRowLayout->addWidget(ctx->tabBar);
    tabRowLayout->addWidget(ctx->newTabBtn);
    tabRowLayout->addStretch();

    // Custom window buttons only on Linux: tiling compositors such as
    // Hyprland draw no title-bar buttons, so Attis must provide its own.
    // On Windows/macOS the native frame already has minimize/maximize/
    // close, and custom buttons would appear doubled.
#ifdef Q_OS_LINUX
    // Window Control Buttons
    auto *winCtrlWidget = new QWidget(ctx->tabRowWidget);
    winCtrlWidget->setObjectName("winControls");
    auto *winCtrlLayout = new QHBoxLayout(winCtrlWidget);
    winCtrlLayout->setContentsMargins(0, 0, 0, 0);
    winCtrlLayout->setSpacing(2);

    ctx->winMinBtn = new QPushButton("−", winCtrlWidget);
    ctx->winMinBtn->setObjectName("winMinBtn");
    ctx->winMinBtn->setFixedSize(32, 28);
    ctx->winMinBtn->setToolTip("Minimize");

    ctx->winMaxBtn = new QPushButton("□", winCtrlWidget);
    ctx->winMaxBtn->setObjectName("winMaxBtn");
    ctx->winMaxBtn->setFixedSize(32, 28);
    ctx->winMaxBtn->setToolTip("Maximize / Restore");

    ctx->winCloseBtn = new QPushButton("✕", winCtrlWidget);
    ctx->winCloseBtn->setObjectName("winCloseBtn");
    ctx->winCloseBtn->setFixedSize(32, 28);
    ctx->winCloseBtn->setToolTip("Close");

    winCtrlLayout->addWidget(ctx->winMinBtn);
    winCtrlLayout->addWidget(ctx->winMaxBtn);
    winCtrlLayout->addWidget(ctx->winCloseBtn);
    tabRowLayout->addWidget(winCtrlWidget);

    QObject::connect(ctx->winMinBtn, &QPushButton::clicked, [ctx]() { ctx->window->showMinimized(); });
    QObject::connect(ctx->winMaxBtn, &QPushButton::clicked, [ctx]() {
        if (ctx->window->isMaximized()) {
            ctx->window->showNormal();
            ctx->winMaxBtn->setText("□");
        } else {
            ctx->window->showMaximized();
            ctx->winMaxBtn->setText("❐");
        }
    });
    QObject::connect(ctx->winCloseBtn, &QPushButton::clicked, [ctx]() { ctx->window->close(); });
#endif // Q_OS_LINUX

    // Tab Context Menu (Right-click on tab)
    QObject::connect(ctx->tabBar, &QTabBar::customContextMenuRequested, [ctx](const QPoint &pos) {
        int tabIdx = ctx->tabBar->tabAt(pos);
        if (tabIdx < 0 || tabIdx >= static_cast<int>(ctx->tabs.size())) return;

        QMenu tabMenu(ctx->window);
        auto *actReload = tabMenu.addAction("Reload Tab");
        auto *actDuplicate = tabMenu.addAction("Duplicate Tab");
        auto *actSuspend = tabMenu.addAction("Suspend Tab (Save RAM)");
        tabMenu.addSeparator();
        auto *actCloseOthers = tabMenu.addAction("Close Other Tabs");
        auto *actClose = tabMenu.addAction("Close Tab");

        QAction *chosen = tabMenu.exec(ctx->tabBar->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actReload) {
            if (ctx->tabs[tabIdx].view) ctx->tabs[tabIdx].view->reload();
        } else if (chosen == actDuplicate) {
            QString u = ctx->tabs[tabIdx].savedUrl;
            create_tab_internal(ctx, u.toUtf8().constData(), ctx->tabs[tabIdx].isPrivate);
        } else if (chosen == actSuspend) {
            // Manual suspend honors the same rule as the automatic one:
            // never cut off a tab that is playing audio.
            if (tabIdx != ctx->tabBar->currentIndex() && !ctx->tabs[tabIdx].isSuspended && ctx->tabs[tabIdx].view
                && !ctx->tabs[tabIdx].view->page()->recentlyAudible()) {
                ctx->tabs[tabIdx].savedUrl = ctx->tabs[tabIdx].view->url().toString();
                ctx->tabs[tabIdx].savedTitle = ctx->tabBar->tabText(tabIdx);
                ctx->tabs[tabIdx].isSuspended = true;
                ctx->tabs[tabIdx].view->load(QUrl("about:blank"));
                ctx->tabBar->setTabText(tabIdx, "[Suspended] " + ctx->tabs[tabIdx].savedTitle);
            }
        } else if (chosen == actCloseOthers) {
            for (int i = ctx->tabBar->count() - 1; i >= 0; --i) {
                if (i != tabIdx) browser_close_tab(ctx, i);
            }
        } else if (chosen == actClose) {
            browser_close_tab(ctx, tabIdx);
        }
    });

    // 2. Navigation Bar
    ctx->navBar = new QWidget(centralWidget);
    ctx->navBar->setObjectName("navBar");
    auto *navLayout = new QHBoxLayout(ctx->navBar);
    navLayout->setContentsMargins(12, 7, 12, 7);
    navLayout->setSpacing(8);

    ctx->backBtn = new QPushButton("←", ctx->navBar);
    ctx->backBtn->setObjectName("iconBtn");
    ctx->backBtn->setFixedSize(34, 34);
    ctx->backBtn->setToolTip("Back (Alt+Left)");

    ctx->forwardBtn = new QPushButton("→", ctx->navBar);
    ctx->forwardBtn->setObjectName("iconBtn");
    ctx->forwardBtn->setFixedSize(34, 34);
    ctx->forwardBtn->setToolTip("Forward (Alt+Right)");

    ctx->reloadBtn = new QPushButton("↻", ctx->navBar);
    ctx->reloadBtn->setObjectName("iconBtn");
    ctx->reloadBtn->setFixedSize(34, 34);
    ctx->reloadBtn->setToolTip("Reload (Ctrl+R / F5)");

    ctx->urlBar = new QLineEdit(ctx->navBar);
    ctx->urlBar->setObjectName("urlBar");
    ctx->urlBar->setMinimumHeight(38);
    ctx->urlBar->setPlaceholderText("Search Google or type a URL... (Ctrl+L)");

    // Leading Actions inside URL Bar: Engine Toggle + HTTPS Security Lock
    ctx->engineAction = ctx->urlBar->addAction(QIcon(), QLineEdit::LeadingPosition);
    update_search_engine_button(ctx);
    QObject::connect(ctx->engineAction, &QAction::triggered, [ctx]() {
        ctx->useGoogleSearch = !ctx->useGoogleSearch;
        ctx->customSearchEngine.clear();
        update_search_engine_button(ctx);
        update_url_bar_for_current_tab(ctx);
        refresh_internal_tabs(ctx);
        save_browser_config(ctx);
    });

    ctx->securityAction = ctx->urlBar->addAction(QIcon(), QLineEdit::LeadingPosition);
    update_security_indicator(ctx, "attis:home");

    // Trailing Action inside URL Bar: Bookmark Star
    ctx->bookmarkAction = ctx->urlBar->addAction(QIcon(), QLineEdit::TrailingPosition);
    update_bookmark_button(ctx, false);
    QObject::connect(ctx->bookmarkAction, &QAction::triggered, [ctx]() {
        toggle_bookmark_current_page(ctx);
    });

    ctx->privateBadge = new QLabel("🕶️ Private", ctx->navBar);
    ctx->privateBadge->setObjectName("privateBadge");
    ctx->privateBadge->hide();

    ctx->adblockBadge = new QLabel("🛡️ uBlock: 0", ctx->navBar);
    ctx->adblockBadge->setObjectName("adblockBadge");

    // Quick Menu Button
    ctx->menuBtn = new QPushButton("⋮", ctx->navBar);
    ctx->menuBtn->setObjectName("iconBtn");
    ctx->menuBtn->setFixedSize(34, 34);
    ctx->menuBtn->setToolTip("Menu & Settings (Ctrl+,)");

    auto *menu = new QMenu(ctx->menuBtn);
    auto *actionNewTab = menu->addAction("New Tab");
    actionNewTab->setShortcut(QKeySequence("Ctrl+T"));

    auto *actionNewPrivate = menu->addAction("New Private Tab (Incognito)");
    actionNewPrivate->setShortcut(QKeySequence("Ctrl+Shift+N"));

    auto *actionRestoreTab = menu->addAction("Restore Closed Tab");
    actionRestoreTab->setShortcut(QKeySequence("Ctrl+Shift+T"));

    menu->addSeparator();

    auto *actionBookmarks = menu->addAction("Bookmarks");
    actionBookmarks->setShortcut(QKeySequence("Ctrl+B"));

    auto *actionHistory = menu->addAction("Browsing History");
    actionHistory->setShortcut(QKeySequence("Ctrl+H"));

    auto *actionDownloads = menu->addAction("Download Manager");
    actionDownloads->setShortcut(QKeySequence("Ctrl+J"));

    menu->addSeparator();

    auto *actionFind = menu->addAction("Find in Page...");
    actionFind->setShortcut(QKeySequence("Ctrl+F"));

    // Horizontal Zoom Row Action
    auto *zoomWidgetAction = new QWidgetAction(menu);
    auto *zoomWidget = new QWidget(menu);
    auto *zoomLayout = new QHBoxLayout(zoomWidget);
    zoomLayout->setContentsMargins(16, 4, 16, 4);
    zoomLayout->setSpacing(8);

    auto *zoomLabel = new QLabel("Zoom", zoomWidget);
    auto *zoomOutBtn = new QPushButton("－", zoomWidget);
    zoomOutBtn->setObjectName("iconBtn");
    zoomOutBtn->setFixedSize(28, 28);
    auto *zoomValLabel = new QLabel(QString::asprintf("%d%%", static_cast<int>(ctx->currentZoom * 100)), zoomWidget);
    zoomValLabel->setAlignment(Qt::AlignCenter);
    zoomValLabel->setMinimumWidth(44);
    auto *zoomInBtn = new QPushButton("＋", zoomWidget);
    zoomInBtn->setObjectName("iconBtn");
    zoomInBtn->setFixedSize(28, 28);
    auto *zoomFullBtn = new QPushButton("⛶", zoomWidget);
    zoomFullBtn->setObjectName("iconBtn");
    zoomFullBtn->setFixedSize(28, 28);
    zoomFullBtn->setToolTip("Full Screen (F11)");

    zoomLayout->addWidget(zoomLabel);
    zoomLayout->addStretch();
    zoomLayout->addWidget(zoomOutBtn);
    zoomLayout->addWidget(zoomValLabel);
    zoomLayout->addWidget(zoomInBtn);
    zoomLayout->addWidget(zoomFullBtn);
    zoomWidgetAction->setDefaultWidget(zoomWidget);
    menu->addAction(zoomWidgetAction);

    menu->addSeparator();

    auto *moreMenu = menu->addMenu("More Tools");
    auto *actionEngine = moreMenu->addAction("Ganti Mesin Pencari (Google / DDG)");
    auto *actionTabHide = moreMenu->addAction("Hide / Show Tab Bar");
    auto *actionCleanMem = moreMenu->addAction("Optimize Memory of Inactive Tabs");
    auto *actionFullscreen = moreMenu->addAction("Full Screen (F11)");

    menu->addSeparator();

    auto *actionSettings = menu->addAction("Browser Settings");
    actionSettings->setShortcut(QKeySequence("Ctrl+,"));

    QObject::connect(actionNewTab, &QAction::triggered, [ctx]() { browser_add_tab(ctx, ""); });
    QObject::connect(actionNewPrivate, &QAction::triggered, [ctx]() { browser_add_private_tab(ctx, ""); });
    QObject::connect(actionRestoreTab, &QAction::triggered, [ctx]() { restore_closed_tab(ctx); });
    QObject::connect(actionBookmarks, &QAction::triggered, [ctx]() { show_bookmarks_dialog(ctx); });
    QObject::connect(actionHistory, &QAction::triggered, [ctx]() { show_history_dialog(ctx); });
    QObject::connect(actionDownloads, &QAction::triggered, [ctx]() { show_downloads_dialog(ctx); });
    QObject::connect(actionFind, &QAction::triggered, [ctx]() { toggle_find_bar(ctx, true); });
    QObject::connect(actionSettings, &QAction::triggered, [ctx]() { show_settings_dialog(ctx); });

    auto updateZoomDisplay = [ctx, zoomValLabel]() {
        zoomValLabel->setText(QString::asprintf("%d%%", static_cast<int>(ctx->currentZoom * 100)));
    };
    QObject::connect(zoomOutBtn, &QPushButton::clicked, [ctx, updateZoomDisplay]() {
        adjust_zoom(ctx, -0.1f);
        updateZoomDisplay();
    });
    QObject::connect(zoomInBtn, &QPushButton::clicked, [ctx, updateZoomDisplay]() {
        adjust_zoom(ctx, 0.1f);
        updateZoomDisplay();
    });
    QObject::connect(zoomFullBtn, &QPushButton::clicked, [ctx, menu]() {
        if (ctx->window->isFullScreen()) ctx->window->showNormal();
        else ctx->window->showFullScreen();
        menu->close();
    });

    QObject::connect(actionEngine, &QAction::triggered, [ctx]() {
        ctx->useGoogleSearch = !ctx->useGoogleSearch;
        ctx->customSearchEngine.clear();
        update_search_engine_button(ctx);
        refresh_internal_tabs(ctx);
    });
    QObject::connect(actionTabHide, &QAction::triggered, [ctx]() {
        ctx->tabPosition = (ctx->tabPosition == "hidden") ? "top" : "hidden";
        update_tab_bar_placement(ctx);
    });
    QObject::connect(actionCleanMem, &QAction::triggered, [ctx]() {
        run_smart_memory_cleanup(ctx);
    });
    QObject::connect(actionFullscreen, &QAction::triggered, [ctx]() {
        if (ctx->window->isFullScreen()) ctx->window->showNormal();
        else ctx->window->showFullScreen();
    });

    QObject::connect(ctx->menuBtn, &QPushButton::clicked, [ctx, menu, updateZoomDisplay]() {
        updateZoomDisplay();
        menu->exec(ctx->menuBtn->mapToGlobal(QPoint(0, ctx->menuBtn->height() + 4)));
    });

    navLayout->addWidget(ctx->backBtn);
    navLayout->addWidget(ctx->forwardBtn);
    navLayout->addWidget(ctx->reloadBtn);
    navLayout->addWidget(ctx->urlBar, 1);
    navLayout->addWidget(ctx->privateBadge);
    navLayout->addWidget(ctx->adblockBadge);
    navLayout->addWidget(ctx->menuBtn);

    // 2.1 Thin Load Progress Bar
    ctx->loadProgressBar = new QProgressBar(centralWidget);
    ctx->loadProgressBar->setObjectName("loadProgress");
    ctx->loadProgressBar->setTextVisible(false);
    ctx->loadProgressBar->setRange(0, 100);
    ctx->loadProgressBar->setFixedHeight(2);
    ctx->loadProgressBar->hide();

    // 3. Web Views Stack
    ctx->viewStack = new QStackedWidget(centralWidget);

    // 4. Inline Find-in-Page Bar (Bottom)
    ctx->findBar = new QWidget(centralWidget);
    ctx->findBar->setObjectName("findBar");
    auto *findLayout = new QHBoxLayout(ctx->findBar);
    findLayout->setContentsMargins(14, 6, 14, 6);
    findLayout->setSpacing(8);

    auto *findTitle = new QLabel("Find:", ctx->findBar);
    ctx->findInput = new QLineEdit(ctx->findBar);
    ctx->findInput->setObjectName("findInput");
    ctx->findInput->setPlaceholderText("Ketik teks yang dicari di halaman...");
    ctx->findInput->setMaximumWidth(320);

    auto *findPrevBtn = new QPushButton("▲", ctx->findBar);
    findPrevBtn->setObjectName("iconBtn");
    findPrevBtn->setFixedSize(28, 28);
    findPrevBtn->setToolTip("Sebelumnya (Shift+Enter)");

    auto *findNextBtn = new QPushButton("▼", ctx->findBar);
    findNextBtn->setObjectName("iconBtn");
    findNextBtn->setFixedSize(28, 28);
    findNextBtn->setToolTip("Berikutnya (Enter)");

    ctx->findStatusLabel = new QLabel("", ctx->findBar);
    auto *findCloseBtn = new QPushButton("✕", ctx->findBar);
    findCloseBtn->setObjectName("iconBtn");
    findCloseBtn->setFixedSize(28, 28);
    findCloseBtn->setToolTip("Close (Esc)");

    findLayout->addWidget(findTitle);
    findLayout->addWidget(ctx->findInput);
    findLayout->addWidget(findPrevBtn);
    findLayout->addWidget(findNextBtn);
    findLayout->addWidget(ctx->findStatusLabel);
    findLayout->addStretch();
    findLayout->addWidget(findCloseBtn);
    ctx->findBar->hide();

    QObject::connect(ctx->findInput, &QLineEdit::textChanged, [ctx]() { trigger_find_text(ctx, false); });
    QObject::connect(ctx->findInput, &QLineEdit::returnPressed, [ctx]() { trigger_find_text(ctx, false); });
    QObject::connect(findNextBtn, &QPushButton::clicked, [ctx]() { trigger_find_text(ctx, false); });
    QObject::connect(findPrevBtn, &QPushButton::clicked, [ctx]() { trigger_find_text(ctx, true); });
    QObject::connect(findCloseBtn, &QPushButton::clicked, [ctx]() { toggle_find_bar(ctx, false); });

    // 5. Status / Hover URL & Toast Bar
    ctx->statusBarWidget = new QWidget(centralWidget);
    ctx->statusBarWidget->setObjectName("statusBarWidget");
    auto *statusLayout = new QHBoxLayout(ctx->statusBarWidget);
    statusLayout->setContentsMargins(12, 3, 12, 3);
    ctx->statusHoverLabel = new QLabel("", ctx->statusBarWidget);
    ctx->statusToastLabel = new QLabel("", ctx->statusBarWidget);
    statusLayout->addWidget(ctx->statusHoverLabel, 1);
    statusLayout->addWidget(ctx->statusToastLabel);
    ctx->statusBarWidget->hide();
    // Overlay behavior: the bar floats above the web view and never takes
    // part in the layout, so showing it cannot move the page. Clicks pass
    // through it; it only displays the hovered URL / toast text.
    ctx->statusBarWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
    ctx->statusHoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    ctx->statusToastLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    centralWidget->installEventFilter(new StatusOverlayAnchor(ctx));

    ctx->mainLayout->addWidget(ctx->tabRowWidget);
    ctx->mainLayout->addWidget(ctx->navBar);
    ctx->mainLayout->addWidget(ctx->loadProgressBar);
    ctx->mainLayout->addWidget(ctx->viewStack, 1);
    ctx->mainLayout->addWidget(ctx->findBar);
    ctx->window->setCentralWidget(centralWidget);

    // Tab Bar Event Connections
    QObject::connect(ctx->tabBar, &QTabBar::tabMoved, [ctx](int from, int to) {
        if (from >= 0 && from < static_cast<int>(ctx->tabs.size()) &&
            to >= 0 && to < static_cast<int>(ctx->tabs.size())) {
            if (from < to) {
                std::rotate(ctx->tabs.begin() + from, ctx->tabs.begin() + from + 1, ctx->tabs.begin() + to + 1);
            } else if (from > to) {
                std::rotate(ctx->tabs.begin() + to, ctx->tabs.begin() + from, ctx->tabs.begin() + from + 1);
            }
            if (ctx->tabMoveCallback) {
                ctx->tabMoveCallback(from, to);
            }
        }
    });

    QObject::connect(ctx->tabBar, &QTabBar::currentChanged, [ctx](int index) {
        if (index >= 0 && index < static_cast<int>(ctx->tabs.size())) {
            if (ctx->tabs[index].view) {
                ctx->viewStack->setCurrentWidget(ctx->tabs[index].view);
            }
            auto &meta = ctx->tabs[index];
            meta.lastActive = QDateTime::currentSecsSinceEpoch();

            if (meta.isSuspended && meta.view) {
                meta.isSuspended = false;
                if (!meta.savedUrl.isEmpty()) {
                    if (meta.savedUrl == "attis:settings") {
                        meta.view->page()->setBackgroundColor(Qt::transparent);
                        meta.view->setHtml(generate_settings_page_html(ctx), QUrl("attis:settings"));
                    } else if (meta.savedUrl == "attis:home") {
                        meta.view->page()->setBackgroundColor(Qt::transparent);
                        meta.view->setHtml(generate_start_page_html(ctx), QUrl("attis:home"));
                    } else {
                        meta.view->load(QUrl(meta.savedUrl));
                    }
                }
                ctx->tabBar->setTabText(index, meta.savedTitle.isEmpty() ? "New Tab" : meta.savedTitle);
            }
        }
        update_url_bar_for_current_tab(ctx);
    });

    QObject::connect(ctx->tabBar, &QTabBar::tabCloseRequested, [ctx](int index) {
        browser_close_tab(ctx, index);
    });

    // Button Event Connections
    QObject::connect(ctx->backBtn, &QPushButton::clicked, [ctx]() { browser_back(ctx); });
    QObject::connect(ctx->forwardBtn, &QPushButton::clicked, [ctx]() { browser_forward(ctx); });
    QObject::connect(ctx->reloadBtn, &QPushButton::clicked, [ctx]() { browser_reload(ctx); });

    QObject::connect(ctx->urlBar, &QLineEdit::returnPressed, [ctx]() {
        QString input = ctx->urlBar->text().trimmed();
        if (!input.isEmpty()) {
            browser_navigate(ctx, input.toUtf8().constData());
        }
    });

    QObject::connect(ctx->newTabBtn, &QPushButton::clicked, [ctx]() {
        browser_add_tab(ctx, "");
    });

    // Keyboard Shortcuts
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), ctx->window, [ctx]() { show_settings_dialog(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), ctx->window, [ctx]() { browser_add_tab(ctx, ""); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), ctx->window, [ctx]() { browser_add_private_tab(ctx, ""); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), ctx->window, [ctx]() { restore_closed_tab(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), ctx->window, [ctx]() { browser_close_tab(ctx, ctx->tabBar->currentIndex()); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), ctx->window, [ctx]() { browser_reload(ctx); });
    new QShortcut(QKeySequence(Qt::Key_F5), ctx->window, [ctx]() { browser_reload(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), ctx->window, [ctx]() { ctx->urlBar->setFocus(); ctx->urlBar->selectAll(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), ctx->window, [ctx]() { toggle_find_bar(ctx, true); });
    new QShortcut(QKeySequence(Qt::Key_Escape), ctx->window, [ctx]() {
        if (ctx->findBar && ctx->findBar->isVisible()) {
            toggle_find_bar(ctx, false);
        } else if (ctx->window && ctx->window->isFullScreen()) {
            ctx->window->showNormal();
        }
    });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), ctx->window, [ctx]() { toggle_bookmark_current_page(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_B), ctx->window, [ctx]() { show_bookmarks_dialog(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), ctx->window, [ctx]() { show_history_dialog(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_J), ctx->window, [ctx]() { show_downloads_dialog(ctx); });
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), ctx->window, [ctx]() { browser_back(ctx); });
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), ctx->window, [ctx]() { browser_forward(ctx); });

    // Zoom shortcuts
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), ctx->window, [ctx]() { adjust_zoom(ctx, 0.1f); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), ctx->window, [ctx]() { adjust_zoom(ctx, 0.1f); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), ctx->window, [ctx]() { adjust_zoom(ctx, -0.1f); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), ctx->window, [ctx]() {
        auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
        if (view) {
            ctx->currentZoom = 1.0f;
            view->setZoomFactor(1.0f);
            show_status_toast(ctx, "Zoom: 100%", 1500);
        }
    });

    // Fullscreen shortcut
    new QShortcut(QKeySequence(Qt::Key_F11), ctx->window, [ctx]() {
        if (ctx->window->isFullScreen()) ctx->window->showNormal();
        else ctx->window->showFullScreen();
    });

    // Safe Minimal Mode shortcut (Fixed segfault: removed null button dereference)
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_M), ctx->window, [ctx]() {
        if (!ctx->backBtn) return;
        bool vis = !ctx->backBtn->isVisible();
        ctx->backBtn->setVisible(vis);
        if (ctx->forwardBtn) ctx->forwardBtn->setVisible(vis);
        if (ctx->reloadBtn) ctx->reloadBtn->setVisible(vis);
    });

    // Tab switching
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), ctx->window, [ctx]() {
        int count = ctx->tabBar->count();
        if (count > 1) {
            browser_switch_tab(ctx, (ctx->tabBar->currentIndex() + 1) % count);
        }
    });

    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), ctx->window, [ctx]() {
        int count = ctx->tabBar->count();
        if (count > 1) {
            browser_switch_tab(ctx, (ctx->tabBar->currentIndex() - 1 + count) % count);
        }
    });

    // Smart Memory Daemon Timer (runs every 25s)
    ctx->memoryTimer = new QTimer(ctx->window);
    QObject::connect(ctx->memoryTimer, &QTimer::timeout, [ctx]() {
        run_smart_memory_cleanup(ctx);
    });
    ctx->memoryTimer->start(25000);

    apply_theme_qss(ctx);
    update_search_engine_button(ctx);

    // Start maximized when that is the saved state (the default on first
    // run), so the window always fits the screen it is on.
    if (ctx->windowMaximized) {
        ctx->window->showMaximized();
    } else {
        ctx->window->show();
    }
#ifdef Q_OS_WIN
    // Set the title-bar theme AFTER the window is shown. Setting it
    // earlier (in apply_theme_qss, before show) does not stick: the
    // DWM attribute is lost when the window is first shown/maximized.
    apply_dark_title_bar(ctx->window, is_dark_hex_color(ctx->bgColor));
#endif
    return static_cast<BrowserHandle>(ctx);
}

static int find_tab_index_by_view(BrowserContext *ctx, QWebEngineView *view) {
    if (!ctx || !view) return -1;
    for (size_t i = 0; i < ctx->tabs.size(); ++i) {
        if (ctx->tabs[i].view == view) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static void attach_view_signals(BrowserContext *ctx, QWebEngineView *view) {
    if (ctx->currentZoom > 0.2f && ctx->currentZoom < 5.0f) {
        view->setZoomFactor(ctx->currentZoom);
    }

    // Favicon support on tabs
    QObject::connect(view, &QWebEngineView::iconChanged, [ctx, view](const QIcon &icon) {
        int idx = find_tab_index_by_view(ctx, view);
        if (idx >= 0) {
            ctx->tabBar->setTabIcon(idx, icon);
        }
    });

    // Loading progress bar & reload/stop button state
    QObject::connect(view, &QWebEngineView::loadStarted, [ctx, view]() {
        if (ctx->viewStack->currentWidget() == view) {
            if (ctx->loadProgressBar) {
                ctx->loadProgressBar->setValue(10);
                ctx->loadProgressBar->show();
            }
            if (ctx->reloadBtn) ctx->reloadBtn->setText("✕");
        }
    });

    QObject::connect(view, &QWebEngineView::loadProgress, [ctx, view](int progress) {
        if (ctx->viewStack->currentWidget() == view && ctx->loadProgressBar) {
            ctx->loadProgressBar->setValue(progress);
            if (progress >= 100) {
                ctx->loadProgressBar->hide();
            } else {
                ctx->loadProgressBar->show();
            }
        }
    });

    // Link hover status bar
    QObject::connect(view->page(), &QWebEnginePage::linkHovered, [ctx](const QString &url) {
        if (!ctx || !ctx->statusBarWidget || !ctx->statusHoverLabel) return;
        if (url.isEmpty()) {
            ctx->statusHoverLabel->clear();
            if (ctx->statusToastLabel && ctx->statusToastLabel->text().isEmpty()) {
                ctx->statusBarWidget->hide();
            }
        } else {
            ctx->statusHoverLabel->setText(url);
            position_status_overlay(ctx);
            ctx->statusBarWidget->show();
        }
    });

    // HTML5 Fullscreen request handler (e.g., YouTube fullscreen button)
    QObject::connect(view->page(), &QWebEnginePage::fullScreenRequested, [ctx](QWebEngineFullScreenRequest req) {
        req.accept();
        if (!ctx || !ctx->window) return;
        if (req.toggleOn()) {
            if (ctx->tabRowWidget) ctx->tabRowWidget->hide();
            if (ctx->navBar) ctx->navBar->hide();
            ctx->window->showFullScreen();
        } else {
            if (ctx->navBar) ctx->navBar->show();
            update_tab_bar_placement(ctx);
            ctx->window->showNormal();
        }
    });

    // Feature permission prompt (Camera, Microphone, Geolocation, Notifications)
    QObject::connect(view->page(), &QWebEnginePage::featurePermissionRequested,
        [ctx, view](const QUrl &securityOrigin, QWebEnginePage::Feature feature) {
            if (!ctx || !ctx->window) return;
            QString featName;
            switch (feature) {
                case QWebEnginePage::Geolocation: featName = "Lokasi (Geolocation)"; break;
                case QWebEnginePage::MediaAudioCapture: featName = "Mikrofon"; break;
                case QWebEnginePage::MediaVideoCapture: featName = "Kamera"; break;
                case QWebEnginePage::MediaAudioVideoCapture: featName = "Kamera & Mikrofon"; break;
                case QWebEnginePage::Notifications: featName = "Notifikasi Desktop"; break;
                case QWebEnginePage::DesktopVideoCapture:
                case QWebEnginePage::DesktopAudioVideoCapture: featName = "Perekaman Layar"; break;
                default:
                    view->page()->setFeaturePermission(securityOrigin, feature, QWebEnginePage::PermissionDeniedByUser);
                    return;
            }
            auto reply = QMessageBox::question(
                ctx->window,
                "Izin Situs Web",
                QString("Izinkan <b>%1</b> mengakses <b>%2</b>?").arg(securityOrigin.host(), featName),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );
            view->page()->setFeaturePermission(
                securityOrigin,
                feature,
                reply == QMessageBox::Yes ? QWebEnginePage::PermissionGrantedByUser : QWebEnginePage::PermissionDeniedByUser
            );
        }
    );

    QObject::connect(view, &QWebEngineView::titleChanged, [ctx, view](const QString &title) {
        int idx = find_tab_index_by_view(ctx, view);
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            QString fallback = ctx->tabs[idx].isPrivate ? "Private Tab" : "New Tab";
            QString displayTitle = title.trimmed().isEmpty() ? fallback : title;
            ctx->tabs[idx].savedTitle = displayTitle;
            if (displayTitle.length() > 26) {
                displayTitle = displayTitle.left(23) + "...";
            }
            if (ctx->tabs[idx].isPrivate && !displayTitle.startsWith("🕶️ ")) {
                displayTitle = "🕶️ " + displayTitle;
            }
            if (!ctx->tabs[idx].isSuspended) {
                ctx->tabBar->setTabText(idx, displayTitle);
                ctx->tabBar->setTabToolTip(idx, title);
            }
            if (ctx->tabCallback) {
                QByteArray urlB = view->url().toString().toUtf8();
                QByteArray titleB = title.toUtf8();
                ctx->tabCallback(idx, urlB.constData(), titleB.constData(), ctx->tabs[idx].isPrivate ? 1 : 0);
            }
        }
    });

    QObject::connect(view, &QWebEngineView::urlChanged, [ctx, view](const QUrl &url) {
        int idx = find_tab_index_by_view(ctx, view);
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            ctx->tabs[idx].savedUrl = url.toString();
            ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
        }
        if (idx == ctx->tabBar->currentIndex()) {
            update_url_bar_for_current_tab(ctx);
        }
        if (ctx->tabCallback && idx >= 0) {
            QByteArray urlB = url.toString().toUtf8();
            QByteArray titleB = view->title().toUtf8();
            ctx->tabCallback(idx, urlB.constData(), titleB.constData(), ctx->tabs[idx].isPrivate ? 1 : 0);
        }
    });

    // Cosmetic Filter (from Rust AdBlock engine) & User Custom Script/Style Injection
    QObject::connect(view, &QWebEngineView::loadFinished, [ctx, view](bool ok) {
        if (ctx->viewStack->currentWidget() == view) {
            if (ctx->loadProgressBar) ctx->loadProgressBar->hide();
            if (ctx->reloadBtn) ctx->reloadBtn->setText("↻");
            update_nav_buttons_state(ctx);
        }
        if (!ok) return;

        if (ctx->enableAdblock && ctx->enableCosmeticFilter) {
            QString pageUrl = view->url().toString();
            if (pageUrl.startsWith("http://") || pageUrl.startsWith("https://")) {
                char* rawCss = rust_get_cosmetic_css(pageUrl.toUtf8().constData());
                if (rawCss) {
                    QString cosmeticCss = QString::fromUtf8(rawCss);
                    rust_free_c_str(rawCss);
                    if (!cosmeticCss.isEmpty()) {
                        QString injectCosmetic = QString(
                            "(() => {"
                            "  const hex = '%1';"
                            "  let css = '';"
                            "  for (let i = 0; i < hex.length; i += 2) {"
                            "    css += String.fromCharCode(parseInt(hex.substr(i, 2), 16));"
                            "  }"
                            "  const el = document.createElement('style');"
                            "  el.setAttribute('data-ublock-cosmetic', 'true');"
                            "  el.textContent = css;"
                            "  if (document.head) document.head.appendChild(el);"
                            "})();"
                        ).arg(QString(cosmeticCss.toUtf8().toHex()));
                        view->page()->runJavaScript(injectCosmetic);
                    }
                }
            }
        }

        if (!ctx->customPageCss.isEmpty()) {
            QString cleanInject = QString(
                "(() => {"
                "  const hex = '%1';"
                "  let css = '';"
                "  for (let i = 0; i < hex.length; i += 2) {"
                "    css += String.fromCharCode(parseInt(hex.substr(i, 2), 16));"
                "  }"
                "  const el = document.createElement('style');"
                "  el.textContent = css;"
                "  if (document.head) document.head.appendChild(el);"
                "})();"
            ).arg(QString(ctx->customPageCss.toUtf8().toHex()));
            view->page()->runJavaScript(cleanInject);
        }

        if (!ctx->customUserScript.isEmpty()) {
            view->page()->runJavaScript(ctx->customUserScript);
        }
    });
}

static int create_tab_internal(BrowserContext *ctx, const char* raw_url, bool is_private) {
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return -1;

    QWebEngineProfile *targetProfile = is_private && ctx->privateProfile
        ? ctx->privateProfile
        : QWebEngineProfile::defaultProfile();

    auto *view = new QWebEngineView(ctx->viewStack);
    view->setPage(new BrowserPage(ctx, targetProfile, view));
    attach_view_signals(ctx, view);

    TabMeta meta;
    meta.view = view;
    meta.lastActive = QDateTime::currentSecsSinceEpoch();
    meta.isSuspended = false;
    meta.isPrivate = is_private;
    ctx->tabs.push_back(meta);

    ctx->viewStack->addWidget(view);
    int index = ctx->tabBar->addTab(is_private ? "🕶️ Private Tab" : "New Tab");
    ctx->tabBar->setCurrentIndex(index);
    ctx->viewStack->setCurrentWidget(view);

    if (raw_url && raw_url[0] != '\0') {
        browser_navigate(ctx, raw_url);
    } else {
        browser_navigate(ctx, "attis:home");
    }

    if (is_private) {
        show_status_toast(ctx, "🕶️ Incognito Mode: history & cookies are not saved", 2800);
    }

    return index;
}

int browser_add_tab(BrowserHandle handle, const char* raw_url) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    return create_tab_internal(ctx, raw_url, false);
}

int browser_add_private_tab(BrowserHandle handle, const char* raw_url) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    return create_tab_internal(ctx, raw_url, true);
}

// Lazy restore: buat tab dalam keadaan tersuspensi TANPA memuat URL.
// Tab dimuat saat pertama kali diaktifkan oleh handler currentChanged,
// sama seperti tab yang disuspensi Smart Memory (savedUrl/savedTitle).
static int create_suspended_tab_internal(BrowserContext *ctx, const char* raw_url, const char* raw_title) {
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return -1;

    auto *view = new QWebEngineView(ctx->viewStack);
    view->setPage(new BrowserPage(ctx, QWebEngineProfile::defaultProfile(), view));
    attach_view_signals(ctx, view);

    TabMeta meta;
    meta.view = view;
    meta.lastActive = QDateTime::currentSecsSinceEpoch();
    meta.isSuspended = false;
    meta.isPrivate = false;
    ctx->tabs.push_back(meta);

    QString title = raw_title ? QString::fromUtf8(raw_title).trimmed() : QString();
    if (title.isEmpty()) {
        title = raw_url ? QString::fromUtf8(raw_url) : QString("New Tab");
    }
    QString displayTitle = title;
    if (displayTitle.length() > 26) {
        displayTitle = displayTitle.left(23) + "...";
    }

    ctx->viewStack->addWidget(view);
    int index = ctx->tabBar->addTab("💤 " + displayTitle);
    ctx->tabBar->setCurrentIndex(index);
    ctx->viewStack->setCurrentWidget(view);

    // Tandai tersuspensi SETELAH aktivasi agar currentChanged tidak
    // langsung memulihkan tab ini; pemulihan terjadi saat tab diklik.
    ctx->tabs[index].savedUrl = raw_url ? QString::fromUtf8(raw_url) : QString();
    ctx->tabs[index].savedTitle = title;
    ctx->tabs[index].isSuspended = true;

    return index;
}

int browser_add_suspended_tab(BrowserHandle handle, const char* raw_url, const char* title) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    return create_suspended_tab_internal(ctx, raw_url, title);
}

void browser_close_tab(BrowserHandle handle, int index) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return;

    if (ctx->tabBar->count() > 1 && index >= 0 && index < static_cast<int>(ctx->tabs.size())) {
        const auto &closingMeta = ctx->tabs[index];
        if (!closingMeta.isPrivate &&
            !closingMeta.savedUrl.isEmpty() &&
            closingMeta.savedUrl != "attis:home" &&
            closingMeta.savedUrl != "about:blank") {
            ctx->closedTabs.push_back({ closingMeta.savedUrl, closingMeta.savedTitle });
            if (ctx->closedTabs.size() > 25) {
                ctx->closedTabs.erase(ctx->closedTabs.begin());
            }
        }

        auto *view = ctx->tabs[index].view;
        ctx->tabs.erase(ctx->tabs.begin() + index);
        ctx->tabBar->removeTab(index);
        if (view) {
            ctx->viewStack->removeWidget(view);
            view->deleteLater();
        }
        int newActive = ctx->tabBar->currentIndex();
        if (newActive >= 0 && newActive < static_cast<int>(ctx->tabs.size()) && ctx->tabs[newActive].view) {
            ctx->viewStack->setCurrentWidget(ctx->tabs[newActive].view);
        }
        update_url_bar_for_current_tab(ctx);

        if (ctx->tabCloseCallback) {
            ctx->tabCloseCallback(index);
        }
    } else if (ctx->tabBar->count() == 1) {
        browser_navigate(handle, "attis:home");
    }
}

void browser_switch_tab(BrowserHandle handle, int index) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return;
    if (index >= 0 && index < static_cast<int>(ctx->tabs.size())) {
        ctx->tabBar->setCurrentIndex(index);
        if (ctx->tabs[index].view) {
            ctx->viewStack->setCurrentWidget(ctx->tabs[index].view);
        }
    }
}

void browser_navigate(BrowserHandle handle, const char* raw_url) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->tabBar || !ctx->viewStack || !raw_url) return;

    auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (!view) {
        browser_add_tab(handle, raw_url);
        return;
    }

    QString s = QString::fromUtf8(raw_url).trimmed();
    int idx = ctx->tabBar->currentIndex();

    // Privileged settings actions must never be reachable by typing a
    // URL: attis-set:// is only honored for in-page navigations from
    // attis: pages (see BrowserPage::acceptNavigationRequest). Ignore
    // it here and restore the URL bar to the current page.
    if (s.startsWith("attis-set:", Qt::CaseInsensitive) ||
        s.startsWith("attis-action:", Qt::CaseInsensitive)) {
        update_url_bar_for_current_tab(ctx);
        return;
    }

    if (s == "attis:settings" || s == "about:settings") {
        view->page()->setBackgroundColor(Qt::transparent);
        view->setHtml(generate_settings_page_html(ctx), QUrl("attis:settings"));
        ctx->urlBar->setText("attis:settings");
        update_bookmark_button(ctx, false);
        update_security_indicator(ctx, "attis:settings");
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            ctx->tabs[idx].savedUrl = "attis:settings";
            ctx->tabs[idx].savedTitle = "Settings";
            ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
            ctx->tabs[idx].isSuspended = false;
        }
        ctx->tabBar->setTabText(idx, "⚙️ Settings");
        return;
    }

    if (s.isEmpty() || s == "attis:home" || s == "about:home") {
        view->page()->setBackgroundColor(Qt::transparent);
        view->setHtml(generate_start_page_html(ctx), QUrl("attis:home"));
        ctx->urlBar->setText("");
        ctx->urlBar->setPlaceholderText(ctx->useGoogleSearch
            ? "Search Google or type a URL... (Ctrl+L)"
            : "Search DuckDuckGo or type a URL... (Ctrl+L)");
        update_bookmark_button(ctx, false);
        update_security_indicator(ctx, "attis:home");
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            ctx->tabs[idx].savedUrl = "attis:home";
            ctx->tabs[idx].savedTitle = ctx->tabs[idx].isPrivate ? "Private Tab" : "New Tab";
            ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
            ctx->tabs[idx].isSuspended = false;
            ctx->tabBar->setTabText(idx, ctx->tabs[idx].isPrivate ? "🕶️ Private Tab" : "New Tab");
        }
        return;
    }

    view->page()->setBackgroundColor(Qt::white);
    QUrl url;

    if (!s.contains("://") && !s.startsWith("about:") && !s.startsWith("data:")) {
        if ((s.contains(".") || s.startsWith("localhost")) && !s.contains(" ")) {
            url = QUrl("https://" + s);
        } else {
            if (s.startsWith("!g ") || s.startsWith("g ")) {
                QString q = s.mid(s.indexOf(' ') + 1).trimmed();
                url = QUrl("https://www.google.com/search?q=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (s.startsWith("!d ") || s.startsWith("d ")) {
                QString q = s.mid(s.indexOf(' ') + 1).trimmed();
                url = QUrl("https://html.duckduckgo.com/html/?q=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (s.startsWith("!gh ")) {
                QString q = s.mid(4).trimmed();
                url = QUrl("https://github.com/search?q=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (s.startsWith("!yt ")) {
                QString q = s.mid(4).trimmed();
                url = QUrl("https://www.youtube.com/results?search_query=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (s.startsWith("!w ")) {
                QString q = s.mid(3).trimmed();
                url = QUrl("https://id.wikipedia.org/w/index.php?search=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (!ctx->customSearchEngine.isEmpty() &&
                       ctx->customSearchEngine.contains("%s") &&
                       !ctx->customSearchEngine.contains("google.com/search") &&
                       !ctx->customSearchEngine.contains("duckduckgo.com")) {
                QString target = ctx->customSearchEngine;
                target.replace("%s", QString::fromUtf8(QUrl::toPercentEncoding(s)));
                url = QUrl(target);
            } else if (ctx->useGoogleSearch) {
                url = QUrl("https://www.google.com/search?q=" + QString::fromUtf8(QUrl::toPercentEncoding(s)));
            } else {
                url = QUrl("https://html.duckduckgo.com/html/?q=" + QString::fromUtf8(QUrl::toPercentEncoding(s)));
            }
        }
    } else {
        url = QUrl(s);
    }

    if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
        ctx->tabs[idx].savedUrl = url.toString();
        ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
        ctx->tabs[idx].isSuspended = false;
    }

    view->load(url);
}

void browser_back(BrowserHandle handle) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->viewStack) return;
    auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (view) {
        view->back();
    }
}

void browser_forward(BrowserHandle handle) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->viewStack) return;
    auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (view) {
        view->forward();
    }
}

void browser_reload(BrowserHandle handle) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->viewStack) return;
    auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (view) {
        view->reload();
    }
}

void browser_update_adblock_badge(BrowserHandle handle, uint64_t blocked_count) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->adblockBadge) return;
    QMetaObject::invokeMethod(ctx->adblockBadge, [ctx, blocked_count]() {
        ctx->adblockBadge->setText(QString("🛡️ uBlock: %1").arg(blocked_count));
    }, Qt::QueuedConnection);
}

int browser_run(BrowserHandle handle) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->app) return 0;
    return ctx->app->exec();
}

void browser_destroy(BrowserHandle handle) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx) return;
    g_block_cb = nullptr;
    if (ctx->memoryTimer) {
        ctx->memoryTimer->stop();
        delete ctx->memoryTimer;
    }
    if (ctx->window) {
        delete ctx->window;
    }
    if (ctx->ownApp && ctx->app) {
        delete ctx->app;
    }
    delete ctx;
}
