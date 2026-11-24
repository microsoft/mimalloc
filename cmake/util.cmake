# ----------------------------------------------------------------------------
# Utility functions for CMakeLists.txt
# ----------------------------------------------------------------------------
include(CheckCCompilerFlag)
include(CheckCSourceRuns)
include(CheckCSourceCompiles)
include(CheckIncludeFiles)
include(CheckSymbolExists)
include(CMakePushCheckState)
include(GNUInstallDirs)
include("${CMAKE_CURRENT_LIST_DIR}/JoinPaths.cmake")

# Set the allowed values of the cache string `var` and validate its value (case-insensitive, and
# boolean constants map to ON or OFF); the value is normalized to the spelling of the allowed value.
function(set_property_and_validate var)
  set_property(CACHE ${var} PROPERTY STRINGS ${ARGN})
  string(TOUPPER "${${var}}" value)
  if(value MATCHES "^(ON|YES|Y|TRUE|1)$")
    set(value "ON")
  elseif(value MATCHES "^(OFF|NO|N|FALSE|0|)$")
    set(value "OFF")
  endif()
  foreach(allowed IN LISTS ARGN)
    string(TOUPPER "${allowed}" allowed_upper)
    if(value STREQUAL allowed_upper)
      set(${var} "${allowed}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REPLACE ";" ", " allowed "${ARGN}")
  message(FATAL_ERROR "Invalid option value for ${var}: ${${var}} (expecting one of: ${allowed})")
endfunction()

# Define a cache string option `name` with a list of `allowed` values (where the first one is the default)
function(option_list name allowed description)
  list(GET allowed 0 default)
  set(${name} "${default}" CACHE STRING "${description}")
  set_property_and_validate(${name} ${allowed})
  set(${name} "${${name}}" PARENT_SCOPE)
endfunction()

# case insensitive comparison
function(STRIEQUAL str1 str2 is_equal)
    string(TOUPPER "${str1}" str1_upper)
    string(TOUPPER "${str2}" str2_upper)
    if(str1_upper STREQUAL str2_upper)
        set(${is_equal} TRUE PARENT_SCOPE)
    else()
        set(${is_equal} FALSE PARENT_SCOPE)
    endif()
endfunction()

# check if `symbol` is declared in `headers` and links when also linking with `libs`
function(check_symbol_exists_with_libs symbol headers libs outvar)
  cmake_push_check_state()
  list(APPEND CMAKE_REQUIRED_LIBRARIES ${libs})
  check_symbol_exists(${symbol} "${headers}" ${outvar})
  cmake_pop_check_state()
endfunction()

function(check_big_endian outvar)
  check_c_source_runs("int main(void) { const unsigned int n = 1; return *(const unsigned char*)&n; }" ${outvar})
endfunction()

# determine if `libatomic` is needed for 64-bit and pointer-sized atomics (on some 32-bit platforms);
# sets `outvar` to the library to link with (or empty if not needed or not found) (issue #634)
function(find_libatomic_if_needed cflags outvar)
  set(${outvar} "" PARENT_SCOPE)
  set(src "
    #include <stdatomic.h>
    #include <stdint.h>
    static _Atomic(int64_t) x64;
    static _Atomic(uintptr_t) xp;
    int main(void) {
      volatile int64_t v64 = 1;  volatile uintptr_t vp = 1;
      int64_t e64 = atomic_load_explicit(&x64, memory_order_acquire);
      uintptr_t ep = atomic_load_explicit(&xp, memory_order_acquire);
      atomic_store_explicit(&x64, v64, memory_order_release);
      atomic_fetch_add_explicit(&x64, v64, memory_order_relaxed);
      atomic_compare_exchange_strong(&x64, &e64, v64);
      atomic_exchange(&xp, vp);
      atomic_compare_exchange_weak(&xp, &ep, vp);
      return (int)atomic_fetch_add_explicit(&xp, vp, memory_order_acq_rel);
    }")
  cmake_push_check_state(RESET)
  string(REPLACE ";" " " CMAKE_REQUIRED_FLAGS "${cflags}")
  check_c_source_compiles("${src}" mi_has_builtin_atomics)
  if(NOT mi_has_builtin_atomics)
    find_link_library("atomic" libatomic)
    if(libatomic)
      set(CMAKE_REQUIRED_LIBRARIES ${libatomic})
      check_c_source_compiles("${src}" mi_has_libatomic_atomics)
      if(mi_has_libatomic_atomics)
        set(${outvar} ${libatomic} PARENT_SCOPE)
      endif()
    endif()
    if(NOT mi_has_libatomic_atomics)
      message(WARNING "Unable to link 64-bit atomic operations (with or without libatomic)")
    endif()
  endif()
  cmake_pop_check_state()
endfunction()

# we prefer -l<lib> test over `find_library` as sometimes core libraries
# like `libatomic` are not on the system path (see issue #898)
function(find_link_library libname outlibname)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES ${libname})
  check_c_source_compiles("int main(void) { return 0; }" mi_has_lib${libname})
  cmake_pop_check_state()
  if (mi_has_lib${libname})
    message(VERBOSE "link library: -l${libname}")
    set(${outlibname} ${libname} PARENT_SCOPE)
  else()
    find_library(MI_LIBPATH_${libname} ${libname})
    if (MI_LIBPATH_${libname})
      message(VERBOSE "link library ${libname} at ${MI_LIBPATH_${libname}}")
      set(${outlibname} ${MI_LIBPATH_${libname}} PARENT_SCOPE)
    else()
      message(VERBOSE "link library not found: ${libname}")
      set(${outlibname} "" PARENT_SCOPE)
    endif()
  endif()
endfunction()

# describe the compiler for `lang` (C or CXX) as its path (with symlinks resolved), id, and version
function(compiler_info lang outvar)
  set(compiler "${CMAKE_${lang}_COMPILER}")
  get_filename_component(compiler_path "${compiler}" REALPATH)
  if(NOT compiler_path STREQUAL compiler)
    string(APPEND compiler " -> ${compiler_path}")
  endif()
  set(${outvar} "${compiler} (${CMAKE_${lang}_COMPILER_ID} ${CMAKE_${lang}_COMPILER_VERSION}, ${lang})" PARENT_SCOPE)
endfunction()
