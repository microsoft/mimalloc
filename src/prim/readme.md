## Portability Primitives

This is the portability layer where all primitives needed from the OS are defined.

- `prim.h`: API definition
- `prim.c`: Selects one of `unix/prim.c`, `wasi/prim.c`, or `windows/prim.c` depending on the host platform.

Note: still work in progress, there may still be places in the sources that still depend on OS ifdef's.