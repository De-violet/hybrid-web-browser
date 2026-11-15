#include "web_bridge.h"

#include <QString>
#include <QApplication>
#include <QColor>
#include <QMainWindow>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineDownloadRequest>
#include <QShortcut>
#include <QKeySequence>
#include <QStandardPaths>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
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
#include <QWidgetAction>
#include <QListWidget>
#include <QInputDialog>
#include <QAction>
#include <QFile>
#include <QTimer>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <QUrl>
#include <QUrlQuery>
#include <vector>

static RustBlockCallback g_block_cb = nullptr;

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
};

struct BrowserContext {
    QApplication *app = nullptr;
    QMainWindow *window = nullptr;
    QTabBar *tabBar = nullptr;
    QStackedWidget *viewStack = nullptr;
    QWidget *navBar = nullptr;
    QPushButton *backBtn = nullptr;
    QPushButton *forwardBtn = nullptr;
    QPushButton *reloadBtn = nullptr;
    QPushButton *engineToggleBtn = nullptr;
    QAction *engineAction = nullptr;
    QAction *bookmarkAction = nullptr;
    QLineEdit *urlBar = nullptr;
    QPushButton *newTabBtn = nullptr;
    QPushButton *menuBtn = nullptr;
    QLabel *adblockBadge = nullptr;
    BridgeInterceptor *interceptor = nullptr;
    RustTabCallback tabCallback = nullptr;
    RustTabCloseCallback tabCloseCallback = nullptr;
    QTimer *memoryTimer = nullptr;

    std::vector<TabMeta> tabs;
    bool useGoogleSearch = true; // default: Google
    QString customSearchEngine;
    QString downloadDir;
    QString customUserStyle;
    QString customUserScript;
    float currentZoom = 1.0f;
    bool ownApp = false;
    QString currentTheme = "dark";
    QString bgColor = "#181825";
    QString fgColor = "#cdd6f4";
    QString accentColor = "#89b4fa";
    QString surfaceColor = "#1e1e2e";
    QString borderColor = "#313244";
    float bgOpacity = 0.85f;
    QString uiDensity = "comfortable";
    QString tabPosition = "top";
    bool showHomeSearch = true;
    bool showHomeTiles = true;
    bool showHomeClock = true;
};

class BrowserPage : public QWebEnginePage {
public:
    explicit BrowserPage(BrowserContext *ctx, QObject *parent = nullptr)
        : QWebEnginePage(parent), m_ctx(ctx) {}

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;

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

static void apply_theme_qss(BrowserContext *ctx) {
    if (!ctx || !ctx->window) return;

    QString closeSvg = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/tab_close.svg";
    QString closeHoverSvg = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/tab_close_hover.svg";

    QColor bg(ctx->bgColor);
    if (!bg.isValid()) bg = QColor("#181825");
    float op = qBound(0.2f, ctx->bgOpacity, 1.0f);
    QString bgRgba = QString("rgba(%1, %2, %3, %4)").arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(op);
    QString surfaceRgba = ctx->surfaceColor.isEmpty() ? "#1e1e2e" : ctx->surfaceColor;
    QString fg = ctx->fgColor.isEmpty() ? "#cdd6f4" : ctx->fgColor;
    QString accent = ctx->accentColor.isEmpty() ? "#89b4fa" : ctx->accentColor;
    QString border = ctx->borderColor.isEmpty() ? "#313244" : ctx->borderColor;

    bool isLight = bg.lightness() > 128;
    QString hoverOverlay = isLight ? "rgba(0, 0, 0, 0.06)" : "rgba(255, 255, 255, 0.08)";
    QString activeHoverOverlay = isLight ? "rgba(0, 0, 0, 0.10)" : "rgba(255, 255, 255, 0.14)";

    bool isCompact = (ctx->uiDensity == "compact");
    QString tabPadding = isCompact ? "7px 16px" : "11px 24px";
    QString tabMinW = isCompact ? "110px" : "140px";
    QString urlPadding = isCompact ? "7px 16px" : "10px 20px";
    QString urlFontSize = isCompact ? "13px" : "14px";

    QString qss = QString(R"QSS(
QMainWindow { background-color: %1; }
QWidget { color: %2; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Ubuntu, sans-serif; font-size: 13px; }
QWidget#tabStrip { background-color: %1; }
QTabBar { background: transparent; border: none; }
QTabBar::tab {
    background-color: transparent;
    color: %2;
    border: none;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
    padding: %3;
    margin-right: 4px;
    min-width: %4;
    max-width: 260px;
    font-size: 13px;
    font-weight: 500;
}
QTabBar::tab:hover:!selected {
    background-color: %10;
    color: %2;
}
QTabBar::tab:selected {
    background-color: %5;
    color: %6;
    font-weight: 600;
    border-top: 1px solid rgba(255, 255, 255, 0.16);
    border-left: 1px solid rgba(255, 255, 255, 0.08);
    border-right: 1px solid rgba(255, 255, 255, 0.08);
}
QTabBar::close-button {
    subcontrol-position: right;
    margin-right: 6px;
    border-radius: 4px;
    width: 14px;
    height: 14px;
    background: transparent;
    image: url(@CLOSE_ICON@);
}
QTabBar::close-button:hover {
    image: url(@CLOSE_HOVER@);
    background-color: %11;
}
QPushButton#newTabBtn {
    background: transparent;
    color: %2;
    border: none;
    border-radius: 8px;
    font-size: 18px;
    font-weight: normal;
}
QPushButton#newTabBtn:hover {
    background-color: %10;
}
QWidget#navBar {
    background-color: %5;
    border-bottom: 1px solid %7;
}
QPushButton#iconBtn, QPushButton#engineBtn {
    background-color: transparent;
    color: %2;
    border: none;
    border-radius: 8px;
    padding: 4px;
    font-size: 16px;
}
QPushButton#iconBtn::menu-indicator, QPushButton::menu-indicator {
    image: none;
    width: 0px;
}
QPushButton#iconBtn:hover, QPushButton#engineBtn:hover {
    background-color: %10;
}
QPushButton#iconBtn:pressed, QPushButton#engineBtn:pressed {
    background-color: %11;
}
QPushButton#iconBtn:disabled {
    color: %7;
}
QLineEdit#urlBar {
    background-color: rgba(0, 0, 0, 0.40);
    color: %2;
    border: 1px solid %7;
    border-radius: 14px;
    padding: %8;
    font-size: %9;
    selection-background-color: %7;
}
QLineEdit#urlBar:hover {
    border: 1px solid rgba(255, 255, 255, 0.28);
    background-color: rgba(0, 0, 0, 0.48);
}
QLineEdit#urlBar:focus {
    border: 1.5px solid %6;
    background-color: rgba(0, 0, 0, 0.72);
    color: #ffffff;
}
QLabel#adblockBadge {
    background-color: rgba(166, 227, 161, 0.12);
    color: #a6e3a1;
    border: 1px solid rgba(166, 227, 161, 0.25);
    border-radius: 13px;
    padding: 4px 12px;
    font-size: 11px;
    font-weight: 600;
}
QMenu {
    background-color: %5;
    color: %2;
    border: 1px solid %7;
    border-radius: 8px;
    padding: 6px;
}
QMenu::item {
    padding: 6px 24px;
    border-radius: 4px;
}
QMenu::shortcut {
    color: %2;
    padding-left: 20px;
    opacity: 0.7;
}
QMenu::item:selected {
    background-color: %7;
    color: %6;
}
QMenu::separator {
    height: 1px;
    background: %7;
    margin: 4px 0px;
}
)QSS")
    .arg(bgRgba)
    .arg(fg)
    .arg(tabPadding)
    .arg(tabMinW)
    .arg(surfaceRgba)
    .arg(accent)
    .arg(border)
    .arg(urlPadding)
    .arg(urlFontSize)
    .arg(hoverOverlay)
    .arg(activeHoverOverlay);

    qss.replace("@CLOSE_ICON@", closeSvg).replace("@CLOSE_HOVER@", closeHoverSvg);

    if (!ctx->customUserStyle.isEmpty()) {
        qss += "\n/* User Custom QSS */\n" + ctx->customUserStyle;
    }

    ctx->window->setStyleSheet(qss);

    if (ctx->tabBar) {
        if (ctx->tabPosition == "hidden") {
            ctx->tabBar->hide();
            if (ctx->newTabBtn) ctx->newTabBtn->hide();
        } else {
            ctx->tabBar->show();
            if (ctx->newTabBtn) ctx->newTabBtn->show();
            if (ctx->tabPosition == "left") {
                ctx->tabBar->setShape(QTabBar::RoundedWest);
            } else {
                ctx->tabBar->setShape(QTabBar::RoundedNorth);
            }
        }
    }
}

void browser_set_theme(BrowserHandle handle, const char* theme_name, const char* custom_qss) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->window) return;

    if (custom_qss && custom_qss[0] != '\0') {
        ctx->customUserStyle = QString::fromUtf8(custom_qss);
    }

    if (theme_name) {
        QString t = QString::fromUtf8(theme_name).toLower();
        ctx->currentTheme = t;
        if (t == "dark_glass" || t == "dark") {
            ctx->bgColor = "#11111b"; ctx->surfaceColor = "#181825"; ctx->fgColor = "#cdd6f4"; ctx->accentColor = "#89b4fa"; ctx->borderColor = "#313244"; ctx->bgOpacity = 0.85f;
        } else if (t == "catppuccin") {
            ctx->bgColor = "#1e1e2e"; ctx->surfaceColor = "#181825"; ctx->fgColor = "#cdd6f4"; ctx->accentColor = "#cba6f7"; ctx->borderColor = "#45475a"; ctx->bgOpacity = 0.92f;
        } else if (t == "light") {
            ctx->bgColor = "#eff1f5"; ctx->surfaceColor = "#e6e9ef"; ctx->fgColor = "#4c4f69"; ctx->accentColor = "#1e66f5"; ctx->borderColor = "#ccd0da"; ctx->bgOpacity = 0.95f;
        } else if (t == "minimal") {
            ctx->bgColor = "#121212"; ctx->surfaceColor = "#1c1c1c"; ctx->fgColor = "#e0e0e0"; ctx->accentColor = "#ffffff"; ctx->borderColor = "#2c2c2c"; ctx->bgOpacity = 0.90f;
        } else if (t == "nord") {
            ctx->bgColor = "#2e3440"; ctx->surfaceColor = "#242933"; ctx->fgColor = "#eceff4"; ctx->accentColor = "#88c0d0"; ctx->borderColor = "#3b4252"; ctx->bgOpacity = 0.90f;
        } else if (t == "gruvbox") {
            ctx->bgColor = "#282828"; ctx->surfaceColor = "#1d2021"; ctx->fgColor = "#ebdbb2"; ctx->accentColor = "#fe8019"; ctx->borderColor = "#3c3836"; ctx->bgOpacity = 0.92f;
        }
    }

    apply_theme_qss(ctx);
}

void browser_set_colors(BrowserHandle handle, const char* bg_color, const char* fg_color, const char* accent_color) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx) return;
    if (bg_color && bg_color[0] != '\0') ctx->bgColor = QString::fromUtf8(bg_color);
    if (fg_color && fg_color[0] != '\0') ctx->fgColor = QString::fromUtf8(fg_color);
    if (accent_color && accent_color[0] != '\0') ctx->accentColor = QString::fromUtf8(accent_color);
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

    if (tab_position == 2) {
        ctx->tabPosition = "hidden";
    } else if (tab_position == 3) {
        ctx->tabPosition = "left";
    } else {
        ctx->tabPosition = "top";
    }

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
        ctx->customSearchEngine = QString::fromUtf8(template_url);
    }
}

void browser_set_download_dir(BrowserHandle handle, const char* dir) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && dir) {
        ctx->downloadDir = QString::fromUtf8(dir);
    }
}

void browser_set_user_agent(BrowserHandle handle, const char* ua) {
    (void)handle;
    if (ua && ua[0] != '\0') {
        QWebEngineProfile::defaultProfile()->setHttpUserAgent(QString::fromUtf8(ua));
    }
}

