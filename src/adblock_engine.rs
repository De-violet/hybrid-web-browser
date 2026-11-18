use crate::filter_lists::count_rules;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::Engine;
use std::collections::{HashMap, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Mutex, RwLock};

/// Rule bawaan offline: hanya ~70 rule inti untuk pelacak umum, agar
/// browser tetap memblokir tanpa daftar yang diunduh. Pemblokiran agresif
/// penuh berasal dari daftar filter yang diunduh (lihat `filter_lists`).
const DEFAULT_RULES: &str = r#"
! -------------------------------------------------------------
! Built-in uBlock Origin / EasyList core rules for Attis
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
    for content in list_contents {
        filter_set.add_filter_list(content.clone(), ParseOptions::default());
    }
    if !custom_rules.is_empty() {
        filter_set.add_filter_list(custom_rules.join("\n"), ParseOptions::default());
    }
    (
        Engine::new_with_filter_set(filter_set),
        count_all_rules(list_contents, custom_rules),
    )
}

/// Hitung jumlah rule efektif (bawaan + daftar + kustom) tanpa membangun
/// mesin — dipakai jalur cache agar `rule_count` tetap akurat saat engine
/// di-deserialize, bukan di-parse.
fn count_all_rules(list_contents: &[String], custom_rules: &[String]) -> usize {
    let mut rule_count = count_rules(DEFAULT_RULES);
    for content in list_contents {
        rule_count += count_rules(content);
    }
    if !custom_rules.is_empty() {
        rule_count += count_rules(&custom_rules.join("\n"));
    }
    rule_count
}

/// Normalisasi daftar domain allowlist: duplikat dibuang, urutan stabil.
fn normalize_allowlist(domains: &[String]) -> Vec<String> {
    let mut normalized: Vec<String> = domains.iter().filter_map(|d| normalize_domain(d)).collect();
    normalized.sort();
    normalized.dedup();
    normalized
}

/// Parse isi file rule menjadi daftar rule: tiap baris di-trim, baris
/// kosong dan komentar (`!`) dibuang. Dipakai `load_rule_file` dan
/// pemuatan awal `custom_rules.txt` agar perilakunya identik.
pub fn parse_rule_lines(content: &str) -> Vec<String> {
    content
        .lines()
        .map(|l| l.trim().to_string())
        .filter(|l| !l.is_empty() && !l.starts_with('!'))
        .collect()
}

/// Baca file rule dari disk dan parse isinya (lihat `parse_rule_lines`).
pub fn read_rule_file_lines<P: AsRef<Path>>(path: P) -> Result<Vec<String>, String> {
    let content = fs::read_to_string(path).map_err(|e| e.to_string())?;
    Ok(parse_rule_lines(&content))
}

/// Nomor versi format cache engine terserialisasi. Naikkan bila format
/// berubah agar cache dari format lama tidak pernah dibaca.
const ENGINE_CACHE_FORMAT_VERSION: u32 = 1;
/// Magic header file cache engine terserialisasi.
const ENGINE_CACHE_MAGIC: &[u8; 8] = b"ATTISENG";
/// Nama file cache engine terserialisasi di direktori `filter_lists/`.
const ENGINE_CACHE_FILE: &str = "engine_cache.bin";

/// Path file cache engine terserialisasi untuk direktori cache tertentu.
pub fn engine_cache_path(cache_dir: &Path) -> PathBuf {
    cache_dir.join(ENGINE_CACHE_FILE)
}

