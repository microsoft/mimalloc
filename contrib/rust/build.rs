/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use std::env;

// The `Allocator` trait is stable since Rust 1.100, 1.100 pre-release
// builds may still lack it, so we only enable this for 1.100 stable or newer.
fn allocator_api_is_stable() -> bool {
    let rustc = env::var_os("RUSTC").unwrap_or_else(|| "rustc".into());
    let Ok(out) = std::process::Command::new(rustc).arg("-vV").output() else {
        return false;
    };
    let Ok(stdout) = String::from_utf8(out.stdout) else {
        return false;
    };

    let mut minor = None;
    let mut is_stable = false;
    for line in stdout.lines() {
        if let Some(release) = line.strip_prefix("release: ") {
            minor = release
                .split('.')
                .nth(1)
                .and_then(|m| m.parse::<u32>().ok());
        } else if let Some(channel) = line.strip_prefix("channel: ") {
            is_stable = channel.trim() == "stable";
        }
    }

    minor.map_or(false, |m| m > 100 || (m == 100 && is_stable))
}

fn main() {
    println!("cargo:rerun-if-env-changed=RUSTC");
    println!("cargo:rustc-check-cfg=cfg(allocator_api_stable)");
    if allocator_api_is_stable() {
        println!("cargo:rustc-cfg=allocator_api_stable");
    }
}
