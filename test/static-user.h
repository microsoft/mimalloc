/* ----------------------------------------------------------------------------
Copyright (c) 2021 Frank Richter
This is free software; you can redistribute it and/or modify it under the
terms of the MIT license. A copy of the license can be found in the file
"LICENSE" at the root of this distribution.
-----------------------------------------------------------------------------*/

#ifndef STATIC_USER_H_
#define STATIC_USER_H_

#if defined(_MSC_VER) || defined(__MINGW32__)
#define DLLEXPORT   __declspec(dllexport)
#define DLLIMPORT   __declspec(dllimport)
#elif defined(__GNUC__)
#define DLLEXPORT   __attribute__((visibility("default")))
#define DLLIMPORT
#else
#define DLLEXPORT
#define DLLIMPORT
#endif

#if defined(STATIC_USER_SHARED)
#if defined(STATIC_USER_BUILD)
#define STATIC_USER_API DLLEXPORT
#else
#define STATIC_USER_API DLLIMPORT
#endif
#else
#define STATIC_USER_API
#endif

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

STATIC_USER_API void static_user_ref();

#if defined(__cplusplus)
} // extern "C"
#endif // defined(__cplusplus)

#endif // STATIC_USER_H_
