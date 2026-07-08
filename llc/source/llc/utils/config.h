#pragma once

#include <cstdlib>

// Compiler/workaround feature macros shared by tests and runtime headers.

#if defined(_MSC_VER) && !defined(__clang__)
#define LLC_COMPILER_MSVC 1
#define LLC_COMPILER_MSVC_VERSION _MSC_VER
#else
#define LLC_COMPILER_MSVC 0
#define LLC_COMPILER_MSVC_VERSION 0
#endif

// Visual Studio issue:
// https://developercommunity.visualstudio.com/t/Unable-to-destroy-C20-coroutine-in-fin/10657377
//
// Reported fixed in VS 2026 toolset v145, still reproducible in v143.
// We treat _MSC_VER < 1950 as affected.
#if LLC_COMPILER_MSVC && (LLC_COMPILER_MSVC_VERSION < 1950) && \
    (defined(_CRT_USE_ADDRESS_SANITIZER) || defined(__SANITIZE_ADDRESS__))
#define LLC_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 1
#else
#define LLC_WORKAROUND_MSVC_COROUTINE_ASAN_UAF 0
#endif

// [[msvc::no_unique_address]] corrupts coroutine frame layout under MSVC ASAN.
// Even without ASAN, MSVC miscompiles the layout of classes that use this
// attribute when those classes are stored inside a coroutine frame (local
// variables, not just promise types). Only safe on types that never live in
// a coroutine frame - e.g. Outcome<>, which is embedded in the promise itself.
// See: https://developercommunity.visualstudio.com/t/msvc::no_unique_address-nonconforman/10504173
//      https://developercommunity.visualstudio.com/t/c20-coroutine-memory-corruption/1683791
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(msvc::no_unique_address) && !LLC_WORKAROUND_MSVC_COROUTINE_ASAN_UAF
#define LLC_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif __has_cpp_attribute(no_unique_address)
#define LLC_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define LLC_NO_UNIQUE_ADDRESS
#endif
#else
#define LLC_NO_UNIQUE_ADDRESS
#endif

// Windows ASAN (both MSVC and clang-cl) corrupts exception objects caught
// inside coroutine frames, making e.what() crash.
#if defined(_WIN32) && (defined(__SANITIZE_ADDRESS__) || defined(_CRT_USE_ADDRESS_SANITIZER))
#define LLC_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION 1
#elif defined(_WIN32) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LLC_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION 1
#endif
#endif
#ifndef LLC_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION
#define LLC_WORKAROUND_WINDOWS_ASAN_COROUTINE_EXCEPTION 0
#endif

#if defined(LLC_ENABLE_EXCEPTIONS)
#if LLC_ENABLE_EXCEPTIONS && !defined(__cpp_exceptions)
#undef LLC_ENABLE_EXCEPTIONS
#define LLC_ENABLE_EXCEPTIONS 0
#endif
#elif defined(__cpp_exceptions)
#define LLC_ENABLE_EXCEPTIONS 1
#else
#define LLC_ENABLE_EXCEPTIONS 0
#endif

#if LLC_ENABLE_EXCEPTIONS
#define LLC_THROW(exception_expr) throw exception_expr
#define LLC_TRY try
#define LLC_CATCH_ALL() catch (...)
#define LLC_RETHROW() throw
#else
#define LLC_THROW(exception_expr)                  \
    do {                                           \
        static_cast<void>(sizeof(exception_expr)); \
        std::abort();                              \
    } while (false)
#define LLC_TRY if (true)
#define LLC_CATCH_ALL() else
#define LLC_RETHROW() std::abort()
#endif
