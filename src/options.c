/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "mimalloc.h"
#include "mimalloc-internal.h"
#include "mimalloc-atomic.h"

#include <stdio.h>
#include <stdlib.h> // strtol
#include <string.h> // strncpy, strncat, strlen, strstr
#include <ctype.h>  // toupper
#include <stdarg.h>

int mi_version(void) mi_attr_noexcept {
  return MI_MALLOC_VERSION;
}

#ifdef _WIN32
#include <conio.h>
#endif

// --------------------------------------------------------
// Options
// These can be accessed by multiple threads and may be 
// concurrently initialized, but an initializing data race
// is ok since they resolve to the same value.
// --------------------------------------------------------
typedef enum mi_init_e {
  UNINIT,       // not yet initialized
  DEFAULTED,    // not found in the environment, use default value
  INITIALIZED   // found in environment or set explicitly
} mi_init_t;

typedef struct mi_option_desc_s {
  long        value;  // the value
  mi_init_t   init;   // is it initialized yet? (from the environment)
  mi_option_t option; // for debugging: the option index should match the option
  const char* name;   // option name without `mimalloc_` prefix
} mi_option_desc_t;

#define MI_OPTION(opt)        mi_option_##opt, #opt
#define MI_OPTION_DESC(opt)   {0, UNINIT, MI_OPTION(opt) }

static mi_option_desc_t options[_mi_option_last] =
{
  // stable options
  { MI_DEBUG, UNINIT, MI_OPTION(show_errors) },
  { 0, UNINIT, MI_OPTION(show_stats) },
  { 0, UNINIT, MI_OPTION(verbose) },

  // the following options are experimental and not all combinations make sense.
  { 1, UNINIT, MI_OPTION(eager_commit) },        // note: needs to be on when eager_region_commit is enabled
  #ifdef _WIN32   // and BSD?
  { 0, UNINIT, MI_OPTION(eager_region_commit) }, // don't commit too eagerly on windows (just for looks...)
  #else
  { 1, UNINIT, MI_OPTION(eager_region_commit) },
  #endif
  { 0, UNINIT, MI_OPTION(large_os_pages) },      // use large OS pages, use only with eager commit to prevent fragmentation of VMA's
  { 0, UNINIT, MI_OPTION(reserve_huge_os_pages) },
  { 0, UNINIT, MI_OPTION(segment_cache) },       // cache N segments per thread
  { 0, UNINIT, MI_OPTION(page_reset) },
  { 0, UNINIT, MI_OPTION(cache_reset) },
  { 0, UNINIT, MI_OPTION(reset_decommits) },     // note: cannot enable this if secure is on
  { 0, UNINIT, MI_OPTION(eager_commit_delay) },  // the first N segments per thread are not eagerly committed
  { 0, UNINIT, MI_OPTION(segment_reset) },       // reset segment memory on free
  { 100, UNINIT, MI_OPTION(os_tag) }             // only apple specific for now but might serve more or less related purpose
};

static void mi_option_init(mi_option_desc_t* desc);

void _mi_options_init(void) {
  // called on process load
  for(int i = 0; i < _mi_option_last; i++ ) {
    mi_option_t option = (mi_option_t)i;
    mi_option_get(option); // initialize
    if (option != mi_option_verbose) {
      mi_option_desc_t* desc = &options[option];
      _mi_verbose_message("option '%s': %ld\n", desc->name, desc->value);
    }
  }
}

long mi_option_get(mi_option_t option) {
  mi_assert(option >= 0 && option < _mi_option_last);
  mi_option_desc_t* desc = &options[option];
  mi_assert(desc->option == option);  // index should match the option
  if (mi_unlikely(desc->init == UNINIT)) {
    mi_option_init(desc);    
  }
  return desc->value;
}

void mi_option_set(mi_option_t option, long value) {
  mi_assert(option >= 0 && option < _mi_option_last);
  mi_option_desc_t* desc = &options[option];
  mi_assert(desc->option == option);  // index should match the option
  desc->value = value;
  desc->init = INITIALIZED;
}

