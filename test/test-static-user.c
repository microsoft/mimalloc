/* ----------------------------------------------------------------------------
Copyright (c) 2018-2020, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"

#include "static-user.h"
#include "testhelper.h"

// ---------------------------------------------------------------------------
// Main testing
// ---------------------------------------------------------------------------
int main(void) {
  mi_option_disable(mi_option_verbose);

  // The real test is whether the test executable crashes at exit
  static_user_ref();

  // ---------------------------------------------------
  // Done
  // ---------------------------------------------------[]
  return print_test_summary();
}
