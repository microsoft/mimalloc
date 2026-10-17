#![cfg(any(feature = "nightly", allocator_api_stable))]
#![cfg_attr(feature = "nightly", feature(allocator_api), allow(stable_features))]

use mimalloc::{Heap, MiMalloc};

#[repr(align(64))]
#[derive(Clone, Copy, Debug, PartialEq)]
struct A64(u32);

#[test]
fn global_allocator_api() {
    let mut v: Vec<A64, MiMalloc> = Vec::new_in(MiMalloc);
    for i in 0..10_000 {
        v.push(A64(i));
    }
    assert!(v.iter().all(|x| (x as *const A64 as usize) % 64 == 0));
    v.truncate(10);
    v.shrink_to_fit();
    assert_eq!(v[9], A64(9));
    let b = Box::new_in([1u8; 3], MiMalloc);
    assert_eq!(*b, [1, 1, 1]);
}

#[test]
fn heap_allocator_api() {
    let heap = Heap::new();
    let mut v = Vec::new_in(&heap);
    for i in 0..100_000u64 {
        v.push(i);
    }
    assert!(heap.contains(v.as_ptr().cast()));
    assert_eq!(v.iter().sum::<u64>(), 99_999 * 100_000 / 2);
    let mut w: Vec<A64, &Heap> = Vec::with_capacity_in(1, &heap);
    w.extend((0..1000).map(A64));
    assert!(heap.contains(w.as_ptr().cast()));
    assert_eq!(w.as_ptr() as usize % 64, 0);
}

#[test]
fn grow_zeroed() {
    use core::alloc::{Allocator, Layout};
    let heap = Heap::new();
    for (old, new, align) in [
        (8, 1000, 8),
        (24, 64, 16),
        (100, 100_000, 64),
        (1000, 5000, 4096),
    ] {
        let (old_layout, new_layout) = (
            Layout::from_size_align(old, align).unwrap(),
            Layout::from_size_align(new, align).unwrap(),
        );
        for a in [&MiMalloc as &dyn Allocator, &&heap] {
            let p = a.allocate_zeroed(old_layout).unwrap().cast::<u8>();
            unsafe {
                assert!((0..old).all(|i| p.as_ptr().add(i).read() == 0));
                p.as_ptr().write_bytes(0xAB, old);
                let q = a.grow_zeroed(p, old_layout, new_layout).unwrap();
                assert_eq!(q.len(), new);
                let q = q.cast::<u8>().as_ptr();
                assert_eq!(q as usize % align, 0);
                assert!((0..old).all(|i| q.add(i).read() == 0xAB));
                assert!((old..new).all(|i| q.add(i).read() == 0));
                a.deallocate(core::ptr::NonNull::new_unchecked(q), new_layout);
            }
        }
    }
}
