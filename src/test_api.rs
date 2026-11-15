use adblock::lists::{FilterSet, ParseOptions};
use adblock::Engine;

fn main() {
    let mut filter_set = FilterSet::new(false);
    let rules = vec![
        "||doubleclick.net^".to_string(),
        "||google-analytics.com^".to_string(),
        "/banner/*".to_string(),
    ];
    filter_set.add_filters(&rules, ParseOptions::default());
    let engine = Engine::from_filter_set(filter_set, true);
    let blocker_result = engine.check_network_request(&adblock::request::Request::new(
        "https://doubleclick.net/ad.js",
        "https://example.com",
        "script",
    ).unwrap());
    println!("Matched: {:?}", blocker_result.matched);
}
