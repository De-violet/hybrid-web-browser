use crate::filter_lists::count_rules;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::collections::{HashMap, VecDeque};
use std::fs;
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Mutex, RwLock};

/// Rule bawaan offline: hanya ~70 rule inti untuk pelacak umum, agar
/// browser tetap memblokir tanpa daftar yang diunduh. Pemblokiran agresif
/// penuh berasal dari daftar filter yang diunduh (lihat `filter_lists`).
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
||sentry.io/api/*$third-party
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

/// Jumlah maksimum entri cache CSS kosmetik. Batas ini menjaga cache tetap
/// kecil (CSS per halaman bisa panjang) sambil menghemat komputasi ulang
/// untuk halaman yang sering dibuka kembali.
const MAX_COSMETIC_CACHE_ENTRIES: usize = 64;

/// Cache FIFO sederhana: URL -> CSS kosmetik yang sudah dihitung.
struct CosmeticCache {
    map: HashMap<String, String>,
    order: VecDeque<String>,
}

impl CosmeticCache {
    fn new() -> Self {
        Self {
            map: HashMap::new(),
            order: VecDeque::new(),
        }
    }

    fn get(&self, url: &str) -> Option<String> {
        self.map.get(url).cloned()
    }

    fn insert(&mut self, url: String, css: String) {
        let is_new = self.map.insert(url.clone(), css).is_none();
        if !is_new {
            return;
        }
        self.order.push_back(url);
        while self.map.len() > MAX_COSMETIC_CACHE_ENTRIES {
            match self.order.pop_front() {
                Some(oldest) => {
                    self.map.remove(&oldest);
                }
                None => break,
            }
        }
    }

    fn clear(&mut self) {
        self.map.clear();
        self.order.clear();
    }
}

/// Normalisasi input pengguna menjadi domain allowlist: huruf kecil, tanpa
/// skema, path, query, port, `www.`, dan titik di akhir.
/// Mengembalikan `None` untuk input kosong/tidak valid.
pub fn normalize_domain(input: &str) -> Option<String> {
    let trimmed = input.trim().to_lowercase();
    if trimmed.is_empty() || trimmed.contains(char::is_whitespace) {
        return None;
    }
    let no_scheme = trimmed
        .strip_prefix("https://")
        .or_else(|| trimmed.strip_prefix("http://"))
        .unwrap_or(&trimmed);
    let host = no_scheme.split(['/', '?', '#']).next().unwrap_or("");
    let host = host.split('@').next_back().unwrap_or(host);
    // Port: pisahkan ':' terakhir hanya jika bukan alamat IPv6 dalam kurung.
    let host = if host.starts_with('[') {
        host.split(']').next().unwrap_or(host)
    } else {
        host.split(':').next().unwrap_or(host)
    };
    let host = host.trim_end_matches('.');
    let host = host.strip_prefix("www.").unwrap_or(host);
    if host.is_empty() {
        None
    } else {
        Some(host.to_string())
    }
}

/// Ambil host (huruf kecil) dari sebuah URL, dengan fallback parsing manual
/// untuk URL tanpa skema.
fn host_of(url_str: &str) -> Option<String> {
    if let Ok(parsed) = url::Url::parse(url_str) {
        if let Some(host) = parsed.host_str() {
            return Some(host.to_lowercase());
        }
    }
    normalize_domain(url_str)
}

/// Apakah `host` sama dengan atau subdomain dari `domain` (tanpa alokasi).
fn host_matches_domain(host: &str, domain: &str) -> bool {
    if host == domain {
        return true;
    }
    host.len() > domain.len()
        && host.ends_with(domain)
        && host.as_bytes()[host.len() - domain.len() - 1] == b'.'
}

fn build_engine(list_contents: &[String], custom_rules: &[String]) -> (Engine, usize) {
    let mut filter_set = FilterSet::new(false);
    filter_set.add_filter_list(DEFAULT_RULES.to_string(), ParseOptions::default());
    let mut rule_count = count_rules(DEFAULT_RULES);
    for content in list_contents {
        rule_count += count_rules(content);
        filter_set.add_filter_list(content.clone(), ParseOptions::default());
    }
    if !custom_rules.is_empty() {
        let joined = custom_rules.join("\n");
        rule_count += count_rules(&joined);
        filter_set.add_filter_list(joined, ParseOptions::default());
    }
    (Engine::new_with_filter_set(filter_set), rule_count)
}

pub struct AdBlockManager {
    engine: RwLock<Engine>,
    list_contents: RwLock<Vec<String>>,
    custom_rules: RwLock<Vec<String>>,
    allowlist: RwLock<Vec<String>>,
    cosmetic_cache: Mutex<CosmeticCache>,
    blocked_count: AtomicU64,
    rule_count: AtomicUsize,
    enabled: AtomicBool,
}

impl AdBlockManager {
    pub fn new() -> Self {
        Self::with_custom_rules(&[], true)
    }

    pub fn with_custom_rules(extra_rules: &[String], enabled: bool) -> Self {
        Self::with_lists(extra_rules, enabled, &[], &[])
    }

    /// Konstruktor lengkap: rule kustom + isi daftar filter yang diunduh
    /// + allowlist domain.
    pub fn with_lists(
        extra_rules: &[String],
        enabled: bool,
        list_contents: &[String],
        allowlist: &[String],
    ) -> Self {
        let (engine, rule_count) = build_engine(list_contents, extra_rules);
        let mut normalized: Vec<String> = allowlist
            .iter()
            .filter_map(|d| normalize_domain(d))
            .collect();
        normalized.sort();
        normalized.dedup();
        Self {
            engine: RwLock::new(engine),
            list_contents: RwLock::new(list_contents.to_vec()),
            custom_rules: RwLock::new(extra_rules.to_vec()),
            allowlist: RwLock::new(normalized),
            cosmetic_cache: Mutex::new(CosmeticCache::new()),
            blocked_count: AtomicU64::new(0),
            rule_count: AtomicUsize::new(rule_count),
            enabled: AtomicBool::new(enabled),
        }
    }

    pub fn set_enabled(&self, enabled: bool) {
        self.enabled.store(enabled, Ordering::Relaxed);
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled.load(Ordering::Relaxed)
    }

    /// Jumlah rule efektif yang dimuat ke mesin (bawaan + daftar + kustom).
    pub fn rule_count(&self) -> usize {
        self.rule_count.load(Ordering::Relaxed)
    }

    /// Bangun ulang mesin dari isi daftar + rule kustom yang tersimpan.
    /// Dipanggil setelah daftar diperbarui atau rule kustom berubah.
    fn rebuild(&self) {
        let lists = self
            .list_contents
            .read()
            .map(|l| l.clone())
            .unwrap_or_default();
        let custom = self
            .custom_rules
            .read()
            .map(|c| c.clone())
            .unwrap_or_default();
        let (new_engine, new_count) = build_engine(&lists, &custom);
        if let Ok(mut cache) = self.cosmetic_cache.lock() {
            cache.clear();
        }
        self.rule_count.store(new_count, Ordering::Relaxed);
        if let Ok(mut lock) = self.engine.write() {
            *lock = new_engine;
        }
    }

    /// Muat ulang mesin dengan isi daftar filter baru (mis. setelah update).
    pub fn reload_lists(&self, list_contents: Vec<String>) {
        if let Ok(mut lock) = self.list_contents.write() {
            *lock = list_contents;
        }
        self.rebuild();
    }

    pub fn set_custom_rules(&self, rules: Vec<String>) {
        if let Ok(mut lock) = self.custom_rules.write() {
            *lock = rules;
        }
        self.rebuild();
    }

    /// Ganti allowlist domain (input dinormalisasi, duplikat dibuang).
    pub fn set_allowlist(&self, domains: Vec<String>) {
        let mut normalized: Vec<String> =
            domains.iter().filter_map(|d| normalize_domain(d)).collect();
        normalized.sort();
        normalized.dedup();
        if let Ok(mut lock) = self.allowlist.write() {
            *lock = normalized;
        }
        if let Ok(mut cache) = self.cosmetic_cache.lock() {
            cache.clear();
        }
    }

    pub fn allowlist(&self) -> Vec<String> {
        self.allowlist.read().map(|l| l.clone()).unwrap_or_default()
    }

    /// Apakah halaman sumber (`source_url`) masuk allowlist (cocok persis
    /// atau subdomain). `www.` diabaikan di kedua sisi.
    pub fn is_allowlisted(&self, source_url: &str) -> bool {
        let Some(host) = host_of(source_url) else {
            return false;
        };
        let host = host.strip_prefix("www.").unwrap_or(&host);
        if let Ok(list) = self.allowlist.read() {
            list.iter().any(|d| host_matches_domain(host, d))
        } else {
            false
        }
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
        self.rebuild();
        Ok(count)
    }

    pub fn check_url(&self, url: &str, resource_type: i32, source_url: &str) -> bool {
        if !self.enabled.load(Ordering::Relaxed) {
            return false;
        }

        let effective_source = if source_url.is_empty() {
            url
        } else {
            source_url
        };
        // Allowlist diperiksa sebelum parsing request: murah dan membuat
        // situs yang dikecualikan benar-benar bebas dari pemblokiran.
        if self.is_allowlisted(effective_source) {
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
        if self.is_allowlisted(url) {
            return String::new();
        }
        if let Ok(cache) = self.cosmetic_cache.lock() {
            if let Some(cached) = cache.get(url) {
                return cached;
            }
        }
        let css = if let Ok(engine) = self.engine.read() {
            let resources = engine.url_cosmetic_resources(url);
            if !resources.hide_selectors.is_empty() {
                let selectors: Vec<&str> = resources
                    .hide_selectors
                    .iter()
                    .map(|s| s.as_str())
                    .collect();
                format!("{} {{ display: none !important; }}", selectors.join(", "))
            } else {
                String::new()
            }
        } else {
            String::new()
        };
        if let Ok(mut cache) = self.cosmetic_cache.lock() {
            cache.insert(url.to_string(), css.clone());
        }
        css
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
