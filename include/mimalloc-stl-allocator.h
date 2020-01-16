#pragma once
#ifndef MIMALLOC_STL_ALLOCATOR_H
#define MIMALLOC_STL_ALLOCATOR_H

#ifdef __cplusplus
/* ----------------------------------------------------------------------------
This header can be used to hook mimalloc into STL containers in place of 
std::allocator.
-----------------------------------------------------------------------------*/
#include <type_traits> // true_type

#pragma warning(disable: 4100)

template <class T>
struct mi_stl_allocator {
  typedef T value_type;
  
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  using is_always_equal = std::true_type;

  mi_stl_allocator() noexcept {}
  mi_stl_allocator(const mi_stl_allocator& other) noexcept {}
  template <class U>
  mi_stl_allocator(const mi_stl_allocator<U>& other) noexcept {}

  T* allocate(size_t n, const void* hint = 0) {
    return (T*)mi_mallocn(n, sizeof(T));
  }

  void deallocate(T* p, size_t n) {
    mi_free(p);
  }
};

template <class T1, class T2>
bool operator==(const mi_stl_allocator<T1>& lhs, const mi_stl_allocator<T2>& rhs) noexcept { return true; }
template <class T1, class T2>
bool operator!=(const mi_stl_allocator<T1>& lhs, const mi_stl_allocator<T2>& rhs) noexcept { return false; }

#endif // __cplusplus
#endif // MIMALLOC_STL_ALLOCATOR_H
