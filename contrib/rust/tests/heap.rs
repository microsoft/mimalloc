use core::alloc::Layout;
use mimalloc::{Heap, sys};
use std::sync::Arc;
use std::thread;

#[test]
fn heap_alloc() {
    let heap = Heap::new();
    for align in [1, 8, 16, 64, 4096, 1 << 16] {
        for size in [1, 8, 24, 1024, 1025, 100_000, 1 << 20] {
            let layout = Layout::from_size_align(size, align).unwrap();
            let p = heap.alloc(layout);
            assert!(!p.is_null());
            assert_eq!(p as usize % align, 0);
            assert!(heap.contains(p));
            unsafe {
                p.write_bytes(1, size);
                let q = heap.realloc(p, layout, 2 * size);
                assert_eq!(q as usize % align, 0);
                assert!(heap.contains(q));
                assert!((0..size).all(|i| q.add(i).read() == 1));
                heap.dealloc(q, Layout::from_size_align(2 * size, align).unwrap());
            }
            let z = heap.alloc_zeroed(layout);
            assert!((0..size).all(|i| unsafe { z.add(i).read() } == 0));
            unsafe { heap.dealloc(z, layout) };
        }
    }
    heap.collect(true);
}

#[test]
fn heap_contains() {
    let (a, b) = (Heap::new(), Heap::new());
    let layout = Layout::new::<u64>();
    let p = a.alloc(layout);
    assert!(a.contains(p) && !b.contains(p));
    assert_eq!(sys::mi_heap_of(p.cast()), a.as_ptr());
    unsafe { a.dealloc(p, layout) };
}

#[test]
fn heap_drop_keeps_live_blocks() {
    let layout = Layout::from_size_align(256, 32).unwrap();
    let blocks: Vec<usize> = {
        let heap = Heap::new();
        (0..1000)
            .map(|i| {
                let p = heap.alloc(layout);
                unsafe { p.write_bytes(i as u8, 256) };
                p as usize
            })
            .collect()
    };
    for (i, &p) in blocks.iter().enumerate() {
        let p = p as *mut u8;
        assert!(sys::mi_any_heap_contains(p.cast()));
        assert!((0..256).all(|j| unsafe { p.add(j).read() } == i as u8));
        unsafe { sys::mi_free(p.cast()) };
    }
}

#[test]
fn heap_destroy() {
    let heap = Heap::new();
    for size in [16, 1000, 100_000] {
        for _ in 0..100 {
            assert!(!heap.alloc(Layout::array::<u8>(size).unwrap()).is_null());
        }
    }
    unsafe { heap.destroy() };
}

#[test]
fn heap_shared_between_threads() {
    let heap = Arc::new(Heap::new());
    let layout = Layout::from_size_align(48, 16).unwrap();
    let threads: Vec<_> = (0..8)
        .map(|t| {
            let heap = heap.clone();
            thread::spawn(move || {
                (0..2000)
                    .map(|_| {
                        let p = heap.alloc(layout);
                        unsafe { p.write_bytes(t, 48) };
                        p as usize
                    })
                    .collect::<Vec<_>>()
            })
        })
        .collect();
    let blocks: Vec<Vec<usize>> = threads.into_iter().map(|t| t.join().unwrap()).collect();
    // free all blocks in other threads than the ones that allocated them
    let frees: Vec<_> = blocks
        .into_iter()
        .enumerate()
        .map(|(t, ps)| {
            let heap = heap.clone();
            thread::spawn(move || {
                for p in ps {
                    let p = p as *mut u8;
                    assert!(heap.contains(p));
                    assert!((0..48).all(|j| unsafe { p.add(j).read() } == t as u8));
                    unsafe { heap.dealloc(p, layout) };
                }
            })
        })
        .collect();
    frees.into_iter().for_each(|t| t.join().unwrap());
}

#[test]
fn heap_stats() {
    let heap = Heap::new();
    let p = heap.alloc(Layout::new::<[u8; 100]>());
    let json = heap.stats_json().expect("stats");
    assert!(json.contains("\"mimalloc_version\""));
    unsafe { heap.dealloc(p, Layout::new::<[u8; 100]>()) };
}
