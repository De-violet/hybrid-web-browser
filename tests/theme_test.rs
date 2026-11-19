use attis::theme::generate_qss;

fn qss(bg: &str, surface: &str, fg: &str, accent: &str, border: &str) -> String {
    generate_qss(
        bg,
        surface,
        fg,
        accent,
        border,
        0.88,
        "comfortable",
        "",
        "",
        "",
    )
}

fn qss_defaults() -> String {
    qss("#121214", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33")
}

#[test]
fn valid_colors_pass_through_unchanged() {
    let out = qss_defaults();
    assert!(
        out.contains("rgba(18, 18, 20, 0.88)"),
        "bg parses to RGB: {out}"
    );
    assert!(out.contains("#1c1c1f"));
    assert!(out.contains("#e8e8ea"));
    assert!(out.contains("#8b5cf6"));
    assert!(out.contains("#2e2e33"));
}

#[test]
fn valid_hex_bg_parses_to_rgb() {
    let out = qss("#8b5cf6", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(139, 92, 246, 0.88)"), "got: {out}");
}

#[test]
fn short_rgb_colors_are_accepted() {
    let out = qss("#121214", "#abc", "#def", "#123", "#456");
    assert!(out.contains("#abc"));
    assert!(out.contains("#def"));
    assert!(out.contains("#123"));
    assert!(out.contains("#456"));
}

#[test]
fn whitespace_around_colors_is_trimmed() {
    let out = qss("  #121214  ", "  #1c1c1f ", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"));
    assert!(out.contains("#1c1c1f"));
}

#[test]
fn non_ascii_bg_falls_back_without_panic() {
    // "#€€" is 6 bytes but only 2 characters: byte-offset slicing panics.
    let out = qss("#€€", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"), "got: {out}");
}

#[test]
fn all_non_ascii_bg_falls_back() {
    let out = qss("€€€€€€", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"), "got: {out}");
}

#[test]
fn short_bg_falls_back() {
    let out = qss("#12", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"), "got: {out}");
}

#[test]
fn empty_bg_falls_back() {
    let out = qss("", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"), "got: {out}");
}

#[test]
fn bad_hex_bg_falls_back() {
    let out = qss("#zzzzzz", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"), "got: {out}");
}

#[test]
fn overlong_bg_falls_back() {
    let out = qss("#8b5cf6ff00", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(out.contains("rgba(18, 18, 20, 0.88)"), "got: {out}");
}

const INJECTION: &str = "red; } QLineEdit#urlBar { color: transparent; }";

#[test]
fn injection_payload_in_surface_is_neutralized() {
    let out = qss("#121214", INJECTION, "#e8e8ea", "#8b5cf6", "#2e2e33");
    assert!(!out.contains("QLineEdit#urlBar { color: transparent"));
    assert!(out.contains("#1c1c1f"), "fallback surface must be used");
}

#[test]
fn injection_payload_in_fg_is_neutralized() {
    let out = qss("#121214", "#1c1c1f", INJECTION, "#8b5cf6", "#2e2e33");
    assert!(!out.contains("QLineEdit#urlBar { color: transparent"));
    assert!(out.contains("#e8e8ea"), "fallback fg must be used");
}

#[test]
fn injection_payload_in_accent_is_neutralized() {
    let out = qss("#121214", "#1c1c1f", "#e8e8ea", INJECTION, "#2e2e33");
    assert!(!out.contains("QLineEdit#urlBar { color: transparent"));
    assert!(out.contains("#8b5cf6"), "fallback accent must be used");
}

#[test]
fn injection_payload_in_border_is_neutralized() {
    let out = qss("#121214", "#1c1c1f", "#e8e8ea", "#8b5cf6", INJECTION);
    assert!(!out.contains("QLineEdit#urlBar { color: transparent"));
    assert!(out.contains("#2e2e33"), "fallback border must be used");
}

#[test]
fn injection_marker_never_appears_in_output() {
    let payload = "x; } QMenu { color: #123456; } /* INJECTED_MARKER */";
    let out = qss("#121214", payload, payload, payload, payload);
    assert!(!out.contains("INJECTED_MARKER"));
    assert!(!out.contains("#123456"));
}

#[test]
fn named_colors_are_rejected() {
    let out = qss("#121214", "#1c1c1f", "#e8e8ea", "rebeccapurple", "#2e2e33");
    assert!(!out.contains("rebeccapurple"));
    assert!(out.contains("#8b5cf6"), "fallback accent must be used");
}

#[test]
fn eight_digit_hex_is_rejected() {
    let out = qss("#121214", "#1c1c1f", "#e8e8ea", "#8b5cf6", "#2e2e33ff");
    assert!(!out.contains("#2e2e33ff"));
    assert!(out.contains("#2e2e33"), "fallback border must be used");
}

#[test]
fn empty_color_tokens_use_defaults() {
    let out = qss("#121214", "", "", "", "");
    assert!(out.contains("#1c1c1f"));
    assert!(out.contains("#e8e8ea"));
    assert!(out.contains("#8b5cf6"));
    assert!(out.contains("#2e2e33"));
}

#[test]
fn icon_paths_with_spaces_are_quoted_in_qss_urls() {
    // Path direktori config Windows dapat mengandung spasi (mis. nama
    // pengguna "John Doe"); URL QSS tanpa quote akan rusak, jadi path
    // ikon harus muncul dalam tanda kutip.
    let out = generate_qss(
        "#121214",
        "#1c1c1f",
        "#e8e8ea",
        "#8b5cf6",
        "#2e2e33",
        0.88,
        "comfortable",
        "",
        "C:/Users/John Doe/AppData/Roaming/attis/tab_close.svg",
        "C:/Users/John Doe/AppData/Roaming/attis/tab_close_hover.svg",
    );
    assert!(
        out.contains("image: url(\"C:/Users/John Doe/AppData/Roaming/attis/tab_close.svg\");"),
        "got: {out}"
    );
    assert!(
        out.contains(
            "image: url(\"C:/Users/John Doe/AppData/Roaming/attis/tab_close_hover.svg\");"
        ),
        "got: {out}"
    );
}
