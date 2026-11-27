// swift-tools-version: 6.2
/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

import PackageDescription

// Extra defines, e.g. `MIMALLOC_DEFINES="MI_SECURE=2 MI_DEBUG=3"`; these take precedence over the traits.
let userDefines = (Context.environment["MIMALLOC_DEFINES"] ?? "")
    .split(whereSeparator: { $0.isWhitespace || $0 == "," })
    .map { "-D\($0)" }

let package = Package(
    name: "mimalloc",
    products: [
        .library(name: "mimalloc", type: .static, targets: ["Cmimalloc"]),
    ],
    traits: [
        .default(enabledTraits: ["Override"]),
        .trait(name: "Override", description: "Replace malloc, free and friends process-wide (all platforms except Windows)."),
        .trait(name: "Secure", description: "Security mitigations (MI_SECURE=4)."),
        .trait(name: "SecureChecked", description: "SecureBasic plus validated free pointers (MI_SECURE=2)."),
        .trait(name: "SecureBasic", description: "Cheap security mitigations (MI_SECURE=1)."),
        .trait(name: "Debug", description: "Assertions and internal invariant checks (MI_DEBUG=2)."),
        .trait(name: "DebugInDebug", description: "As Debug, in debug builds only."),
        .trait(name: "LocalDynamicTLS", description: "Local-dynamic TLS model, for mimalloc inside a dlopen'ed library or on musl."),
        .trait(name: "NoTHP", description: "Disable transparent huge pages by default (Linux)."),
        .trait(name: "MTE", description: "Memory tagging, required at runtime (aarch64 Linux and macOS); also enables PAC."),
        .trait(name: "PAC", description: "Pointer authentication, required at runtime (aarch64 Linux and macOS)."),
    ],
    targets: [
        .target(
            name: "Cmimalloc",
            path: ".",
            sources: ["contrib/swift/mimalloc.c"],
            // Not include/: it would export mimalloc-override.h and its malloc/free macros to C clients.
            publicHeadersPath: "contrib/swift/include",
            cSettings: [
                .headerSearchPath("include"),

                // contrib/swift/mimalloc.c maps these to mimalloc defines.
                .define("MI_SWIFT_OVERRIDE", .when(traits: ["Override"])),
                .define("MI_SWIFT_SECURE", .when(traits: ["Secure"])),
                .define("MI_SWIFT_SECURE_CHECKED", .when(traits: ["SecureChecked"])),
                .define("MI_SWIFT_SECURE_BASIC", .when(traits: ["SecureBasic"])),
                .define("MI_SWIFT_DEBUG", .when(traits: ["Debug"])),
                .define("MI_SWIFT_DEBUG", .when(configuration: .debug, traits: ["DebugInDebug"])),

                .define("MI_DEFAULT_ALLOW_THP", to: "0", .when(traits: ["NoTHP"])),
                // include/mimalloc/arch.h rejects both on other architectures.
                .define("MI_MTE", to: "2", .when(traits: ["MTE"])),
                .define("MI_PAC", to: "2", .when(traits: ["PAC"])),

                // Compiler flags from CMakeLists.txt.
                .unsafeFlags(["-O3"], .when(configuration: .release)),
                .unsafeFlags(["-mno-outline"]),
                .unsafeFlags(["-fno-builtin-malloc"], .when(traits: ["Override"])),
                .unsafeFlags(["-ftls-model=initial-exec"]),
                // Must follow initial-exec: the last -ftls-model wins.
                .unsafeFlags(["-ftls-model=local-dynamic"], .when(traits: ["LocalDynamicTLS"])),
                .unsafeFlags(
                    ["-Xclang", "-fno-split-cold-code"],
                    .when(platforms: [.macOS, .macCatalyst, .iOS, .tvOS, .watchOS, .visionOS])
                ),

                .unsafeFlags(userDefines),
            ]
        ),
        .testTarget(
            name: "MimallocTests",
            dependencies: ["Cmimalloc"],
            path: "contrib/swift/Tests"
        ),
    ]
)
