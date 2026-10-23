# mimalloc-sys

Raw bindings to the [mimalloc](https://github.com/microsoft/mimalloc) C API (`mimalloc.h`).
This crate builds and links the C library; use the `mimalloc` crate (in the parent
directory) for the global allocator, heaps, and safe helpers.

See the [`mimalloc` readme](../README.md) for the features, build options, and how the
C sources are located.
