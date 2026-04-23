/* ----------------------------------------------------------------------------
Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#ifndef _WIN32
#error "test-win-msvc-c-guarded.c is a Windows-only test"
#endif

#include <windows.h>
#include <mimalloc.h>

// Reproduces the Windows ARM64 MSVC plain-C path where the first aligned
// allocation on a fresh thread must not treat an acquire load like a CAS.
static DWORD WINAPI mi_guarded_worker(LPVOID arg) {
  (void)arg;
  void* p = mi_malloc_aligned(0x74, 2);
  if (p == NULL) return 1;
  mi_free(p);
  return 0;
}

int main(void) {
  HANDLE thread = CreateThread(NULL, 0, &mi_guarded_worker, NULL, 0, NULL);
  if (thread == NULL) return 2;

  const DWORD wait = WaitForSingleObject(thread, INFINITE);
  if (wait != WAIT_OBJECT_0) {
    CloseHandle(thread);
    return 3;
  }

  DWORD exit_code = 0;
  if (!GetExitCodeThread(thread, &exit_code)) {
    CloseHandle(thread);
    return 4;
  }

  CloseHandle(thread);
  return (int)exit_code;
}
