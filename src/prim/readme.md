This is the portability layer where all primitives needed from the OS are defined.

- `prim.h`: API definition
- `prim.c`: Selects one of `prim-unix.c`, `prim-wasi.c`, or `prim-windows.c` depending on the host platform.

Note: still work in progress, there may be other places in the sources that still depend on OS ifdef's.