use core::alloc::{GlobalAlloc, Layout};
use core::mem::MaybeUninit;
use core::ptr::NonNull;
use mimalloc::{Arena, ExternalMemory, Heap, MiMalloc, ReserveOptions};

const MIB: usize = 1 << 20;
const EXCLUSIVE: ReserveOptions = ReserveOptions {
    exclusive: true,
    commit: false,
    allow_large_pages: false,
};

fn build_define(name: &str, default: usize) -> usize {
    for define in option_env!("MIMALLOC_DEFINES")
        .unwrap_or_default()
        .rsplit(|c: char| c.is_whitespace() || c == ',')
    {
        let (key, value) = define.split_once('=').unwrap_or((define, "1"));
        if key == name {
            return value
                .parse()
                .unwrap_or_else(|_| panic!("expected a numeric {name}, got {value}"));
        }
    }
    default
}

fn expect_pinned_arena(arena: Option<Arena>) -> Option<Arena> {
    // Match the C defaults and explicit overrides, not just the Cargo `secure` feature.
    let default_secure = if cfg!(feature = "secure") {
        4
    } else if build_define("MI_MTE", 2 * usize::from(cfg!(feature = "mte"))) != 0
        || build_define("MI_PAC", 2 * usize::from(cfg!(feature = "pac"))) != 0
    {
        2
    } else if build_define("MI_CHERI", 0) != 0 {
        3
    } else {
        0
    };
    let secure = build_define("MI_SECURE", default_secure);
    let guard_pages = build_define(
        "MI_OS_HAS_GUARD_PAGES",
        usize::from(!cfg!(target_family = "wasm")),
    );
    if secure > 0 && guard_pages != 0 {
        assert!(arena.is_none(), "secure mode must reject pinned arenas");
        None
    } else {
        Some(arena.expect("pinned arenas must be accepted without secure guard pages"))
    }
}

fn area_contains(arena: Arena, p: *const u8, size: usize) -> bool {
    let area = arena.area();
    let start = area.cast::<u8>().as_ptr() as usize;
    // ignore a memory tag in the top byte (with the `mte` feature)
    let p = if cfg!(target_arch = "aarch64") {
        p as usize & ((1 << 56) - 1)
    } else {
        p as usize
    };
    p >= start && p + size <= start + area.len()
}

// allocate, write, and free blocks of various sizes; all must be inside the arena
fn check_heap_uses_arena(heap: &Heap, arena: Arena) {
    for size in [8, 100, 1024, 10_000, 100_000, MIB] {
        let layout = Layout::from_size_align(size, 8).unwrap();
        let p = heap.alloc(layout);
        assert!(!p.is_null());
        assert!(arena.contains(p) && area_contains(arena, p, size));
        unsafe {
            p.write_bytes(1, size);
            heap.dealloc(p, layout);
        }
    }
}

// size of memory at an unaligned address that is always large enough for an arena
fn unaligned_arena_size() -> usize {
    Arena::min_size().max(Arena::min_alignment()) + Arena::min_alignment()
}

// leak `size` bytes of memory that start at an `align`-aligned address
// (we align by hand: with the `override` feature the system allocator is mimalloc, which does not
// support alignments as large as `Arena::min_alignment()`)
fn leak_aligned_memory(size: usize, align: usize) -> *mut u8 {
    let memory = Box::leak(Box::<[u8]>::new_uninit_slice(size + align));
    let start = memory.as_mut_ptr().cast::<u8>();
    unsafe { start.add((start as usize).wrapping_neg() & (align - 1)) }
}

#[test]
fn reserve_from_os_arena() {
    let arena = Arena::reserve_from_os(Arena::min_size(), EXCLUSIVE).unwrap();
    assert_eq!(arena.area().len(), Arena::min_size());
    let heap = Heap::new_in_arena(arena);
    check_heap_uses_arena(&heap, arena);

    // alignments above the arena slice size (64 KiB, or up to 256 KiB in secure builds) are allocated
    // from the OS, which an arena heap never does
    let layout = Layout::from_size_align(64, 64 << 10).unwrap();
    let p = heap.alloc(layout);
    assert!(!p.is_null() && p as usize % (64 << 10) == 0 && arena.contains(p));
    unsafe { heap.dealloc(p, layout) };
    assert!(
        heap.alloc(Layout::from_size_align(64, 1 << 20).unwrap())
            .is_null()
    );

    // other heaps never allocate in an exclusive arena
    let layout = Layout::new::<u64>();
    let p = unsafe { MiMalloc.alloc(layout) };
    assert!(!arena.contains(p));
    unsafe { MiMalloc.dealloc(p, layout) };
}