void mi_option_set_default(mi_option_t option, long value) {
  mi_assert(option >= 0 && option < _mi_option_last);
  mi_option_desc_t* desc = &options[option];
  if (desc->init != INITIALIZED) {
    desc->value = value;
  }
}

bool mi_option_is_enabled(mi_option_t option) {
  return (mi_option_get(option) != 0);
}

void mi_option_set_enabled(mi_option_t option, bool enable) {
  mi_option_set(option, (enable ? 1 : 0));
}

void mi_option_set_enabled_default(mi_option_t option, bool enable) {
  mi_option_set_default(option, (enable ? 1 : 0));
}

void mi_option_enable(mi_option_t option) {
  mi_option_set_enabled(option,true);
}

void mi_option_disable(mi_option_t option) {
  mi_option_set_enabled(option,false);
}


static void mi_out_stderr(const char* msg) {
  #ifdef _WIN32
  // on windows with redirection, the C runtime cannot handle locale dependent output 
  // after the main thread closes so we use direct console output.
  _cputs(msg);
  #else
  fputs(msg, stderr);
  #endif
}

// --------------------------------------------------------
// Default output handler
// --------------------------------------------------------

// Should be atomic but gives errors on many platforms as generally we cannot cast a function pointer to a uintptr_t.
// For now, don't register output from multiple threads.
#pragma warning(suppress:4180)
static mi_output_fun* volatile mi_out_default; // = NULL

static mi_output_fun* mi_out_get_default(void) {
  mi_output_fun* out = mi_out_default;
  return (out == NULL ? &mi_out_stderr : out);
}

void mi_register_output(mi_output_fun* out) mi_attr_noexcept {
  mi_out_default = out;
}


// --------------------------------------------------------
// Messages
// --------------------------------------------------------
#define MAX_ERROR_COUNT (10)
static volatile _Atomic(uintptr_t) error_count; // = 0;  // when MAX_ERROR_COUNT stop emitting errors and warnings

// When overriding malloc, we may recurse into mi_vfprintf if an allocation
// inside the C runtime causes another message.
static mi_decl_thread bool recurse = false;

void _mi_fputs(mi_output_fun* out, const char* prefix, const char* message) {
  if (_mi_preloading() || recurse) return;
  if (out==NULL || (FILE*)out==stdout || (FILE*)out==stderr) out = mi_out_get_default();
  recurse = true;
  if (prefix != NULL) out(prefix);
  out(message);
  recurse = false;
  return;
}

// Define our own limited `fprintf` that avoids memory allocation.
// We do this using `snprintf` with a limited buffer.
static void mi_vfprintf( mi_output_fun* out, const char* prefix, const char* fmt, va_list args ) {
  char buf[512];
  if (fmt==NULL) return;
  if (_mi_preloading() || recurse) return;
  recurse = true;
  vsnprintf(buf,sizeof(buf)-1,fmt,args);
  recurse = false;
  _mi_fputs(out,prefix,buf);
}


void _mi_fprintf( mi_output_fun* out, const char* fmt, ... ) {
  va_list args;
  va_start(args,fmt);
  mi_vfprintf(out,NULL,fmt,args);
  va_end(args);
}

void _mi_trace_message(const char* fmt, ...) {
  if (mi_option_get(mi_option_verbose) <= 1) return;  // only with verbose level 2 or higher
  va_list args;
  va_start(args, fmt);
  mi_vfprintf(NULL, "mimalloc: ", fmt, args);
  va_end(args);
}

void _mi_verbose_message(const char* fmt, ...) {
  if (!mi_option_is_enabled(mi_option_verbose)) return;
  va_list args;
  va_start(args,fmt);
  mi_vfprintf(NULL, "mimalloc: ", fmt, args);
  va_end(args);
}