void browser_set_custom_user_style(BrowserHandle handle, const char* css) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (ctx && css) {
        ctx->customUserStyle = QString::fromUtf8(css);
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

static void update_url_bar_for_current_tab(BrowserContext *ctx) {
    if (!ctx || !ctx->tabBar || !ctx->urlBar || !ctx->viewStack) return;
    int idx = ctx->tabBar->currentIndex();
    QString currentUrl;
    if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
        if (ctx->tabs[idx].isSuspended) {
            currentUrl = ctx->tabs[idx].savedUrl;
            ctx->urlBar->setText(currentUrl);
            update_bookmark_button(ctx, is_url_bookmarked(currentUrl));
            return;
        }
    }
    auto *currentView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (currentView) {
        currentUrl = currentView->url().toString();
        if (currentUrl == "falkon:home") {
            ctx->urlBar->setText("");
            ctx->urlBar->setPlaceholderText("Search Google or type a URL... (Ctrl+L)");
            update_bookmark_button(ctx, false);
        } else if (currentUrl == "falkon:settings") {
            ctx->urlBar->setText("falkon:settings");
            update_bookmark_button(ctx, false);
        } else {
            ctx->urlBar->setText(currentUrl);
            update_bookmark_button(ctx, is_url_bookmarked(currentUrl));
        }
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
    }
}

// Smart Memory: discard/suspend inactive background tabs unless playing audio/video
static void run_smart_memory_cleanup(BrowserContext *ctx) {
    if (!ctx || !ctx->tabBar || !ctx->viewStack || ctx->tabs.size() <= 1) return;

    int activeIdx = ctx->tabBar->currentIndex();
    qint64 now = QDateTime::currentSecsSinceEpoch();

    for (size_t i = 0; i < ctx->tabs.size(); ++i) {
        if (static_cast<int>(i) == activeIdx) {
            ctx->tabs[i].lastActive = now;
            continue;
        }

        auto &meta = ctx->tabs[i];
        if (meta.isSuspended) continue;

        // Streaming audio/video protection: NEVER suspend playing media
        if (meta.view && meta.view->page() && meta.view->page()->recentlyAudible()) {
            continue;
        }

        // Suspend tabs inactive for > 120s or if total tabs > 3
        if ((now - meta.lastActive > 120) || (ctx->tabs.size() > 3 && now - meta.lastActive > 45)) {
            QString currentUrl = meta.view->url().toString();
            if (!currentUrl.isEmpty() && currentUrl != "about:blank") {
                meta.savedUrl = currentUrl;
                meta.savedTitle = ctx->tabBar->tabText(static_cast<int>(i));
                meta.isSuspended = true;

                // Unload content to free up RAM
                meta.view->load(QUrl("about:blank"));
                ctx->tabBar->setTabText(static_cast<int>(i), "💤 " + meta.savedTitle);
            }
        }
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

static void update_search_engine_button(BrowserContext *ctx) {
    if (!ctx) return;
    QPixmap px;
    if (ctx->useGoogleSearch) {
        px.loadFromData(reinterpret_cast<const uchar*>(GOOGLE_SVG), static_cast<uint>(strlen(GOOGLE_SVG)), "SVG");
        if (ctx->engineToggleBtn) {
            ctx->engineToggleBtn->setIcon(QIcon(px));
            ctx->engineToggleBtn->setIconSize(QSize(18, 18));
            ctx->engineToggleBtn->setText("");
            ctx->engineToggleBtn->setToolTip("Google Search aktif (Klik untuk ganti ke DuckDuckGo)");
        }
        if (ctx->engineAction) {
            ctx->engineAction->setIcon(QIcon(px));
            ctx->engineAction->setToolTip("Google Search aktif (Klik untuk ganti ke DuckDuckGo)");
        }
    } else {
        px.loadFromData(reinterpret_cast<const uchar*>(DDG_SVG), static_cast<uint>(strlen(DDG_SVG)), "SVG");
        if (ctx->engineToggleBtn) {
            ctx->engineToggleBtn->setIcon(QIcon(px));
            ctx->engineToggleBtn->setIconSize(QSize(18, 18));
            ctx->engineToggleBtn->setText("");
            ctx->engineToggleBtn->setToolTip("DuckDuckGo aktif (Klik untuk ganti ke Google)");
        }
        if (ctx->engineAction) {
            ctx->engineAction->setIcon(QIcon(px));
            ctx->engineAction->setToolTip("DuckDuckGo aktif (Klik untuk ganti ke Google)");
        }
    }
}

static QString generate_start_page_html(BrowserContext *ctx) {
    if (!ctx) return QString();
    QString clockDisp = ctx->showHomeClock ? "" : "display:none !important;";
    QString searchDisp = ctx->showHomeSearch ? "" : "display:none !important;";
    QString tilesDisp = ctx->showHomeTiles ? "" : "display:none !important;";
    QString searchAction = ctx->useGoogleSearch ? "https://www.google.com/search" : "https://html.duckduckgo.com/html/";

    return QString(R"RAW_START(<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Tab Baru</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body {
    width: 100%;
    height: 100vh;
    margin: 0;
    padding: 0;
    background: transparent;
    color: %1;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Ubuntu, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    user-select: none;
    overflow: hidden;
  }
  .clock-container {
    display: flex;
    flex-direction: column;
    align-items: center;
    margin-bottom: 28px;
    %3
  }
  .clock {
    font-size: 68px;
    font-weight: 300;
    color: #F4F4F5;
    letter-spacing: -2px;
    line-height: 1;
    text-shadow: 0 4px 24px rgba(0, 0, 0, 0.40);
  }
  .greeting {
    font-size: 14px;
    font-weight: 400;
    color: rgba(244, 244, 245, 0.65);
    margin-top: 10px;
    letter-spacing: 0.2px;
  }
  form.search-box {
    width: 90%;
    max-width: 620px;
    display: flex;
    align-items: center;
    background: rgba(24, 24, 37, 0.85);
    border: 1px solid rgba(255, 255, 255, 0.16);
    border-radius: 26px;
    padding: 8px 18px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.36), inset 0 1px 0 rgba(255, 255, 255, 0.08);
    transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
    %4
  }
  form.search-box:hover {
    border-color: rgba(255, 255, 255, 0.28);
    box-shadow: 0 12px 36px rgba(0, 0, 0, 0.42), inset 0 1px 0 rgba(255, 255, 255, 0.12);
  }
  form.search-box:focus-within {
    border-color: %2;
    background: rgba(28, 28, 44, 0.96);
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.48), 0 0 0 1px %2;
  }
  .search-icon {
    width: 18px;
    height: 18px;
    stroke: #F4F4F5;
    opacity: 0.85;
    flex-shrink: 0;
  }
  input {
    flex: 1;
    background: transparent;
    border: none;
    outline: none;
    color: #F4F4F5;
    font-size: 15px;
    font-family: inherit;
    padding: 6px 14px;
  }
  input::placeholder {
    color: rgba(255, 255, 255, 0.50);
    opacity: 1;
  }
  .search-btn {
    background: transparent;
    border: none;
    color: %2;
    font-size: 14px;
    font-weight: 600;
    cursor: pointer;
    padding: 4px 10px;
    opacity: 0.9;
    border-radius: 12px;
    transition: opacity 0.15s, background 0.15s;
  }
  .search-btn:hover {
    opacity: 1;
    background: rgba(255, 255, 255, 0.08);
  }
  .tiles {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: 16px;
    margin-top: 32px;
    width: 90%;
    max-width: 640px;
    %5
  }
  .tile {
    display: flex;
    flex-direction: column;
    align-items: center;
    text-decoration: none;
    width: 88px;
    padding: 14px 8px 12px 8px;
    border-radius: 16px;
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.07);
    transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .tile:hover {
    transform: translateY(-3px);
    background: rgba(255, 255, 255, 0.10);
    border-color: rgba(255, 255, 255, 0.20);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.30);
  }
  .tile-icon {
    width: 44px;
    height: 44px;
    border-radius: 12px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid rgba(255, 255, 255, 0.10);
    display: flex;
    align-items: center;
    justify-content: center;
    margin-bottom: 8px;
    transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1);
  }
  .tile:hover .tile-icon {
    background: rgba(255, 255, 255, 0.12);
    border-color: rgba(255, 255, 255, 0.25);
    transform: scale(1.05);
  }
  .tile-name {
    color: rgba(244, 244, 245, 0.85);
    font-size: 12px;
    font-weight: 500;
    text-align: center;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 76px;
  }
</style>
</head>
<body>
  <div class="clock-container">
    <div class="clock" id="clock">--:--</div>
    <div class="greeting" id="greeting"></div>
  </div>
  <form class="search-box" action="%6" method="get">
    <svg class="search-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <circle cx="11" cy="11" r="8"/>
      <line x1="21" y1="21" x2="16.65" y2="16.65"/>
    </svg>
    <input type="text" name="q" placeholder="Cari di web..." autofocus autocomplete="off">
    <button type="submit" class="search-btn">Cari</button>
  </form>
  <div class="tiles">
    <a class="tile" href="https://www.google.com">
      <div class="tile-icon">
        <svg width="24" height="24" viewBox="0 0 24 24"><path fill="#4285F4" d="M23.745 12.27c0-.7-.06-1.4-.19-2.07H12v4.51h6.6c-.29 1.52-1.14 2.82-2.4 3.68v3.05h3.88c2.27-2.09 3.66-5.17 3.66-9.17z"/><path fill="#34A853" d="M12 24c3.24 0 5.95-1.08 7.93-2.91l-3.88-3.05c-1.08.72-2.45 1.16-4.05 1.16-3.12 0-5.77-2.1-6.72-4.93H1.25v3.15C3.26 21.36 7.33 24 12 24z"/><path fill="#FBBC05" d="M5.28 14.27c-.25-.72-.38-1.49-.38-2.27s.13-1.55.38-2.27V6.58H1.25C.45 8.18 0 10.03 0 12s.45 3.82 1.25 5.42l4.03-3.15z"/><path fill="#EA4335" d="M12 4.75c1.77 0 3.35.61 4.6 1.8l3.42-3.42C17.95 1.19 15.24 0 12 0 7.33 0 3.26 2.64 1.25 6.58l4.03 3.15c.95-2.83 3.6-4.98 6.72-4.98z"/></svg>
      </div>
      <span class="tile-name">Google</span>
    </a>
    <a class="tile" href="https://duckduckgo.com">
      <div class="tile-icon">
        <svg width="24" height="24" viewBox="0 0 24 24"><circle cx="12" cy="12" r="11" fill="#DE5833"/><path d="M16.5 11.5c-.7-1.7-2.5-2.5-4.5-2.5s-3.8.8-4.3 2.5c-.5 1.4 0 3.3 1.4 4.3 1.4 1 4.3 1.4 6.2.5 1.9-.9 1.9-3.4 1.2-4.8z" fill="#FFF"/><circle cx="10" cy="11" r="1" fill="#222"/><path d="M7.8 12.5c-1.4 0-2.3 1.8-1.8 2.8.5 1 2 0 2.8-.8l-1-2z" fill="#F9A03F"/><path d="M11 16.5c1 0 2.3-.5 3.2-1.4l-1-1c-1 .5-2 .5-2.2.5v1.9z" fill="#4BA3E3"/></svg>
      </div>
      <span class="tile-name">DuckDuckGo</span>
    </a>
    <a class="tile" href="https://github.com">
      <div class="tile-icon">
        <svg width="24" height="24" viewBox="0 0 24 24" fill="#F4F4F5"><path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0024 12c0-6.63-5.37-12-12-12z"/></svg>
      </div>
      <span class="tile-name">GitHub</span>
    </a>
    <a class="tile" href="https://youtube.com">
      <div class="tile-icon">
        <svg width="24" height="24" viewBox="0 0 24 24"><rect width="24" height="17" x="0" y="3.5" fill="#FF0000" rx="4.5"/><polygon points="9.5,7.5 16,12 9.5,16.5" fill="#FFFFFF"/></svg>
      </div>
      <span class="tile-name">YouTube</span>
    </a>
    <a class="tile" href="https://wikipedia.org">
      <div class="tile-icon">
        <svg width="24" height="24" viewBox="0 0 24 24" fill="#F4F4F5"><path d="M12.09 13.62l1.97-5.88h1.86l-3.08 8.44h-1.63L8.9 9.87l-2.3 6.31H4.97L1.92 7.74h1.72l1.94 5.97 1.83-5.97h1.53l2.29 5.88 1.97-5.88h1.72l-2.83 7.94 2.02-2.06zM22.08 7.74h1.92l-4.11 8.44h-1.67l-2.61-6.19 1.58-.2 1.86 4.45 3.03-6.5z"/></svg>
      </div>
      <span class="tile-name">Wikipedia</span>
    </a>
    <a class="tile" href="https://reddit.com">
      <div class="tile-icon">
        <svg width="24" height="24" viewBox="0 0 24 24"><circle cx="12" cy="12" r="11" fill="#FF4500"/><path fill="#FFF" d="M19.3 12.3c0-.8-.6-1.4-1.4-1.4-.4 0-.8.2-1 .5-1.1-.8-2.6-1.3-4.3-1.4l.7-3.4 2.4.5c0 .6.5 1.1 1.1 1.1.6 0 1.1-.5 1.1-1.1s-.5-1.1-1.1-1.1c-.5 0-.9.3-1 .8l-2.7-.6c-.2 0-.3.1-.4.3l-.9 4c-1.7.1-3.2.6-4.3 1.4-.3-.3-.6-.5-1-.5-.8 0-1.4.6-1.4 1.4 0 .6.4 1.1.9 1.3-.1.3-.1.6-.1.9 0 2.3 2.6 4.1 5.9 4.1s5.9-1.8 5.9-4.1c0-.3 0-.6-.1-.9.5-.2.9-.7.9-1.3zm-10.4.7c0-.6.5-1.1 1.1-1.1s1.1.5 1.1 1.1-.5 1.1-1.1 1.1-1.1-.5-1.1-1.1zm6.2 2.8c-.8.8-2.3.8-3.1.8-.8 0-2.3 0-3.1-.8-.2-.2-.2-.4 0-.6.2-.2.4-.2.6 0 .6.6 1.8.6 2.5.6.7 0 1.9 0 2.5-.6.2-.2.4-.2.6 0 .2.2.2.4 0 .6zm-.2-1.7c-.6 0-1.1-.5-1.1-1.1s.5-1.1 1.1-1.1 1.1.5 1.1 1.1-.5 1.1-1.1 1.1z"/></svg>
      </div>
      <span class="tile-name">Reddit</span>
    </a>
  </div>
  <script>
    function updateTime() {
      var d = new Date();
      var h = ('0' + d.getHours()).slice(-2);
      var m = ('0' + d.getMinutes()).slice(-2);
      var el = document.getElementById('clock');
      if (el) el.textContent = h + ':' + m;

      var days = ['Minggu', 'Senin', 'Selasa', 'Rabu', 'Kamis', 'Jumat', 'Sabtu'];
      var months = ['Januari', 'Februari', 'Maret', 'April', 'Mei', 'Juni', 'Juli', 'Agustus', 'September', 'Oktober', 'November', 'Desember'];
      var gEl = document.getElementById('greeting');
      if (gEl) {
        gEl.textContent = days[d.getDay()] + ', ' + d.getDate() + ' ' + months[d.getMonth()];
      }
    }
    updateTime();
    setInterval(updateTime, 1000);
  </script>
