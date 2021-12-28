/* ----------------------------------------------------------------------------
Copyright (c) 2021 Frank Richter
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#include "static-user.h"

#include "mimalloc.h"

struct HeapWrapper
{
    struct Container
    {
        mi_heap_t *heap;
    };
    Container *container;

    HeapWrapper()
    {
        container = mi_malloc_tp(Container);
        container->heap = mi_heap_new();
    }
    ~HeapWrapper()
    {
        mi_heap_destroy(container->heap);
        mi_free(container);
    }
};

static HeapWrapper static_heap;

void static_user_ref()
{
    auto *p = mi_heap_malloc(static_heap.container->heap, 123);
    mi_free(p);
}
