use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::fs;
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
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
||facebook.com/tr/*$third-party
||connect.facebook.net^$third-party
||telemetry.*^
||analytics.*^
||adnxs.com^$third-party
||criteo.com^$third-party
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
/banner/*$image
/ads/*$image
/adverts/*
/popunder/*
/pop_ads/*
"#;

pub struct AdBlockManager {
    engine: RwLock<Engine>,
    blocked_count: AtomicU64,
}

impl AdBlockManager {
    pub fn new() -> Self {
        let mut filter_set = FilterSet::new(false);
        filter_set.add_filter_list(DEFAULT_RULES.to_string(), ParseOptions::default());
        let engine = Engine::new_with_filter_set(filter_set);

        Self {
            engine: RwLock::new(engine),
            blocked_count: AtomicU64::new(0),
        }
    }

    pub fn load_rule_file<P: AsRef<Path>>(&self, path: P) -> Result<usize, String> {
        let content = fs::read_to_string(path).map_err(|e| e.to_string())?;
        let count = content.lines().count();

        let mut filter_set = FilterSet::new(false);
        filter_set.add_filter_list(DEFAULT_RULES.to_string(), ParseOptions::default());
        filter_set.add_filter_list(content, ParseOptions::default());

        let new_engine = Engine::new_with_filter_set(filter_set);
        if let Ok(mut lock) = self.engine.write() {
            *lock = new_engine;
        }

        Ok(count)
    }

    pub fn check_url(&self, url: &str, resource_type: i32, source_url: &str) -> bool {
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
            9 => "websocket",
            _ => "other",
        };

        if let Ok(req) = Request::new(url, source_url, req_type, "GET") {
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
