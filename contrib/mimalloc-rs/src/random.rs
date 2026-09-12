use crate::*;
use crate::mi_rotl32;
use crate::super_function_unit5::_mi_assert_fail;
use std::convert::TryInto;


pub fn qround(x: &mut [u32; 16], a: usize, b: usize, c: usize, d: usize) {
    x[a] = x[a].wrapping_add(x[b]);
    x[d] = mi_rotl32(x[d] ^ x[a], 16);
    x[c] = x[c].wrapping_add(x[d]);
    x[b] = mi_rotl32(x[b] ^ x[c], 12);
    x[a] = x[a].wrapping_add(x[b]);
    x[d] = mi_rotl32(x[d] ^ x[a], 8);
    x[c] = x[c].wrapping_add(x[d]);
    x[b] = mi_rotl32(x[b] ^ x[c], 7);
}
pub fn chacha_block(ctx: &mut crate::mi_random_ctx_t::mi_random_ctx_t) {
    let mut x = [0u32; 16];
    for i in 0..16 {
        x[i] = ctx.input[i];
    }

    for _ in 0..10 {
        crate::qround(&mut x, 0, 4, 8, 12);
        crate::qround(&mut x, 1, 5, 9, 13);
        crate::qround(&mut x, 2, 6, 10, 14);
        crate::qround(&mut x, 3, 7, 11, 15);
        crate::qround(&mut x, 0, 5, 10, 15);
        crate::qround(&mut x, 1, 6, 11, 12);
        crate::qround(&mut x, 2, 7, 8, 13);
        crate::qround(&mut x, 3, 4, 9, 14);
    }

    for i in 0..16 {
        ctx.output[i] = x[i].wrapping_add(ctx.input[i]);
    }

    ctx.output_available = 16;
    ctx.input[12] = ctx.input[12].wrapping_add(1);
    if ctx.input[12] == 0 {
        ctx.input[13] = ctx.input[13].wrapping_add(1);
        if ctx.input[13] == 0 {
            ctx.input[14] = ctx.input[14].wrapping_add(1);
        }
    }
}
pub fn chacha_next32(ctx: &mut crate::mi_random_ctx_t::mi_random_ctx_t) -> u32 {
    if ctx.output_available <= 0 {
        crate::chacha_block(ctx);
        ctx.output_available = 16;
    }
    let index = (16 - ctx.output_available) as usize;
    let x = ctx.output[index];
    ctx.output[index] = 0;
    ctx.output_available -= 1;
    x
}
pub fn mi_random_is_initialized(ctx: Option<&crate::mi_random_ctx_t::mi_random_ctx_t>) -> bool {
    match ctx {
        Some(ctx) => ctx.input[0] != 0,
        None => false,
    }
}
pub fn _mi_random_next(ctx: &mut crate::mi_random_ctx_t::mi_random_ctx_t) -> u64 {
    // Assertion check
    if !mi_random_is_initialized(Some(ctx)) {
        crate::super_function_unit5::_mi_assert_fail(
            "mi_random_is_initialized(ctx)".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/random.c".as_ptr() as *const std::os::raw::c_char,
            140,
            "_mi_random_next\0".as_ptr() as *const std::os::raw::c_char,
        );
    }

    let mut r: u64;
    loop {
        let high = (chacha_next32(ctx) as u64) << 32;
        let low = chacha_next32(ctx) as u64;
        r = high | low;
        if r != 0 {
            break;
        }
    }
    r
}