</body>
</html>)RAW_START")
    .arg(ctx->fgColor)
    .arg(ctx->accentColor)
    .arg(clockDisp)
    .arg(searchDisp)
    .arg(tilesDisp)
    .arg(searchAction);
}

static QString generate_dialog_stylesheet(const QString &bg, const QString &fg, const QString &accent) {
    QColor bgCol(bg);
    bool isLight = bgCol.isValid() && bgCol.lightnessF() > 0.5;
    QString inputBg = isLight ? "rgba(0, 0, 0, 0.04)" : "rgba(255, 255, 255, 0.05)";
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
    border-radius: 4px;
    padding: 5px 8px;
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
    border-radius: 4px;
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
    border-radius: 4px;
    padding: 4px;
}
QListWidget::item {
    padding: 6px 8px;
    border-radius: 4px;
}
QListWidget::item:hover {
    background-color: %6;
}
QListWidget::item:selected {
    background-color: %3;
    color: %1;
}
)").arg(bg, fg, accent, inputBg, borderCol, btnBg, btnHover);
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
    if (url.isEmpty() || url == "falkon:home" || url == "about:blank") return false;
    QString bmPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/bookmarks.json";
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
    ctx->bookmarkAction->setToolTip(isBookmarked ? "Hapus dari Bookmark (Ctrl+D)" : "Tambahkan ke Bookmark (Ctrl+D)");
}

static void toggle_bookmark_current_page(BrowserContext *ctx) {
    if (!ctx || !ctx->viewStack) return;
    auto *curView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (!curView) return;
    QString url = curView->url().toString();
    QString title = curView->title();
    if (title.isEmpty()) title = url;
    if (url.isEmpty() || url == "falkon:home" || url == "about:blank") return;

    QString bmPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/bookmarks.json";
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
}

static void show_bookmarks_dialog(BrowserContext *ctx) {
    if (!ctx || !ctx->window) return;
    QDialog dlg(ctx->window);
    dlg.setWindowTitle("Bookmarks");
    dlg.resize(480, 360);
    dlg.setStyleSheet(generate_dialog_stylesheet(ctx->bgColor, ctx->fgColor, ctx->accentColor));

    auto *layout = new QVBoxLayout(&dlg);
    auto *listWidget = new QListWidget(&dlg);

    QString bmPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/bookmarks.json";
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

    auto *btnBox = new QHBoxLayout();
    auto *openBtn = new QPushButton("Buka", &dlg);
    auto *delBtn = new QPushButton("Hapus", &dlg);
    auto *closeBtn = new QPushButton("Tutup", &dlg);
    btnBox->addWidget(openBtn);
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
    dlg.setWindowTitle("Riwayat Penjelajahan");
    dlg.resize(540, 400);
    dlg.setStyleSheet(generate_dialog_stylesheet(ctx->bgColor, ctx->fgColor, ctx->accentColor));

    auto *layout = new QVBoxLayout(&dlg);
    auto *listWidget = new QListWidget(&dlg);

    QString histPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/history.json";
    QFile f(histPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (int i = arr.size() - 1; i >= 0; --i) {
                QJsonObject obj = arr[i].toObject();
                QString title = obj["title"].toString();
                QString url = obj["url"].toString();
                auto *item = new QListWidgetItem(QString("%1\n%2").arg(title.isEmpty() ? url : title, url), listWidget);
                item->setData(Qt::UserRole, url);
            }
        }
    }

    layout->addWidget(listWidget);

    auto *btnBox = new QHBoxLayout();
    auto *openBtn = new QPushButton("Buka", &dlg);
    auto *clearBtn = new QPushButton("Hapus Semua", &dlg);
    auto *closeBtn = new QPushButton("Tutup", &dlg);
    btnBox->addWidget(openBtn);
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

    QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
        listWidget->clear();
        QFile wf(histPath);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            wf.write("[]");
        }
    });

    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

static void show_find_dialog(BrowserContext *ctx) {
    if (!ctx || !ctx->window || !ctx->viewStack) return;
    auto *curView = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
    if (!curView) return;

    bool ok = false;
    QString query = QInputDialog::getText(ctx->window, "Cari di Halaman", "Masukkan kata yang dicari:", QLineEdit::Normal, "", &ok);
    if (ok && !query.isEmpty()) {
        curView->findText(query);
    }
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

static void save_browser_config(BrowserContext *ctx, const QString &homepage = "") {
    if (!ctx) return;
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/config.json";
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
        root["homepage"] = "falkon:home";
    }
    root["show_nav_buttons"] = (ctx->backBtn && ctx->backBtn->isVisible());
    root["show_adblock_badge"] = (ctx->adblockBadge && ctx->adblockBadge->isVisible());
    root["show_new_tab_button"] = true;
    root["default_zoom"] = ctx->currentZoom;
    root["download_dir"] = ctx->downloadDir;
    root["enable_adblock"] = true;
    root["enable_cosmetic_filter"] = true;
    root["user_agent"] = "";
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
    if (ctx->window) {
        root["window_width"] = ctx->window->width();
        root["window_height"] = ctx->window->height();
    }

    QFile wf(configPath);
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        wf.write(QJsonDocument(root).toJson());
        wf.close();
    }
}

static QString generate_settings_page_html(BrowserContext *ctx);

static void refresh_internal_tabs(BrowserContext *ctx) {
    if (!ctx) return;
    for (auto &t : ctx->tabs) {
        if (!t.view) continue;
        if (t.savedUrl == "falkon:settings") {
            t.view->page()->setBackgroundColor(Qt::transparent);
            t.view->setHtml(generate_settings_page_html(ctx), QUrl("falkon:settings"));
        } else if (t.savedUrl == "falkon:home") {
            t.view->page()->setBackgroundColor(Qt::transparent);
            t.view->setHtml(generate_start_page_html(ctx), QUrl("falkon:home"));
        }
    }
}

static QString generate_settings_page_html(BrowserContext *ctx) {
    if (!ctx) return QString();

    QString curTheme = ctx->currentTheme.toLower();
    bool isGoogle = ctx->useGoogleSearch;

    QString hp = "falkon:home";
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/config.json";
    QFile f(configPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject() && doc.object().contains("homepage")) {
            hp = doc.object()["homepage"].toString();
        }
    }

    QString tpl = R"RAW_HTML(<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Pengaturan</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body {
    width: 100%;
    height: 100vh;
    margin: 0;
    padding: 0;
    background: transparent;
    color: @FG@;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Ubuntu, sans-serif;
    user-select: none;
    overflow: hidden;
  }
  .app-container {
    display: flex;
    width: 100%;
    height: 100%;
    background: rgba(17, 17, 27, 0.78);
  }
  /* Sidebar */
  .sidebar {
    width: 250px;
    background: rgba(0, 0, 0, 0.20);
    border-right: 1px solid rgba(255, 255, 255, 0.08);
    display: flex;
    flex-direction: column;
    padding: 28px 16px;
    gap: 6px;
    flex-shrink: 0;
  }
  .sidebar-header {
    font-size: 19px;
    font-weight: 600;
    padding: 0 12px 20px 12px;
    letter-spacing: -0.3px;
    display: flex;
    align-items: center;
    gap: 10px;
    color: #ffffff;
  }
  .nav-btn {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 10px 14px;
    border-radius: 8px;
    border: none;
    background: transparent;
    color: @FG@;
    opacity: 0.8;
    font-size: 13.5px;
    font-weight: 500;
    cursor: pointer;
    text-align: left;
    transition: all 0.15s ease;
  }
  .nav-btn:hover {
    background: rgba(255, 255, 255, 0.08);
    opacity: 1;
  }
  .nav-btn.active {
    background: rgba(255, 255, 255, 0.14);
    color: @ACCENT@;
    opacity: 1;
    font-weight: 600;
  }
  /* Content Area */
  .content {
    flex: 1;
    overflow-y: auto;
    padding: 36px 48px;
    display: flex;
    flex-direction: column;
    gap: 24px;
    max-width: 820px;
  }
  .tab-pane {
    display: none;
    flex-direction: column;
    gap: 20px;
  }
  .tab-pane.active {
    display: flex;
  }
  .section-title {
    font-size: 22px;
    font-weight: 600;
    margin-bottom: 2px;
    color: #ffffff;
  }
  .section-desc {
    font-size: 13px;
    opacity: 0.7;
    margin-bottom: 8px;
  }
  /* Cards */
  .card {
    background: rgba(255, 255, 255, 0.04);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 12px;
    overflow: hidden;
  }
  .card-item {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 16px 20px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    gap: 16px;
  }
  .card-item:last-child {
    border-bottom: none;
  }
  .card-item-info {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }
  .card-item-title {
    font-size: 14px;
    font-weight: 500;
    color: #ffffff;
  }
  .card-item-desc {
    font-size: 12px;
    opacity: 0.65;
  }
  /* Modern Toggle Switch */
  .switch {
    position: relative;
    display: inline-block;
    width: 44px;
    height: 24px;
    flex-shrink: 0;
  }
  .switch input {
    opacity: 0;
    width: 0;
    height: 0;
  }
  .slider {
    position: absolute;
    cursor: pointer;
    top: 0; left: 0; right: 0; bottom: 0;
    background-color: rgba(255, 255, 255, 0.16);
    transition: 0.2s cubic-bezier(0.4, 0, 0.2, 1);
    border-radius: 24px;
  }
  .slider:before {
    position: absolute;
    content: "";
    height: 18px;
    width: 18px;
    left: 3px;
    bottom: 3px;
    background-color: #ffffff;
    transition: 0.2s cubic-bezier(0.4, 0, 0.2, 1);
    border-radius: 50%;
  }
  input:checked + .slider {
    background-color: @ACCENT@;
  }
  input:checked + .slider:before {
    transform: translateX(20px);
  }
  /* Inputs & Buttons */
  .text-input {
    background: rgba(0, 0, 0, 0.35);
    border: 1px solid rgba(255, 255, 255, 0.15);
    color: #ffffff;
    padding: 8px 12px;
    border-radius: 6px;
    font-size: 13px;
    outline: none;
    min-width: 220px;
    transition: border-color 0.15s;
  }
  .text-input:focus {
    border-color: @ACCENT@;
  }
  .btn {
    background: rgba(255, 255, 255, 0.08);
    border: 1px solid rgba(255, 255, 255, 0.12);
    color: #ffffff;
    padding: 8px 16px;
    border-radius: 6px;
    font-size: 13px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .btn:hover {
    background: rgba(255, 255, 255, 0.14);
  }
  .btn-accent {
    background: @ACCENT@;
    color: #11111b;
    border: none;
    font-weight: 600;
  }
  .btn-accent:hover {
    opacity: 0.9;
  }
  .btn-danger {
    background: rgba(243, 139, 168, 0.15);
    color: #f38ba8;
    border: 1px solid rgba(243, 139, 168, 0.3);
  }
  .btn-danger:hover {
    background: rgba(243, 139, 168, 0.25);
  }
  /* Palette Presets Grid */
  .palette-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
    gap: 12px;
    padding: 16px 20px;
  }
  .palette-card {
    background: rgba(255, 255, 255, 0.03);
    border: 1.5px solid rgba(255, 255, 255, 0.10);
    border-radius: 10px;
    padding: 14px;
    cursor: pointer;
    display: flex;
    flex-direction: column;
    gap: 10px;
    transition: all 0.15s ease;
  }
  .palette-card:hover {
    background: rgba(255, 255, 255, 0.07);
    border-color: rgba(255, 255, 255, 0.25);
  }
  .palette-card.selected {
    border-color: @ACCENT@;
    background: rgba(255, 255, 255, 0.08);
  }
  .palette-dots {
    display: flex;
    gap: 6px;
  }
  .dot {
    width: 20px;
    height: 20px;
    border-radius: 50%;
    border: 1px solid rgba(255, 255, 255, 0.15);
  }
  .palette-name {
    font-size: 13px;
    font-weight: 500;
    color: #ffffff;
  }
  /* Color swatches */
  .swatch-row {
    display: flex;
    gap: 6px;
    align-items: center;
  }
  .swatch {
    width: 22px;
    height: 22px;
    border-radius: 50%;
    cursor: pointer;
    border: 2px solid rgba(255, 255, 255, 0.15);
    transition: transform 0.12s ease, border-color 0.12s ease;
  }
  .swatch:hover {
    transform: scale(1.18);
    border-color: #ffffff;
  }
  /* Engine options */
  .engine-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    padding: 16px 20px;
  }
  .engine-card {
    background: rgba(255, 255, 255, 0.03);
    border: 1.5px solid rgba(255, 255, 255, 0.10);
    border-radius: 10px;
    padding: 14px 18px;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 14px;
    transition: all 0.15s ease;
  }
  .engine-card:hover {
    background: rgba(255, 255, 255, 0.07);
  }
  .engine-card.selected {
    border-color: @ACCENT@;
    background: rgba(255, 255, 255, 0.08);
  }
  .engine-icon {
    width: 28px;
    height: 28px;
    flex-shrink: 0;
  }
  .engine-title {
    font-size: 14px;
    font-weight: 500;
    color: #ffffff;
  }
