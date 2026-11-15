fn main() {
    println!("cargo:rerun-if-changed=csrc/web_bridge.cpp");
    println!("cargo:rerun-if-changed=csrc/web_bridge.h");

    let qt = pkg_config::Config::new()
        .probe("Qt6WebEngineWidgets")
        .expect("Qt6WebEngineWidgets must be installed");

    let mut build = cc::Build::new();
    build.cpp(true);
    build.std("c++17");
    build.pic(true);
    build.file("csrc/web_bridge.cpp");

    for inc in &qt.include_paths {
        build.include(inc);
    }

    build.compile("web_bridge");

    for lib in &qt.libs {
        println!("cargo:rustc-link-lib={}", lib);
    }
    println!("cargo:rustc-link-lib=Qt6Svg");
    for path in &qt.link_paths {
        println!("cargo:rustc-link-search=native={}", path.display());
    }
}
