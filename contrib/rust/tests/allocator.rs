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

#[test]
fn excess_allocator_api() {
    use core::alloc::{Allocator, Layout};
    use mimalloc::{good_size, usable_size, MiMallocExcess};
    let a = MiMallocExcess;
    // returned sizes match `good_size` (unless padding is enabled)
    let exact = good_size(0) == 8;
    for (size, align) in [
        (0, 1),
        (100, 8),
        (1000, 8),
        (5000, 16),
        (100, 64),
        (300_000, 4096),
    ] {
        let p = a
            .allocate_zeroed(Layout::from_size_align(size, align).unwrap())
            .unwrap();
        let q = p.cast::<u8>().as_ptr();
        assert!(p.len() >= size && (!exact || p.len() == good_size(size)));
        assert!(p.len() <= unsafe { usable_size(q) });
        assert!((0..p.len()).all(|i| unsafe { q.add(i).read() } == 0));
        assert_eq!(q as usize % align, 0);
        let (old, new) = (
            Layout::from_size_align(p.len(), align).unwrap(),
            Layout::from_size_align(p.len() + 1, align).unwrap(),
        );
        let r = unsafe { a.grow_zeroed(p.cast(), old, new) }.unwrap();
        let q = r.cast::<u8>().as_ptr();
        assert!(r.len() >= new.size() && (!exact || r.len() == good_size(new.size())));
        assert!(r.len() <= unsafe { usable_size(q) });
        assert!((old.size()..r.len()).all(|i| unsafe { q.add(i).read() } == 0));
        unsafe { a.deallocate(r.cast(), Layout::from_size_align(r.len(), align).unwrap()) };
    }
    for size in 0..=mimalloc::sys::MI_SMALL_SIZE_MAX {
        let layout = Layout::from_size_align(size, 1).unwrap();
        let p = a.allocate(layout).unwrap();
        let len = unsafe { usable_size(p.cast::<u8>().as_ptr()) };
        assert!(p.len() >= size && p.len() <= len && (!exact || p.len() == good_size(size)));
        unsafe { a.deallocate(p.cast(), Layout::from_size_align(p.len(), 1).unwrap()) };
    }
    // `Vec` ignores the excess for now
    let mut v = Vec::with_capacity_in(100, a);
    v.extend(0..10_000u32);
    v.truncate(10);
    v.shrink_to_fit();
    assert_eq!(v[9], 9);
}
