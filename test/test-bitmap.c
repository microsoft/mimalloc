#include "../src/bitmap-chunk.h"
#include "testhelper.h"

static bool test_fields(void) {
  for (size_t n = 1; n <= MI_BFIELD_BITS; n++) {
    for (size_t start = 0; start + n <= MI_BFIELD_BITS; start++) {
      mi_bfield_t field = mi_bfield_mask(n, start);
      size_t idx;
      if (mi_bfield_popcount(field) != n ||
          !mi_bfield_find_least_bit(field, &idx) || idx != start ||
          !mi_bfield_find_highest_bit(field, &idx) || idx != start + n - 1) return false;
      for (size_t i = 0; i < n; i++) {
        if (!mi_bfield_foreach_bit(&field, &idx) || idx != start + i) return false;
      }
      if (field != 0 || mi_bfield_foreach_bit(&field, &idx)) return false;
    }
  }
  return true;
}

static bool test_chunk_ranges(void) {
  mi_bchunk_t chunk;
  for (size_t n = 1; n <= MI_BCHUNK_BITS; n++) {
    const size_t starts[] = { 0, MI_BFIELD_BITS - 1, MI_BFIELD_BITS, MI_BCHUNK_BITS - n };
    for (size_t i = 0; i < sizeof(starts)/sizeof(starts[0]); i++) {
      const size_t start = starts[i];
      if (start + n > MI_BCHUNK_BITS) continue;
      for (size_t j = 0; j < MI_BCHUNK_FIELDS; j++) {
        mi_atomic_store_relaxed(&chunk.bfields[j], 0);
      }
      size_t already_set;
      if (!mi_bchunk_setN(&chunk, start, n, &already_set) || already_set != 0) return false;
      if (mi_bchunk_setN(&chunk, start, n, &already_set) || already_set != n) return false;
      if (!mi_bchunk_is_xsetN(MI_BIT_SET, &chunk, start, n) ||
          mi_bchunk_popcountN(&chunk, start, n) != n ||
          mi_bchunk_popcount(&chunk) != n) return false;
      size_t idx;
      if (!mi_bchunk_bsr(&chunk, &idx) || idx != start + n - 1) return false;
      bool all_clear = false;
      bool temporary = false;
      if (!mi_bchunk_try_clearN(&chunk, start, n, &all_clear, &temporary) ||
          !all_clear || temporary || !mi_bchunk_all_are_clear_relaxed(&chunk)) return false;
      if (mi_bchunk_try_clearN(&chunk, start, n, &all_clear, &temporary)) return false;

      mi_bchunk_setN(&chunk, start, n, NULL);
      if (n > 1) {
        mi_bchunk_clear(&chunk, start + n - 1, NULL);
        temporary = false;
        if (mi_bchunk_try_clearN(&chunk, start, n, &all_clear, &temporary) ||
            mi_bchunk_popcount(&chunk) != n - 1 ||
            !mi_bchunk_is_xsetN(MI_BIT_SET, &chunk, start, n - 1)) return false;
      }
      mi_bchunk_clearN(&chunk, start, n, &all_clear);
      if (!all_clear || !mi_bchunk_all_are_clear_relaxed(&chunk)) return false;
    }
  }
  return true;
}

