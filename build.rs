fn main() {
    println!("cargo:rerun-if-changed=csrc/web_bridge.cpp");
    println!("cargo:rerun-if-changed=csrc/web_bridge.h");

    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    let mut build = cc::Build::new();
    build.cpp(true);
    build.std("c++17");
    build.pic(true);
    build.file("csrc/web_bridge.cpp");

    if target_os == "windows" {
        let qt_dir = std::env::var("QT_DIR")
            .or_else(|_| std::env::var("QT_ROOT_DIR"))
            .or_else(|_| std::env::var("Qt6_DIR"))
            .expect("QT_DIR, QT_ROOT_DIR, or Qt6_DIR environment variable must be set on Windows");
        let qt_path = std::path::Path::new(&qt_dir);

        let inc_dir = qt_path.join("include");
        build.include(&inc_dir);
        build.include(inc_dir.join("QtCore"));
        build.include(inc_dir.join("QtGui"));
        build.include(inc_dir.join("QtWidgets"));
        build.include(inc_dir.join("QtWebEngineWidgets"));
        build.include(inc_dir.join("QtWebEngineCore"));
        build.include(inc_dir.join("QtSvg"));

        build.compile("web_bridge");

        let lib_dir = qt_path.join("lib");
        println!("cargo:rustc-link-search=native={}", lib_dir.display());

        let libs = [
            "Qt6Core",
            "Qt6Gui",
            "Qt6Widgets",
            "Qt6WebEngineWidgets",
            "Qt6WebEngineCore",
            "Qt6Svg",
        ];
        for lib in &libs {
            println!("cargo:rustc-link-lib={}", lib);
        }
    } else {
        let qt = pkg_config::Config::new()
            .probe("Qt6WebEngineWidgets")
            .expect("Qt6WebEngineWidgets must be installed");

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
}
