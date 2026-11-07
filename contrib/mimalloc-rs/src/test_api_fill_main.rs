use std::env;
use std::ffi::c_void;
use translate_new::*;
pub fn main() {
    // Disable verbose option
    mi_option_disable(crate::mi_option_t::MiOption::Verbose);
    
    // Test 1: zeroinit-zalloc-small
    eprint!("test: {}...  ", "zeroinit-zalloc-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = unsafe { mi_zalloc(zalloc_size) };
            let p_slice = if p.is_null() {
                Option::None
            } else {
                Some(unsafe { std::slice::from_raw_parts(p as *const u8, zalloc_size) })
            };
            result = check_zero_init(p_slice, zalloc_size);
            mi_free(Some(p));
            done = check_result(result, "zeroinit-zalloc-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 30);
        }
    }
    
    // Test 2: zeroinit-zalloc-large
    eprint!("test: {}...  ", "zeroinit-zalloc-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = unsafe { mi_zalloc(zalloc_size) };
            let p_slice = if p.is_null() {
                Option::None
            } else {
                Some(unsafe { std::slice::from_raw_parts(p as *const u8, zalloc_size) })
            };
            result = check_zero_init(p_slice, zalloc_size);
            mi_free(Some(p));
            done = check_result(result, "zeroinit-zalloc-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 36);
        }
    }
    
    // Test 3: zeroinit-zalloc_small
    eprint!("test: {}...  ", "zeroinit-zalloc_small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_zalloc_small(zalloc_size);
            let p_slice = p.as_ref().map(|ptr| unsafe { std::slice::from_raw_parts(*ptr as *const c_void as *const u8, zalloc_size) });
            result = check_zero_init(p_slice, zalloc_size);
            mi_free(p.map(|ptr| ptr as *mut c_void));
            done = check_result(result, "zeroinit-zalloc_small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 42);
        }
    }
    
    // Test 4: zeroinit-calloc-small
    eprint!("test: {}...  ", "zeroinit-calloc-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let calloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = unsafe { mi_calloc(calloc_size, 1) };
            let p_slice = if p.is_null() {
                Option::None
            } else {
                Some(unsafe { std::slice::from_raw_parts(p as *const u8, calloc_size) })
            };
            result = check_zero_init(p_slice, calloc_size);
            mi_free(Some(p));
            done = check_result(result, "zeroinit-calloc-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 49);
        }
    }
    
    // Test 5: zeroinit-calloc-large
    eprint!("test: {}...  ", "zeroinit-calloc-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let calloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = unsafe { mi_calloc(calloc_size, 1) };
            let p_slice = if p.is_null() {
                Option::None
            } else {
                Some(unsafe { std::slice::from_raw_parts(p as *const u8, calloc_size) })
            };
            result = check_zero_init(p_slice, calloc_size);
            mi_free(Some(p));
            done = check_result(result, "zeroinit-calloc-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 55);
        }
    }
    
    // Test 6: zeroinit-rezalloc-small
    eprint!("test: {}...  ", "zeroinit-rezalloc-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let mut p = unsafe { mi_zalloc(zalloc_size) };
            result = check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, zalloc_size) }) },
                zalloc_size
            );
            zalloc_size *= 3;
            let p_as_void = if p.is_null() { Option::None } else { Some(p as *mut c_void) };
            let p2 = mi_rezalloc(p_as_void.and_then(|ptr| Some(unsafe { &mut *ptr })), zalloc_size);
            if let Some(p2_ptr) = p2 {
                p = p2_ptr;
            }
            result &= check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, zalloc_size) }) },
                zalloc_size
            );
            mi_free(Some(p));
            done = check_result(result, "zeroinit-rezalloc-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 62);
        }
    }
    
    // Test 7: zeroinit-rezalloc-large
    eprint!("test: {}...  ", "zeroinit-rezalloc-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let mut p = unsafe { mi_zalloc(zalloc_size) };
            result = check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, zalloc_size) }) },
                zalloc_size
            );
            zalloc_size *= 3;
            let p_as_void = if p.is_null() { Option::None } else { Some(p as *mut c_void) };
            let p2 = mi_rezalloc(p_as_void.and_then(|ptr| Some(unsafe { &mut *ptr })), zalloc_size);
            if let Some(p2_ptr) = p2 {
                p = p2_ptr;
            }
            result &= check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, zalloc_size) }) },
                zalloc_size
            );
            mi_free(Some(p));
            done = check_result(result, "zeroinit-rezalloc-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 71);
        }
    }
    
    // Test 8: zeroinit-recalloc-small
    eprint!("test: {}...  ", "zeroinit-recalloc-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut calloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let mut p = unsafe { mi_calloc(calloc_size, 1) };
            result = check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, calloc_size) }) },
                calloc_size
            );
            calloc_size *= 3;
            let p_as_void = if p.is_null() { Option::None } else { Some(p as *mut c_void) };
            let p2 = mi_recalloc(p_as_void.and_then(|ptr| Some(unsafe { &mut *ptr })), calloc_size, 1);
            if let Some(p2_ptr) = p2 {
                p = p2_ptr;
            }
            result &= check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, calloc_size) }) },
                calloc_size
            );
            mi_free(Some(p));
            done = check_result(result, "zeroinit-recalloc-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 81);
        }
    }
    
    // Test 9: zeroinit-recalloc-large
    eprint!("test: {}...  ", "zeroinit-recalloc-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut calloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let mut p = unsafe { mi_calloc(calloc_size, 1) };
            result = check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, calloc_size) }) },
                calloc_size
            );
            calloc_size *= 3;
            let p_as_void = if p.is_null() { Option::None } else { Some(p as *mut c_void) };
            let p2 = mi_recalloc(p_as_void.and_then(|ptr| Some(unsafe { &mut *ptr })), calloc_size, 1);
            if let Some(p2_ptr) = p2 {
                p = p2_ptr;
            }
            result &= check_zero_init(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, calloc_size) }) },
                calloc_size
            );
            mi_free(Some(p));
            done = check_result(result, "zeroinit-recalloc-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 90);
        }
    }
    
    // Test 10: zeroinit-zalloc_aligned-small
    eprint!("test: {}...  ", "zeroinit-zalloc_aligned-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_zalloc_aligned(zalloc_size, 16 * 2);
            result = check_zero_init(p.as_deref(), zalloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-zalloc_aligned-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 103);
        }
    }
    
    // Test 11: zeroinit-zalloc_aligned-large
    eprint!("test: {}...  ", "zeroinit-zalloc_aligned-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = mi_zalloc_aligned(zalloc_size, 16 * 2);
            result = check_zero_init(p.as_deref(), zalloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-zalloc_aligned-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 109);
        }
    }
    
    // Test 12: zeroinit-calloc_aligned-small
    eprint!("test: {}...  ", "zeroinit-calloc_aligned-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let calloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_calloc_aligned(calloc_size, 1, 16 * 2);
            result = check_zero_init(p.as_deref(), calloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-calloc_aligned-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 116);
        }
    }
    
    // Test 13: zeroinit-calloc_aligned-large
    eprint!("test: {}...  ", "zeroinit-calloc_aligned-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let calloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = mi_calloc_aligned(calloc_size, 1, 16 * 2);
            result = check_zero_init(p.as_deref(), calloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-calloc_aligned-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 122);
        }
    }
    
    // Test 14: zeroinit-rezalloc_aligned-small
    eprint!("test: {}...  ", "zeroinit-rezalloc_aligned-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let mut p = mi_zalloc_aligned(zalloc_size, 16 * 2);
            result = check_zero_init(p.as_deref(), zalloc_size);
            zalloc_size *= 3;
            let p2 = mi_rezalloc_aligned(p, zalloc_size, 16 * 2);
            p = p2;
            result &= check_zero_init(p.as_deref(), zalloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-rezalloc_aligned-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 129);
        }
    }
    
    // Test 15: zeroinit-rezalloc_aligned-large
    eprint!("test: {}...  ", "zeroinit-rezalloc_aligned-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut zalloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let mut p = mi_zalloc_aligned(zalloc_size, 16 * 2);
            result = check_zero_init(p.as_deref(), zalloc_size);
            zalloc_size *= 3;
            let p2 = mi_rezalloc_aligned(p, zalloc_size, 16 * 2);
            p = p2;
            result &= check_zero_init(p.as_deref(), zalloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-rezalloc_aligned-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 138);
        }
    }
    
    // Test 16: zeroinit-recalloc_aligned-small
    eprint!("test: {}...  ", "zeroinit-recalloc_aligned-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut calloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let mut p = mi_calloc_aligned(calloc_size, 1, 16 * 2);
            result = check_zero_init(p.as_deref(), calloc_size);
            calloc_size *= 3;
            let p2 = mi_recalloc_aligned(p, calloc_size, 1, 16 * 2);
            p = p2;
            result &= check_zero_init(p.as_deref(), calloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-recalloc_aligned-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 148);
        }
    }
    
    // Test 17: zeroinit-recalloc_aligned-large
    eprint!("test: {}...  ", "zeroinit-recalloc_aligned-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut calloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let mut p = mi_calloc_aligned(calloc_size, 1, 16 * 2);
            result = check_zero_init(p.as_deref(), calloc_size);
            calloc_size *= 3;
            let p2 = mi_recalloc_aligned(p, calloc_size, 1, 16 * 2);
            p = p2;
            result &= check_zero_init(p.as_deref(), calloc_size);
            mi_free(p.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "zeroinit-recalloc_aligned-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 157);
        }
    }
    
    // Test 18: uninit-malloc-small
    eprint!("test: {}...  ", "uninit-malloc-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = unsafe { mi_malloc(malloc_size) };
            let p_slice = if p.is_null() {
                Option::None
            } else {
                Some(unsafe { std::slice::from_raw_parts(p as *const u8, malloc_size) })
            };
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(Some(p));
            done = check_result(result, "uninit-malloc-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 171);
        }
    }
    
    // Test 19: uninit-malloc-large
    eprint!("test: {}...  ", "uninit-malloc-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = unsafe { mi_malloc(malloc_size) };
            let p_slice = if p.is_null() {
                Option::None
            } else {
                Some(unsafe { std::slice::from_raw_parts(p as *const u8, malloc_size) })
            };
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(Some(p));
            done = check_result(result, "uninit-malloc-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 177);
        }
    }
    
    // Test 20: uninit-malloc_small
    eprint!("test: {}...  ", "uninit-malloc_small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_malloc_small(malloc_size);
            let p_slice = p.as_ref().map(|ptr| unsafe { std::slice::from_raw_parts(*ptr as *const c_void as *const u8, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(p.map(|ptr| ptr as *mut c_void));
            done = check_result(result, "uninit-malloc_small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 184);
        }
    }
    
    // Test 21: uninit-realloc-small
    eprint!("test: {}...  ", "uninit-realloc-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let mut p = unsafe { mi_malloc(malloc_size) };
            result = check_debug_fill_uninit(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, malloc_size) }) },
                malloc_size
            );
            malloc_size *= 3;
            let p_as_void = if p.is_null() { Option::None } else { Some(p as *mut c_void) };
            let p2 = mi_realloc(p_as_void, malloc_size);
            if let Some(p2_ptr) = p2 {
                p = p2_ptr;
            }
            result &= check_debug_fill_uninit(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, malloc_size) }) },
                malloc_size
            );
            mi_free(Some(p));
            done = check_result(result, "uninit-realloc-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 191);
        }
    }
    
    // Test 22: uninit-realloc-large
    eprint!("test: {}...  ", "uninit-realloc-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let mut p = unsafe { mi_malloc(malloc_size) };
            result = check_debug_fill_uninit(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, malloc_size) }) },
                malloc_size
            );
            malloc_size *= 3;
            let p_as_void = if p.is_null() { Option::None } else { Some(p as *mut c_void) };
            let p2 = mi_realloc(p_as_void, malloc_size);
            if let Some(p2_ptr) = p2 {
                p = p2_ptr;
            }
            result &= check_debug_fill_uninit(
                if p.is_null() { Option::None } else { Some(unsafe { std::slice::from_raw_parts(p as *const u8, malloc_size) }) },
                malloc_size
            );
            mi_free(Some(p));
            done = check_result(result, "uninit-realloc-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 200);
        }
    }
    
    // Test 23: uninit-mallocn-small
    eprint!("test: {}...  ", "uninit-mallocn-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_mallocn(malloc_size, 1);
            let p_slice = p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr as *const u8, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(p);
            done = check_result(result, "uninit-mallocn-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 210);
        }
    }
    
    // Test 24: uninit-mallocn-large
    eprint!("test: {}...  ", "uninit-mallocn-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = mi_mallocn(malloc_size, 1);
            let p_slice = p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr as *const u8, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(p);
            done = check_result(result, "uninit-mallocn-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 216);
        }
    }
    
    // Test 25: uninit-reallocn-small
    eprint!("test: {}...  ", "uninit-reallocn-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let mut p = mi_mallocn(malloc_size, 1);
            result = check_debug_fill_uninit(
                p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr as *const u8, malloc_size) }),
                malloc_size
            );
            malloc_size *= 3;
            let p2 = mi_reallocn(p, malloc_size, 1);
            p = p2;
            result &= check_debug_fill_uninit(
                p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr as *const u8, malloc_size) }),
                malloc_size
            );
            mi_free(p);
            done = check_result(result, "uninit-reallocn-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 223);
        }
    }
    
    // Test 26: uninit-reallocn-large
    eprint!("test: {}...  ", "uninit-reallocn-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let mut p = mi_mallocn(malloc_size, 1);
            result = check_debug_fill_uninit(
                p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr as *const u8, malloc_size) }),
                malloc_size
            );
            malloc_size *= 3;
            let p2 = mi_reallocn(p, malloc_size, 1);
            p = p2;
            result &= check_debug_fill_uninit(
                p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr as *const u8, malloc_size) }),
                malloc_size
            );
            mi_free(p);
            done = check_result(result, "uninit-reallocn-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 232);
        }
    }
    
    // Test 27: uninit-malloc_aligned-small
    eprint!("test: {}...  ", "uninit-malloc_aligned-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_malloc_aligned(malloc_size, 16 * 2);
            let p_slice = p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(p.map(|ptr| ptr as *mut c_void));
            done = check_result(result, "uninit-malloc_aligned-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 242);
        }
    }
    
    // Test 28: uninit-malloc_aligned-large
    eprint!("test: {}...  ", "uninit-malloc_aligned-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = mi_malloc_aligned(malloc_size, 16 * 2);
            let p_slice = p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            mi_free(p.map(|ptr| ptr as *mut c_void));
            done = check_result(result, "uninit-malloc_aligned-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 248);
        }
    }
    
    // Test 29: uninit-realloc_aligned-small
    eprint!("test: {}...  ", "uninit-realloc_aligned-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = mi_malloc_aligned(malloc_size, 16 * 2);
            let p_slice = p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            malloc_size *= 3;
            let p_slice_ref = p.map(|ptr| unsafe { std::slice::from_raw_parts_mut(ptr, malloc_size / 3) });
            let p2 = mi_realloc_aligned(p_slice_ref, malloc_size, 16 * 2);
            result &= check_debug_fill_uninit(p2.as_deref(), malloc_size);
            mi_free(p2.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "uninit-realloc_aligned-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 255);
        }
    }
    
    // Test 30: uninit-realloc_aligned-large
    eprint!("test: {}...  ", "uninit-realloc_aligned-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let mut malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = mi_malloc_aligned(malloc_size, 16 * 2);
            let p_slice = p.map(|ptr| unsafe { std::slice::from_raw_parts(ptr, malloc_size) });
            result = check_debug_fill_uninit(p_slice, malloc_size);
            malloc_size *= 3;
            let p_slice_ref = p.map(|ptr| unsafe { std::slice::from_raw_parts_mut(ptr, malloc_size / 3) });
            let p2 = mi_realloc_aligned(p_slice_ref, malloc_size, 16 * 2);
            result &= check_debug_fill_uninit(p2.as_deref(), malloc_size);
            mi_free(p2.map(|slice| slice.as_mut_ptr() as *mut c_void));
            done = check_result(result, "uninit-realloc_aligned-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 264);
        }
    }
    
    // Test 31: fill-freed-small
    eprint!("test: {}...  ", "fill-freed-small");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) / 2;
            let p = unsafe { mi_malloc(malloc_size) };
            mi_free(Some(p));
            let freed_ptr = if p.is_null() {
                std::ptr::null()
            } else {
                unsafe { p.add(std::mem::size_of::<*mut c_void>()) as *const u8 }
            };
            result = check_debug_fill_freed(freed_ptr, malloc_size - std::mem::size_of::<*mut c_void>());
            done = check_result(result, "fill-freed-small", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 275);
        }
    }
    
    // Test 32: fill-freed-large
    eprint!("test: {}...  ", "fill-freed-large");
    {
        let mut done = false;
        let mut result = true;
        while !done {
            let malloc_size = (128 * std::mem::size_of::<*mut c_void>()) * 2;
            let p = unsafe { mi_malloc(malloc_size) };
            mi_free(Some(p));
            let freed_ptr = if p.is_null() {
                std::ptr::null()
            } else {
                unsafe { p.add(std::mem::size_of::<*mut c_void>()) as *const u8 }
            };
            result = check_debug_fill_freed(freed_ptr, malloc_size - std::mem::size_of::<*mut c_void>());
            done = check_result(result, "fill-freed-large", "/workdir/C2RustTranslation-main/subjects/mimalloc/test/test-api-fill.c", 282);
        }
    }
    
    // Return test summary
    let _ = print_test_summary();
}