static bool test_chunk_find(void) {
  mi_bchunk_t chunk;
  for (size_t i = 0; i < MI_BCHUNK_FIELDS; i++) {
    mi_atomic_store_relaxed(&chunk.bfields[i], 0);
  }
  mi_bchunk_setN(&chunk, 0, MI_BCHUNK_BITS, NULL);
  if (!mi_bchunk_all_are_set_relaxed(&chunk)) return false;
  for (size_t i = 0; i < MI_BCHUNK_BITS; i++) {
    size_t idx;
    if (!mi_bchunk_try_find_and_clear(&chunk, &idx) || idx != i) return false;
  }
  size_t idx;
  if (mi_bchunk_try_find_and_clear(&chunk, &idx)) return false;

  mi_bchunk_setN(&chunk, 0, MI_BCHUNK_BITS, NULL);
  for (size_t i = 0; i < MI_BCHUNK_BITS; i += 8) {
    bool temporary = false;
    if (!mi_bchunk_try_find_and_clear_8(&chunk, 8, &idx, &temporary) || idx != i || temporary) return false;
  }
  for (size_t n = 1; n <= MI_BCHUNK_BITS; n++) {
    const size_t start = (n < MI_BFIELD_BITS ? MI_BFIELD_BITS - 1 : MI_BCHUNK_BITS - n);
    mi_bchunk_setN(&chunk, start, n, NULL);
    bool temporary = false;
    const bool found = (n <= MI_BFIELD_BITS
        ? mi_bchunk_try_find_and_clearNX(&chunk, n, &idx, &temporary)
        : mi_bchunk_try_find_and_clearNC(&chunk, n, &idx, &temporary));
    if (!found || idx != start || temporary || !mi_bchunk_all_are_clear_relaxed(&chunk)) return false;
  }
  return true;
}

static bool test_bitmap_ranges(void) {
  const size_t bits = 3 * MI_BCHUNK_BITS;
  mi_bitmap_t* bitmap = (mi_bitmap_t*)mi_malloc_aligned(mi_bitmap_size(bits, NULL), MI_BCHUNK_SIZE);
  if (bitmap == NULL) return false;
  mi_bitmap_init(bitmap, bits, false);
  const size_t start = MI_BCHUNK_BITS - 1;
  const size_t count = MI_BCHUNK_BITS + 2;
  size_t already_set;
  size_t idx;
  bool result = mi_bitmap_setN(bitmap, start, count, &already_set) && already_set == 0;
  result = result && mi_bitmap_popcount(bitmap) == count &&
           mi_bitmap_popcountN(bitmap, start, count) == count &&
           mi_bitmap_is_setN(bitmap, start, count) &&
           mi_bitmap_bsr(bitmap, &idx) && idx == start + count - 1;
  result = result && mi_bitmap_clearN(bitmap, start, count) && mi_bitmap_is_all_clear(bitmap);
  mi_bitmap_unsafe_setN(bitmap, 0, bits);
  result = result && mi_bitmap_popcount(bitmap) == bits;
  mi_free(bitmap);
  return result;
}

static bool test_bbitmap_find(void) {
  const size_t bits = 3 * MI_BCHUNK_BITS;
  mi_bbitmap_t* bitmap = (mi_bbitmap_t*)mi_malloc_aligned(mi_bbitmap_size(bits, NULL), MI_BCHUNK_SIZE);
  if (bitmap == NULL) return false;
  const size_t sizes[] = { 1, 8, MI_BFIELD_BITS - 1, MI_BFIELD_BITS, MI_BFIELD_BITS + 1, MI_BCHUNK_BITS, MI_BCHUNK_BITS + 1 };
  bool result = true;
  for (size_t i = 0; result && i < sizeof(sizes)/sizeof(sizes[0]); i++) {
    mi_bbitmap_init(_mi_subproc(), bitmap, bits, false);
    mi_bbitmap_unsafe_setN(bitmap, 0, bits);
    size_t idx;
    size_t highest;
    const size_t n = sizes[i];
    result = mi_bbitmap_try_find_and_clearN(bitmap, 0, n, &idx) && idx == 0 &&
             mi_bbitmap_is_xsetN(MI_BIT_CLEAR, bitmap, idx, n) &&
             mi_bbitmap_bsr_inv(bitmap, &highest) && highest == n - 1;
    result = result && mi_bbitmap_setN(bitmap, idx, n) &&
             mi_bbitmap_is_xsetN(MI_BIT_SET, bitmap, 0, bits);
  }
  mi_free(bitmap);
  return result;
}

int main(void) {
  CHECK("bitmap-fields", test_fields());
  CHECK("bitmap-chunk-ranges", test_chunk_ranges());
  CHECK("bitmap-chunk-find", test_chunk_find());
  CHECK("bitmap-ranges", test_bitmap_ranges());
  CHECK("bitmap-binned-find", test_bbitmap_find());
  return print_test_summary();
}