</style>
</head>
<body>
  <div class="app-container">
    <div class="sidebar">
      <div class="sidebar-header">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="@ACCENT@" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
        <span>Pengaturan</span>
      </div>
      <button class="nav-btn active" id="nav-general" onclick="showTab('general')">Umum</button>
      <button class="nav-btn" id="nav-appearance" onclick="showTab('appearance')">Tampilan & Tema</button>
      <button class="nav-btn" id="nav-search" onclick="showTab('search')">Pencarian</button>
      <button class="nav-btn" id="nav-privacy" onclick="showTab('privacy')">Privasi & Iklan</button>
      <button class="nav-btn" id="nav-advanced" onclick="showTab('advanced')">Lanjutan</button>
    </div>

    <div class="content">
      <!-- 1. UMUM -->
      <div class="tab-pane active" id="pane-general">
        <div>
          <div class="section-title">Umum</div>
          <div class="section-desc">Konfigurasi beranda, skala tampilan, dan unduhan</div>
        </div>
        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Halaman Beranda</div>
              <div class="card-item-desc">Alamat halaman saat tab baru atau browser dibuka</div>
            </div>
            <input type="text" class="text-input" value="@HOMEPAGE@" onchange="sendAction('homepage', {val: this.value})">
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Skala Zoom Default</div>
              <div class="card-item-desc">Tingkat pembesaran halaman saat pertama dibuka</div>
            </div>
            <select class="text-input" style="min-width:140px;" onchange="sendAction('zoom', {val: this.value})">
              <option value="0.8" @ZOOM_08@>80%</option>
              <option value="0.9" @ZOOM_09@>90%</option>
              <option value="1.0" @ZOOM_10@>100% (Normal)</option>
              <option value="1.1" @ZOOM_11@>110%</option>
              <option value="1.25" @ZOOM_125@>125%</option>
              <option value="1.5" @ZOOM_15@>150%</option>
            </select>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Folder Unduhan</div>
              <div class="card-item-desc">@DOWNLOAD_DIR@</div>
            </div>
            <button class="btn" onclick="sendAction('open_downloads', {})">Buka Folder</button>
          </div>
        </div>
      </div>

      <!-- 2. TAMPILAN & TEMA -->
      <div class="tab-pane" id="pane-appearance">
        <div>
          <div class="section-title">Tampilan & Tema</div>
          <div class="section-desc">Sesuaikan preset tema, warna aksen, tata letak, dan kustomisasi lanjutan</div>
        </div>

        <!-- 1. KUSTOMISASI MUDAH (Selalu Tampil) -->
        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Preset Palet Tema</div>
              <div class="card-item-desc">Pilih tema siap pakai yang langsung diterapkan secara real-time</div>
            </div>
          </div>
          <div class="palette-grid">
            <div class="palette-card @THEME_DARK_SEL@" id="theme-dark_glass" onclick="selectTheme('dark_glass')">
              <div class="palette-dots">
                <div class="dot" style="background: #11111b;"></div>
                <div class="dot" style="background: #181825;"></div>
                <div class="dot" style="background: #89b4fa;"></div>
              </div>
              <div class="palette-name">Dark Glass</div>
            </div>
            <div class="palette-card @THEME_CATPPUCCIN_SEL@" id="theme-catppuccin" onclick="selectTheme('catppuccin')">
              <div class="palette-dots">
                <div class="dot" style="background: #1e1e2e;"></div>
                <div class="dot" style="background: #181825;"></div>
                <div class="dot" style="background: #cba6f7;"></div>
              </div>
              <div class="palette-name">Catppuccin</div>
            </div>
            <div class="palette-card @THEME_LIGHT_SEL@" id="theme-light" onclick="selectTheme('light')">
              <div class="palette-dots">
                <div class="dot" style="background: #eff1f5;"></div>
                <div class="dot" style="background: #e6e9ef;"></div>
                <div class="dot" style="background: #1e66f5;"></div>
              </div>
              <div class="palette-name">Light</div>
            </div>
            <div class="palette-card @THEME_MINIMAL_SEL@" id="theme-minimal" onclick="selectTheme('minimal')">
              <div class="palette-dots">
                <div class="dot" style="background: #121212;"></div>
                <div class="dot" style="background: #1c1c1c;"></div>
                <div class="dot" style="background: #ffffff;"></div>
              </div>
              <div class="palette-name">Minimal</div>
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Warna Aksen</div>
              <div class="card-item-desc">Warna sorotan untuk tab aktif, tombol, dan link</div>
            </div>
            <div style="display: flex; align-items: center; gap: 8px;">
              <div class="swatch-row">
                <div class="swatch" style="background: #89b4fa;" onclick="setAccent('#89b4fa')"></div>
                <div class="swatch" style="background: #cba6f7;" onclick="setAccent('#cba6f7')"></div>
                <div class="swatch" style="background: #a6e3a1;" onclick="setAccent('#a6e3a1')"></div>
                <div class="swatch" style="background: #fab387;" onclick="setAccent('#fab387')"></div>
                <div class="swatch" style="background: #f38ba8;" onclick="setAccent('#f38ba8')"></div>
                <div class="swatch" style="background: #1e66f5;" onclick="setAccent('#1e66f5')"></div>
                <div class="swatch" style="background: #fe8019;" onclick="setAccent('#fe8019')"></div>
                <div class="swatch" style="background: #ffffff;" onclick="setAccent('#ffffff')"></div>
              </div>
              <input type="color" id="accentColorPicker" value="@ACCENT@" style="width: 32px; height: 32px; border: none; border-radius: 6px; cursor: pointer; background: transparent;" onchange="setAccent(this.value)">
            </div>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Transparansi Background</div>
              <div class="card-item-desc">Tingkat kepekatan latar belakang browser (30% - 100%)</div>
            </div>
            <div style="display: flex; align-items: center; gap: 12px; min-width: 220px;">
              <input type="range" id="opacitySlider" min="30" max="100" value="@OPACITY_VAL@" style="flex: 1; accent-color: @ACCENT@;" oninput="document.getElementById('opacityLabel').textContent = this.value + '%'" onchange="sendAction('bg_opacity', {val: this.value})">
              <span id="opacityLabel" style="font-size: 13px; font-weight: 500; min-width: 42px; text-align: right;">@OPACITY_VAL@%</span>
            </div>
          </div>
        </div>

        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Posisi Tab Bar</div>
              <div class="card-item-desc">Letak deretan tab di jendela browser</div>
            </div>
            <select class="text-input" style="min-width: 150px;" onchange="sendAction('tab_position', {val: this.value})">
              <option value="top" @TAB_POS_TOP@>Atas</option>
              <option value="left" @TAB_POS_LEFT@>Kiri</option>
              <option value="hidden" @TAB_POS_HIDDEN@>Sembunyikan</option>
            </select>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Ukuran Antarmuka (UI Density)</div>
              <div class="card-item-desc">Kerapatan ruang tab, address bar, dan tombol</div>
            </div>
            <select class="text-input" style="min-width: 150px;" onchange="sendAction('ui_density', {val: this.value})">
              <option value="comfortable" @DENSITY_COMF@>Nyaman (Comfortable)</option>
              <option value="compact" @DENSITY_COMP@>Padat (Compact)</option>
            </select>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Elemen Halaman Tab Baru (Home)</div>
              <div class="card-item-desc">Komponen yang aktif pada halaman tab baru</div>
            </div>
            <div style="display: flex; gap: 16px; align-items: center;">
              <label style="display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer;">
                <input type="checkbox" id="homeClockToggle" @HOME_CLOCK_CHECKED@ onchange="sendAction('home_toggle', {element: 'clock', val: this.checked ? '1' : '0'})"> Jam
              </label>
              <label style="display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer;">
                <input type="checkbox" id="homeSearchToggle" @HOME_SEARCH_CHECKED@ onchange="sendAction('home_toggle', {element: 'search', val: this.checked ? '1' : '0'})"> Search Bar
              </label>
              <label style="display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer;">
                <input type="checkbox" id="homeTilesToggle" @HOME_TILES_CHECKED@ onchange="sendAction('home_toggle', {element: 'tiles', val: this.checked ? '1' : '0'})"> Quick Links
              </label>
            </div>
          </div>
        </div>

        <!-- 2. KUSTOMISASI BEBAS / LANJUTAN (Collapsible) -->
        <details class="card" style="margin-top: 4px;">
          <summary style="padding: 16px 20px; font-weight: 600; cursor: pointer; display: flex; align-items: center; justify-content: space-between; outline: none; list-style: none;">
            <span>Kustomisasi Lanjutan (Token Warna, CSS/QSS & Backup)</span>
            <span style="font-size: 12px; opacity: 0.65;">Klik untuk buka/tutup ▾</span>
          </summary>
          <div style="padding: 0 20px 20px 20px; display: flex; flex-direction: column; gap: 20px;">
            <div style="border-top: 1px solid rgba(255, 255, 255, 0.08); padding-top: 16px;">
              <div class="card-item-title" style="margin-bottom: 4px;">Editor Token Warna Lengkap</div>
              <div class="card-item-desc" style="margin-bottom: 14px;">Atur nilai warna spesifik (Hex / RGBA) untuk setiap elemen browser</div>

              <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
                <div style="display: flex; align-items: center; gap: 10px;">
                  <input type="color" id="pickerBg" value="@BG@" onchange="document.getElementById('tokenBg').value = this.value" style="width: 32px; height: 32px; border: none; border-radius: 6px; cursor: pointer; background: transparent;">
                  <div style="flex: 1;">
                    <div style="font-size: 11px; opacity: 0.7;">Background</div>
                    <input type="text" id="tokenBg" class="text-input" style="min-width: 0; width: 100%; padding: 6px 10px;" value="@BG@" onchange="document.getElementById('pickerBg').value = this.value">
                  </div>
                </div>
                <div style="display: flex; align-items: center; gap: 10px;">
                  <input type="color" id="pickerSurface" value="@SURFACE@" onchange="document.getElementById('tokenSurface').value = this.value" style="width: 32px; height: 32px; border: none; border-radius: 6px; cursor: pointer; background: transparent;">
                  <div style="flex: 1;">
                    <div style="font-size: 11px; opacity: 0.7;">Surface (Toolbar/Tab)</div>
                    <input type="text" id="tokenSurface" class="text-input" style="min-width: 0; width: 100%; padding: 6px 10px;" value="@SURFACE@" onchange="document.getElementById('pickerSurface').value = this.value">
                  </div>
                </div>
                <div style="display: flex; align-items: center; gap: 10px;">
                  <input type="color" id="pickerFg" value="@FG@" onchange="document.getElementById('tokenFg').value = this.value" style="width: 32px; height: 32px; border: none; border-radius: 6px; cursor: pointer; background: transparent;">
                  <div style="flex: 1;">
                    <div style="font-size: 11px; opacity: 0.7;">Teks Utama</div>
                    <input type="text" id="tokenFg" class="text-input" style="min-width: 0; width: 100%; padding: 6px 10px;" value="@FG@" onchange="document.getElementById('pickerFg').value = this.value">
                  </div>
                </div>
                <div style="display: flex; align-items: center; gap: 10px;">
                  <input type="color" id="pickerAccent" value="@ACCENT@" onchange="document.getElementById('tokenAccent').value = this.value" style="width: 32px; height: 32px; border: none; border-radius: 6px; cursor: pointer; background: transparent;">
                  <div style="flex: 1;">
                    <div style="font-size: 11px; opacity: 0.7;">Warna Aksen</div>
                    <input type="text" id="tokenAccent" class="text-input" style="min-width: 0; width: 100%; padding: 6px 10px;" value="@ACCENT@" onchange="document.getElementById('pickerAccent').value = this.value">
                  </div>
                </div>
                <div style="display: flex; align-items: center; gap: 10px;">
                  <input type="color" id="pickerBorder" value="@BORDER@" onchange="document.getElementById('tokenBorder').value = this.value" style="width: 32px; height: 32px; border: none; border-radius: 6px; cursor: pointer; background: transparent;">
                  <div style="flex: 1;">
                    <div style="font-size: 11px; opacity: 0.7;">Border</div>
                    <input type="text" id="tokenBorder" class="text-input" style="min-width: 0; width: 100%; padding: 6px 10px;" value="@BORDER@" onchange="document.getElementById('pickerBorder').value = this.value">
                  </div>
                </div>
              </div>
              <div style="display: flex; justify-content: flex-end; margin-top: 12px;">
                <button class="btn btn-accent" onclick="applyTokens()">Terapkan Token Warna</button>
              </div>
            </div>

            <!-- Custom QSS / CSS Editor -->
            <div style="border-top: 1px solid rgba(255, 255, 255, 0.08); padding-top: 16px;">
              <div class="card-item-title" style="margin-bottom: 4px;">Custom QSS (Live Reload)</div>
              <div class="card-item-desc" style="margin-bottom: 10px;">Tulis aturan Qt StyleSheet untuk menimpa gaya jendela browser secara instan</div>
              <textarea id="customQssInput" class="text-input" style="width: 100%; height: 90px; font-family: monospace; font-size: 12px; resize: vertical;" placeholder="/* Contoh: QTabBar::tab { border-radius: 4px; } */">@CUSTOM_QSS@</textarea>
              <div style="display: flex; justify-content: flex-end; margin-top: 8px;">
                <button class="btn btn-accent" onclick="applyCustomQss()">Terapkan QSS</button>
              </div>
            </div>

            <!-- Backup / Restore / Reset -->
            <div style="border-top: 1px solid rgba(255, 255, 255, 0.08); padding-top: 16px; display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 10px;">
              <div style="display: flex; gap: 8px;">
                <button class="btn" onclick="exportTheme()">Export Tema (.json)</button>
                <input type="file" id="themeFileInput" style="display:none;" accept=".json" onchange="importThemeFile(this)">
                <button class="btn" onclick="document.getElementById('themeFileInput').click()">Import Tema (.json)</button>
              </div>
              <button class="btn btn-danger" onclick="resetThemeDefaults()">Reset ke Pengaturan Awal</button>
            </div>
          </div>
        </details>
      </div>

      <!-- 3. PENCARIAN -->
      <div class="tab-pane" id="pane-search">
        <div>
          <div class="section-title">Pencarian</div>
          <div class="section-desc">Pilih mesin pencari default yang digunakan pada bilah alamat</div>
        </div>
        <div class="card">
          <div class="engine-grid">
            <div class="engine-card @ENGINE_GOOGLE_SEL@" id="engine-google" onclick="selectEngine('google')">
              <svg class="engine-icon" viewBox="0 0 48 48">
                <path fill="#EA4335" d="M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z"/>
                <path fill="#4285F4" d="M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z"/>
                <path fill="#FBBC05" d="M10.53 28.59c-.48-1.45-.76-2.99-.76-4.59s.27-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z"/>
                <path fill="#34A853" d="M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z"/>
              </svg>
              <div>
                <div class="engine-title">Google</div>
                <div class="card-item-desc">Hasil pencarian lengkap dan terpopuler</div>
              </div>
            </div>
            <div class="engine-card @ENGINE_DDG_SEL@" id="engine-duckduckgo" onclick="selectEngine('duckduckgo')">
              <svg class="engine-icon" viewBox="0 0 48 48">
                <circle cx="24" cy="24" r="23" fill="#DE5833"/>
                <path fill="#FFF" d="M34 23c-1.5-3.5-5-5-10-5s-8 2-9 5c-1 3 0 7 3 9 3 2 9 3 13 1 4-2 4-7 3-10z"/>
                <circle cx="20" cy="22" r="2" fill="#222"/>
                <path fill="#F9A03F" d="M15 25c-3 1-5 4-4 6 1 1 4 0 6-2l-2-4z"/>
                <path fill="#4BA3E3" d="M22 34c2 0 5-1 7-3l-2-2c-2 1-4 1-5 1v4z"/>
              </svg>
              <div>
                <div class="engine-title">DuckDuckGo Lite</div>
                <div class="card-item-desc">Privasi tinggi, bebas pelacakan, ultra-ringan</div>
              </div>
            </div>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Router Pintasan Mesin Pencari</div>
              <div class="card-item-desc">Ketik awalan "!g kata_kunci" untuk cari Google, atau "!d kata_kunci" untuk cari DuckDuckGo secara instan di bilah alamat</div>
            </div>
          </div>
        </div>
      </div>

      <!-- 4. PRIVASI & IKLAN -->
      <div class="tab-pane" id="pane-privacy">
        <div>
          <div class="section-title">Privasi & Iklan</div>
          <div class="section-desc">Proteksi uBlock Origin bawaan dan pembersihan riwayat browser</div>
        </div>
        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Pemblokir Iklan uBlock Origin</div>
              <div class="card-item-desc">Blokir pelacak, iklan banner, malware domain, dan analitik pihak ketiga</div>
            </div>
            <label class="switch">
              <input type="checkbox" checked disabled>
              <span class="slider"></span>
            </label>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Filter Kosmetik CSS</div>
              <div class="card-item-desc">Secara otomatis menyembunyikan elemen kosong placeholder iklan pada halaman web</div>
            </div>
            <label class="switch">
              <input type="checkbox" checked disabled>
              <span class="slider"></span>
            </label>
          </div>
        </div>
        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Bersihkan Cache Memori</div>
              <div class="card-item-desc">Kosongkan cache HTTP dan shader WebEngine untuk menyegarkan memori RAM</div>
            </div>
            <button class="btn" onclick="sendAction('clear_cache', {}); alert('Cache memori berhasil dibersihkan.');">Bersihkan Cache</button>
          </div>
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Hapus Riwayat Penjelajahan</div>
              <div class="card-item-desc">Hapus seluruh catatan riwayat situs yang pernah Anda kunjungi</div>
            </div>
            <button class="btn btn-danger" onclick="if(confirm('Hapus seluruh riwayat penjelajahan?')) { sendAction('clear_history', {}); alert('Riwayat berhasil dihapus.'); }">Hapus Riwayat</button>
          </div>
        </div>
      </div>

      <!-- 5. LANJUTAN -->
      <div class="tab-pane" id="pane-advanced">
        <div>
          <div class="section-title">Lanjutan</div>
          <div class="section-desc">Pengoptimalan performa memori dan pembersihan background</div>
        </div>
        <div class="card">
          <div class="card-item">
            <div class="card-item-info">
              <div class="card-item-title">Penghemat Memori Otomatis (Smart Memory)</div>
              <div class="card-item-desc">Tangguhkan (suspend) tab latar belakang setelah 3 menit tidak aktif untuk menghemat baterai & RAM</div>
            </div>
            <label class="switch">
              <input type="checkbox" checked onchange="sendAction('smart_memory', {})">
              <span class="slider"></span>
            </label>
          </div>
        </div>
      </div>
    </div>
  </div>

  <script>
    function sendAction(action, params) {
      params = params || {};
      var qs = new URLSearchParams(params).toString();
      var url = "falkon-set://" + action + (qs ? "?" + qs : "");
      var iframe = document.createElement("iframe");
      iframe.style.display = "none";
      iframe.src = url;
      document.body.appendChild(iframe);
      setTimeout(function() { iframe.remove(); }, 150);
    }

    function showTab(id) {
      document.querySelectorAll('.tab-pane').forEach(function(el) { el.classList.remove('active'); });
      document.querySelectorAll('.nav-btn').forEach(function(el) { el.classList.remove('active'); });
      var target = document.getElementById('pane-' + id);
      if (target) target.classList.add('active');
      var btn = document.getElementById('nav-' + id);
      if (btn) btn.classList.add('active');
    }

    function selectTheme(themeName) {
      document.querySelectorAll('.palette-card').forEach(function(el) { el.classList.remove('selected'); });
      var el = document.getElementById('theme-' + themeName);
      if (el) el.classList.add('selected');
      sendAction('theme', { val: themeName });
    }

    function setAccent(color) {
      var picker = document.getElementById('accentColorPicker');
      if (picker) picker.value = color;
      var tokenAccent = document.getElementById('tokenAccent');
      if (tokenAccent) tokenAccent.value = color;
      var pickerAccent = document.getElementById('pickerAccent');
      if (pickerAccent) pickerAccent.value = color;
      sendAction('accent_color', { val: color });
    }

    function selectEngine(engineName) {
      document.querySelectorAll('.engine-card').forEach(function(el) { el.classList.remove('selected'); });
      var el = document.getElementById('engine-' + engineName);
      if (el) el.classList.add('selected');
      sendAction('engine', { val: engineName });
    }

    function applyTokens() {
      var bg = document.getElementById('tokenBg').value.trim();
      var surface = document.getElementById('tokenSurface').value.trim();
      var fg = document.getElementById('tokenFg').value.trim();
      var accent = document.getElementById('tokenAccent').value.trim();
      var border = document.getElementById('tokenBorder').value.trim();
      sendAction('tokens', { bg: bg, surface: surface, fg: fg, accent: accent, border: border });
    }

    function applyCustomQss() {
      var qss = document.getElementById('customQssInput').value;
      sendAction('custom_qss', { qss: encodeURIComponent(qss) });
    }

    function exportTheme() {
      var themeObj = {
        theme: "custom",
        bg_color: document.getElementById('tokenBg').value.trim(),
        surface_color: document.getElementById('tokenSurface').value.trim(),
        fg_color: document.getElementById('tokenFg').value.trim(),
        accent_color: document.getElementById('tokenAccent').value.trim(),
        border_color: document.getElementById('tokenBorder').value.trim(),
        bg_opacity: parseInt(document.getElementById('opacitySlider').value, 10) / 100.0,
        ui_density: document.querySelector('#pane-appearance select[onchange*="ui_density"]').value
      };
      var blob = new Blob([JSON.stringify(themeObj, null, 2)], { type: "application/json" });
      var url = URL.createObjectURL(blob);
      var a = document.createElement("a");
      a.href = url;
      a.download = "falkon-theme.json";
      a.click();
      setTimeout(function() { URL.revokeObjectURL(url); }, 500);
    }

    function importThemeFile(input) {
      if (!input.files || !input.files[0]) return;
      var reader = new FileReader();
      reader.onload = function(e) {
        try {
          var data = JSON.parse(e.target.result);
          sendAction('import_theme', {
            bg: data.bg_color || '',
            surface: data.surface_color || '',
            fg: data.fg_color || '',
            accent: data.accent_color || '',
            border: data.border_color || '',
            opacity: data.bg_opacity !== undefined ? String(Math.round(data.bg_opacity * 100)) : '',
            density: data.ui_density || ''
          });
        } catch (err) {
          alert('Format file tema JSON tidak valid.');
        }
      };
      reader.readAsText(input.files[0]);
      input.value = '';
    }

    function resetThemeDefaults() {
      if (confirm('Pulihkan semua pengaturan tema ke setelan bawaan?')) {
        sendAction('reset_defaults', {});
      }
    }
  </script>