/// FNV-1a 64-bit: hash deterministik tanpa dependensi baru.
/// `DefaultHasher` tidak boleh dipakai di sini karena kuncinya acak
/// per proses, sehingga hash cache tidak stabil antar peluncuran.
fn fnv1a_update(mut hash: u64, bytes: &[u8]) -> u64 {
    for &byte in bytes {
        hash ^= byte as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

/// Hash deterministik atas semua input pembangun engine: versi format,
/// rule bawaan, isi daftar filter (sesuai urutan pemuatan), dan rule
/// kustom. Perubahan apa pun pada input menghasilkan hash berbeda
/// sehingga cache basi tidak pernah dipakai.
fn engine_inputs_hash(list_contents: &[String], custom_rules: &[String]) -> u64 {
    let mut hash = 0xcbf29ce484222325u64;
    hash = fnv1a_update(hash, &ENGINE_CACHE_FORMAT_VERSION.to_le_bytes());
    hash = fnv1a_update(hash, DEFAULT_RULES.as_bytes());
    for chunk in list_contents.iter().chain(custom_rules.iter()) {
        hash = fnv1a_update(hash, &(chunk.len() as u64).to_le_bytes());
        hash = fnv1a_update(hash, chunk.as_bytes());
    }
    hash
}

/// Coba muat engine dari cache terserialisasi. Mengembalikan `None` bila
/// cache tidak ada, rusak, versinya tidak cocok, atau hash-nya tidak
/// cocok dengan input saat ini — pemanggil harus parse penuh.
fn try_load_cached_engine(cache_dir: &Path, expected_hash: u64) -> Option<Engine> {
    let bytes = fs::read(engine_cache_path(cache_dir)).ok()?;
    let magic_len = ENGINE_CACHE_MAGIC.len();
    let header_len = magic_len + 4 + 8;
    if bytes.len() <= header_len || &bytes[..magic_len] != ENGINE_CACHE_MAGIC {
        return None;
    }
    let version = u32::from_le_bytes(bytes[magic_len..magic_len + 4].try_into().ok()?);
    if version != ENGINE_CACHE_FORMAT_VERSION {
        return None;
    }
    let stored_hash = u64::from_le_bytes(bytes[magic_len + 4..header_len].try_into().ok()?);
    if stored_hash != expected_hash {
        return None;
    }
    let mut engine = Engine::default();
    engine.deserialize(&bytes[header_len..]).ok()?;
    Some(engine)
}

/// Tulis engine terserialisasi ke cache secara atomik (best-effort:
/// semua kegagalan I/O diabaikan, browser tetap berjalan normal).
fn store_cached_engine(cache_dir: &Path, hash: u64, engine: &Engine) {
    if fs::create_dir_all(cache_dir).is_err() {
        return;
    }
    let payload = engine.serialize();
    let mut bytes = Vec::with_capacity(20 + payload.len());
    bytes.extend_from_slice(ENGINE_CACHE_MAGIC);
    bytes.extend_from_slice(&ENGINE_CACHE_FORMAT_VERSION.to_le_bytes());
    bytes.extend_from_slice(&hash.to_le_bytes());
    bytes.extend_from_slice(&payload);
    let path = engine_cache_path(cache_dir);
    let tmp_path = path.with_extension(format!("tmp.{}", std::process::id()));
    if fs::write(&tmp_path, &bytes).is_ok() {
        let _ = fs::rename(&tmp_path, &path);
    } else {
        let _ = fs::remove_file(&tmp_path);
    }
}

/// Map kode `QWebEngineUrlRequestInfo::ResourceType` (Qt6) ke tipe
/// request `adblock`. Nilai enum terverifikasi dari header Qt 6.7
/// (`qwebengineurlrequestinfo.h`):
/// 0 MainFrame, 1 SubFrame, 2 Stylesheet, 3 Script, 4 Image,
/// 5 FontResource, 6 SubResource, 7 Object, 8 Media, 9 Worker,
/// 10 SharedWorker, 11 Prefetch, 12 Favicon, 13 Xhr, 14 Ping,
/// 15 ServiceWorker, 16 CspReport, 17 PluginResource, 18 tidak dipakai,
/// 19 NavigationPreloadMainFrame, 20 NavigationPreloadSubFrame,
/// 21 Json (Qt 6.8+), 254 WebSocket, 255 Unknown.
/// Kode yang tidak dipetakan eksplisit jatuh ke `"other"`.
pub fn resource_type_str(code: i32) -> &'static str {
    match code {
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
        254 => "websocket",
        _ => "other",
    }
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
    /// Direktori cache engine terserialisasi (`None` = tanpa cache).
    engine_cache_dir: Option<PathBuf>,
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
        Self {
            engine: RwLock::new(engine),
            list_contents: RwLock::new(list_contents.to_vec()),
            custom_rules: RwLock::new(extra_rules.to_vec()),
            allowlist: RwLock::new(normalize_allowlist(allowlist)),
            cosmetic_cache: Mutex::new(CosmeticCache::new()),
            blocked_count: AtomicU64::new(0),
            rule_count: AtomicUsize::new(rule_count),
            enabled: AtomicBool::new(enabled),
            engine_cache_dir: None,
        }
    }

    /// Konstruktor lengkap dengan cache engine terserialisasi di
    /// `cache_dir` (direktori `filter_lists/`). Bila cache cocok dengan
    /// input (daftar + rule kustom), engine di-deserialize dari cache
    /// sehingga startup tidak perlu mem-parse ulang puluhan ribu rule;
    /// bila cache hilang/rusak/tidak cocok, parse penuh dilakukan dan
    /// cache ditulis ulang. Perilaku pemblokiran identik di semua jalur.
    pub fn with_lists_and_cache(
        extra_rules: &[String],
        enabled: bool,
        list_contents: &[String],
        allowlist: &[String],
        cache_dir: &Path,
    ) -> Self {
        let hash = engine_inputs_hash(list_contents, extra_rules);
        let rule_count = count_all_rules(list_contents, extra_rules);
        let engine = try_load_cached_engine(cache_dir, hash).unwrap_or_else(|| {
            let (engine, _) = build_engine(list_contents, extra_rules);
            store_cached_engine(cache_dir, hash, &engine);
            engine
        });
        Self {
            engine: RwLock::new(engine),
            list_contents: RwLock::new(list_contents.to_vec()),
            custom_rules: RwLock::new(extra_rules.to_vec()),
            allowlist: RwLock::new(normalize_allowlist(allowlist)),
            cosmetic_cache: Mutex::new(CosmeticCache::new()),
            blocked_count: AtomicU64::new(0),
            rule_count: AtomicUsize::new(rule_count),
            enabled: AtomicBool::new(enabled),
            engine_cache_dir: Some(cache_dir.to_path_buf()),
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
    /// Bila cache engine aktif, cache ditulis ulang agar startup
    /// berikutnya memakai isi terbaru.
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
        if let Some(dir) = self.engine_cache_dir.as_ref() {
            let hash = engine_inputs_hash(&lists, &custom);
            store_cached_engine(dir, hash, &new_engine);
        }
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
        let normalized = normalize_allowlist(&domains);
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
        let lines = read_rule_file_lines(path)?;
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

        // Mapping kode ResourceType Qt6 ke tipe request adblock
        // (lihat `resource_type_str`).
        let req_type = resource_type_str(resource_type);

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

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_temp_dir(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "attis_test_{}_{}_{}",
            name,
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or(0)
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn test_resource_type_str_mapping() {
        // Arm yang dipetakan eksplisit.
        assert_eq!(resource_type_str(0), "main_frame");
        assert_eq!(resource_type_str(1), "sub_frame");
        assert_eq!(resource_type_str(2), "stylesheet");
        assert_eq!(resource_type_str(3), "script");
        assert_eq!(resource_type_str(4), "image");
        assert_eq!(resource_type_str(5), "font");
        assert_eq!(resource_type_str(6), "subdocument");
        assert_eq!(resource_type_str(7), "object");
        assert_eq!(resource_type_str(8), "media");
        assert_eq!(resource_type_str(12), "image");
        assert_eq!(resource_type_str(13), "xmlhttprequest");
        assert_eq!(resource_type_str(14), "ping");
        assert_eq!(resource_type_str(16), "csp_report");
        // WebSocket adalah 254 (header Qt 6.7), bukan 20.
        assert_eq!(resource_type_str(254), "websocket");
        // 19/20 adalah navigation preload, 21 Json (Qt 6.8+), 255
        // Unknown; semuanya jatuh ke "other", begitu pula kode liar.
        assert_eq!(resource_type_str(19), "other");
        assert_eq!(resource_type_str(20), "other");
        assert_eq!(resource_type_str(21), "other");
        assert_eq!(resource_type_str(255), "other");
        assert_eq!(resource_type_str(999), "other");
        assert_eq!(resource_type_str(-1), "other");
    }

    #[test]
    fn test_engine_inputs_hash_is_deterministic_and_sensitive() {
        let lists = vec!["||a.example^\n".to_string()];
        let custom = vec!["||b.example^".to_string()];
        let hash = engine_inputs_hash(&lists, &custom);
        assert_eq!(hash, engine_inputs_hash(&lists, &custom));
        // Perubahan isi daftar, rule kustom, atau urutan mengubah hash.
        assert_ne!(
            hash,
            engine_inputs_hash(&["||a.example^\n||c.example^\n".to_string()], &custom)
        );
        assert_ne!(hash, engine_inputs_hash(&lists, &[]));
        assert_ne!(hash, engine_inputs_hash(&[], &custom));
        assert_ne!(
            hash,
            engine_inputs_hash(
                &["||b.example^".to_string()],
                &["||a.example^\n".to_string()]
            )
        );
    }

    #[test]
    fn test_cached_engine_roundtrip_rejects_bad_cache() {
        let dir = unique_temp_dir("engine_cache");
        let lists = vec!["||cache-test.example^\n".to_string()];
        let custom = vec!["||custom-cache.example^".to_string()];
        let hash = engine_inputs_hash(&lists, &custom);

        // Belum ada cache.
        assert!(try_load_cached_engine(&dir, hash).is_none());

        let (engine, _) = build_engine(&lists, &custom);
        store_cached_engine(&dir, hash, &engine);

        // Cache valid: mesin hasil deserialize memblokir seperti hasil parse.
        let loaded = try_load_cached_engine(&dir, hash).expect("cache harus bisa dimuat");
        let req = Request::new(
            "https://cache-test.example/x.js",
            "https://example.com",
            "script",
            "GET",
        )
        .expect("request valid");
        assert!(loaded.check_network_request(&req).should_block());

        // Hash tidak cocok -> None.
        assert!(try_load_cached_engine(&dir, hash.wrapping_add(1)).is_none());

        // Isi sampah -> None.
        fs::write(engine_cache_path(&dir), b"garbage").unwrap();
        assert!(try_load_cached_engine(&dir, hash).is_none());

        // File terpotong -> None.
        store_cached_engine(&dir, hash, &engine);
        let mut bytes = fs::read(engine_cache_path(&dir)).unwrap();
        bytes.truncate(10);
        fs::write(engine_cache_path(&dir), bytes).unwrap();
        assert!(try_load_cached_engine(&dir, hash).is_none());

        let _ = fs::remove_dir_all(&dir);
    }
}
