/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

// The `Allocator` trait is stable since Rust 1.100 (but not on all 1.100 nightlies)
fn allocator_api_is_stable() -> bool {
    rustversion::cfg!(since(1.101)) ||
        // all stable 1.100 versions, but not nightly ones
        (rustversion::cfg!(since(1.100)) && !rustversion::cfg!(nightly))
}

fn main() {
    println!("cargo:rustc-check-cfg=cfg(allocator_api_stable)");
    if allocator_api_is_stable() {
        println!("cargo:rustc-cfg=allocator_api_stable");
    }
}