</body>
</html>)RAW_HTML";

    tpl.replace("@BG@", ctx->bgColor);
    tpl.replace("@SURFACE@", ctx->surfaceColor);
    tpl.replace("@FG@", ctx->fgColor);
    tpl.replace("@ACCENT@", ctx->accentColor);
    tpl.replace("@BORDER@", ctx->borderColor);
    tpl.replace("@OPACITY_VAL@", QString::number(static_cast<int>(std::round(ctx->bgOpacity * 100.0f))));
    tpl.replace("@HOMEPAGE@", hp);
    tpl.replace("@DOWNLOAD_DIR@", ctx->downloadDir.isEmpty() ? "~/Downloads" : ctx->downloadDir);
    tpl.replace("@CUSTOM_QSS@", ctx->customUserStyle);

    tpl.replace("@THEME_DARK_SEL@", (curTheme == "dark_glass" || curTheme == "dark") ? "selected" : "");
    tpl.replace("@THEME_CATPPUCCIN_SEL@", curTheme == "catppuccin" ? "selected" : "");
    tpl.replace("@THEME_LIGHT_SEL@", curTheme == "light" ? "selected" : "");
    tpl.replace("@THEME_MINIMAL_SEL@", curTheme == "minimal" ? "selected" : "");

    tpl.replace("@TAB_POS_TOP@", ctx->tabPosition == "top" ? "selected" : "");
    tpl.replace("@TAB_POS_LEFT@", ctx->tabPosition == "left" ? "selected" : "");
    tpl.replace("@TAB_POS_HIDDEN@", ctx->tabPosition == "hidden" ? "selected" : "");

    tpl.replace("@DENSITY_COMF@", ctx->uiDensity != "compact" ? "selected" : "");
    tpl.replace("@DENSITY_COMP@", ctx->uiDensity == "compact" ? "selected" : "");

    tpl.replace("@HOME_CLOCK_CHECKED@", ctx->showHomeClock ? "checked" : "");
    tpl.replace("@HOME_SEARCH_CHECKED@", ctx->showHomeSearch ? "checked" : "");
    tpl.replace("@HOME_TILES_CHECKED@", ctx->showHomeTiles ? "checked" : "");

    tpl.replace("@ENGINE_GOOGLE_SEL@", isGoogle ? "selected" : "");
    tpl.replace("@ENGINE_DDG_SEL@", !isGoogle ? "selected" : "");

    tpl.replace("@ZOOM_08@", std::abs(ctx->currentZoom - 0.8f) < 0.05f ? "selected" : "");
    tpl.replace("@ZOOM_09@", std::abs(ctx->currentZoom - 0.9f) < 0.05f ? "selected" : "");
    tpl.replace("@ZOOM_10@", std::abs(ctx->currentZoom - 1.0f) < 0.05f ? "selected" : "");
    tpl.replace("@ZOOM_11@", std::abs(ctx->currentZoom - 1.1f) < 0.05f ? "selected" : "");
    tpl.replace("@ZOOM_125@", std::abs(ctx->currentZoom - 1.25f) < 0.05f ? "selected" : "");
    tpl.replace("@ZOOM_15@", std::abs(ctx->currentZoom - 1.5f) < 0.05f ? "selected" : "");

    return tpl;
}

