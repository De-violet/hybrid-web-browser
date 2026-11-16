use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::fs;
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::RwLock;

const DEFAULT_RULES: &str = r#"
! -------------------------------------------------------------
! Built-in uBlock Origin / EasyList core rules for Falkon-Rust
! -------------------------------------------------------------
||google-analytics.com^$third-party
||googletagservices.com^$third-party
||googlesyndication.com^$third-party
||doubleclick.net^$third-party
||adservice.google.*^$third-party
||pagead2.googlesyndication.com^$third-party
||tpc.googlesyndication.com^$third-party
||facebook.com/tr/*$third-party
||connect.facebook.net^$third-party
||telemetry.*^
||analytics.*^
||adnxs.com^$third-party
||criteo.com^$third-party
||criteo.net^$third-party
||outbrain.com^$third-party
||taboola.com^$third-party
||scorecardresearch.com^$third-party
||quantserve.com^$third-party
||amazon-adsystem.com^$third-party
||hotjar.com^$third-party
||clarity.ms^$third-party
||yandex.ru/metrika^
||mc.yandex.ru^
||adsystem.*^
||adservice.*^
||adsrvr.org^$third-party
||rubiconproject.com^$third-party
||pubmatic.com^$third-party
||openx.net^$third-party
||casalemedia.com^$third-party
||moatads.com^$third-party
||smartadserver.com^$third-party
||teads.tv^$third-party
||popads.net^$third-party
||popcash.net^$third-party
||propellerads.com^$third-party
||adsterra.com^$third-party
||exoclick.com^$third-party
||juicyads.com^$third-party
|| sentry.io/api/*$third-party
||bugsnag.com^$third-party
||mixpanel.com^$third-party
||segment.io^$third-party
||amplitude.com^$third-party
||mouseflow.com^$third-party
||fullstory.com^$third-party
||luckyorange.com^$third-party
||newrelic.com^$third-party
||nr-data.net^$third-party
/banner/*$image
/ads/*$image
/adverts/*
/popunder/*
/pop_ads/*
/pagead/*
/adframe.*
/adsense.*
! --- Cosmetic Filters ---
##.adsbygoogle
##.ad-banner
##.ad-container
##.ad-slot
##.ad-wrapper
##.advertisement
##.sponsored-content
##.taboola-widget
##.outbrain-widget
##[id^="google_ads_"]
##[id^="div-gpt-ad"]
##[class*="ad-banner"]
##[class*="sponsored-"]
"#;

pub struct AdBlockManager {
    engine: RwLock<Engine>,
    custom_rules: RwLock<Vec<String>>,
    blocked_count: AtomicU64,
    enabled: AtomicBool,
}

impl AdBlockManager {
    pub fn new() -> Self {
        Self::with_custom_rules(&[], true)
    }

    pub fn with_custom_rules(extra_rules: &[String], enabled: bool) -> Self {
        let mut filter_set = FilterSet::new(false);
        filter_set.add_filter_list(DEFAULT_RULES.to_string(), ParseOptions::default());
        if !extra_rules.is_empty() {
            filter_set.add_filter_list(extra_rules.join("\n"), ParseOptions::default());
        }
        let engine = Engine::new_with_filter_set(filter_set);

        Self {
            engine: RwLock::new(engine),
            custom_rules: RwLock::new(extra_rules.to_vec()),
            blocked_count: AtomicU64::new(0),
            enabled: AtomicBool::new(enabled),
        }
    }

    pub fn set_enabled(&self, enabled: bool) {
        self.enabled.store(enabled, Ordering::Relaxed);
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Relaxed)
    }

    fn rebuild_engine(&self, file_contents: Option<&str>) {
        let mut filter_set = FilterSet::new(false);
        filter_set.add_filter_list(DEFAULT_RULES.to_string(), ParseOptions::default());
        if let Ok(custom) = self.custom_rules.read() {
            if !custom.is_empty() {
                filter_set.add_filter_list(custom.join("\n"), ParseOptions::default());
            }
        }
        if let Some(extra) = file_contents {
            filter_set.add_filter_list(extra.to_string(), ParseOptions::default());
        }
        let new_engine = Engine::new_with_filter_set(filter_set);
        if let Ok(mut lock) = self.engine.write() {
            *lock = new_engine;
        }
    }

    pub fn set_custom_rules(&self, rules: Vec<String>) {
        if let Ok(mut lock) = self.custom_rules.write() {
            *lock = rules;
        }
        self.rebuild_engine(None);
    }

    pub fn load_rule_file<P: AsRef<Path>>(&self, path: P) -> Result<usize, String> {
        let content = fs::read_to_string(path).map_err(|e| e.to_string())?;
        let lines: Vec<String> = content
            .lines()
            .map(|l| l.trim().to_string())
            .filter(|l| !l.is_empty() && !l.starts_with('!'))
            .collect();
        let count = lines.len();

        if let Ok(mut custom) = self.custom_rules.write() {
            for line in lines {
                if !custom.contains(&line) {
                    custom.push(line);
                }
            }
        }
        self.rebuild_engine(None);
        Ok(count)
    }

    pub fn check_url(&self, url: &str, resource_type: i32, source_url: &str) -> bool {
        if !self.enabled.load(Ordering::Relaxed) {
            return false;
        }

        // Mapping sesuai enum QWebEngineUrlRequestInfo::ResourceType di Qt6:
        // 0: MainFrame, 1: SubFrame, 2: Stylesheet, 3: Script, 4: Image,
        // 5: FontResource, 6: SubResource, 7: Object, 8: Media, 9: Worker,
        // 10: SharedWorker, 11: Prefetch, 12: Favicon, 13: Xhr, 14: Ping,
        // 15: ServiceWorker, 16: CspReport, 17: PluginResource, 20: WebSocket
        let req_type = match resource_type {
            0 => "main_frame",
            1 => "sub_frame",
            2 => "stylesheet",
            3 => "script",
            4 => "image",
            5 => "font",
            6 => "subdocument",
            7 => "object",
            8 => "media",
            12 => "image",
            13 => "xmlhttprequest",
            14 => "ping",
            16 => "csp_report",
            20 => "websocket",
            _ => "other",
        };

        let effective_source = if source_url.is_empty() { url } else { source_url };

        if let Ok(req) = Request::new(url, effective_source, req_type, "GET") {
            if let Ok(engine) = self.engine.read() {
                let res = engine.check_network_request(&req);
                if res.should_block() {
                    self.blocked_count.fetch_add(1, Ordering::Relaxed);
                    return true;
                }
            }
        }
        false
    }

    pub fn cosmetic_css_for_url(&self, url: &str) -> String {
        if !self.enabled.load(Ordering::Relaxed) {
            return String::new();
        }
        if let Ok(engine) = self.engine.read() {
            let resources = engine.url_cosmetic_resources(url);
            if !resources.hide_selectors.is_empty() {
                let selectors: Vec<&str> = resources
                    .hide_selectors
                    .iter()
                    .map(|s| s.as_str())
                    .collect();
                return format!("{} {{ display: none !important; }}", selectors.join(", "));
            }
        }
        String::new()
    }

    pub fn blocked_count(&self) -> u64 {
        self.blocked_count.load(Ordering::Relaxed)
    }

    pub fn reset_count(&self) {
        self.blocked_count.store(0, Ordering::Relaxed);
    }
}

impl Default for AdBlockManager {
    fn default() -> Self {
        Self::new()
    }
}
