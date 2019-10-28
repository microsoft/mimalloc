#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <mimalloc.h>
#include <mimalloc-override.h>  // redefines malloc etc.

#include <stdint.h>
#include <stdbool.h>

#define MI_INTPTR_SIZE 8
#define MI_LARGE_WSIZE_MAX (4*1024*1024 / MI_INTPTR_SIZE)

#define MI_BIN_HUGE 100
//#define MI_ALIGN2W

// Bit scan reverse: return the index of the highest bit.
static inline uint8_t mi_bsr32(uint32_t x);

#if defined(_MSC_VER)
#include <windows.h>
#include <intrin.h>
static inline uint8_t mi_bsr32(uint32_t x) {
  uint32_t idx;
  _BitScanReverse((DWORD*)&idx, x);
  return idx;
}
#elif defined(__GNUC__) || defined(__clang__)
static inline uint8_t mi_bsr32(uint32_t x) {
  return (31 - __builtin_clz(x));
}
#else
static inline uint8_t mi_bsr32(uint32_t x) {
  // de Bruijn multiplication, see <http://supertech.csail.mit.edu/papers/debruijn.pdf>
  static const uint8_t debruijn[32] = {
     31,  0, 22,  1, 28, 23, 18,  2, 29, 26, 24, 10, 19,  7,  3, 12,
     30, 21, 27, 17, 25,  9,  6, 11, 20, 16,  8,  5, 15,  4, 14, 13,
  };
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x++;
  return debruijn[(x*0x076be629) >> 27];
}
#endif

// Bit scan reverse: return the index of the highest bit.
uint8_t _mi_bsr(uintptr_t x) {
  if (x == 0) return 0;
  #if MI_INTPTR_SIZE==8
  uint32_t hi = (x >> 32);
  return (hi == 0 ? mi_bsr32((uint32_t)x) : 32 + mi_bsr32(hi));
  #elif MI_INTPTR_SIZE==4
  return mi_bsr32(x);
  #else
  # error "define bsr for non-32 or 64-bit platforms"
  #endif
}

static inline size_t _mi_wsize_from_size(size_t size) {
  return (size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
}

// Return the bin for a given field size.
// Returns MI_BIN_HUGE if the size is too large.
// We use `wsize` for the size in "machine word sizes",
// i.e. byte size == `wsize*sizeof(void*)`.
extern inline uint8_t _mi_bin8(size_t size) {
  size_t wsize = _mi_wsize_from_size(size);
  uint8_t bin;
  if (wsize <= 1) {
    bin = 1;
  }
  #if defined(MI_ALIGN4W)
  else if (wsize <= 4) {
    bin = (uint8_t)((wsize+1)&~1); // round to double word sizes
  }
  #elif defined(MI_ALIGN2W)
  else if (wsize <= 8) {
    bin = (uint8_t)((wsize+1)&~1); // round to double word sizes
  }
  #else
  else if (wsize <= 8) {
    bin = (uint8_t)wsize;
  }
  #endif
  else if (wsize > MI_LARGE_WSIZE_MAX) {
    bin = MI_BIN_HUGE;
  }
  else {
    #if defined(MI_ALIGN4W)
    if (wsize <= 16) { wsize = (wsize+3)&~3; } // round to 4x word sizes
    #endif
    wsize--;
    // find the highest bit
    uint8_t b = mi_bsr32((uint32_t)wsize);
    // and use the top 3 bits to determine the bin (~12.5% worst internal fragmentation).
    // - adjust with 3 because we use do not round the first 8 sizes
    //   which each get an exact bin
    bin = ((b << 2) + (uint8_t)((wsize >> (b - 2)) & 0x03)) - 3;
  }
  return bin;
}

extern inline uint8_t _mi_bin4(size_t size) {
  size_t wsize = _mi_wsize_from_size(size);
  uint8_t bin;
  if (wsize <= 1) {
    bin = 1;
  }
  #if defined(MI_ALIGN4W)
  else if (wsize <= 4) {
    bin = (uint8_t)((wsize+1)&~1); // round to double word sizes
  }
  #elif defined(MI_ALIGN2W)
  else if (wsize <= 8) {
    bin = (uint8_t)((wsize+1)&~1); // round to double word sizes
  }
  #else
  else if (wsize <= 8) {
    bin = (uint8_t)wsize;
  }
  #endif
  else if (wsize > MI_LARGE_WSIZE_MAX) {
    bin = MI_BIN_HUGE;
  }
  else {
    uint8_t b = mi_bsr32((uint32_t)wsize);
    bin = ((b << 1) + (uint8_t)((wsize >> (b - 1)) & 0x01)) + 3;
  }
  return bin;
}

size_t _mi_binx4(size_t bsize) {
  if (bsize==0) return 0;
  uint8_t b = mi_bsr32((uint32_t)bsize);
  if (b <= 1) return bsize;
  size_t bin =  ((b << 1) | (bsize >> (b - 1))&0x01);
  return bin;
}

size_t _mi_binx8(size_t bsize) {
  if (bsize<=1) return bsize;
  uint8_t b = mi_bsr32((uint32_t)bsize);
  if (b <= 2) return bsize;
  size_t bin = ((b << 2) | (bsize >> (b - 2))&0x03) - 5;
  return bin;
}

void mi_bins() {
  //printf("  QNULL(1), /* 0 */ \\\n  ");
  size_t last_bin = 0;
  size_t min_bsize = 0;
  size_t last_bsize = 0;
  for (size_t bsize = 1; bsize < 2*1024; bsize++) {
    size_t size = bsize * 64 * 1024;
    size_t bin = _mi_binx8(bsize);
    if (bin != last_bin) {      
      printf("min bsize: %6zd, max bsize: %6zd, bin: %6zd\n", min_bsize, last_bsize, last_bin);
      //printf("QNULL(%6zd), ", wsize);
      //if (last_bin%8 == 0) printf("/* %i */ \\\n  ", last_bin);
      last_bin = bin;
      min_bsize = bsize;
    }
    last_bsize = bsize;
  }
}

int main() {
  mi_version();
  mi_bins();

  void* p1 = malloc(78);
  void* p2 = malloc(24);
  free(p1);
  p1 = malloc(8);
  //char* s = strdup("hello\n");
  free(p2);
  p2 = malloc(16);
  p1 = realloc(p1, 32);
  free(p1);
  free(p2);
  //free(s);
  //mi_collect(true);

  /* now test if override worked by allocating/freeing across the api's*/
  //p1 = mi_malloc(32);
  //free(p1);
  //p2 = malloc(32);
  //mi_free(p2);  
  mi_collect(true);
  mi_stats_print(NULL);
  return 0;
}

