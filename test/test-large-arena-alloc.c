// Issue #1142: Test that allocating large amounts of memory (3+ bits in the bitmap)
// uses the preallocated arena when a large enough arena is provided.
#include <stdio.h>
#include <mimalloc.h>

#define ARENA_SIZE (256 * 1024 * 1024)
#define ALLOC_SIZE (64 * 1024 * 1024)

char memory[ARENA_SIZE] = {0};

int main(void) {
  mi_option_set_enabled(mi_option_disallow_os_alloc, true);
  mi_option_set_enabled(mi_option_verbose, true);

  mi_manage_os_memory(
    memory,
    sizeof(memory),
    1 /* committed */,
    0 /* large     */,
    0 /* zero      */,
    -1 /* numa_node */
  );

  void * ptr = mi_malloc(ALLOC_SIZE);

  fprintf(stderr, "ptr = %p\n", ptr);

  mi_option_set_enabled(mi_option_verbose, false);
  return ptr ? 0 : 1;
}