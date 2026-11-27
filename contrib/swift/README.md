# mimalloc for Swift

Swift package for [mimalloc](https://github.com/microsoft/mimalloc), built from the sources in this repository.

## Usage

Requires Swift 6.2 or later.

Add the product to an executable target:

```swift
dependencies: [
    .package(url: "https://github.com/microsoft/mimalloc", from: "3.8.1"),
],
targets: [
    .executableTarget(
        name: "MyServer",
        dependencies: [.product(name: "mimalloc", package: "mimalloc")]
    ),
]
```

The default `Override` trait replaces `malloc`, `free` and friends for the whole process on every platform except Windows: Swift runtime, Foundation and C libraries all allocate from mimalloc, no `import` needed. Override is skipped under ASan and TSan.

The C API (`mi_malloc`, `mi_heap_new`, `mi_stats_get`, ...) is available with `import Cmimalloc`; see the [API documentation](https://microsoft.github.io/mimalloc/).

## Traits

Build options are package traits:

- `Override` (default): replace `malloc` process-wide, see above.
- `Secure`, `SecureChecked`, `SecureBasic`: `MI_SECURE` 4, 2 and 1; the highest enabled level wins.
- `Debug`: assertions and invariant checks (`MI_DEBUG=2`); `DebugInDebug` only in debug builds. Otherwise mimalloc builds with `MI_DEBUG=0` and `NDEBUG`.
- `LocalDynamicTLS`: `-ftls-model=local-dynamic`, for mimalloc inside a `dlopen`ed library. On musl (Swift static Linux SDK) enable it unless the executable is fully static; the manifest cannot detect musl.
- `NoTHP`: no transparent huge pages by default on Linux.
- `MTE`, `PAC`: memory tagging and pointer authentication, required at runtime (aarch64 Linux and macOS). On macOS, MTE also needs the executable signed with the entitlements in `cmake/mte-entitlements.plist`.

```swift
.package(url: "https://github.com/microsoft/mimalloc", from: "3.8.1", traits: ["Override", "SecureBasic"]),
```

Traits unify across the dependency graph: if any package enables `Override`, the whole process gets it. A library should therefore disable the default traits and leave every trait to the executable; `MTE` and `PAC`, for one, fail to compile on x86_64:

```swift
.package(url: "https://github.com/microsoft/mimalloc", from: "3.8.1", traits: []),
```

Never enable `Override` in a dynamic library: a second allocator in the process would receive pointers it did not allocate.

## Other options

Defines without a trait go through the `MIMALLOC_DEFINES` environment variable at build time, e.g. `MIMALLOC_DEFINES="MI_SECURE=2 MI_GUARDED=1" swift build`; these take precedence over the traits. Runtime options are the usual `MIMALLOC_*` environment variables or `mi_option_set`.

## Test

```sh
swift test
```