void _mi_error_message(const char* fmt, ...) {
  if (!mi_option_is_enabled(mi_option_show_errors) && !mi_option_is_enabled(mi_option_verbose)) return;
  if (mi_atomic_increment(&error_count) > MAX_ERROR_COUNT) return;
  va_list args;
  va_start(args,fmt);
  mi_vfprintf(NULL, "mimalloc: error: ", fmt, args);
  va_end(args);
  mi_assert(false);
}

void _mi_warning_message(const char* fmt, ...) {
  if (!mi_option_is_enabled(mi_option_show_errors) && !mi_option_is_enabled(mi_option_verbose)) return;
  if (mi_atomic_increment(&error_count) > MAX_ERROR_COUNT) return;
  va_list args;
  va_start(args,fmt);
  mi_vfprintf(NULL, "mimalloc: warning: ", fmt, args);
  va_end(args);
}


#if MI_DEBUG
void _mi_assert_fail(const char* assertion, const char* fname, unsigned line, const char* func ) {
  _mi_fprintf(NULL,"mimalloc: assertion failed: at \"%s\":%u, %s\n  assertion: \"%s\"\n", fname, line, (func==NULL?"":func), assertion);
  abort();
}
#endif

// --------------------------------------------------------
// Initialize options by checking the environment
// --------------------------------------------------------

static void mi_strlcpy(char* dest, const char* src, size_t dest_size) {
  dest[0] = 0;
  #pragma warning(suppress:4996)
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = 0;
}

static void mi_strlcat(char* dest, const char* src, size_t dest_size) {
  #pragma warning(suppress:4996)
  strncat(dest, src, dest_size - 1);
  dest[dest_size - 1] = 0;
}

#if defined _WIN32
// On Windows use GetEnvironmentVariable instead of getenv to work
// reliably even when this is invoked before the C runtime is initialized.
// i.e. when `_mi_preloading() == true`.
// Note: on windows, environment names are not case sensitive.
#include <windows.h>
static bool mi_getenv(const char* name, char* result, size_t result_size) {
  result[0] = 0;
  size_t len = GetEnvironmentVariableA(name, result, (DWORD)result_size);  
  return (len > 0 && len < result_size);
}
#else
static bool mi_getenv(const char* name, char* result, size_t result_size) {
  const char* s = getenv(name);
  if (s == NULL) {
    // in unix environments we check the upper case name too.
    char buf[64+1];
    size_t len = strlen(name);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    for (size_t i = 0; i < len; i++) {
      buf[i] = toupper(name[i]);
    }
    buf[len] = 0;
    s = getenv(buf);
  }
  if (s != NULL && strlen(s) < result_size) {
    mi_strlcpy(result, s, result_size);
    return true;
  }
  else {
    return false;
  }
}
#endif
static void mi_option_init(mi_option_desc_t* desc) {  
  // Read option value from the environment
  char buf[64+1];
  mi_strlcpy(buf, "mimalloc_", sizeof(buf));
  mi_strlcat(buf, desc->name, sizeof(buf));
  char s[64+1];
  if (mi_getenv(buf, s, sizeof(s))) {
    size_t len = strlen(s);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    for (size_t i = 0; i < len; i++) {
      buf[i] = toupper(s[i]);
    }
    buf[len] = 0;
    if (buf[0]==0 || strstr("1;TRUE;YES;ON", buf) != NULL) {
      desc->value = 1;
      desc->init = INITIALIZED;
    }
    else if (strstr("0;FALSE;NO;OFF", buf) != NULL) {
      desc->value = 0;
      desc->init = INITIALIZED;
    }
    else {
      char* end = buf;
      long value = strtol(buf, &end, 10);
      if (*end == 0) {
        desc->value = value;
        desc->init = INITIALIZED;
      }
      else {
        _mi_warning_message("environment option mimalloc_%s has an invalid value: %s\n", desc->name, buf);
        desc->init = DEFAULTED;
      }
    }
  }
  else {
    desc->init = DEFAULTED;
  }
  mi_assert_internal(desc->init != UNINIT);
}
