/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use std::env;

// The `Allocator` trait is stable since Rust 1.100 (but not on all 1.100 nightlies)
fn allocator_api_is_stable() -> bool {
    let rustc = env::var_os("RUSTC").unwrap_or_else(|| "rustc".into());
    let Ok(out) = std::process::Command::new(rustc).arg("--version").output() else {
        return false;
    };
    let version = String::from_utf8_lossy(&out.stdout);
    let minor = version
        .split_whitespace()
        .nth(1)
        .and_then(|v| v.split('.').nth(1))
        .and_then(|m| m.parse::<u32>().ok());
    minor.is_some_and(|m| m > 100 || (m == 100 && !version.contains("nightly")))
}

fn main() {
    println!("cargo:rustc-check-cfg=cfg(allocator_api_stable)");
    if allocator_api_is_stable() {
        println!("cargo:rustc-cfg=allocator_api_stable");
    }
}
