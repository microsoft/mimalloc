/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
fn main() {
    if rustversion::cfg!(since(1.80)) {
        println!("cargo:rustc-check-cfg=cfg(allocator_api_stable)");
    }
    // the `Allocator` trait is stable since Rust 1.100 (but not on all 1.100 nightlies)
    if rustversion::cfg!(any(since(1.101), all(since(1.100), not(nightly)))) {
        println!("cargo:rustc-cfg=allocator_api_stable");
    }
}
