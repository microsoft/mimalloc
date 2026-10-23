use core::alloc::{GlobalAlloc, Layout};
use mimalloc::{MiMalloc, sys};
use std::sync::mpsc;
use std::thread;

#[global_allocator]
static GLOBAL: MiMalloc = MiMalloc;

const SIZES: &[usize] = &[
    1,
    7,
    8,
    15,
    16,
    24,
    40,
    100,
    512,
    1000,
    1024,
    1025,
    4000,
    8192,
    65536,
    1 << 20,
    8 << 20,
];

fn alignments() -> impl Iterator<Item = usize> {
    (0..=22).map(|i| 1usize << i)
}

fn fill(p: *mut u8, size: usize, seed: usize) {
    for i in 0..size {
        unsafe { p.add(i).write((i ^ seed) as u8) }
    }
}

fn check(p: *const u8, size: usize, seed: usize) {
    for i in 0..size {
        assert_eq!(unsafe { p.add(i).read() }, (i ^ seed) as u8, "byte {i}");
    }
}

#[test]
fn uses_mimalloc() {
    let b = Box::new(42u64);
    assert!(sys::mi_any_heap_contains((&*b as *const u64).cast()));
    let v: Vec<u8> = Vec::with_capacity(1 << 20);
    assert!(sys::mi_any_heap_contains(v.as_ptr().cast()));
}

#[test]
fn alloc_aligned() {
    for align in alignments() {
        for &size in SIZES {
            let layout = Layout::from_size_align(size, align).unwrap();
            unsafe {
                let p = MiMalloc.alloc(layout);
                assert!(!p.is_null());
                assert_eq!(p as usize % align, 0, "size {size}, align {align}");
                assert!(MiMalloc.usable_size(p) >= size);
                fill(p, size.min(4096), size);
                check(p, size.min(4096), size);
                MiMalloc.dealloc(p, layout);
            }
        }
    }
}

#[test]
fn alloc_zeroed() {
    for align in alignments().take(14) {
        for &size in SIZES {
            let layout = Layout::from_size_align(size, align).unwrap();
            unsafe {
                // dirty some blocks first so they may be reused
                let q = MiMalloc.alloc(layout);
                q.write_bytes(0xFF, size);
                MiMalloc.dealloc(q, layout);
                let p = MiMalloc.alloc_zeroed(layout);
                assert!(!p.is_null());
                assert_eq!(p as usize % align, 0);
                assert!(
                    (0..size).all(|i| p.add(i).read() == 0),
                    "size {size}, align {align}"
                );
                MiMalloc.dealloc(p, layout);
            }
        }
    }
}

#[test]
fn realloc_aligned() {
    for align in alignments().take(14) {
        for &size in SIZES {
            for &new_size in &[1, size / 2 + 1, size, size + 1, 2 * size, 4096, 100_000] {
                let layout = Layout::from_size_align(size, align).unwrap();
                unsafe {
                    let p = MiMalloc.alloc(layout);
                    fill(p, size.min(4096), align);
                    let q = MiMalloc.realloc(p, layout, new_size);
                    assert!(!q.is_null());
                    assert_eq!(
                        q as usize % align,
                        0,
                        "size {size} -> {new_size}, align {align}"
                    );
                    check(q, size.min(new_size).min(4096), align);
                    MiMalloc.dealloc(q, Layout::from_size_align(new_size, align).unwrap());
                }
            }
        }
    }
}

// alignment above the size but at most 16 (as in mimalloc_rust issue #87)
#[test]
fn small_over_aligned() {
    #[repr(align(16))]
    struct A16(#[allow(dead_code)] u8);
    #[repr(align(32))]
    struct A32(#[allow(dead_code)] [u8; 24]);
    let a: Vec<Box<A16>> = (0..1000).map(|i| Box::new(A16(i as u8))).collect();
    let b: Vec<Box<A32>> = (0..1000).map(|_| Box::new(A32([0; 24]))).collect();
    assert!(a.iter().all(|x| (&**x as *const A16 as usize) % 16 == 0));
    assert!(b.iter().all(|x| (&**x as *const A32 as usize) % 32 == 0));
}

#[test]
fn collections() {
    let mut v = Vec::new();
    for i in 0..100_000u64 {
        v.push(i);
    }
    v.shrink_to_fit();
    assert_eq!(v.iter().sum::<u64>(), 99_999 * 100_000 / 2);
    let s: String = (0..10_000)
        .map(|i| char::from(b'a' + (i % 26) as u8))
        .collect();
    assert_eq!(s.len(), 10_000);
    let m: std::collections::BTreeMap<u32, String> =
        (0..10_000).map(|i| (i, i.to_string())).collect();
    assert_eq!(m[&1234], "1234");
}

#[test]
fn cross_thread_free() {
    let (tx, rx) = mpsc::channel::<Vec<Box<[u8]>>>();
    let consumer = thread::spawn(move || rx.iter().map(|v| v.len()).sum::<usize>());
    let producers: Vec<_> = (0..8)
        .map(|t| {
            let tx = tx.clone();
            thread::spawn(move || {
                for round in 0..50 {
                    let v: Vec<Box<[u8]>> = (0..200)
                        .map(|i| vec![t as u8; (i * 7 + round) % 3000].into())
                        .collect();
                    tx.send(v).unwrap();
                }
            })
        })
        .collect();
    drop(tx);
    producers.into_iter().for_each(|p| p.join().unwrap());
    assert_eq!(consumer.join().unwrap(), 8 * 50 * 200);
}

#[test]
fn version_and_sizes() {
    assert_eq!(mimalloc::version(), sys::MI_MALLOC_VERSION as u32);
    assert_eq!(MiMalloc.version(), mimalloc::version());
    assert!(mimalloc::good_size(100) >= 100);
    assert_eq!(unsafe { mimalloc::usable_size(core::ptr::null()) }, 0);
}

#[test]
fn options() {
    let max = mimalloc::option_get(sys::mi_option_max_warnings);
    mimalloc::option_set(sys::mi_option_max_warnings, max + 1);
    assert_eq!(mimalloc::option_get(sys::mi_option_max_warnings), max + 1);
    mimalloc::option_set(sys::mi_option_max_warnings, max);
    assert_eq!(
        sys::mi_option_get_size(sys::mi_option_arena_reserve),
        (sys::mi_option_get(sys::mi_option_arena_reserve) as usize) * 1024
    );
}

#[test]
fn stats() {
    let _v = vec![0u8; 100_000];
    mimalloc::collect(false);
    let json = mimalloc::stats_json().expect("stats");
    assert!(json.trim_start().starts_with('{') && json.trim_end().ends_with('}'));
    assert!(json.contains("\"mimalloc_version\""));
}
