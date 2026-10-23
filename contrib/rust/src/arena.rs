/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, OscarTHZhang, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
use crate::sys;
use core::ffi::{c_int, c_void};
use core::mem::MaybeUninit;
use core::ptr::{self, NonNull};

/// Options for [`Arena::reserve_from_os`].
#[derive(Clone, Copy, Debug, Default)]
pub struct ReserveOptions {
    /// Only heaps created with [`Heap::new_in_arena`](crate::Heap::new_in_arena) allocate from
    /// the arena. If not set, any heap may allocate from it, including the global allocator.
    pub exclusive: bool,
    /// Commit all memory up front instead of on first use.
    pub commit: bool,
    /// Use large OS pages (such as 2 MiB pages) if available;
    /// such memory is never returned to the OS.
    pub allow_large_pages: bool,
}

/// Describes memory passed to [`Arena::from_external_memory`].
#[derive(Clone, Copy, Debug, Default)]
pub struct ExternalMemory {
    /// Only heaps created with [`Heap::new_in_arena`](crate::Heap::new_in_arena) allocate from
    /// the arena. If not set, any heap may allocate from it, including the global allocator.
    pub exclusive: bool,
    /// The memory is already committed (readable and writable).
    /// If not, mimalloc commits parts of it with OS calls before use.
    pub committed: bool,
    /// The memory is all zeros.
    pub zeroed: bool,
    /// mimalloc never decommits, resets, or protects any part of the memory.
    pub pinned: bool,
    /// The NUMA node of the memory, if known.
    pub numa_node: Option<u16>,
}

/// Arenas are large memory areas (usually 1GiB+) from which mimalloc allocates memory.
///
/// The arenas are usually allocated on-demand from the OS but can be reserved explicitly.
/// It is also possible to give previously allocated memory to mimalloc to manage. Heaps
/// can be associated with a specific arena to only allocate memory from that arena.
#[derive(Clone, Copy, Debug)]
pub struct Arena {
    // a non-null `sys::mi_arena_id_t` (null means no arena)
    id: NonNull<c_void>,
}

unsafe impl Send for Arena {}
unsafe impl Sync for Arena {}

impl Arena {
    /// Construct an arena by reserving from OS memory.
    pub fn reserve_from_os(size: usize, options: ReserveOptions) -> Option<Arena> {
        let mut id = ptr::null_mut();
        let err = unsafe {
            sys::mi_reserve_os_memory_ex(
                size,
                options.commit,
                options.allow_large_pages,
                options.exclusive,
                &mut id,
            )
        };
        if err != 0 {
            return None;
        }

        NonNull::new(id).map(|id| Arena { id })
    }

    /// Construct an arena from a static memory buffer.
    ///
    /// This function is safe because the static memory buffer lives for the entire
    /// process lifetime and won't be deallocated until the process exits.
    pub fn from_static_memory(
        memory: &'static mut [MaybeUninit<u8>],
        exclusive: bool,
    ) -> Option<Arena> {
        let len = memory.len();
        let memory = NonNull::slice_from_raw_parts(NonNull::from(memory).cast::<u8>(), len);
        let info = ExternalMemory {
            exclusive,
            committed: true,
            zeroed: false,
            pinned: true,
            numa_node: None,
        };
        // Safety: the memory lives for the rest of the process
        unsafe { Self::from_external_memory(memory, info) }
    }

    /// Construct an arena from externally managed OS memory.
    ///
    /// # Safety
    /// - `memory` must have a longer lifetime than the arena object returned by this function.
    /// - `info` must describe the memory correctly. Unless both `info.pinned` and `info.committed`
    ///   are set, the memory must come directly from the OS (ex. `mmap`/`VirtualAlloc`), because
    ///   mimalloc may commit, decommit, or reset parts of it using OS calls.
    pub unsafe fn from_external_memory(
        memory: NonNull<[u8]>,
        info: ExternalMemory,
    ) -> Option<Arena> {
        let mut id = ptr::null_mut();
        let ok = unsafe {
            sys::mi_manage_os_memory_ex(
                memory.cast::<c_void>().as_ptr(),
                memory.len(),
                info.committed,
                info.pinned,
                info.zeroed,
                info.numa_node.map_or(-1, c_int::from),
                info.exclusive,
                &mut id,
            )
        };
        if !ok {
            return None;
        }

        NonNull::new(id).map(|id| Arena { id })
    }

    /// The arena id
    pub fn as_raw(&self) -> sys::mi_arena_id_t {
        self.id.as_ptr()
    }

    /// Is `ptr` inside this arena?
    pub fn contains(&self, ptr: *const u8) -> bool {
        unsafe { sys::mi_arena_contains(self.as_raw(), ptr.cast()) }
    }

    /// The memory area of this arena, including the metadata mimalloc keeps at its start.
    pub fn area(&self) -> NonNull<[u8]> {
        let mut size = 0;
        // Safety: the arena is live with a non-null id so
        // the arena must be valid for mimalloc to query.
        let start = unsafe { sys::mi_arena_area(self.as_raw(), &mut size) };
        // Safety: the area of a live arena is never null.
        let start = unsafe { NonNull::new_unchecked(start.cast::<u8>()) };
        NonNull::slice_from_raw_parts(start, size)
    }

    /// The minimum size of an arena
    pub fn min_size() -> usize {
        sys::mi_arena_min_size()
    }

    /// The alignment of the start of an arena
    pub fn min_alignment() -> usize {
        sys::mi_arena_min_alignment()
    }

    /// The largest block that can be allocated in an arena
    pub fn max_object_size() -> usize {
        sys::mi_arena_max_object_size()
    }
}