#[test]
fn full_arena_returns_null() {
    let arena = Arena::reserve_from_os(Arena::min_size(), EXCLUSIVE).unwrap();
    let heap = Heap::new_in_arena(arena);
    let layout = Layout::from_size_align(MIB, 8).unwrap();
    let mut blocks = Vec::new();
    loop {
        let p = heap.alloc(layout);
        if p.is_null() {
            break;
        }
        assert!(area_contains(arena, p, MIB));
        blocks.push(p);
    }
    assert!(!blocks.is_empty() && blocks.len() <= Arena::min_size() / MIB);

    // freed memory can be reused
    for p in blocks {
        unsafe { heap.dealloc(p, layout) };
    }
    let p = heap.alloc(layout);
    assert!(!p.is_null());
    unsafe { heap.dealloc(p, layout) };
}

#[test]
fn from_static_memory_unaligned() {
    // what the README recommends for memory at an unaligned address
    let memory = Box::leak(Box::<[u8]>::new_uninit_slice(unaligned_arena_size()));
    let (start, len) = (memory.as_ptr() as usize, memory.len());
    let Some(arena) = expect_pinned_arena(Arena::from_static_memory(memory, true)) else {
        return;
    };
    let area = arena.area();
    assert!(area.cast::<u8>().as_ptr() as usize >= start);
    assert!(area.cast::<u8>().as_ptr() as usize + area.len() <= start + len);
    check_heap_uses_arena(&Heap::new_in_arena(arena), arena);
}

#[test]
fn from_static_memory_inside_mimalloc_block() {
    // an arena inside a leaked mimalloc block
    let len = unaligned_arena_size();
    let p = unsafe { MiMalloc.alloc(Layout::from_size_align(len, 16).unwrap()) };
    assert!(!p.is_null());
    let memory = unsafe { core::slice::from_raw_parts_mut(p.cast::<MaybeUninit<u8>>(), len) };
    if let Some(arena) = expect_pinned_arena(Arena::from_static_memory(memory, false)) {
        let heap = Heap::new_in_arena(arena);
        check_heap_uses_arena(&heap, arena);
        heap.collect(true);
    }

    // the global allocator keeps working
    for size in [8, 1000, 100_000, 10 * MIB] {
        let layout = Layout::from_size_align(size, 8).unwrap();
        let q = unsafe { MiMalloc.alloc(layout) };
        assert!(!q.is_null());
        unsafe {
            q.write_bytes(1, size);
            MiMalloc.dealloc(q, layout);
        }
    }
    mimalloc::collect(true);
}

#[test]
fn from_external_memory_aligned() {
    let len = Arena::min_size();
    let start = leak_aligned_memory(len, Arena::min_alignment());
    let memory = NonNull::slice_from_raw_parts(NonNull::new(start).unwrap(), len);
    let info = ExternalMemory {
        exclusive: true,
        committed: true,
        pinned: true,
        ..ExternalMemory::default()
    };
    let Some(arena) = expect_pinned_arena(unsafe { Arena::from_external_memory(memory, info) })
    else {
        return;
    };
    assert_eq!(arena.area(), memory);
    check_heap_uses_arena(&Heap::new_in_arena(arena), arena);
}

#[test]
fn too_small_memory_is_rejected() {
    let (min_size, align) = (Arena::min_size(), Arena::min_alignment());

    let memory = Box::leak(Box::<[u8]>::new_uninit_slice(MIB));
    assert!(Arena::from_static_memory(memory, false).is_none());

    // `min_size` bytes are not enough if the memory is not aligned
    let start = leak_aligned_memory(min_size + 1, align);
    let memory = unsafe {
        core::slice::from_raw_parts_mut(start.add(1).cast::<MaybeUninit<u8>>(), min_size)
    };
    assert!(Arena::from_static_memory(memory, false).is_none());
}
