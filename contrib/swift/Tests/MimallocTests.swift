import Cmimalloc
import Dispatch
import Testing

#if canImport(Glibc)
    import Glibc
#elseif canImport(Darwin)
    import Darwin
#endif

// Sanitizer builds skip the override (see contrib/swift/mimalloc.c), so ownership flips to false.
let overridden: Bool = {
    #if Override
        let process = dlopen(nil, RTLD_NOW)
        return dlsym(process, "__asan_init") == nil && dlsym(process, "__tsan_init") == nil
    #else
        return false
    #endif
}()

// Assumes MIMALLOC_DEFINES is unset; it can change these levels too.
#if Debug || (DebugInDebug && DEBUG)
    let expectedDebugLevel = 2
#else
    let expectedDebugLevel = 0
#endif

#if Secure
    let expectedSecureLevel = 4
#elseif SecureChecked
    let expectedSecureLevel = 2
#elseif SecureBasic
    let expectedSecureLevel = 1
#elseif MTE || PAC
    let expectedSecureLevel = 2  // include/mimalloc/config.h default with MTE or PAC
#else
    let expectedSecureLevel = 0
#endif

// MI_DEBUG and MI_SECURE have no getter; mi_options_print_out is the only API that reports them,
// as lines like "secure level: 4".
func buildLevels() -> (debug: Int?, secure: Int?) {
    var printed = ""
    withUnsafeMutablePointer(to: &printed) { printed in
        mi_options_print_out(
            { message, arg in
                arg!.assumingMemoryBound(to: String.self).pointee += String(cString: message!)
            }, printed)
    }
    var levels: [String: Int] = [:]
    for line in printed.split(separator: "\n") {
        let parts = line.split(separator: ":", maxSplits: 1).map { $0.filter { $0 != " " } }
        if parts.count == 2, let value = Int(parts[1]) { levels[parts[0]] = value }
    }
    return (levels["debuglevel"], levels["securelevel"])
}

final class Box {
    var value = 0
}

@Suite struct MimallocTests {
    @Test func traitsSetBuildLevels() {
        let levels = buildLevels()
        #expect(levels.debug == expectedDebugLevel)
        #expect(levels.secure == expectedSecureLevel)
    }

    // mi_stats_get rejects a mi_stats_t whose size or version differs from the compiled library's.
    @Test func statsLayoutMatchesC() {
        var stats = mi_stats_t()
        mi_stats_init(&stats)
        #expect(mi_stats_get(&stats))
    }

    @Test func directAPIAllocatesFromMimalloc() {
        let ptr = mi_malloc(100)!
        defer { mi_free(ptr) }
        #expect(mi_is_in_heap_region(ptr))
        #expect(mi_usable_size(ptr) >= 100)
    }

    #if NoTHP
        @Test func noTHPDisablesHugePages() {
            #expect(mi_option_get(mi_option_allow_thp) == 0)
        }
    #endif

    @Test func swiftObjectsComeFromMimalloc() {
        let box = Box()
        #expect(mi_is_in_heap_region(Unmanaged.passUnretained(box).toOpaque()) == overridden)
    }

    enum Allocator: CaseIterable {
        case viaMalloc, viaCalloc, viaRealloc, viaPosixMemalign, viaAlignedAlloc

        func allocate() -> (ptr: UnsafeMutableRawPointer, size: Int, alignment: Int) {
            switch self {
            case .viaMalloc:
                return (malloc(100)!, 100, MemoryLayout<Int>.alignment)
            case .viaCalloc:
                return (calloc(25, 4)!, 100, MemoryLayout<Int>.alignment)
            case .viaRealloc:
                return (realloc(malloc(16), 5000)!, 5000, MemoryLayout<Int>.alignment)
            case .viaPosixMemalign:
                var ptr: UnsafeMutableRawPointer?
                precondition(posix_memalign(&ptr, 256, 1000) == 0)
                return (ptr!, 1000, 256)
            case .viaAlignedAlloc:
                return (aligned_alloc(4096, 8192)!, 8192, 4096)
            }
        }
    }

    @Test(arguments: Allocator.allCases)
    func allocatorFamily(_ allocator: Allocator) {
        let (ptr, size, alignment) = allocator.allocate()
        defer { free(ptr) }
        #expect(mi_is_in_heap_region(ptr) == overridden)
        #expect(Int(bitPattern: ptr) % alignment == 0)
        // The Swift runtime sizes Array/String capacity from this, so it must come from the same allocator.
        #if canImport(Glibc)
            // Glibc's Swift module omits malloc.h; dlsym finds the symbol the Swift runtime binds to.
            typealias UsableSize = @convention(c) (UnsafeMutableRawPointer?) -> Int
            let usableSize = unsafeBitCast(dlsym(dlopen(nil, RTLD_NOW), "malloc_usable_size")!, to: UsableSize.self)
            #expect(usableSize(ptr) >= size)
        #else
            #expect(malloc_size(ptr) >= size)
        #endif
        if allocator == .viaCalloc {
            #expect(UnsafeRawBufferPointer(start: ptr, count: size).allSatisfy { $0 == 0 })
        }
    }

    @Test func crossThreadFree() {
        let count = 64
        nonisolated(unsafe) let handoff = UnsafeMutablePointer<UnsafeMutableRawPointer?>.allocate(capacity: count)
        handoff.initialize(repeating: nil, count: count)
        defer { handoff.deallocate() }
        DispatchQueue.concurrentPerform(iterations: count) { i in
            handoff[i] = malloc(64 + i)
        }
        for i in 0..<count { #expect(mi_is_in_heap_region(handoff[i]!) == overridden) }
        DispatchQueue.concurrentPerform(iterations: count) { i in
            free(handoff[count - 1 - i])
        }
    }
}