static void handle_settings_action(BrowserContext *ctx, const QUrl &url) {
    if (!ctx) return;
    QString action = url.host();
    QUrlQuery q(url);

    if (action == "theme") {
        QString val = q.queryItemValue("val");
        browser_set_theme(ctx, val.toUtf8().constData(), nullptr);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "accent_color") {
        QString val = q.queryItemValue("val").trimmed();
        if (!val.isEmpty()) {
            ctx->accentColor = val;
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
    } else if (action == "ui_density") {
        QString val = q.queryItemValue("val");
        ctx->uiDensity = val;
        apply_theme_qss(ctx);
        save_browser_config(ctx);
    } else if (action == "home_toggle") {
        QString elem = q.queryItemValue("element");
        bool val = (q.queryItemValue("val") == "1");
        if (elem == "clock") ctx->showHomeClock = val;
        else if (elem == "search") ctx->showHomeSearch = val;
        else if (elem == "tiles") ctx->showHomeTiles = val;
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "tokens") {
        QString bg = q.queryItemValue("bg").trimmed();
        QString surface = q.queryItemValue("surface").trimmed();
        QString fg = q.queryItemValue("fg").trimmed();
        QString accent = q.queryItemValue("accent").trimmed();
        QString border = q.queryItemValue("border").trimmed();
        if (!bg.isEmpty()) ctx->bgColor = bg;
        if (!surface.isEmpty()) ctx->surfaceColor = surface;
        if (!fg.isEmpty()) ctx->fgColor = fg;
        if (!accent.isEmpty()) ctx->accentColor = accent;
        if (!border.isEmpty()) ctx->borderColor = border;
        ctx->currentTheme = "custom";
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "custom_qss") {
        QString raw = q.queryItemValue("qss");
        QString qss = QUrl::fromPercentEncoding(raw.toUtf8());
        ctx->customUserStyle = qss;
        QString userQssPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/user_chrome.qss";
        QFile f(userQssPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(qss.toUtf8());
            f.close();
        }
        apply_theme_qss(ctx);
        save_browser_config(ctx);
    } else if (action == "import_theme") {
        QString bg = q.queryItemValue("bg").trimmed();
        QString surface = q.queryItemValue("surface").trimmed();
        QString fg = q.queryItemValue("fg").trimmed();
        QString accent = q.queryItemValue("accent").trimmed();
        QString border = q.queryItemValue("border").trimmed();
        QString opStr = q.queryItemValue("opacity").trimmed();
        QString density = q.queryItemValue("density").trimmed();
        if (!bg.isEmpty()) ctx->bgColor = bg;
        if (!surface.isEmpty()) ctx->surfaceColor = surface;
        if (!fg.isEmpty()) ctx->fgColor = fg;
        if (!accent.isEmpty()) ctx->accentColor = accent;
        if (!border.isEmpty()) ctx->borderColor = border;
        if (!opStr.isEmpty()) {
            int op = opStr.toInt();
            if (op >= 20 && op <= 100) ctx->bgOpacity = op / 100.0f;
        }
        if (!density.isEmpty()) ctx->uiDensity = density;
        ctx->currentTheme = "custom";
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "reset_defaults") {
        ctx->currentTheme = "dark";
        ctx->bgColor = "#181825";
        ctx->surfaceColor = "#1e1e2e";
        ctx->fgColor = "#cdd6f4";
        ctx->accentColor = "#89b4fa";
        ctx->borderColor = "#313244";
        ctx->bgOpacity = 0.85f;
        ctx->uiDensity = "comfortable";
        ctx->tabPosition = "top";
        ctx->showHomeClock = true;
        ctx->showHomeSearch = true;
        ctx->showHomeTiles = true;
        ctx->customUserStyle = "";
        QString userQssPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/user_chrome.qss";
        QFile::remove(userQssPath);
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "tab_bar") {
        bool show = (q.queryItemValue("val") == "top");
        ctx->tabPosition = show ? "top" : "hidden";
        apply_theme_qss(ctx);
        save_browser_config(ctx);
    } else if (action == "engine") {
        QString val = q.queryItemValue("val");
        ctx->useGoogleSearch = (val == "google");
        update_search_engine_button(ctx);
        save_browser_config(ctx);
    } else if (action == "nav_buttons") {
        bool show = (q.queryItemValue("val") == "1");
        if (ctx->backBtn) ctx->backBtn->setVisible(show);
        if (ctx->forwardBtn) ctx->forwardBtn->setVisible(show);
        if (ctx->reloadBtn) ctx->reloadBtn->setVisible(show);
        save_browser_config(ctx);
    } else if (action == "adblock_badge") {
        bool show = (q.queryItemValue("val") == "1");
        if (ctx->adblockBadge) ctx->adblockBadge->setVisible(show);
        save_browser_config(ctx);
    } else if (action == "zoom") {
        float z = q.queryItemValue("val").toFloat();
        if (z >= 0.5f && z <= 3.0f) {
            ctx->currentZoom = z;
            adjust_zoom(ctx, 0.0f);
            save_browser_config(ctx);
        }
    } else if (action == "homepage") {
        QString hp = q.queryItemValue("val").trimmed();
        save_browser_config(ctx, hp);
    } else if (action == "open_downloads") {
        open_downloads_folder(ctx);
    } else if (action == "color") {
        QString bg = q.queryItemValue("bg").trimmed();
        QString fg = q.queryItemValue("fg").trimmed();
        QString accent = q.queryItemValue("accent").trimmed();
        if (!bg.isEmpty()) ctx->bgColor = bg;
        if (!fg.isEmpty()) ctx->fgColor = fg;
        if (!accent.isEmpty()) ctx->accentColor = accent;
        ctx->currentTheme = "custom";
        apply_theme_qss(ctx);
        save_browser_config(ctx);
        refresh_internal_tabs(ctx);
    } else if (action == "clear_history") {
        QString histPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust/history.json";
        QFile wf(histPath);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            wf.write("[]");
        }
    } else if (action == "clear_cache") {
        QWebEngineProfile::defaultProfile()->clearHttpCache();
    } else if (action == "smart_memory") {
        run_smart_memory_cleanup(ctx);
    }
}

