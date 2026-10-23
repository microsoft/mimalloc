/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use std::env;
use std::path::PathBuf;

// Locate the mimalloc sources: `MIMALLOC_SRC_DIR`, a vendored `mimalloc/` directory, or this repository.
fn mimalloc_dir() -> PathBuf {
    println!("cargo:rerun-if-env-changed=MIMALLOC_SRC_DIR");
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let dir = match env::var_os("MIMALLOC_SRC_DIR") {
        Some(dir) => PathBuf::from(dir),
        None if manifest.join("mimalloc/src/static.c").exists() => manifest.join("mimalloc"),
        None => manifest.join("../../.."),
    };
    if !dir.join("src/static.c").exists() {
        panic!(
            "cannot find the mimalloc sources in {} (set MIMALLOC_SRC_DIR)",
            dir.display()
        );
    }
    dir
}

fn feature(name: &str) -> bool {
    env::var_os(format!("CARGO_FEATURE_{}", name.to_uppercase())).is_some()
}

// Extra defines from `MIMALLOC_DEFINES`, e.g. `MI_DEBUG=3 MI_SKIP_COLLECT_ON_EXIT=1`
fn user_defines() -> Vec<(String, Option<String>)> {
    println!("cargo:rerun-if-env-changed=MIMALLOC_DEFINES");
    let defines = env::var("MIMALLOC_DEFINES").unwrap_or_default();
    defines
        .split(|c: char| c.is_whitespace() || c == ',')
        .filter(|d| !d.is_empty())
        .map(|d| match d.split_once('=') {
            Some((name, value)) => (name.to_string(), Some(value.to_string())),
            None => (d.to_string(), None),
        })
        .collect()
}

fn main() {
    let dir = mimalloc_dir();
    let include = dir.join("include");
    println!("cargo:rerun-if-changed={}", include.display());
    println!("cargo:rerun-if-changed={}", dir.join("src").display());
    println!("cargo:include={}", include.display());

    let var = |name: &str| env::var(name).unwrap_or_default();
    let target_os = var("CARGO_CFG_TARGET_OS");
    let target_env = var("CARGO_CFG_TARGET_ENV");
    let target_arch = var("CARGO_CFG_TARGET_ARCH");
    let target_vendor = var("CARGO_CFG_TARGET_VENDOR");
    let target_features = var("CARGO_CFG_TARGET_FEATURE");
    let has_features = |names: &[&str]| {
        names
            .iter()
            .all(|f| target_features.split(',').any(|t| t == *f))
    };
    let is_apple = target_vendor == "apple";
    let is_windows = target_os == "windows";
    let is_musl = target_env == "musl";

    let mut build = cc::Build::new();
    build.include(&include).file(dir.join("src/static.c"));
    let compiler = build.get_compiler();
    let is_msvc = compiler.is_like_msvc();
    let is_gnu_like = (compiler.is_like_gnu() || compiler.is_like_clang()) && !is_msvc;

    // `MIMALLOC_DEFINES` take precedence over the defines set by features
    let user = user_defines();
    let is_user = |name: &str| user.iter().any(|(n, _)| n == name);
    let mut define = |name: &str, value: &str| {
        if !is_user(name) {
            build.define(name, value);
        }
    };

    // build options
    let debug = feature("debug")
        || (feature("debug_in_debug") && env::var_os("CARGO_CFG_DEBUG_ASSERTIONS").is_some());
    if debug {
        define("MI_DEBUG", "2");
    } else if !is_user("MI_DEBUG") {
        define("MI_DEBUG", "0");
        define("NDEBUG", "1");
    }
    if feature("secure") {
        define("MI_SECURE", "4");
    }
    let is_override = feature("override") && !is_windows;
    if is_override {
        define("MI_MALLOC_OVERRIDE", "1");
        if is_apple {
            define("MI_OSX_ZONE", "1");
            define("MI_OSX_INTERPOSE", "1");
        }
    }
    if feature("no_thp") && (target_os == "linux" || target_os == "android") {
        define("MI_DEFAULT_ALLOW_THP", "0");
    }
    if is_musl {
        define("MI_LIBC_MUSL", "1");
    }
    if feature("win_direct_tls") && is_windows {
        define("MI_WIN_DIRECT_TLS", "1");
    }
    for (name, value) in &user {
        build.define(name, value.as_deref());
    }
    if is_override && is_gnu_like {
        build.flag("-fno-builtin-malloc");
    }

    // compiler flags (as in CMakeLists.txt)
    if is_msvc {
        // use C++ atomics with msvc and clang-cl
        build
            .cpp(true)
            .flag("/TP")
            .flag("/Zc:__cplusplus")
            .std("c++17");
    } else if is_gnu_like {
        build
            .flag("-fvisibility=hidden")
            .flag("-Wno-unknown-pragmas");
        if compiler.is_like_clang() {
            build
                .flag("-Wno-static-in-inline")
                .flag_if_supported("-mno-outline");
            if is_apple && target_arch == "aarch64" && is_apple_clang(&compiler) {
                build.flag("-Xclang").flag("-fno-split-cold-code");
            }
        }
        if target_os != "haiku" {
            let tls = if feature("local_dynamic_tls") || is_musl {
                "local-dynamic"
            } else {
                "initial-exec"
            };
            build.flag(format!("-ftls-model={tls}"));
        }
    }

    // architecture specific optimizations: enabled with `opt_arch`, or if the Rust target already implies them
    let opt_arch = feature("opt_arch");
    match target_arch.as_str() {
        "x86_64" if opt_arch || has_features(&["avx2", "bmi1", "bmi2", "lzcnt"]) => {
            if is_msvc {
                build.flag("/arch:AVX2");
            } else if is_gnu_like {
                build.flag("-march=haswell").flag("-mavx2");
            }
        }
        "aarch64" if is_apple && (opt_arch || has_features(&["lse", "rcpc"])) => {
            if is_gnu_like {
                build.flag("-march=armv8.3-a");
            }
        }
        "aarch64" if opt_arch || has_features(&["lse"]) => {
            if is_msvc {
                build.flag("/arch:armv8.1");
            } else if is_gnu_like {
                build.flag("-march=armv8.1-a");
            }
        }
        _ => {}
    }

    build.compile("mimalloc");

    // system libraries
    if is_windows {
        for lib in ["psapi", "shell32", "user32", "advapi32", "bcrypt"] {
            println!("cargo:rustc-link-lib={lib}");
        }
    } else if target_os == "linux" && (target_arch == "arm" || target_arch.starts_with("riscv32")) {
        println!("cargo:rustc-link-lib=atomic");
    }
}

fn is_apple_clang(compiler: &cc::Tool) -> bool {
    let out = compiler.to_command().arg("--version").output();
    out.is_ok_and(|out| String::from_utf8_lossy(&out.stdout).contains("Apple clang"))
}