pub fn read32(p: &[u8], idx32: usize) -> u32 {
    let i = 4 * idx32;
    
    // Ensure we have enough bytes to read
    if i + 3 >= p.len() {
        return 0; // Or handle error appropriately
    }
    
    // Use little-endian conversion for safety and clarity
    u32::from_le_bytes(p[i..i + 4].try_into().unwrap())
}
fn chacha_init(ctx: &mut crate::mi_random_ctx_t::mi_random_ctx_t, key: &[u8; 32], nonce: u64) {
    // Zero out the context
    let ctx_bytes = unsafe {
        std::slice::from_raw_parts_mut(
            ctx as *mut crate::mi_random_ctx_t::mi_random_ctx_t as *mut u8,
            std::mem::size_of::<crate::mi_random_ctx_t::mi_random_ctx_t>()
        )
    };
    _mi_memzero(ctx_bytes, std::mem::size_of::<crate::mi_random_ctx_t::mi_random_ctx_t>());
    
    // Initialize with constant "expand 32-byte k"
    let sigma = b"expand 32-byte k";
    for i in 0..4 {
        ctx.input[i] = read32(sigma, i);
    }
    
    // Add key material
    for i in 0..8 {
        ctx.input[i + 4] = read32(key, i);
    }
    
    // Add nonce
    ctx.input[12] = 0;
    ctx.input[13] = 0;
    ctx.input[14] = nonce as u32;
    ctx.input[15] = (nonce >> 32) as u32;
}
pub fn _mi_os_random_weak(extra_seed: usize) -> usize {
    let mut x = ((&_mi_os_random_weak as *const _ as usize) ^ extra_seed) as u64;
    x ^= _mi_prim_clock_now() as u64;
    let max = ((x ^ (x >> 17)) & 0x0F) + 1;
    
    let mut i = 0;
    while (i < max) || (x == 0) {
        x = _mi_random_shuffle(x);
        i += 1;
        x += 1;  // This matches the C for-loop's x += 1
    }
    
    if x == 0 {
        // Use a fully qualified path to disambiguate
        super_function_unit5::_mi_assert_fail(
            "x != 0\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/random.c\0".as_ptr() as *const std::os::raw::c_char,
            168,
            "_mi_os_random_weak\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    
    x as usize
}
#[derive(Clone)]
pub struct mi_random_ctx_t {
    pub input: [u32; 16],
    pub output: [u32; 16],
    pub output_available: i32,
    pub weak: bool,
}
pub fn _mi_random_init(ctx: &mut mi_random_ctx_t) {
    // Initialize the random context
    ctx.input = [0u32; 16];
    ctx.output = [0u32; 16];
    ctx.output_available = 0;
    ctx.weak = false;
}
pub fn chacha_split(ctx: &crate::mi_random_ctx_t::mi_random_ctx_t, nonce: u64, ctx_new: &mut crate::mi_random_ctx_t::mi_random_ctx_t) {
    // Zero out ctx_new
    let ctx_new_bytes = unsafe {
        std::slice::from_raw_parts_mut(
            ctx_new as *mut _ as *mut u8,
            std::mem::size_of::<crate::mi_random_ctx_t::mi_random_ctx_t>()
        )
    };
    crate::_mi_memzero(ctx_new_bytes, std::mem::size_of::<crate::mi_random_ctx_t::mi_random_ctx_t>());
    
    // Copy input array using _mi_memcpy as in original C code
    let src_bytes = unsafe {
        std::slice::from_raw_parts(
            ctx.input.as_ptr() as *const u8,
            std::mem::size_of::<[u32; 16]>()
        )
    };
    let dst_bytes = unsafe {
        std::slice::from_raw_parts_mut(
            ctx_new.input.as_mut_ptr() as *mut u8,
            std::mem::size_of::<[u32; 16]>()
        )
    };
    crate::_mi_memcpy(dst_bytes, src_bytes, std::mem::size_of::<[u32; 16]>());
    
    // Set specific input values
    ctx_new.input[12] = 0;
    ctx_new.input[13] = 0;
    ctx_new.input[14] = nonce as u32;
    ctx_new.input[15] = (nonce >> 32) as u32;
    
    // Assert condition - match original C logic: call _mi_assert_fail when condition is false
    // Original C: (condition) ? (void)0 : _mi_assert_fail(...)
    // Condition: (ctx->input[14] != ctx_new->input[14]) || (ctx->input[15] != ctx_new->input[15])
    // So we call _mi_assert_fail when: !((ctx.input[14] != ctx_new.input[14]) || (ctx.input[15] != ctx_new.input[15]))
    // Which simplifies to: ctx.input[14] == ctx_new.input[14] && ctx.input[15] == ctx_new.input[15]
    if ctx.input[14] == ctx_new.input[14] && ctx.input[15] == ctx_new.input[15] {
        crate::super_function_unit5::_mi_assert_fail(
            "ctx->input[14] != ctx_new->input[14] || ctx->input[15] != ctx_new->input[15]\0".as_ptr() as *const std::os::raw::c_char,
            "/workdir/C2RustTranslation-main/subjects/mimalloc/src/random.c\0".as_ptr() as *const std::os::raw::c_char,
            118,
            "chacha_split\0".as_ptr() as *const std::os::raw::c_char
        );
    }
    
    // Call chacha_block
    crate::chacha_block(ctx_new);
}
pub fn _mi_random_split(
    ctx: &crate::mi_random_ctx_t::mi_random_ctx_t,
    ctx_new: &mut crate::mi_random_ctx_t::mi_random_ctx_t,
) {
    // Disambiguate _mi_assert_fail by importing from a specific module
    
    if !mi_random_is_initialized(Some(ctx)) {
        _mi_assert_fail(
            b"mi_random_is_initialized(ctx)\0".as_ptr() as *const std::os::raw::c_char,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/random.c\0".as_ptr()
                as *const std::os::raw::c_char,
            134,
            b"_mi_random_split\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    if std::ptr::eq(ctx, ctx_new) {
        _mi_assert_fail(
            b"ctx != ctx_new\0".as_ptr() as *const std::os::raw::c_char,
            b"/workdir/C2RustTranslation-main/subjects/mimalloc/src/random.c\0".as_ptr()
                as *const std::os::raw::c_char,
            135,
            b"_mi_random_split\0".as_ptr() as *const std::os::raw::c_char,
        );
    }
    chacha_split(ctx, ctx_new as *const _ as *const () as u64, ctx_new);
}
pub fn _mi_random_reinit_if_weak(ctx: &mut mi_random_ctx_t) {
    if ctx.weak {
        _mi_random_init(ctx);
    }
}
pub fn _mi_random_init_weak(ctx: &mut mi_random_ctx_t) {
    _mi_random_init(ctx);
    ctx.weak = true;
}