bool BrowserPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) {
    if (url.scheme() == "falkon-set") {
        handle_settings_action(m_ctx, url);
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

static void open_settings_tab(BrowserContext *ctx) {
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return;

    for (size_t i = 0; i < ctx->tabs.size(); ++i) {
        if (ctx->tabs[i].savedUrl == "falkon:settings") {
            ctx->tabBar->setCurrentIndex(static_cast<int>(i));
            ctx->viewStack->setCurrentIndex(static_cast<int>(i));
            return;
        }
    }

    int curIdx = ctx->tabBar->currentIndex();
    if (curIdx >= 0 && curIdx < static_cast<int>(ctx->tabs.size())) {
        if (ctx->tabs[curIdx].savedUrl.isEmpty() ||
            ctx->tabs[curIdx].savedUrl == "falkon:home" ||
            ctx->tabs[curIdx].savedUrl == "about:blank") {
            browser_navigate(ctx, "falkon:settings");
            return;
        }
    }

    browser_add_tab(ctx, "falkon:settings");
}

static void show_settings_dialog(BrowserContext *ctx) {
    open_settings_tab(ctx);
}

BrowserHandle browser_create(const char* title, int width, int height) {
    // 1. Ultra-Low RAM & Hardware Acceleration Optimization Flags
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
        "--renderer-process-limit=1 "
        "--enable-low-end-device-mode "
        "--disable-gpu-shader-disk-cache "
        "--disable-gpu-program-cache "
        "--disk-cache-size=10485760 "
        "--media-cache-size=10485760 "
        "--disable-speech-api "
        "--disable-breakpad "
        "--enable-features=VaapiVideoDecoder,VaapiVideoDecodeLinuxGL "
        "--enable-accelerated-video-decode"
    );

    auto *ctx = new BrowserContext();

    if (!qApp) {
        static int argc = 1;
        static char arg0[] = "falkon-rust";
        static char* argv[] = { arg0, nullptr };
        ctx->app = new QApplication(argc, argv);
        ctx->ownApp = true;
    } else {
        ctx->app = qApp;
    }

    ctx->window = new QMainWindow();
    ctx->window->setWindowTitle(QString::fromUtf8(title ? title : "Falkon-Rust"));
    ctx->window->resize(width > 0 ? width : 1280, height > 0 ? height : 800);

    // Setup Persistent Cache & Memory Limit (10MB Cap)
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/falkon-rust";
    QDir().mkpath(configPath + "/cache");
    QDir().mkpath(configPath + "/storage");

    QString closeSvg = configPath + "/tab_close.svg";
    QString closeHoverSvg = configPath + "/tab_close_hover.svg";
    {
        QFile f(closeSvg);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><line x1=\"3\" y1=\"8\" x2=\"13\" y2=\"8\" stroke=\"#808499\" stroke-width=\"2\" stroke-linecap=\"round\"/></svg>");
        }
        QFile f2(closeHoverSvg);
        if (f2.open(QIODevice::WriteOnly)) {
            f2.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><line x1=\"3\" y1=\"8\" x2=\"13\" y2=\"8\" stroke=\"#ffffff\" stroke-width=\"2\" stroke-linecap=\"round\"/></svg>");
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
            if (o.contains("bg_color")) ctx->bgColor = o["bg_color"].toString();
            if (o.contains("surface_color")) ctx->surfaceColor = o["surface_color"].toString();
            if (o.contains("fg_color")) ctx->fgColor = o["fg_color"].toString();
            if (o.contains("accent_color")) ctx->accentColor = o["accent_color"].toString();
            if (o.contains("border_color")) ctx->borderColor = o["border_color"].toString();
            if (o.contains("bg_opacity")) ctx->bgOpacity = static_cast<float>(o["bg_opacity"].toDouble(0.85));
            if (o.contains("ui_density")) ctx->uiDensity = o["ui_density"].toString();
            if (o.contains("tab_position")) ctx->tabPosition = o["tab_position"].toString();
            if (o.contains("show_home_search")) ctx->showHomeSearch = o["show_home_search"].toBool(true);
            if (o.contains("show_home_tiles")) ctx->showHomeTiles = o["show_home_tiles"].toBool(true);
            if (o.contains("show_home_clock")) ctx->showHomeClock = o["show_home_clock"].toBool(true);
            if (o.contains("search_engine")) {
                ctx->useGoogleSearch = o["search_engine"].toString().contains("google");
            }
            if (o.contains("download_dir")) ctx->downloadDir = o["download_dir"].toString();
        }
        rf.close();
    }

    QString userQssPath = configPath + "/user_chrome.qss";
    QFile qssFile(userQssPath);
    if (qssFile.open(QIODevice::ReadOnly)) {
        ctx->customUserStyle = QString::fromUtf8(qssFile.readAll());
        qssFile.close();
    }

    auto *profile = QWebEngineProfile::defaultProfile();
    profile->setCachePath(configPath + "/cache");
    profile->setPersistentStoragePath(configPath + "/storage");
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setHttpCacheMaximumSize(10 * 1024 * 1024); // 10MB memory cache cap

    // WebEngine Performance & Audio/Video Settings
    auto *settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, true);
    settings->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    settings->setAttribute(QWebEngineSettings::WebGLEnabled, true);

    // Setup uBlock Request Interceptor
    ctx->interceptor = new BridgeInterceptor(ctx->window);
    profile->setUrlRequestInterceptor(ctx->interceptor);

    // Setup Automatic Downloads handling
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
    });

    // Main layout
    auto *centralWidget = new QWidget(ctx->window);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. Top Tab Bar Row (Tabs + New Tab Button + Spacer)
    auto *tabRowWidget = new QWidget(centralWidget);
    tabRowWidget->setObjectName("tabStrip");
    auto *tabRowLayout = new QHBoxLayout(tabRowWidget);
    tabRowLayout->setContentsMargins(14, 10, 14, 0);
    tabRowLayout->setSpacing(6);

    ctx->tabBar = new QTabBar(tabRowWidget);
    ctx->tabBar->setTabsClosable(true);
    ctx->tabBar->setMovable(true);
    ctx->tabBar->setExpanding(false);
    ctx->tabBar->setElideMode(Qt::ElideRight);

    ctx->newTabBtn = new QPushButton("+", tabRowWidget);
    ctx->newTabBtn->setObjectName("newTabBtn");
    ctx->newTabBtn->setFixedSize(32, 32);
    ctx->newTabBtn->setToolTip("New Tab (Ctrl+T)");
    ctx->newTabBtn->setCursor(Qt::PointingHandCursor);

    tabRowLayout->addWidget(ctx->tabBar);
    tabRowLayout->addWidget(ctx->newTabBtn);
    tabRowLayout->addStretch();

    // 2. Navigation Bar
    ctx->navBar = new QWidget(centralWidget);
    ctx->navBar->setObjectName("navBar");
    auto *navLayout = new QHBoxLayout(ctx->navBar);
    navLayout->setContentsMargins(14, 8, 14, 8);
    navLayout->setSpacing(10);

    ctx->backBtn = new QPushButton("←", ctx->navBar);
    ctx->backBtn->setObjectName("iconBtn");
    ctx->backBtn->setFixedSize(36, 36);
    ctx->backBtn->setToolTip("Back (Alt+Left)");

    ctx->forwardBtn = new QPushButton("→", ctx->navBar);
    ctx->forwardBtn->setObjectName("iconBtn");
    ctx->forwardBtn->setFixedSize(36, 36);
    ctx->forwardBtn->setToolTip("Forward (Alt+Right)");

    ctx->reloadBtn = new QPushButton("↻", ctx->navBar);
    ctx->reloadBtn->setObjectName("iconBtn");
    ctx->reloadBtn->setFixedSize(36, 36);
    ctx->reloadBtn->setToolTip("Reload (Ctrl+R / F5)");

    ctx->urlBar = new QLineEdit(ctx->navBar);
    ctx->urlBar->setObjectName("urlBar");
    ctx->urlBar->setMinimumHeight(42);
    ctx->urlBar->setPlaceholderText("Search Google or type a URL... (Ctrl+L)");

    // Search Engine Icon inside URL Bar (Leading)
    ctx->engineAction = ctx->urlBar->addAction(QIcon(), QLineEdit::LeadingPosition);
    update_search_engine_button(ctx);
    QObject::connect(ctx->engineAction, &QAction::triggered, [ctx]() {
        ctx->useGoogleSearch = !ctx->useGoogleSearch;
        update_search_engine_button(ctx);
    });

    // Bookmark Star Action inside URL Bar (Trailing)
    ctx->bookmarkAction = ctx->urlBar->addAction(QIcon(), QLineEdit::TrailingPosition);
    update_bookmark_button(ctx, false);
    QObject::connect(ctx->bookmarkAction, &QAction::triggered, [ctx]() {
        toggle_bookmark_current_page(ctx);
    });

    ctx->adblockBadge = new QLabel("🛡️ uBlock: 0", ctx->navBar);
    ctx->adblockBadge->setObjectName("adblockBadge");

    // Quick Menu Button
    ctx->menuBtn = new QPushButton("⋮", ctx->navBar);
    ctx->menuBtn->setObjectName("iconBtn");
    ctx->menuBtn->setFixedSize(36, 36);
    ctx->menuBtn->setToolTip("Menu & Pengaturan (Ctrl+,)");

    auto *menu = new QMenu(ctx->menuBtn);

    auto *actionNewTab = menu->addAction("Tab Baru");
    actionNewTab->setShortcut(QKeySequence("Ctrl+T"));

    auto *actionNewPrivate = menu->addAction("Jendela Penyamaran Baru");
    actionNewPrivate->setShortcut(QKeySequence("Ctrl+Shift+N"));

    menu->addSeparator();

    auto *actionBookmarks = menu->addAction("Bookmarks");
    actionBookmarks->setShortcut(QKeySequence("Ctrl+B"));

    auto *actionHistory = menu->addAction("Riwayat");
    actionHistory->setShortcut(QKeySequence("Ctrl+H"));

    auto *actionDownloads = menu->addAction("Unduhan");
    actionDownloads->setShortcut(QKeySequence("Ctrl+J"));

    menu->addSeparator();

    auto *actionFind = menu->addAction("Cari di Halaman...");
    actionFind->setShortcut(QKeySequence("Ctrl+F"));

    // Horizontal Zoom Row Action
    auto *zoomWidgetAction = new QWidgetAction(menu);
    auto *zoomWidget = new QWidget(menu);
    auto *zoomLayout = new QHBoxLayout(zoomWidget);
    zoomLayout->setContentsMargins(18, 4, 18, 4);
    zoomLayout->setSpacing(10);

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
    auto *zoomResetBtn = new QPushButton("⛶", zoomWidget);
    zoomResetBtn->setObjectName("iconBtn");
    zoomResetBtn->setFixedSize(28, 28);
    zoomResetBtn->setToolTip("Layar Penuh (F11)");

    zoomLayout->addWidget(zoomLabel);
    zoomLayout->addStretch();
    zoomLayout->addWidget(zoomOutBtn);
    zoomLayout->addWidget(zoomValLabel);
    zoomLayout->addWidget(zoomInBtn);
    zoomLayout->addWidget(zoomResetBtn);
    zoomWidgetAction->setDefaultWidget(zoomWidget);
    menu->addAction(zoomWidgetAction);

    menu->addSeparator();

    // Submenu "Alat Lainnya"
    auto *moreMenu = menu->addMenu("Alat Lainnya");
    auto *actionEngine = moreMenu->addAction("Ganti Mesin Pencari (Google / DDG)");
    auto *actionTabHide = moreMenu->addAction("Sembunyikan / Tampilkan Tab Bar");
    auto *actionCleanMem = moreMenu->addAction("Bersihkan Memori Tab Tidak Aktif");
    auto *actionFullscreen = moreMenu->addAction("Layar Penuh");
    actionFullscreen->setShortcut(QKeySequence("F11"));

    menu->addSeparator();

    auto *actionSettings = menu->addAction("Pengaturan");
    actionSettings->setShortcut(QKeySequence("Ctrl+,"));

    QObject::connect(actionNewTab, &QAction::triggered, [ctx]() { browser_add_tab(ctx, ""); });
    QObject::connect(actionNewPrivate, &QAction::triggered, [ctx]() { browser_add_tab(ctx, ""); });
    QObject::connect(actionBookmarks, &QAction::triggered, [ctx]() { show_bookmarks_dialog(ctx); });
    QObject::connect(actionHistory, &QAction::triggered, [ctx]() { show_history_dialog(ctx); });
    QObject::connect(actionDownloads, &QAction::triggered, [ctx]() { open_downloads_folder(ctx); });
    QObject::connect(actionFind, &QAction::triggered, [ctx]() { show_find_dialog(ctx); });
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
    QObject::connect(zoomResetBtn, &QPushButton::clicked, [ctx, menu]() {
        if (ctx->window->isFullScreen()) ctx->window->showNormal();
        else ctx->window->showFullScreen();
        menu->close();
    });

    QObject::connect(actionEngine, &QAction::triggered, [ctx]() {
        ctx->useGoogleSearch = !ctx->useGoogleSearch;
        update_search_engine_button(ctx);
    });
    QObject::connect(actionTabHide, &QAction::triggered, [ctx]() {
        ctx->tabBar->setVisible(!ctx->tabBar->isVisible());
        ctx->newTabBtn->setVisible(ctx->tabBar->isVisible());
    });
    QObject::connect(actionCleanMem, &QAction::triggered, [ctx]() {
        run_smart_memory_cleanup(ctx);
    });
    QObject::connect(actionFullscreen, &QAction::triggered, [ctx]() {
        if (ctx->window->isFullScreen()) ctx->window->showNormal();
        else ctx->window->showFullScreen();
    });

    QObject::connect(ctx->menuBtn, &QPushButton::clicked, [ctx, menu]() {
        menu->exec(ctx->menuBtn->mapToGlobal(QPoint(0, ctx->menuBtn->height() + 4)));
    });

    // Window shortcuts
    ctx->window->addAction(actionNewTab);
    ctx->window->addAction(actionBookmarks);
    ctx->window->addAction(actionHistory);
    ctx->window->addAction(actionDownloads);
    ctx->window->addAction(actionFind);
    ctx->window->addAction(actionSettings);

    auto *actionBookmarkShortcut = new QAction(ctx->window);
    actionBookmarkShortcut->setShortcut(QKeySequence("Ctrl+D"));
    QObject::connect(actionBookmarkShortcut, &QAction::triggered, [ctx]() { toggle_bookmark_current_page(ctx); });
    ctx->window->addAction(actionBookmarkShortcut);

    navLayout->addWidget(ctx->backBtn);
    navLayout->addWidget(ctx->forwardBtn);
    navLayout->addWidget(ctx->reloadBtn);
    navLayout->addWidget(ctx->urlBar);
    navLayout->addWidget(ctx->adblockBadge);
    navLayout->addSpacing(8);
    navLayout->addWidget(ctx->menuBtn);

    // 3. Web Views Stack
    ctx->viewStack = new QStackedWidget(centralWidget);

    mainLayout->addWidget(tabRowWidget);
    mainLayout->addWidget(ctx->navBar);
    mainLayout->addWidget(ctx->viewStack, 1);
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
        }
    });

    QObject::connect(ctx->tabBar, &QTabBar::currentChanged, [ctx](int index) {
        if (index >= 0 && index < static_cast<int>(ctx->tabs.size())) {
            ctx->viewStack->setCurrentIndex(index);
            auto &meta = ctx->tabs[index];
            meta.lastActive = QDateTime::currentSecsSinceEpoch();

            if (meta.isSuspended) {
                meta.isSuspended = false;
                if (!meta.savedUrl.isEmpty()) {
                    if (meta.savedUrl == "falkon:settings") {
                        meta.view->page()->setBackgroundColor(Qt::transparent);
                        meta.view->setHtml(generate_settings_page_html(ctx), QUrl("falkon:settings"));
                    } else if (meta.savedUrl == "falkon:home") {
                        meta.view->page()->setBackgroundColor(Qt::transparent);
                        meta.view->setHtml(generate_start_page_html(ctx), QUrl("falkon:home"));
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
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), ctx->window, [ctx]() { browser_close_tab(ctx, ctx->tabBar->currentIndex()); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), ctx->window, [ctx]() { browser_reload(ctx); });
    new QShortcut(QKeySequence(Qt::Key_F5), ctx->window, [ctx]() { browser_reload(ctx); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), ctx->window, [ctx]() { ctx->urlBar->setFocus(); ctx->urlBar->selectAll(); });
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), ctx->window, [ctx]() { browser_back(ctx); });
    new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), ctx->window, [ctx]() { browser_forward(ctx); });

    // Zoom shortcuts
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), ctx->window, [ctx]() { adjust_zoom(ctx, 0.1f); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), ctx->window, [ctx]() { adjust_zoom(ctx, 0.1f); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), ctx->window, [ctx]() { adjust_zoom(ctx, -0.1f); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), ctx->window, [ctx]() {
        auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->currentWidget());
        if (view) { ctx->currentZoom = 1.0f; view->setZoomFactor(1.0f); }
    });

    // Fullscreen shortcut
    new QShortcut(QKeySequence(Qt::Key_F11), ctx->window, [ctx]() {
        if (ctx->window->isFullScreen()) ctx->window->showNormal();
        else ctx->window->showFullScreen();
    });

    // Compact mode shortcut
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_M), ctx->window, [ctx]() {
        bool vis = ctx->backBtn->isVisible();
        ctx->backBtn->setVisible(!vis);
        ctx->forwardBtn->setVisible(!vis);
        ctx->reloadBtn->setVisible(!vis);
        ctx->engineToggleBtn->setVisible(!vis);
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

    // 2. Setup Smart Memory Daemon Timer (runs every 25s)
    ctx->memoryTimer = new QTimer(ctx->window);
    QObject::connect(ctx->memoryTimer, &QTimer::timeout, [ctx]() {
        run_smart_memory_cleanup(ctx);
    });
    ctx->memoryTimer->start(25000);

    apply_theme_qss(ctx);
    update_search_engine_button(ctx);

    ctx->window->show();
    return static_cast<BrowserHandle>(ctx);
}

