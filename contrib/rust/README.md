# mimalloc for Rust

Rust bindings for [mimalloc](https://github.com/microsoft/mimalloc), a fast general purpose
allocator, built directly from the mimalloc sources in this repository.

## Quick start

Add the dependency to your `Cargo.toml`:

```toml
[dependencies]
mimalloc = { git = "https://github.com/microsoft/mimalloc", tag = "v3.7.2" }
```

and use it as the global allocator:

```rust
use mimalloc::MiMalloc;

#[global_allocator]
static GLOBAL: MiMalloc = MiMalloc;
```

That is all: every allocation in your program (including `std`) now goes through mimalloc.
To use the basic security mitigations as well, enable a feature:

```toml
[dependencies]
mimalloc = { git = "https://github.com/microsoft/mimalloc", tag = "v3.7.2", features = ["secure_basic"] }
```

## Performance tips

- Enable LTO so that allocations in other crates (and std) are inlined with a statically
  known size and alignment (instead of going through the `__rust_alloc` shim):
  ```toml
  [profile.release]
  lto = "thin"
  ```
- Optimize mimalloc in debug builds too (the C library is compiled as part of `mimalloc-sys`):
  ```toml
  [profile.dev.package.mimalloc-sys]
  opt-level = 3
  ```
- Use the `opt_arch` feature (or `-C target-cpu=native`) for architecture specific optimizations.
- Cross-language LTO can inline the allocation fast paths into Rust code: build with
  clang (matching the LLVM version of `rustc`) using `CC=clang CFLAGS=-flto=thin` and
  `RUSTFLAGS=-Clinker-plugin-lto` (see the `rustc` [linker-plugin-lto](https://doc.rust-lang.org/rustc/linker-plugin-lto.html) documentation).

## Options

### Build features

| Feature             | Description |
|---------------------|-------------|
| `secure`            | Full security mitigations (as `-DMI_SECURE=ON`) |
| `secure_basic`      | Cheap security mitigations: guard pages, randomization, encoded free lists, double free detection (as `-DMI_SECURE=BASIC`) |
| `secure_checked`    | `secure_basic` plus validated free pointers and randomized free lists (as `-DMI_SECURE=CHECKED`) |
| `override`          | Also override `malloc`, `free`, etc. in the final binary |
| `debug`             | Build mimalloc with assertions and internal checks |
| `debug_in_debug`    | As `debug`, but only when `debug_assertions` are on |
| `local_dynamic_tls` | Use the local-dynamic TLS model (for dynamically loaded libraries) |
| `no_thp`            | Disable transparent huge pages by default on Linux |
| `opt_arch`          | Architecture specific optimizations (as `-DMI_OPT_ARCH=ON`) |
| `mte`               | Memory tagging; requires MTE support at runtime (aarch64 Linux, Android, and Apple only (where macOS also needs the MTE entitlement); as `-DMI_MTE=ON`, which also enables PAC unless `MIMALLOC_DEFINES="MI_PAC=0"`) |
| `pac`               | Sign internal pointers with pointer authentication; requires PAC support at runtime (aarch64 Linux, Android, and Apple only; as `-DMI_PAC=ON`) |
| `nightly`           | `Allocator` support on nightly compilers before Rust 1.100 (`mimalloc` only) |

All features except `nightly` are build options of the C library in `mimalloc-sys`
(and are forwarded by `mimalloc`). Feature `mte` does not need a separate `secure` or `pac` (but encompasses it).
If several `secure*` features are enabled, the highest level is used.

Architecture optimizations are also enabled automatically if the Rust target features
already imply them (e.g. `-C target-cpu=native`).

Features can be enabled per target, e.g. to only override `malloc` on Linux:

```toml
[target.'cfg(target_os = "linux")'.dependencies]
mimalloc = { git = "https://github.com/microsoft/mimalloc", tag = "v3.7.2", features = ["override"] }
```

Other build options can be passed as C defines in the `MIMALLOC_DEFINES` environment
variable (separated by spaces or commas); these take precedence over the defines set by features.
For example: `MIMALLOC_DEFINES="MI_DEBUG=3 MI_SKIP_COLLECT_ON_EXIT=1" cargo build`.

### Runtime options

mimalloc reads its runtime options from `MIMALLOC_*` environment variables (see the
[environment options](https://github.com/microsoft/mimalloc#environment-options)), for example:

```sh
MIMALLOC_SHOW_STATS=1 cargo run --release     # print statistics at exit
MIMALLOC_VERBOSE=1 cargo run --release        # print verbose messages and statistics
```

Options can also be set from code (preferably at the start of `main`):

```rust
use mimalloc::sys;

mimalloc::option_set(sys::mi_option_purge_delay, 100);   // purge unused memory after 100ms
mimalloc::option_set_enabled(sys::mi_option_show_stats, true);
```

## Programming with mimalloc

As usual for Rust FFI bindings, the bindings are split into two crates:

- `mimalloc-sys` (in `sys/`): builds and links the C library and has the raw C API.
- `mimalloc`: the global allocator, heaps, and safe helpers (with the raw API re-exported as `mimalloc::sys`).

The `mimalloc` crate provides:

- `MiMalloc`: the global allocator (with fast paths for small and sized allocations).
- `Heap`: first-class heaps that can be shared between threads; dropping a heap
  keeps its live blocks valid (`Heap::destroy` frees them all at once).
- `Arena`: reserve memory for heaps that only allocate in it (see [Arenas](#arenas)).
- `sys`: the raw C API (`mimalloc.h`).
- Both `MiMalloc` and `&Heap` implement `core::alloc::Allocator` on Rust 1.100+
  (or on an older nightly with the `nightly` feature). For speed, `allocate` returns the
  requested size instead of the full usable size; to use the full capacity, request
  `good_size(n)` bytes upfront, or use `MiMallocExcess` that returns the full usable size.
- Helpers such as `collect`, `option_set`, `stats_print` and `stats_json`.

### Statistics

```rust
mimalloc::stats_print();                     // print to stderr
if let Some(json) = mimalloc::stats_json() {
    println!("{json}");
}
mimalloc::collect(true);                     // return unused memory to the OS
```

### Heaps

A `Heap` can be used as an allocator for collections. Its blocks can be freed from any
thread, and dropping the heap migrates any live blocks to the main heap:

```rust
use mimalloc::Heap;

let heap = Heap::new();
let mut v = Vec::new_in(&heap);
v.push(42);
let b = Box::new_in([0u8; 64], &heap);
```

When all blocks of a heap are no longer needed, `Heap::destroy` (unsafe) frees them all at once,
without dropping each of them individually.

### Arenas

An arena is a large area of memory that heaps allocate from. A heap created with
`Heap::new_in_arena` only allocates in its arena, and returns null when the arena is full.

```rust
use mimalloc::{Arena, Heap, ReserveOptions};

let options = ReserveOptions { exclusive: true, ..ReserveOptions::default() };
let arena = Arena::reserve_from_os(64 << 20, options).unwrap();
let heap = Heap::new_in_arena(arena);
```

- `Arena::reserve_from_os` reserves fresh OS memory; `Arena::from_static_memory` uses memory
  that lives forever (such as a leaked `Box`); `Arena::from_external_memory` (unsafe) uses any
  other memory.
  They return `None` if the arena cannot be created.
- An `exclusive` arena keeps all other heaps out; otherwise any heap can use it as well.
- Arenas cannot be removed, so they live until the process exits.
- Arenas need at least `Arena::min_size()` bytes (32 MiB on 64-bit), and their start is
  aligned to `Arena::min_alignment()` (256 MiB on 64-bit, or 64 KiB with the `secure` or `secure_checked` feature),
  so memory at an unaligned address needs `max(min_size, min_alignment) + min_alignment` bytes.

## Building and publishing

The C sources are found by `mimalloc-sys` in the following order:
1. The `MIMALLOC_SRC_DIR` environment variable.
2. A vendored `mimalloc/` directory next to `sys/Cargo.toml` (with `include/`, `src/`, and `LICENSE`).
3. The enclosing repository (`../../..` from `sys/`).

For a local checkout of this repository, use a path dependency:

```toml
[dependencies]
mimalloc = { path = ".../mimalloc/contrib/rust" }
```

To publish crates based on these, copy `contrib/rust`, vendor the sources into
`sys/mimalloc/` (`cp -r ../../include ../../src ../../LICENSE sys/mimalloc/`), and rename both
packages (the names `mimalloc` and `mimalloc-sys` are already taken on crates.io). The
`mimalloc` crate depends on the exact version of `mimalloc-sys` as its fast paths depend
on internal entry points such as `mi_wmalloc_small` and `MI_SMALL_SIZE_MAX`.

## Testing

```sh
cargo test --workspace
cargo test --release --features secure
cargo test --features debug,override
cargo +nightly test --features nightly
```

The tests also run as `test-rust` with `ctest` in a CMake build (if `cargo` is found),
using features that match the CMake configuration.