static void attach_view_signals(BrowserContext *ctx, QWebEngineView *view) {
    if (ctx->currentZoom > 0.2f && ctx->currentZoom < 5.0f) {
        view->setZoomFactor(ctx->currentZoom);
    }

    QObject::connect(view, &QWebEngineView::titleChanged, [ctx, view](const QString &title) {
        int idx = ctx->viewStack->indexOf(view);
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            QString displayTitle = title.trimmed().isEmpty() ? "New Tab" : title;
            ctx->tabs[idx].savedTitle = displayTitle;
            if (displayTitle.length() > 25) {
                displayTitle = displayTitle.left(22) + "...";
            }
            if (!ctx->tabs[idx].isSuspended) {
                ctx->tabBar->setTabText(idx, displayTitle);
            }
            if (ctx->tabCallback) {
                QByteArray urlB = view->url().toString().toUtf8();
                QByteArray titleB = title.toUtf8();
                ctx->tabCallback(idx, urlB.constData(), titleB.constData());
            }
        }
    });

    QObject::connect(view, &QWebEngineView::urlChanged, [ctx, view](const QUrl &url) {
        int idx = ctx->viewStack->indexOf(view);
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            ctx->tabs[idx].savedUrl = url.toString();
            ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
        }
        if (idx == ctx->tabBar->currentIndex()) {
            ctx->urlBar->setText(url.toString());
            update_bookmark_button(ctx, is_url_bookmarked(url.toString()));
        }
        if (ctx->tabCallback && idx >= 0) {
            QByteArray urlB = url.toString().toUtf8();
            QByteArray titleB = view->title().toUtf8();
            ctx->tabCallback(idx, urlB.constData(), titleB.constData());
        }
    });

    // Cosmetic Filter & User Custom Script/Style Injection
    QObject::connect(view, &QWebEngineView::loadFinished, [ctx, view](bool ok) {
        if (!ok) return;

        static const QString cosmeticFilterJs =
            "(() => {"
            "  const css = '.adsbygoogle, .ad-banner, .advertisement, [id^=\"google_ads_\"], [id^=\"ad-\"], [class*=\"sponsored\"], [class*=\"ad-slot\"] { display: none !important; }';"
            "  const el = document.createElement('style');"
            "  el.setAttribute('data-ublock-cosmetic', 'true');"
            "  el.textContent = css;"
            "  if (document.head) document.head.appendChild(el);"
            "})();";
        view->page()->runJavaScript(cosmeticFilterJs);

        if (!ctx->customUserStyle.isEmpty()) {
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
            ).arg(QString(ctx->customUserStyle.toUtf8().toHex()));
            view->page()->runJavaScript(cleanInject);
        }

        if (!ctx->customUserScript.isEmpty()) {
            view->page()->runJavaScript(ctx->customUserScript);
        }
    });
}

int browser_add_tab(BrowserHandle handle, const char* raw_url) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return -1;

    auto *view = new QWebEngineView(ctx->viewStack);
    view->setPage(new BrowserPage(ctx, view));
    attach_view_signals(ctx, view);

    TabMeta meta;
    meta.view = view;
    meta.lastActive = QDateTime::currentSecsSinceEpoch();
    meta.isSuspended = false;
    ctx->tabs.push_back(meta);

    ctx->viewStack->addWidget(view);
    int index = ctx->tabBar->addTab("New Tab");
    ctx->tabBar->setCurrentIndex(index);
    ctx->viewStack->setCurrentIndex(index);

    if (raw_url && raw_url[0] != '\0') {
        browser_navigate(handle, raw_url);
    } else {
        browser_navigate(handle, "falkon:home");
    }

    return index;
}

void browser_close_tab(BrowserHandle handle, int index) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return;

    if (ctx->tabBar->count() > 1 && index >= 0 && index < static_cast<int>(ctx->tabs.size())) {
        auto *view = ctx->viewStack->widget(index);
        ctx->tabBar->removeTab(index);
        if (view) {
            ctx->viewStack->removeWidget(view);
            view->deleteLater();
        }
        ctx->tabs.erase(ctx->tabs.begin() + index);
        if (ctx->tabCloseCallback) {
            ctx->tabCloseCallback(index);
        }
    } else if (ctx->tabBar->count() == 1) {
        auto *view = qobject_cast<QWebEngineView*>(ctx->viewStack->widget(0));
        if (view) {
            view->load(QUrl("about:blank"));
            if (!ctx->tabs.empty()) {
                ctx->tabs[0].savedUrl = "about:blank";
                ctx->tabs[0].savedTitle = "New Tab";
                ctx->tabs[0].isSuspended = false;
            }
            ctx->tabBar->setTabText(0, "New Tab");
            ctx->urlBar->setText("");
            ctx->urlBar->setPlaceholderText("Search Google or type a URL... (Ctrl+L)");
            update_bookmark_button(ctx, false);
        }
    }
}

void browser_switch_tab(BrowserHandle handle, int index) {
    auto *ctx = static_cast<BrowserContext*>(handle);
    if (!ctx || !ctx->tabBar || !ctx->viewStack) return;
    if (index >= 0 && index < ctx->tabBar->count()) {
        ctx->tabBar->setCurrentIndex(index);
        ctx->viewStack->setCurrentIndex(index);
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

    if (s == "falkon:settings" || s == "about:settings") {
        view->page()->setBackgroundColor(Qt::transparent);
        view->setHtml(generate_settings_page_html(ctx), QUrl("falkon:settings"));
        ctx->urlBar->setText("falkon:settings");
        update_bookmark_button(ctx, false);
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            ctx->tabs[idx].savedUrl = "falkon:settings";
            ctx->tabs[idx].savedTitle = "Pengaturan";
            ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
            ctx->tabs[idx].isSuspended = false;
        }
        ctx->tabBar->setTabText(idx, "Pengaturan");
        return;
    }

    if (s.isEmpty() || s == "falkon:home" || s == "about:home") {
        view->page()->setBackgroundColor(Qt::transparent);
        view->setHtml(generate_start_page_html(ctx), QUrl("falkon:home"));
        ctx->urlBar->setText("");
        ctx->urlBar->setPlaceholderText("Search Google or type a URL... (Ctrl+L)");
        update_bookmark_button(ctx, false);
        if (idx >= 0 && idx < static_cast<int>(ctx->tabs.size())) {
            ctx->tabs[idx].savedUrl = "falkon:home";
            ctx->tabs[idx].savedTitle = "New Tab";
            ctx->tabs[idx].lastActive = QDateTime::currentSecsSinceEpoch();
            ctx->tabs[idx].isSuspended = false;
        }
        ctx->tabBar->setTabText(idx, "New Tab");
        return;
    }

    view->page()->setBackgroundColor(Qt::white);
    QUrl url;

    // Check if query is a URL or search query
    if (!s.contains("://") && !s.startsWith("about:")) {
        if (s.contains(".") && !s.contains(" ")) {
            url = QUrl("https://" + s);
        } else {
            // Dual Engine Router: Support !g / g prefix for Google, !d / d prefix for DuckDuckGo
            if (s.startsWith("!g ") || s.startsWith("g ")) {
                QString q = s.mid(s.indexOf(' ') + 1).trimmed();
                url = QUrl("https://www.google.com/search?q=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (s.startsWith("!d ") || s.startsWith("d ")) {
                QString q = s.mid(s.indexOf(' ') + 1).trimmed();
                url = QUrl("https://html.duckduckgo.com/html/?q=" + QString::fromUtf8(QUrl::toPercentEncoding(q)));
            } else if (ctx->useGoogleSearch) {
                // Active toggle is Google
                url = QUrl("https://www.google.com/search?q=" + QString::fromUtf8(QUrl::toPercentEncoding(s)));
            } else {
                // Active toggle is DuckDuckGo Lite (ultra fast, minimal RAM)
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
