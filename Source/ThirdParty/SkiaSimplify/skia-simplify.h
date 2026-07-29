
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* THIS IS A SUBSET OF THE SKIA LIBRARY'S PATHOPS MODULE WITH ONLY THE FOLLOWING FUNCTIONALITY:
 *   - Path simplification
 *   - Path boolean operations
 *   - Path parser from SVG representation
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#define TArray SKIA_TArray

#define SKNX_NO_SIMD

#ifdef SKIA_SIMPLIFY_NAMESPACE
namespace SKIA_SIMPLIFY_NAMESPACE {
#endif

#if !defined(SK_BUILD_FOR_ANDROID) && !defined(SK_BUILD_FOR_IOS) && !defined(SK_BUILD_FOR_WIN) && \
    !defined(SK_BUILD_FOR_UNIX) && !defined(SK_BUILD_FOR_MAC)

    #ifdef __APPLE__
    #endif

    #if defined(_WIN32) || defined(__SYMBIAN32__)
        #define SK_BUILD_FOR_WIN
    #elif defined(ANDROID) || defined(__ANDROID__)
        #define SK_BUILD_FOR_ANDROID
    #elif defined(linux) || defined(__linux) || defined(__FreeBSD__) || \
          defined(__OpenBSD__) || defined(__sun) || defined(__NetBSD__) || \
          defined(__DragonFly__) || defined(__Fuchsia__) || \
          defined(__GLIBC__) || defined(__GNU__) || defined(__unix__)
        #define SK_BUILD_FOR_UNIX
    #elif TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
        #define SK_BUILD_FOR_IOS
    #else
        #define SK_BUILD_FOR_MAC
    #endif
#endif // end SK_BUILD_FOR_*


#if defined(SK_BUILD_FOR_WIN) && !defined(__clang__)
    #if !defined(SK_RESTRICT)
        #define SK_RESTRICT __restrict
    #endif
#endif

#if !defined(SK_RESTRICT)
    #define SK_RESTRICT __restrict__
#endif

#if !defined(SK_CPU_BENDIAN) && !defined(SK_CPU_LENDIAN)
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        #define SK_CPU_BENDIAN
    #elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        #define SK_CPU_LENDIAN
    #elif defined(__sparc) || defined(__sparc__) || \
      defined(_POWER) || defined(__powerpc__) || \
      defined(__ppc__) || defined(__hppa) || \
      defined(__PPC__) || defined(__PPC64__) || \
      defined(_MIPSEB) || defined(__ARMEB__) || \
      defined(__s390__) || \
      (defined(__sh__) && defined(__BIG_ENDIAN__)) || \
      (defined(__ia64) && defined(__BIG_ENDIAN__))
         #define SK_CPU_BENDIAN
    #else
        #define SK_CPU_LENDIAN
    #endif
#endif

#if defined(__i386) || defined(_M_IX86) ||  defined(__x86_64__) || defined(_M_X64)
  #define SK_CPU_X86 1
#endif

/**
 *  SK_CPU_SSE_LEVEL
 *
 *  If defined, SK_CPU_SSE_LEVEL should be set to the highest supported level.
 *  On non-intel CPU this should be undefined.
 */
#define SK_CPU_SSE_LEVEL_SSE1     10
#define SK_CPU_SSE_LEVEL_SSE2     20
#define SK_CPU_SSE_LEVEL_SSE3     30
#define SK_CPU_SSE_LEVEL_SSSE3    31
#define SK_CPU_SSE_LEVEL_SSE41    41
#define SK_CPU_SSE_LEVEL_SSE42    42
#define SK_CPU_SSE_LEVEL_AVX      51
#define SK_CPU_SSE_LEVEL_AVX2     52
#define SK_CPU_SSE_LEVEL_SKX      60

// TODO(brianosman,kjlubick) clean up these checks

// Are we in GCC/Clang?
#ifndef SK_CPU_SSE_LEVEL
    // These checks must be done in descending order to ensure we set the highest
    // available SSE level.
    #if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
        defined(__AVX512BW__) && defined(__AVX512VL__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SKX
    #elif defined(__AVX2__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_AVX2
    #elif defined(__AVX__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_AVX
    #elif defined(__SSE4_2__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSE42
    #elif defined(__SSE4_1__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSE41
    #elif defined(__SSSE3__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSSE3
    #elif defined(__SSE3__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSE3
    #elif defined(__SSE2__)
        #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSE2
    #endif
#endif

// Are we in VisualStudio?
#ifndef SK_CPU_SSE_LEVEL
    // These checks must be done in descending order to ensure we set the highest
    // available SSE level. 64-bit intel guarantees at least SSE2 support.
    #if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512CD__) && \
        defined(__AVX512BW__) && defined(__AVX512VL__)
        #define SK_CPU_SSE_LEVEL        SK_CPU_SSE_LEVEL_SKX
    #elif defined(__AVX2__)
        #define SK_CPU_SSE_LEVEL        SK_CPU_SSE_LEVEL_AVX2
    #elif defined(__AVX__)
        #define SK_CPU_SSE_LEVEL        SK_CPU_SSE_LEVEL_AVX
    #elif defined(_M_X64) || defined(_M_AMD64)
        #define SK_CPU_SSE_LEVEL        SK_CPU_SSE_LEVEL_SSE2
    #elif defined(_M_IX86_FP)
        #if _M_IX86_FP >= 2
            #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSE2
        #elif _M_IX86_FP == 1
            #define SK_CPU_SSE_LEVEL    SK_CPU_SSE_LEVEL_SSE1
        #endif
    #endif
#endif

// ARM defines
#if defined(__arm__) && (!defined(__APPLE__) || !TARGET_IPHONE_SIMULATOR)
    #define SK_CPU_ARM32
#elif defined(__aarch64__)
    #define SK_CPU_ARM64
#endif

// All 64-bit ARM chips have NEON.  Many 32-bit ARM chips do too.
#if !defined(SK_ARM_HAS_NEON) && defined(__ARM_NEON)
    #define SK_ARM_HAS_NEON
#endif

/*  SkTypes.h, the root of the public header files, includes this file
    SkUserConfig.h after first initializing certain Skia defines, letting
    this file change or augment those flags.

    Below are optional defines that add, subtract, or change default behavior
    in Skia. Your port can locally edit this file to enable/disable flags as
    you choose, or these can be declared on your command line (i.e. -Dfoo).

    By default, this #include file will always default to having all the flags
    commented out, so including it will have no effect.
*/

///////////////////////////////////////////////////////////////////////////////

/*  Skia has lots of debug-only code. Often this is just null checks or other
    parameter checking, but sometimes it can be quite intrusive (e.g. check that
    each 32bit pixel is in premultiplied form). This code can be very useful
    during development, but will slow things down in a shipping product.

    By default, these mutually exclusive flags are defined in SkTypes.h,
    based on the presence or absence of NDEBUG, but that decision can be changed
    here.
*/
//#define SK_DEBUG
//#define SK_RELEASE

/*  To write debug messages to a console, skia will call SkDebugf(...) following
    printf conventions (e.g. const char* format, ...). If you want to redirect
    this to something other than printf, define yours here
*/
#define SkDebugf(...) ((void) 0)

/* Skia has both debug and release asserts. When an assert fails SK_ABORT will
   be used to report an abort message. SK_ABORT is expected not to return. Skia
   provides a default implementation which will print the message with SkDebugf
   and then call sk_abort_no_print.
*/
//#define SK_ABORT(message, ...)

/* To specify a different default font strike cache memory limit, define this. If this is
   undefined, skia will use a built-in value.
*/
//#define SK_DEFAULT_FONT_CACHE_LIMIT   (1024 * 1024)

/* To specify a different default font strike cache count limit, define this. If this is
   undefined, skia will use a built-in value.
*/
// #define SK_DEFAULT_FONT_CACHE_COUNT_LIMIT   2048

/* To specify the default size of the image cache, undefine this and set it to
   the desired value (in bytes). SkGraphics.h as a runtime API to set this
   value as well. If this is undefined, a built-in value will be used.
*/
//#define SK_DEFAULT_IMAGE_CACHE_LIMIT (1024 * 1024)

/*  Define this to set the upper limit for text to support LCD. Values that
    are very large increase the cost in the font cache and draw slower, without
    improving readability. If this is undefined, Skia will use its default
    value (e.g. 48)
*/
//#define SK_MAX_SIZE_FOR_LCDTEXT     48

/*  Change the kN32_SkColorType ordering to BGRA to work in X windows.
*/
//#define SK_R32_SHIFT    16

/* Determines whether to build code that supports the Ganesh GPU backend. Some classes
   that are not GPU-specific, such as SkShader subclasses, have optional code
   that is used allows them to interact with this GPU backend. If you'd like to
   include this code, include -DSK_GANESH in your cflags or uncomment below.
   Defaults to not set (No Ganesh GPU backend).
   This define affects the ABI of Skia, so make sure it matches the client which uses
   the compiled version of Skia.
*/
//#define SK_GANESH

/* Skia makes use of histogram logging macros to trace the frequency of
   events. By default, Skia provides no-op versions of these macros.
   Skia consumers can provide their own definitions of these macros to
   integrate with their histogram collection backend.
*/
//#define SK_HISTOGRAM_BOOLEAN(name, sample)
//#define SK_HISTOGRAM_ENUMERATION(name, sample, enum_size)
//#define SK_HISTOGRAM_EXACT_LINEAR(name, sample, value_max)
//#define SK_HISTOGRAM_MEMORY_KB(name, sample)

/* Skia tries to make use of some non-standard C++ language extensions.
   By default, Skia provides msvc and clang/gcc versions of these macros.
   Skia consumers can provide their own definitions of these macros to
   integrate with their own compilers and build system.
*/
//#define SK_ALWAYS_INLINE inline __attribute__((always_inline))
//#define SK_NEVER_INLINE __attribute__((noinline))
//#define SK_PRINTF_LIKE(A, B) __attribute__((format(printf, (A), (B))))
//#define SK_NO_SANITIZE(A) __attribute__((no_sanitize(A)))
//#define SK_TRIVIAL_ABI [[clang::trivial_abi]]

/*
 * If compiling Skia as a DLL, public APIs should be exported. Skia will set
 * SK_API to something sensible for Clang and MSVC, but if clients need to
 * customize it for their build system or compiler, they may.
 * If a client needs to use SK_API (e.g. overriding SK_ABORT), then they
 * *must* define their own, the default will not be defined prior to loading
 * this file.
 */
//#define SK_API __declspec(dllexport)

#ifndef SK_USER_CONFIG_WAS_LOADED

// Include this to set reasonable defaults (e.g. for SK_CPU_LENDIAN)

// Allows embedders that want to disable macros that take arguments to just
// define that symbol to be one of these
#define SK_NOTHING_ARG1(arg1)
#define SK_NOTHING_ARG2(arg1, arg2)
#define SK_NOTHING_ARG3(arg1, arg2, arg3)

// IWYU pragma: begin_exports

// Note: SK_USER_CONFIG_HEADER will not work with Bazel builds and some C++ compilers.
#if defined(SK_USER_CONFIG_HEADER)
#elif defined(SK_USE_BAZEL_CONFIG_HEADER)
    // The Bazel config file is presumed to be in the root directory of its Bazel Workspace.
    // This is achieved in Skia by having a nested WORKSPACE in include/config and a cc_library
    // defined in that folder. As a result, we do not try to include SkUserConfig.h from the
    // top of Skia because Bazel sandboxing will move it to a different location.
#else
#endif
// IWYU pragma: end_exports

// Checks to make sure the SkUserConfig options do not conflict.
#if !defined(SK_DEBUG) && !defined(SK_RELEASE)
    #ifdef NDEBUG
        #define SK_RELEASE
    #else
        #define SK_DEBUG
    #endif
#endif

#if defined(SK_DEBUG) && defined(SK_RELEASE)
#  error "cannot define both SK_DEBUG and SK_RELEASE"
#elif !defined(SK_DEBUG) && !defined(SK_RELEASE)
#  error "must define either SK_DEBUG or SK_RELEASE"
#endif

#if defined(SK_CPU_LENDIAN) && defined(SK_CPU_BENDIAN)
#  error "cannot define both SK_CPU_LENDIAN and SK_CPU_BENDIAN"
#elif !defined(SK_CPU_LENDIAN) && !defined(SK_CPU_BENDIAN)
#  error "must define either SK_CPU_LENDIAN or SK_CPU_BENDIAN"
#endif

#if defined(SK_CPU_BENDIAN) && !defined(I_ACKNOWLEDGE_SKIA_DOES_NOT_SUPPORT_BIG_ENDIAN)
    #error "The Skia team is not endian-savvy enough to support big-endian CPUs."
    #error "If you still want to use Skia,"
    #error "please define I_ACKNOWLEDGE_SKIA_DOES_NOT_SUPPORT_BIG_ENDIAN."
#endif

#define SK_USER_CONFIG_WAS_LOADED
#endif // SK_USER_CONFIG_WAS_LOADED

// If SKIA_IMPLEMENTATION is defined as 1, that signals we are building Skia and should
// export our symbols. If it is not set (or set to 0), then Skia is being used by a client
// and we should not export our symbols.
#if !defined(SKIA_IMPLEMENTATION)
    #define SKIA_IMPLEMENTATION 0
#endif

// If we are compiling Skia is being as a DLL, we need to be sure to export all of our public
// APIs to that DLL. If a client is using Skia which was compiled as a DLL, we need to instruct
// the linker to use the symbols from that DLL. This is the goal of the SK_API define.
#if !defined(SK_API)
    #if defined(SKIA_DLL)
        #if defined(_MSC_VER)
            #if SKIA_IMPLEMENTATION
                #define SK_API __declspec(dllexport)
            #else
                #define SK_API __declspec(dllimport)
            #endif
        #else
            #define SK_API __attribute__((visibility("default")))
        #endif
    #else
        #define SK_API
    #endif
#endif

// SK_SPI is functionally identical to SK_API, but used within src to clarify that it's less stable
#if !defined(SK_SPI)
    #define SK_SPI SK_API
#endif

// See https://clang.llvm.org/docs/AttributeReference.html#availability
// The API_AVAILABLE macro comes from <os/availability.h> on MacOS
#if defined(SK_ENABLE_API_AVAILABLE)
#   define SK_API_AVAILABLE API_AVAILABLE
#else
#   define SK_API_AVAILABLE(...)
#endif

#if defined(__clang__) || defined(__GNUC__)
#  define SK_ATTRIBUTE(attr) __attribute__((attr))
#else
#  define SK_ATTRIBUTE(attr)
#endif

/**
 * If your judgment is better than the compiler's (i.e. you've profiled it),
 * you can use SK_ALWAYS_INLINE to force inlining. E.g.
 *     inline void someMethod() { ... }             // may not be inlined
 *     SK_ALWAYS_INLINE void someMethod() { ... }   // should always be inlined
 */
#if !defined(SK_ALWAYS_INLINE)
#  if defined(SK_BUILD_FOR_WIN)
#    define SK_ALWAYS_INLINE __forceinline
#  else
#    define SK_ALWAYS_INLINE SK_ATTRIBUTE(always_inline) inline
#  endif
#endif

/**
 * If your judgment is better than the compiler's (i.e. you've profiled it),
 * you can use SK_NEVER_INLINE to prevent inlining.
 */
#if !defined(SK_NEVER_INLINE)
#  if defined(SK_BUILD_FOR_WIN)
#    define SK_NEVER_INLINE __declspec(noinline)
#  else
#    define SK_NEVER_INLINE SK_ATTRIBUTE(noinline)
#  endif
#endif

/**
 * Used to annotate a function as taking printf style arguments.
 * `A` is the (1 based) index of the format string argument.
 * `B` is the (1 based) index of the first argument used by the format string.
 */
#if !defined(SK_PRINTF_LIKE)
#  define SK_PRINTF_LIKE(A, B) SK_ATTRIBUTE(format(printf, (A), (B)))
#endif

/**
 * Used to ignore sanitizer warnings.
 */
#if !defined(SK_NO_SANITIZE)
#  define SK_NO_SANITIZE(A) SK_ATTRIBUTE(no_sanitize(A))
#endif

/**
 * Helper macro to define no_sanitize attributes only with clang.
 */
#if defined(__clang__) && defined(__has_attribute)
  #if __has_attribute(no_sanitize)
    #define SK_CLANG_NO_SANITIZE(A) SK_NO_SANITIZE(A)
  #endif
#endif

#if !defined(SK_CLANG_NO_SANITIZE)
  #define SK_CLANG_NO_SANITIZE(A)
#endif

/**
 * Annotates a class' non-trivial special functions as trivial for the purposes of calls.
 * Allows a class with a non-trivial destructor to be __is_trivially_relocatable.
 * Use of this attribute on a public API breaks platform ABI.
 * Annotated classes may not hold pointers derived from `this`.
 * Annotated classes must implement move+delete as equivalent to memcpy+free.
 * Use may require more complete types, as callee destroys.
 *
 * https://clang.llvm.org/docs/AttributeReference.html#trivial-abi
 * https://libcxx.llvm.org/DesignDocs/UniquePtrTrivialAbi.html
 */
#if !defined(SK_TRIVIAL_ABI)
#  define SK_TRIVIAL_ABI
#endif

#if !defined(SkDebugf)
    void SK_SPI SkDebugf(const char format[], ...) SK_PRINTF_LIKE(1, 2);
#endif

#if defined(SK_DEBUG)
    #define SkDEBUGCODE(...)  __VA_ARGS__
    #define SkDEBUGF(...)     SkDebugf(__VA_ARGS__)
#else
    #define SkDEBUGCODE(...)
    #define SkDEBUGF(...)
#endif

#if defined(__clang__) && defined(__has_attribute)
    #if __has_attribute(likely)
        #define SK_LIKELY [[likely]]
        #define SK_UNLIKELY [[unlikely]]
    #else
        #define SK_LIKELY
        #define SK_UNLIKELY
    #endif
#else
    #define SK_LIKELY
    #define SK_UNLIKELY
#endif

/** Called internally if we hit an unrecoverable error.
    The platform implementation must not return, but should either throw
    an exception or otherwise exit.
*/
[[noreturn]] SK_API extern void sk_abort_no_print(void);

#if defined(SK_BUILD_FOR_GOOGLE3)
    void SkDebugfForDumpStackTrace(const char* data, void* unused);
    namespace base {
        void DumpStackTrace(int skip_count, void w(const char*, void*), void* arg);
    }
#  define SK_DUMP_GOOGLE3_STACK() ::base::DumpStackTrace(0, SkDebugfForDumpStackTrace, nullptr)
#else
#  define SK_DUMP_GOOGLE3_STACK()
#endif

#if !defined(SK_ABORT)
#  if defined(SK_BUILD_FOR_WIN)
     // This style lets Visual Studio follow errors back to the source file.
#    define SK_DUMP_LINE_FORMAT "%s(%d)"
#  else
#    define SK_DUMP_LINE_FORMAT "%s:%d"
#  endif
#  define SK_ABORT(message, ...) \
    do { \
        SkDebugf(SK_DUMP_LINE_FORMAT ": fatal error: \"" message "\"\n", \
                 __FILE__, __LINE__, ##__VA_ARGS__); \
        SK_DUMP_GOOGLE3_STACK(); \
        sk_abort_no_print(); \
    } while (false)
#endif

// SkASSERT, SkASSERTF and SkASSERT_RELEASE can be used as standalone assertion expressions, e.g.
//    uint32_t foo(int x) {
//        SkASSERT(x > 4);
//        return x - 4;
//    }
// and are also written to be compatible with constexpr functions:
//    constexpr uint32_t foo(int x) {
//        return SkASSERT(x > 4),
//               x - 4;
//    }
#if defined(__clang__)
#define SkASSERT_RELEASE(cond) \
    static_cast<void>( __builtin_expect(static_cast<bool>(cond), 1) \
        ? static_cast<void>(0) \
        : []{ SK_ABORT("check(%s)", #cond); }() )

#define SkASSERTF_RELEASE(cond, fmt, ...)                                  \
    static_cast<void>( __builtin_expect(static_cast<bool>(cond), 1)        \
        ? static_cast<void>(0)                                             \
        : [&]{ SK_ABORT("assertf(%s): " fmt, #cond, ##__VA_ARGS__); }() )
#else
#define SkASSERT_RELEASE(cond) \
    static_cast<void>( (cond) ? static_cast<void>(0) : []{ SK_ABORT("check(%s)", #cond); }() )

#define SkASSERTF_RELEASE(cond, fmt, ...)                                   \
    static_cast<void>( (cond)                                               \
        ? static_cast<void>(0)                                              \
        : [&]{ SK_ABORT("assertf(%s): " fmt, #cond, ##__VA_ARGS__); }() )
#endif

#if defined(SK_DEBUG)
    #define SkASSERT(cond)            SkASSERT_RELEASE(cond)
    #define SkASSERTF(cond, fmt, ...) SkASSERTF_RELEASE(cond, fmt, ##__VA_ARGS__)
    #define SkDEBUGFAIL(message)      SK_ABORT("%s", message)
    #define SkDEBUGFAILF(fmt, ...)    SK_ABORT(fmt, ##__VA_ARGS__)
    #define SkAssertResult(cond)      SkASSERT(cond)
#else
    #define SkASSERT(cond)            static_cast<void>(0)
    #define SkASSERTF(cond, fmt, ...) static_cast<void>(0)
    #define SkDEBUGFAIL(message)
    #define SkDEBUGFAILF(fmt, ...)

    // unlike SkASSERT, this macro executes its condition in the non-debug build.
    // The if is present so that this can be used with functions marked [[nodiscard]].
    #define SkAssertResult(cond)         if (cond) {} do {} while(false)
#endif

#if !defined(SkUNREACHABLE)
#  if defined(_MSC_VER) && !defined(__clang__)
#    define FAST_FAIL_INVALID_ARG                 5
// See https://developercommunity.visualstudio.com/content/problem/1128631/code-flow-doesnt-see-noreturn-with-extern-c.html
// for why this is wrapped. Hopefully removable after msvc++ 19.27 is no longer supported.
[[noreturn]] static inline void sk_fast_fail() { __fastfail(FAST_FAIL_INVALID_ARG); }
#    define SkUNREACHABLE sk_fast_fail()
#  else
#    define SkUNREACHABLE __builtin_trap()
#  endif
#endif

[[noreturn]] SK_API inline void sk_print_index_out_of_bounds(size_t i, size_t size) {
    SK_ABORT("Index (%zu) out of bounds for size %zu.\n", i, size);
}

template <typename T> SK_API inline T sk_collection_check_bounds(T i, T size) {
    if (0 <= i && i < size) SK_LIKELY {
        return i;
    }

    SK_UNLIKELY {
        #if defined(SK_DEBUG)
            sk_print_index_out_of_bounds(static_cast<size_t>(i), static_cast<size_t>(size));
        #else
            SkUNREACHABLE;
        #endif
    }
}

[[noreturn]] SK_API inline void sk_print_length_too_big(size_t i, size_t size) {
    SK_ABORT("Length (%zu) is too big for size %zu.\n", i, size);
}

template <typename T> SK_API inline T sk_collection_check_length(T i, T size) {
    if (0 <= i && i <= size) SK_LIKELY {
        return i;
    }

    SK_UNLIKELY {
        #if defined(SK_DEBUG)
            sk_print_length_too_big(static_cast<size_t>(i), static_cast<size_t>(size));
        #else
            SkUNREACHABLE;
        #endif
    }
}

SK_API inline void sk_collection_not_empty(bool empty) {
    if (empty) SK_UNLIKELY {
        #if defined(SK_DEBUG)
            SK_ABORT("Collection is empty.\n");
        #else
            SkUNREACHABLE;
        #endif
    }
}

[[noreturn]] SK_API inline void sk_print_size_too_big(size_t size, size_t maxSize) {
    SK_ABORT("Size (%zu) can't be represented in bytes. Max size is %zu.\n", size, maxSize);
}

template <typename T>
SK_ALWAYS_INLINE size_t check_size_bytes_too_big(size_t size) {
    const size_t kMaxSize = std::numeric_limits<size_t>::max() / sizeof(T);
    if (size > kMaxSize) {
        #if defined(SK_DEBUG)
            sk_print_size_too_big(size, kMaxSize);
        #else
            SkUNREACHABLE;
        #endif
    }
    return size;
}

// TODO(bungeman,kjlubick) There are a lot of assumptions throughout the codebase that
//   these types are 32 bits, when they could be more or less. Public APIs should stop
//   using these. Internally, we could use uint_fast8_t and uint_fast16_t, but not in
//   public APIs due to ABI incompatibilities.

/** Fast type for unsigned 8 bits. Use for parameter passing and local
    variables, not for storage
*/
typedef unsigned U8CPU;

/** Fast type for unsigned 16 bits. Use for parameter passing and local
    variables, not for storage
*/
typedef unsigned U16CPU;

// Max Signed 16 bit value
static constexpr int16_t SK_MaxS16 = INT16_MAX;
static constexpr int16_t SK_MinS16 = -SK_MaxS16;

static constexpr int32_t SK_MaxS32 = INT32_MAX;
static constexpr int32_t SK_MinS32 = -SK_MaxS32;
static constexpr int32_t SK_NaN32  = INT32_MIN;

static constexpr int64_t SK_MaxS64 = INT64_MAX;
static constexpr int64_t SK_MinS64 = -SK_MaxS64;

// 64bit -> 32bit utilities

// Handy util that can be passed two ints, and will automatically promote to
// 64bits before the multiply, so the caller doesn't have to remember to cast
// e.g. (int64_t)a * b;
static inline int64_t sk_64_mul(int64_t a, int64_t b) {
    return a * b;
}

static inline constexpr int32_t SkLeftShift(int32_t value, int32_t shift) {
    return (int32_t) ((uint32_t) value << shift);
}

static inline constexpr int64_t SkLeftShift(int64_t value, int32_t shift) {
    return (int64_t) ((uint64_t) value << shift);
}

///////////////////////////////////////////////////////////////////////////////

/**
 *  Returns true if value is a power of 2. Does not explicitly check for
 *  value <= 0.
 */
template <typename T> constexpr inline bool SkIsPow2(T value) {
    return (value & (value - 1)) == 0;
}

///////////////////////////////////////////////////////////////////////////////

/**
 *  Return a*b/((1 << shift) - 1), rounding any fractional bits.
 *  Only valid if a and b are unsigned and <= 32767 and shift is > 0 and <= 8
 */
static inline unsigned SkMul16ShiftRound(U16CPU a, U16CPU b, int shift) {
    SkASSERT(a <= 32767);
    SkASSERT(b <= 32767);
    SkASSERT(shift > 0 && shift <= 8);
    unsigned prod = a*b + (1 << (shift - 1));
    return (prod + (prod >> shift)) >> shift;
}

/**
 *  Return a*b/255, rounding any fractional bits.
 *  Only valid if a and b are unsigned and <= 32767.
 */
static inline U8CPU SkMulDiv255Round(U16CPU a, U16CPU b) {
    return SkMul16ShiftRound(a, b, 8);
}

static constexpr int32_t Sk64_pin_to_s32(int64_t x) {
    return x < SK_MinS32 ? SK_MinS32 : (x > SK_MaxS32 ? SK_MaxS32 : (int32_t)x);
}

static constexpr int32_t Sk32_sat_add(int32_t a, int32_t b) {
    return Sk64_pin_to_s32((int64_t)a + (int64_t)b);
}

static constexpr int32_t Sk32_sat_sub(int32_t a, int32_t b) {
    return Sk64_pin_to_s32((int64_t)a - (int64_t)b);
}

// To avoid UBSAN complaints about 2's compliment overflows
//
static constexpr int32_t Sk32_can_overflow_add(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a + (uint32_t)b);
}
static constexpr int32_t Sk32_can_overflow_sub(int32_t a, int32_t b) {
    return (int32_t)((uint32_t)a - (uint32_t)b);
}

/**
 * This is a 'safe' abs for 32-bit integers that asserts when undefined behavior would occur.
 * SkTAbs (in SkTemplates.h) is a general purpose absolute-value function.
 */
static inline int32_t SkAbs32(int32_t value) {
    SkASSERT(value != SK_NaN32);  // The most negative int32_t can't be negated.
    if (value < 0) {
        value = -value;
    }
    return value;
}

struct SkIPoint;

/** SkIVector provides an alternative name for SkIPoint. SkIVector and SkIPoint
    can be used interchangeably for all purposes.
*/
typedef SkIPoint SkIVector;

/** \struct SkIPoint
    SkIPoint holds two 32-bit integer coordinates.
*/
struct SkIPoint {
    int32_t fX; //!< x-axis value
    int32_t fY; //!< y-axis value

    /** Sets fX to x, fY to y.

        @param x  integer x-axis value of constructed SkIPoint
        @param y  integer y-axis value of constructed SkIPoint
        @return   SkIPoint (x, y)
    */
    static constexpr SkIPoint Make(int32_t x, int32_t y) {
        return {x, y};
    }

    /** Returns x-axis value of SkIPoint.

        @return  fX
    */
    constexpr int32_t x() const { return fX; }

    /** Returns y-axis value of SkIPoint.

        @return  fY
    */
    constexpr int32_t y() const { return fY; }

    /** Returns true if fX and fY are both zero.

        @return  true if fX is zero and fY is zero
    */
    bool isZero() const { return (fX | fY) == 0; }

    /** Sets fX to x and fY to y.

        @param x  new value for fX
        @param y  new value for fY
    */
    void set(int32_t x, int32_t y) {
        fX = x;
        fY = y;
    }

    /** Returns SkIPoint changing the signs of fX and fY.

        @return  SkIPoint as (-fX, -fY)
    */
    SkIPoint operator-() const {
        return {-fX, -fY};
    }

    /** Offsets SkIPoint by ivector v. Sets SkIPoint to (fX + v.fX, fY + v.fY).

        @param v  ivector to add
    */
    void operator+=(const SkIVector& v) {
        fX = Sk32_sat_add(fX, v.fX);
        fY = Sk32_sat_add(fY, v.fY);
    }

    /** Subtracts ivector v from SkIPoint. Sets SkIPoint to: (fX - v.fX, fY - v.fY).

        @param v  ivector to subtract
    */
    void operator-=(const SkIVector& v) {
        fX = Sk32_sat_sub(fX, v.fX);
        fY = Sk32_sat_sub(fY, v.fY);
    }

    /** Returns true if SkIPoint is equivalent to SkIPoint constructed from (x, y).

        @param x  value compared with fX
        @param y  value compared with fY
        @return   true if SkIPoint equals (x, y)
    */
    bool equals(int32_t x, int32_t y) const {
        return fX == x && fY == y;
    }

    /** Returns true if a is equivalent to b.

        @param a  SkIPoint to compare
        @param b  SkIPoint to compare
        @return   true if a.fX == b.fX and a.fY == b.fY
    */
    friend bool operator==(const SkIPoint& a, const SkIPoint& b) {
        return a.fX == b.fX && a.fY == b.fY;
    }

    /** Returns true if a is not equivalent to b.

        @param a  SkIPoint to compare
        @param b  SkIPoint to compare
        @return   true if a.fX != b.fX or a.fY != b.fY
    */
    friend bool operator!=(const SkIPoint& a, const SkIPoint& b) {
        return a.fX != b.fX || a.fY != b.fY;
    }

    /** Returns ivector from b to a; computed as (a.fX - b.fX, a.fY - b.fY).

        Can also be used to subtract ivector from ivector, returning ivector.

        @param a  SkIPoint or ivector to subtract from
        @param b  ivector to subtract
        @return   ivector from b to a
    */
    friend SkIVector operator-(const SkIPoint& a, const SkIPoint& b) {
        return { Sk32_sat_sub(a.fX, b.fX), Sk32_sat_sub(a.fY, b.fY) };
    }

    /** Returns SkIPoint resulting from SkIPoint a offset by ivector b, computed as:
        (a.fX + b.fX, a.fY + b.fY).

        Can also be used to offset SkIPoint b by ivector a, returning SkIPoint.
        Can also be used to add ivector to ivector, returning ivector.

        @param a  SkIPoint or ivector to add to
        @param b  SkIPoint or ivector to add
        @return   SkIPoint equal to a offset by b
    */
    friend SkIPoint operator+(const SkIPoint& a, const SkIVector& b) {
        return { Sk32_sat_add(a.fX, b.fX), Sk32_sat_add(a.fY, b.fY) };
    }
};

struct SkPoint;

/** SkVector provides an alternative name for SkPoint. SkVector and SkPoint can
    be used interchangeably for all purposes.
*/
typedef SkPoint SkVector;

/** \struct SkPoint
    SkPoint holds two 32-bit floating point coordinates.
*/
struct SK_API SkPoint {
    float fX; //!< x-axis value
    float fY; //!< y-axis value

    /** Sets fX to x, fY to y. Used both to set SkPoint and vector.

        @param x  float x-axis value of constructed SkPoint or vector
        @param y  float y-axis value of constructed SkPoint or vector
        @return   SkPoint (x, y)
    */
    static constexpr SkPoint Make(float x, float y) {
        return {x, y};
    }

    /** Returns x-axis value of SkPoint or vector.

        @return  fX
    */
    constexpr float x() const { return fX; }

    /** Returns y-axis value of SkPoint or vector.

        @return  fY
    */
    constexpr float y() const { return fY; }

    /** Returns true if fX and fY are both zero.

        @return  true if fX is zero and fY is zero
    */
    bool isZero() const { return (0 == fX) & (0 == fY); }

    /** Sets fX to x and fY to y.

        @param x  new value for fX
        @param y  new value for fY
    */
    void set(float x, float y) {
        fX = x;
        fY = y;
    }

    /** Sets fX to x and fY to y, promoting integers to float values.

        Assigning a large integer value directly to fX or fY may cause a compiler
        error, triggered by narrowing conversion of int to float. This safely
        casts x and y to avoid the error.

        @param x  new value for fX
        @param y  new value for fY
    */
    void iset(int32_t x, int32_t y) {
        fX = static_cast<float>(x);
        fY = static_cast<float>(y);
    }

    /** Sets fX to p.fX and fY to p.fY, promoting integers to float values.

        Assigning an SkIPoint containing a large integer value directly to fX or fY may
        cause a compiler error, triggered by narrowing conversion of int to float.
        This safely casts p.fX and p.fY to avoid the error.

        @param p  SkIPoint members promoted to float
    */
    void iset(const SkIPoint& p) {
        fX = static_cast<float>(p.fX);
        fY = static_cast<float>(p.fY);
    }

    /** Sets fX to absolute value of pt.fX; and fY to absolute value of pt.fY.

        @param pt  members providing magnitude for fX and fY
    */
    void setAbs(const SkPoint& pt) {
        fX = std::abs(pt.fX);
        fY = std::abs(pt.fY);
    }

    /** Adds offset to each SkPoint in points array with count entries.

        @param points  SkPoint array
        @param count   entries in array
        @param offset  vector added to points
    */
    static void Offset(SkPoint points[], int count, const SkVector& offset) {
        Offset(points, count, offset.fX, offset.fY);
    }

    /** Adds offset (dx, dy) to each SkPoint in points array of length count.

        @param points  SkPoint array
        @param count   entries in array
        @param dx      added to fX in points
        @param dy      added to fY in points
    */
    static void Offset(SkPoint points[], int count, float dx, float dy) {
        for (int i = 0; i < count; ++i) {
            points[i].offset(dx, dy);
        }
    }

    /** Adds offset (dx, dy) to SkPoint.

        @param dx  added to fX
        @param dy  added to fY
    */
    void offset(float dx, float dy) {
        fX += dx;
        fY += dy;
    }

    /** Returns the Euclidean distance from origin, computed as:

            sqrt(fX * fX + fY * fY)

        .

        @return  straight-line distance to origin
    */
    float length() const { return SkPoint::Length(fX, fY); }

    /** Returns the Euclidean distance from origin, computed as:

            sqrt(fX * fX + fY * fY)

        .

        @return  straight-line distance to origin
    */
    float distanceToOrigin() const { return this->length(); }

    /** Scales (fX, fY) so that length() returns one, while preserving ratio of fX to fY,
        if possible. If prior length is nearly zero, sets vector to (0, 0) and returns
        false; otherwise returns true.

        @return  true if former length is not zero or nearly zero

        example: https://fiddle.skia.org/c/@Point_normalize_2
    */
    bool normalize();

    /** Sets vector to (x, y) scaled so length() returns one, and so that
        (fX, fY) is proportional to (x, y).  If (x, y) length is nearly zero,
        sets vector to (0, 0) and returns false; otherwise returns true.

        @param x  proportional value for fX
        @param y  proportional value for fY
        @return   true if (x, y) length is not zero or nearly zero

        example: https://fiddle.skia.org/c/@Point_setNormalize
    */
    bool setNormalize(float x, float y);

    /** Scales vector so that distanceToOrigin() returns length, if possible. If former
        length is nearly zero, sets vector to (0, 0) and return false; otherwise returns
        true.

        @param length  straight-line distance to origin
        @return        true if former length is not zero or nearly zero

        example: https://fiddle.skia.org/c/@Point_setLength
    */
    bool setLength(float length);

    /** Sets vector to (x, y) scaled to length, if possible. If former
        length is nearly zero, sets vector to (0, 0) and return false; otherwise returns
        true.

        @param x       proportional value for fX
        @param y       proportional value for fY
        @param length  straight-line distance to origin
        @return        true if (x, y) length is not zero or nearly zero

        example: https://fiddle.skia.org/c/@Point_setLength_2
    */
    bool setLength(float x, float y, float length);

    /** Sets dst to SkPoint times scale. dst may be SkPoint to modify SkPoint in place.

        @param scale  factor to multiply SkPoint by
        @param dst    storage for scaled SkPoint

        example: https://fiddle.skia.org/c/@Point_scale
    */
    void scale(float scale, SkPoint* dst) const;

    /** Scales SkPoint in place by scale.

        @param value  factor to multiply SkPoint by
    */
    void scale(float value) { this->scale(value, this); }

    /** Changes the sign of fX and fY.
    */
    void negate() {
        fX = -fX;
        fY = -fY;
    }

    /** Returns SkPoint changing the signs of fX and fY.

        @return  SkPoint as (-fX, -fY)
    */
    SkPoint operator-() const {
        return {-fX, -fY};
    }

    /** Adds vector v to SkPoint. Sets SkPoint to: (fX + v.fX, fY + v.fY).

        @param v  vector to add
    */
    void operator+=(const SkVector& v) {
        fX += v.fX;
        fY += v.fY;
    }

    /** Subtracts vector v from SkPoint. Sets SkPoint to: (fX - v.fX, fY - v.fY).

        @param v  vector to subtract
    */
    void operator-=(const SkVector& v) {
        fX -= v.fX;
        fY -= v.fY;
    }

    /** Returns SkPoint multiplied by scale.

        @param scale  float to multiply by
        @return       SkPoint as (fX * scale, fY * scale)
    */
    SkPoint operator*(float scale) const {
        return {fX * scale, fY * scale};
    }

    /** Multiplies SkPoint by scale. Sets SkPoint to: (fX * scale, fY * scale).

        @param scale  float to multiply by
        @return       reference to SkPoint
    */
    SkPoint& operator*=(float scale) {
        fX *= scale;
        fY *= scale;
        return *this;
    }

    /** Returns true if both fX and fY are measurable values.

        @return  true for values other than infinities and NaN
    */
    bool isFinite() const {
        float accum = 0;
        accum *= fX;
        accum *= fY;

        // accum is either NaN or it is finite (zero).
        SkASSERT(0 == accum || std::isnan(accum));

        // value==value will be true iff value is not NaN
        // TODO: is it faster to say !accum or accum==accum?
        return !std::isnan(accum);
    }

    /** Returns true if SkPoint is equivalent to SkPoint constructed from (x, y).

        @param x  value compared with fX
        @param y  value compared with fY
        @return   true if SkPoint equals (x, y)
    */
    bool equals(float x, float y) const {
        return fX == x && fY == y;
    }

    /** Returns true if a is equivalent to b.

        @param a  SkPoint to compare
        @param b  SkPoint to compare
        @return   true if a.fX == b.fX and a.fY == b.fY
    */
    friend bool operator==(const SkPoint& a, const SkPoint& b) {
        return a.fX == b.fX && a.fY == b.fY;
    }

    /** Returns true if a is not equivalent to b.

        @param a  SkPoint to compare
        @param b  SkPoint to compare
        @return   true if a.fX != b.fX or a.fY != b.fY
    */
    friend bool operator!=(const SkPoint& a, const SkPoint& b) {
        return a.fX != b.fX || a.fY != b.fY;
    }

    /** Returns vector from b to a, computed as (a.fX - b.fX, a.fY - b.fY).

        Can also be used to subtract vector from SkPoint, returning SkPoint.
        Can also be used to subtract vector from vector, returning vector.

        @param a  SkPoint to subtract from
        @param b  SkPoint to subtract
        @return   vector from b to a
    */
    friend SkVector operator-(const SkPoint& a, const SkPoint& b) {
        return {a.fX - b.fX, a.fY - b.fY};
    }

    /** Returns SkPoint resulting from SkPoint a offset by vector b, computed as:
        (a.fX + b.fX, a.fY + b.fY).

        Can also be used to offset SkPoint b by vector a, returning SkPoint.
        Can also be used to add vector to vector, returning vector.

        @param a  SkPoint or vector to add to
        @param b  SkPoint or vector to add
        @return   SkPoint equal to a offset by b
    */
    friend SkPoint operator+(const SkPoint& a, const SkVector& b) {
        return {a.fX + b.fX, a.fY + b.fY};
    }

    /** Returns the Euclidean distance from origin, computed as:

            sqrt(x * x + y * y)

        .

        @param x  component of length
        @param y  component of length
        @return   straight-line distance to origin

        example: https://fiddle.skia.org/c/@Point_Length
    */
    static float Length(float x, float y);

    /** Scales (vec->fX, vec->fY) so that length() returns one, while preserving ratio of vec->fX
        to vec->fY, if possible. If original length is nearly zero, sets vec to (0, 0) and returns
        zero; otherwise, returns length of vec before vec is scaled.

        Returned prior length may be INFINITY if it can not be represented by float.

        Note that normalize() is faster if prior length is not required.

        @param vec  normalized to unit length
        @return     original vec length

        example: https://fiddle.skia.org/c/@Point_Normalize
    */
    static float Normalize(SkVector* vec);

    /** Returns the Euclidean distance between a and b.

        @param a  line end point
        @param b  line end point
        @return   straight-line distance from a to b
    */
    static float Distance(const SkPoint& a, const SkPoint& b) {
        return Length(a.fX - b.fX, a.fY - b.fY);
    }

    /** Returns the dot product of vector a and vector b.

        @param a  left side of dot product
        @param b  right side of dot product
        @return   product of input magnitudes and cosine of the angle between them
    */
    static float DotProduct(const SkVector& a, const SkVector& b) {
        return a.fX * b.fX + a.fY * b.fY;
    }

    /** Returns the cross product of vector a and vector b.

        a and b form three-dimensional vectors with z-axis value equal to zero. The
        cross product is a three-dimensional vector with x-axis and y-axis values equal
        to zero. The cross product z-axis component is returned.

        @param a  left side of cross product
        @param b  right side of cross product
        @return   area spanned by vectors signed by angle direction
    */
    static float CrossProduct(const SkVector& a, const SkVector& b) {
        return a.fX * b.fY - a.fY * b.fX;
    }

    /** Returns the cross product of vector and vec.

        Vector and vec form three-dimensional vectors with z-axis value equal to zero.
        The cross product is a three-dimensional vector with x-axis and y-axis values
        equal to zero. The cross product z-axis component is returned.

        @param vec  right side of cross product
        @return     area spanned by vectors signed by angle direction
    */
    float cross(const SkVector& vec) const {
        return CrossProduct(*this, vec);
    }

    /** Returns the dot product of vector and vector vec.

        @param vec  right side of dot product
        @return     product of input magnitudes and cosine of the angle between them
    */
    float dot(const SkVector& vec) const {
        return DotProduct(*this, vec);
    }

};

// SkPoint is part of the public API, but is also required by code in base. The following include
// forwarding allows SkPoint to participate in the API and for use by code in base.

/** Convert a sign-bit int (i.e. float interpreted as int) into a 2s compliement
    int. This also converts -0 (0x80000000) to 0. Doing this to a float allows
    it to be compared using normal C operators (<, <=, etc.)
*/
static inline int32_t SkSignBitTo2sCompliment(int32_t x) {
    if (x < 0) {
        x &= 0x7FFFFFFF;
        x = -x;
    }
    return x;
}

/** Convert a 2s compliment int to a sign-bit (i.e. int interpreted as float).
    This undoes the result of SkSignBitTo2sCompliment().
 */
static inline int32_t Sk2sComplimentToSignBit(int32_t x) {
    int sign = x >> 31;
    // make x positive
    x = (x ^ sign) - sign;
    // set the sign bit as needed
    x |= SkLeftShift(sign, 31);
    return x;
}

union SkFloatIntUnion {
    float   fFloat;
    int32_t fSignBitInt;
};

// Helper to see a float as its bit pattern (w/o aliasing warnings)
static inline int32_t SkFloat2Bits(float x) {
    SkFloatIntUnion data;
    data.fFloat = x;
    return data.fSignBitInt;
}

// Helper to see a bit pattern as a float (w/o aliasing warnings)
static inline float SkBits2Float(int32_t floatAsBits) {
    SkFloatIntUnion data;
    data.fSignBitInt = floatAsBits;
    return data.fFloat;
}

constexpr int32_t gFloatBits_exponent_mask = 0x7F800000;
constexpr int32_t gFloatBits_matissa_mask  = 0x007FFFFF;

static inline bool SkFloatBits_IsFinite(int32_t bits) {
    return (bits & gFloatBits_exponent_mask) != gFloatBits_exponent_mask;
}

static inline bool SkFloatBits_IsInf(int32_t bits) {
    return ((bits & gFloatBits_exponent_mask) == gFloatBits_exponent_mask) &&
            (bits & gFloatBits_matissa_mask) == 0;
}

/** Return the float as a 2s compliment int. Just to be used to compare floats
    to each other or against positive float-bit-constants (like 0). This does
    not return the int equivalent of the float, just something cheaper for
    compares-only.
 */
static inline int32_t SkFloatAs2sCompliment(float x) {
    return SkSignBitTo2sCompliment(SkFloat2Bits(x));
}

/** Return the 2s compliment int as a float. This undos the result of
    SkFloatAs2sCompliment
 */
static inline float Sk2sComplimentAsFloat(int32_t x) {
    return SkBits2Float(Sk2sComplimentToSignBit(x));
}

//  Scalar wrappers for float-bit routines

#define SkScalarAs2sCompliment(x)    SkFloatAs2sCompliment(x)

inline constexpr float SK_FloatSqrt2 = 1.41421356f;
inline constexpr float SK_FloatPI    = 3.14159265f;
inline constexpr double SK_DoublePI  = 3.14159265358979323846264338327950288;

static inline float sk_float_sqrt(float x) { return std::sqrt(x); }
static inline float sk_float_sin(float x) { return std::sin(x); }
static inline float sk_float_cos(float x) { return std::cos(x); }
static inline float sk_float_tan(float x) { return std::tan(x); }
static inline float sk_float_floor(float x) { return std::floor(x); }
static inline float sk_float_ceil(float x) { return std::ceil(x); }
static inline float sk_float_trunc(float x) { return std::trunc(x); }
static inline float sk_float_acos(float x) { return std::acos(x); }
static inline float sk_float_asin(float x) { return std::asin(x); }
static inline float sk_float_atan2(float y, float x) { return std::atan2(y,x); }
static inline float sk_float_abs(float x) { return std::fabs(x); }
static inline float sk_float_copysign(float x, float y) { return std::copysign(x, y); }
static inline float sk_float_mod(float x, float y) { return std::fmod(x,y); }
static inline float sk_float_pow(float x, float y) { return std::pow(x, y); }
static inline float sk_float_exp(float x) { return std::exp(x); }
static inline float sk_float_log(float x) { return std::log(x); }
static inline float sk_float_log2(float x) { return std::log2(x); }

static constexpr int sk_float_sgn(float x) {
    return (0.0f < x) - (x < 0.0f);
}

static constexpr float sk_float_degrees_to_radians(float degrees) {
    return degrees * (SK_FloatPI / 180);
}

static constexpr float sk_float_radians_to_degrees(float radians) {
    return radians * (180 / SK_FloatPI);
}

// floor(double+0.5) vs. floorf(float+0.5f) give comparable performance, but upcasting to double
// means tricky values like 0.49999997 and 2^24 get rounded correctly. If these were rounded
// as floatf(x + .5f), they would be 1 higher than expected.
#define sk_float_round(x) (float)sk_double_round((double)(x))

static inline bool sk_float_isfinite(float x) {
    return SkFloatBits_IsFinite(SkFloat2Bits(x));
}

static inline bool sk_floats_are_finite(float a, float b) {
    return sk_float_isfinite(a) && sk_float_isfinite(b);
}

static inline bool sk_floats_are_finite(const float array[], int count) {
    float prod = 0;
    for (int i = 0; i < count; ++i) {
        prod *= array[i];
    }
    // At this point, prod will either be NaN or 0
    return prod == 0;   // if prod is NaN, this check will return false
}

static inline bool sk_float_isinf(float x) {
    return SkFloatBits_IsInf(SkFloat2Bits(x));
}

static constexpr bool sk_float_isnan(float x) { return x != x; }
static constexpr bool sk_double_isnan(double x) { return x != x; }

inline constexpr int SK_MaxS32FitsInFloat = 2147483520;
inline constexpr int SK_MinS32FitsInFloat = -SK_MaxS32FitsInFloat;

// 0x7fffff8000000000
inline constexpr int64_t SK_MaxS64FitsInFloat = SK_MaxS64 >> (63-24) << (63-24);
inline constexpr int64_t SK_MinS64FitsInFloat = -SK_MaxS64FitsInFloat;

/**
 *  Return the closest int for the given float. Returns SK_MaxS32FitsInFloat for NaN.
 */
static constexpr int sk_float_saturate2int(float x) {
    x = x < SK_MaxS32FitsInFloat ? x : SK_MaxS32FitsInFloat;
    x = x > SK_MinS32FitsInFloat ? x : SK_MinS32FitsInFloat;
    return (int)x;
}

/**
 *  Return the closest int for the given double. Returns SK_MaxS32 for NaN.
 */
static constexpr int sk_double_saturate2int(double x) {
    x = x < SK_MaxS32 ? x : SK_MaxS32;
    x = x > SK_MinS32 ? x : SK_MinS32;
    return (int)x;
}

/**
 *  Return the closest int64_t for the given float. Returns SK_MaxS64FitsInFloat for NaN.
 */
static constexpr int64_t sk_float_saturate2int64(float x) {
    x = x < SK_MaxS64FitsInFloat ? x : SK_MaxS64FitsInFloat;
    x = x > SK_MinS64FitsInFloat ? x : SK_MinS64FitsInFloat;
    return (int64_t)x;
}

#define sk_float_floor2int(x)   sk_float_saturate2int(sk_float_floor(x))
#define sk_float_round2int(x)   sk_float_saturate2int(sk_float_round(x))
#define sk_float_ceil2int(x)    sk_float_saturate2int(sk_float_ceil(x))

#define sk_float_floor2int_no_saturate(x)   (int)sk_float_floor(x)
#define sk_float_round2int_no_saturate(x)   (int)sk_float_round(x)
#define sk_float_ceil2int_no_saturate(x)    (int)sk_float_ceil(x)

#define sk_double_floor(x)          floor(x)
#define sk_double_round(x)          floor((x) + 0.5)
#define sk_double_ceil(x)           ceil(x)
#define sk_double_floor2int(x)      (int)sk_double_floor(x)
#define sk_double_round2int(x)      (int)sk_double_round(x)
#define sk_double_ceil2int(x)       (int)sk_double_ceil(x)

// Cast double to float, ignoring any warning about too-large finite values being cast to float.
// Clang thinks this is undefined, but it's actually implementation defined to return either
// the largest float or infinity (one of the two bracketing representable floats).  Good enough!
SK_NO_SANITIZE("float-cast-overflow")
static constexpr float sk_double_to_float(double x) {
    return static_cast<float>(x);
}

inline constexpr float SK_FloatNaN = std::numeric_limits<float>::quiet_NaN();
inline constexpr float SK_FloatInfinity = std::numeric_limits<float>::infinity();
inline constexpr float SK_FloatNegativeInfinity = -SK_FloatInfinity;

inline constexpr double SK_DoubleNaN = std::numeric_limits<double>::quiet_NaN();

// Calculate the midpoint between a and b. Similar to std::midpoint in c++20.
static constexpr float sk_float_midpoint(float a, float b) {
    // Use double math to avoid underflow and overflow.
    return static_cast<float>(0.5 * (static_cast<double>(a) + b));
}

// Returns false if any of the floats are outside the range [0...1].
// Returns true if count is 0.
bool sk_floats_are_unit(const float array[], size_t count);

static inline float sk_float_rsqrt_portable(float x) { return 1.0f / sk_float_sqrt(x); }
static inline float sk_float_rsqrt         (float x) { return 1.0f / sk_float_sqrt(x); }

// The number of significant digits to print.
inline constexpr int SK_FLT_DECIMAL_DIG = std::numeric_limits<float>::max_digits10;

// IEEE defines how float divide behaves for non-finite values and zero-denoms, but C does not,
// so we have a helper that suppresses the possible undefined-behavior warnings.
SK_NO_SANITIZE("float-divide-by-zero")
static constexpr float sk_ieee_float_divide(float numer, float denom) {
    return numer / denom;
}

SK_NO_SANITIZE("float-divide-by-zero")
static constexpr double sk_ieee_double_divide(double numer, double denom) {
    return numer / denom;
}

// Return a*b + c.
static inline float sk_fmaf(float a, float b, float c) {
    return std::fma(a, b, c);
}

// Returns true iff the provided number is within a small epsilon of 0.
bool sk_double_nearly_zero(double a);

// Compare two doubles and return true if they are within maxUlpsDiff of each other.
// * nan as a or b - returns false.
// * infinity, infinity or -infinity, -infinity - returns true.
// * infinity and any other number - returns false.
//
// ulp is an initialism for Units in the Last Place.
bool sk_doubles_nearly_equal_ulps(double a, double b, uint8_t maxUlpsDiff=16);

typedef float SkScalar;

#define SK_Scalar1                  1.0f
#define SK_ScalarHalf               0.5f
#define SK_ScalarSqrt2              SK_FloatSqrt2
#define SK_ScalarPI                 SK_FloatPI
#define SK_ScalarTanPIOver8         0.414213562f
#define SK_ScalarRoot2Over2         0.707106781f
#define SK_ScalarMax                3.402823466e+38f
#define SK_ScalarMin                (-SK_ScalarMax)
#define SK_ScalarInfinity           SK_FloatInfinity
#define SK_ScalarNegativeInfinity   SK_FloatNegativeInfinity
#define SK_ScalarNaN                SK_FloatNaN

#define SkScalarFloorToScalar(x)    sk_float_floor(x)
#define SkScalarCeilToScalar(x)     sk_float_ceil(x)
#define SkScalarRoundToScalar(x)    sk_float_round(x)
#define SkScalarTruncToScalar(x)    sk_float_trunc(x)

#define SkScalarFloorToInt(x)       sk_float_floor2int(x)
#define SkScalarCeilToInt(x)        sk_float_ceil2int(x)
#define SkScalarRoundToInt(x)       sk_float_round2int(x)

#define SkScalarAbs(x)              sk_float_abs(x)
#define SkScalarCopySign(x, y)      sk_float_copysign(x, y)
#define SkScalarMod(x, y)           sk_float_mod(x,y)
#define SkScalarSqrt(x)             sk_float_sqrt(x)
#define SkScalarPow(b, e)           sk_float_pow(b, e)

#define SkScalarSin(radians)        (float)sk_float_sin(radians)
#define SkScalarCos(radians)        (float)sk_float_cos(radians)
#define SkScalarTan(radians)        (float)sk_float_tan(radians)
#define SkScalarASin(val)           (float)sk_float_asin(val)
#define SkScalarACos(val)           (float)sk_float_acos(val)
#define SkScalarATan2(y, x)         (float)sk_float_atan2(y,x)
#define SkScalarExp(x)              (float)sk_float_exp(x)
#define SkScalarLog(x)              (float)sk_float_log(x)
#define SkScalarLog2(x)             (float)sk_float_log2(x)

//////////////////////////////////////////////////////////////////////////////////////////////////

#define SkIntToScalar(x)        static_cast<SkScalar>(x)
#define SkIntToFloat(x)         static_cast<float>(x)
#define SkScalarTruncToInt(x)   sk_float_saturate2int(x)

#define SkScalarToFloat(x)      static_cast<float>(x)
#define SkFloatToScalar(x)      static_cast<SkScalar>(x)
#define SkScalarToDouble(x)     static_cast<double>(x)
#define SkDoubleToScalar(x)     sk_double_to_float(x)

static inline bool SkScalarIsNaN(SkScalar x) { return x != x; }

/** Returns true if x is not NaN and not infinite
 */
static inline bool SkScalarIsFinite(SkScalar x) { return sk_float_isfinite(x); }

static inline bool SkScalarsAreFinite(SkScalar a, SkScalar b) {
    return sk_floats_are_finite(a, b);
}

static inline bool SkScalarsAreFinite(const SkScalar array[], int count) {
    return sk_floats_are_finite(array, count);
}

/** Returns the fractional part of the scalar. */
static inline SkScalar SkScalarFraction(SkScalar x) {
    return x - SkScalarTruncToScalar(x);
}

static inline SkScalar SkScalarSquare(SkScalar x) { return x * x; }

#define SkScalarInvert(x)           (SK_Scalar1 / (x))
#define SkScalarAve(a, b)           (((a) + (b)) * SK_ScalarHalf)
#define SkScalarHalf(a)             ((a) * SK_ScalarHalf)

#define SkDegreesToRadians(degrees) ((degrees) * (SK_ScalarPI / 180))
#define SkRadiansToDegrees(radians) ((radians) * (180 / SK_ScalarPI))

static inline bool SkScalarIsInt(SkScalar x) {
    return x == SkScalarFloorToScalar(x);
}

/**
 *  Returns -1 || 0 || 1 depending on the sign of value:
 *  -1 if x < 0
 *   0 if x == 0
 *   1 if x > 0
 */
static inline int SkScalarSignAsInt(SkScalar x) {
    return x < 0 ? -1 : (x > 0);
}

// Scalar result version of above
static inline SkScalar SkScalarSignAsScalar(SkScalar x) {
    return x < 0 ? -SK_Scalar1 : ((x > 0) ? SK_Scalar1 : 0);
}

#define SK_ScalarNearlyZero         (SK_Scalar1 / (1 << 12))

static inline bool SkScalarNearlyZero(SkScalar x,
                                      SkScalar tolerance = SK_ScalarNearlyZero) {
    SkASSERT(tolerance >= 0);
    return SkScalarAbs(x) <= tolerance;
}

static inline bool SkScalarNearlyEqual(SkScalar x, SkScalar y,
                                       SkScalar tolerance = SK_ScalarNearlyZero) {
    SkASSERT(tolerance >= 0);
    return SkScalarAbs(x-y) <= tolerance;
}

#define SK_ScalarSinCosNearlyZero   (SK_Scalar1 / (1 << 16))

static inline float SkScalarSinSnapToZero(SkScalar radians) {
    float v = SkScalarSin(radians);
    return SkScalarNearlyZero(v, SK_ScalarSinCosNearlyZero) ? 0.0f : v;
}

static inline float SkScalarCosSnapToZero(SkScalar radians) {
    float v = SkScalarCos(radians);
    return SkScalarNearlyZero(v, SK_ScalarSinCosNearlyZero) ? 0.0f : v;
}

/** Linearly interpolate between A and B, based on t.
    If t is 0, return A
    If t is 1, return B
    else interpolate.
    t must be [0..SK_Scalar1]
*/
static inline SkScalar SkScalarInterp(SkScalar A, SkScalar B, SkScalar t) {
    SkASSERT(t >= 0 && t <= SK_Scalar1);
    return A + (B - A) * t;
}

/** Interpolate along the function described by (keys[length], values[length])
    for the passed searchKey. SearchKeys outside the range keys[0]-keys[Length]
    clamp to the min or max value. This function assumes the number of pairs
    (length) will be small and a linear search is used.

    Repeated keys are allowed for discontinuous functions (so long as keys is
    monotonically increasing). If key is the value of a repeated scalar in
    keys the first one will be used.
*/
SkScalar SkScalarInterpFunc(SkScalar searchKey, const SkScalar keys[],
                            const SkScalar values[], int length);

/*
 *  Helper to compare an array of scalars.
 */
static inline bool SkScalarsEqual(const SkScalar a[], const SkScalar b[], int n) {
    SkASSERT(n >= 0);
    for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

struct SkISize {
    int32_t fWidth;
    int32_t fHeight;

    static constexpr SkISize Make(int32_t w, int32_t h) { return {w, h}; }

    static constexpr SkISize MakeEmpty() { return {0, 0}; }

    void set(int32_t w, int32_t h) { *this = SkISize{w, h}; }

    /** Returns true iff fWidth == 0 && fHeight == 0
     */
    bool isZero() const { return 0 == fWidth && 0 == fHeight; }

    /** Returns true if either width or height are <= 0 */
    bool isEmpty() const { return fWidth <= 0 || fHeight <= 0; }

    /** Set the width and height to 0 */
    void setEmpty() { fWidth = fHeight = 0; }

    constexpr int32_t width() const { return fWidth; }
    constexpr int32_t height() const { return fHeight; }

    constexpr int64_t area() const { return fWidth * fHeight; }

    bool equals(int32_t w, int32_t h) const { return fWidth == w && fHeight == h; }
};

static inline bool operator==(const SkISize& a, const SkISize& b) {
    return a.fWidth == b.fWidth && a.fHeight == b.fHeight;
}

static inline bool operator!=(const SkISize& a, const SkISize& b) { return !(a == b); }

///////////////////////////////////////////////////////////////////////////////

struct SkSize {
    SkScalar fWidth;
    SkScalar fHeight;

    static constexpr SkSize Make(SkScalar w, SkScalar h) { return {w, h}; }

    static constexpr SkSize Make(const SkISize& src) {
        return {SkIntToScalar(src.width()), SkIntToScalar(src.height())};
    }

    static constexpr SkSize MakeEmpty() { return {0, 0}; }

    void set(SkScalar w, SkScalar h) { *this = SkSize{w, h}; }

    /** Returns true iff fWidth == 0 && fHeight == 0
     */
    bool isZero() const { return 0 == fWidth && 0 == fHeight; }

    /** Returns true if either width or height are <= 0 */
    bool isEmpty() const { return fWidth <= 0 || fHeight <= 0; }

    /** Set the width and height to 0 */
    void setEmpty() { *this = SkSize{0, 0}; }

    SkScalar width() const { return fWidth; }
    SkScalar height() const { return fHeight; }

    bool equals(SkScalar w, SkScalar h) const { return fWidth == w && fHeight == h; }

    SkISize toRound() const { return {SkScalarRoundToInt(fWidth), SkScalarRoundToInt(fHeight)}; }

    SkISize toCeil() const { return {SkScalarCeilToInt(fWidth), SkScalarCeilToInt(fHeight)}; }

    SkISize toFloor() const { return {SkScalarFloorToInt(fWidth), SkScalarFloorToInt(fHeight)}; }
};

static inline bool operator==(const SkSize& a, const SkSize& b) {
    return a.fWidth == b.fWidth && a.fHeight == b.fHeight;
}

static inline bool operator!=(const SkSize& a, const SkSize& b) { return !(a == b); }

// All of these files should be independent of things users can set via the user config file.
// They should also be able to be included in any order.
// IWYU pragma: begin_exports

// Load and verify defines from the user config file.

// Any includes or defines below can be configured by the user config file.
// IWYU pragma: end_exports


#if !defined(SK_GANESH) && !defined(SK_GRAPHITE)
#  undef SK_GL
#  undef SK_VULKAN
#  undef SK_METAL
#  undef SK_DAWN
#  undef SK_DIRECT3D
#endif

// If SK_R32_SHIFT is set, we'll use that to choose RGBA or BGRA.
// If not, we'll default to RGBA everywhere except BGRA on Windows.
#if defined(SK_R32_SHIFT)
    static_assert(SK_R32_SHIFT == 0 || SK_R32_SHIFT == 16, "");
#elif defined(SK_BUILD_FOR_WIN)
    #define SK_R32_SHIFT 16
#else
    #define SK_R32_SHIFT 0
#endif

#if defined(SK_B32_SHIFT)
    static_assert(SK_B32_SHIFT == (16-SK_R32_SHIFT), "");
#else
    #define SK_B32_SHIFT (16-SK_R32_SHIFT)
#endif

#define SK_G32_SHIFT 8
#define SK_A32_SHIFT 24

/**
 * SK_PMCOLOR_BYTE_ORDER can be used to query the byte order of SkPMColor at compile time.
 */
#ifdef SK_CPU_BENDIAN
#  define SK_PMCOLOR_BYTE_ORDER(C0, C1, C2, C3)     \
        (SK_ ## C3 ## 32_SHIFT == 0  &&             \
         SK_ ## C2 ## 32_SHIFT == 8  &&             \
         SK_ ## C1 ## 32_SHIFT == 16 &&             \
         SK_ ## C0 ## 32_SHIFT == 24)
#else
#  define SK_PMCOLOR_BYTE_ORDER(C0, C1, C2, C3)     \
        (SK_ ## C0 ## 32_SHIFT == 0  &&             \
         SK_ ## C1 ## 32_SHIFT == 8  &&             \
         SK_ ## C2 ## 32_SHIFT == 16 &&             \
         SK_ ## C3 ## 32_SHIFT == 24)
#endif

#if defined SK_DEBUG && defined SK_BUILD_FOR_WIN
    #ifdef free
        #undef free
    #endif
    #undef free
#endif

#ifndef SK_ALLOW_STATIC_GLOBAL_INITIALIZERS
    #define SK_ALLOW_STATIC_GLOBAL_INITIALIZERS 0
#endif

#if !defined(SK_GAMMA_EXPONENT)
    #define SK_GAMMA_EXPONENT (0.0f)  // SRGB
#endif

#if defined(SK_HISTOGRAM_ENUMERATION)  || \
    defined(SK_HISTOGRAM_BOOLEAN)      || \
    defined(SK_HISTOGRAM_EXACT_LINEAR) || \
    defined(SK_HISTOGRAM_MEMORY_KB)
#  define SK_HISTOGRAMS_ENABLED 1
#else
#  define SK_HISTOGRAMS_ENABLED 0
#endif

#ifndef SK_HISTOGRAM_BOOLEAN
#  define SK_HISTOGRAM_BOOLEAN(name, sample)
#endif

#ifndef SK_HISTOGRAM_ENUMERATION
#  define SK_HISTOGRAM_ENUMERATION(name, sample, enum_size)
#endif

#ifndef SK_HISTOGRAM_EXACT_LINEAR
#  define SK_HISTOGRAM_EXACT_LINEAR(name, sample, value_max)
#endif

#ifndef SK_HISTOGRAM_MEMORY_KB
#  define SK_HISTOGRAM_MEMORY_KB(name, sample)
#endif

#define SK_HISTOGRAM_PERCENTAGE(name, percent_as_int) \
    SK_HISTOGRAM_EXACT_LINEAR(name, percent_as_int, 101)

// The top-level define SK_ENABLE_OPTIMIZE_SIZE can be used to remove several large features at once
#if defined(SK_ENABLE_OPTIMIZE_SIZE)
    #if !defined(SK_FORCE_RASTER_PIPELINE_BLITTER)
        #define SK_FORCE_RASTER_PIPELINE_BLITTER
    #endif
    #define SK_DISABLE_SDF_TEXT
#endif

#ifndef SK_DISABLE_LEGACY_SHADERCONTEXT
#   define SK_ENABLE_LEGACY_SHADERCONTEXT
#endif

#if defined(SK_BUILD_FOR_LIBFUZZER) || defined(SK_BUILD_FOR_AFL_FUZZ)
#if !defined(SK_BUILD_FOR_FUZZER)
    #define SK_BUILD_FOR_FUZZER
#endif
#endif

/**
 *  These defines are set to 0 or 1, rather than being undefined or defined
 *  TODO: consider updating these for consistency
 */

#if !defined(GR_CACHE_STATS)
  #if defined(SK_DEBUG) || defined(SK_DUMP_STATS)
      #define GR_CACHE_STATS  1
  #else
      #define GR_CACHE_STATS  0
  #endif
#endif

#if !defined(GR_GPU_STATS)
  #if defined(SK_DEBUG) || defined(SK_DUMP_STATS) || defined(GR_TEST_UTILS)
      #define GR_GPU_STATS    1
  #else
      #define GR_GPU_STATS    0
  #endif
#endif

////////////////////////////////////////////////////////////////////////////////

typedef uint32_t SkFourByteTag;
static inline constexpr SkFourByteTag SkSetFourByteTag(char a, char b, char c, char d) {
    return (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d);
}

////////////////////////////////////////////////////////////////////////////////

/** 32 bit integer to hold a unicode value
*/
typedef int32_t SkUnichar;

/** 16 bit unsigned integer to hold a glyph index
*/
typedef uint16_t SkGlyphID;

/** 32 bit value to hold a millisecond duration
    Note that SK_MSecMax is about 25 days.
*/
typedef uint32_t SkMSec;

/** Maximum representable milliseconds; 24d 20h 31m 23.647s.
*/
static constexpr SkMSec SK_MSecMax = INT32_MAX;

/** The generation IDs in Skia reserve 0 has an invalid marker.
*/
static constexpr uint32_t SK_InvalidGenID = 0;

/** The unique IDs in Skia reserve 0 has an invalid marker.
*/
static constexpr uint32_t SK_InvalidUniqueID = 0;

/**
 * std::underlying_type is only defined for enums. For integral types, we just want the type.
 */
template <typename T, class Enable = void>
struct sk_strip_enum {
    typedef T type;
};

template <typename T>
struct sk_strip_enum<T, typename std::enable_if<std::is_enum<T>::value>::type> {
    typedef typename std::underlying_type<T>::type type;
};


/**
 * In C++ an unsigned to signed cast where the source value cannot be represented in the destination
 * type results in an implementation defined destination value. Unlike C, C++ does not allow a trap.
 * This makes "(S)(D)s == s" a possibly useful test. However, there are two cases where this is
 * incorrect:
 *
 * when testing if a value of a smaller signed type can be represented in a larger unsigned type
 * (int8_t)(uint16_t)-1 == -1 => (int8_t)0xFFFF == -1 => [implementation defined] == -1
 *
 * when testing if a value of a larger unsigned type can be represented in a smaller signed type
 * (uint16_t)(int8_t)0xFFFF == 0xFFFF => (uint16_t)-1 == 0xFFFF => 0xFFFF == 0xFFFF => true.
 *
 * Consider the cases:
 *   u = unsigned, less digits
 *   U = unsigned, more digits
 *   s = signed, less digits
 *   S = signed, more digits
 *   v is the value we're considering.
 *
 * u -> U: (u)(U)v == v, trivially true
 * U -> u: (U)(u)v == v, both casts well defined, test works
 * s -> S: (s)(S)v == v, trivially true
 * S -> s: (S)(s)v == v, first cast implementation value, second cast defined, test works
 * s -> U: (s)(U)v == v, *this is bad*, the second cast results in implementation defined value
 * S -> u: (S)(u)v == v, the second cast is required to prevent promotion of rhs to unsigned
 * u -> S: (u)(S)v == v, trivially true
 * U -> s: (U)(s)v == v, *this is bad*,
 *                             first cast results in implementation defined value,
 *                             second cast is defined. However, this creates false positives
 *                             uint16_t x = 0xFFFF
 *                                (uint16_t)(int8_t)x == x
 *                             => (uint16_t)-1        == x
 *                             => 0xFFFF              == x
 *                             => true
 *
 * So for the eight cases three are trivially true, three more are valid casts, and two are special.
 * The two 'full' checks which otherwise require two comparisons are valid cast checks.
 * The two remaining checks s -> U [v >= 0] and U -> s [v <= max(s)] can be done with one op.
 */

template <typename D, typename S>
static constexpr inline
typename std::enable_if<(std::is_integral<S>::value || std::is_enum<S>::value) &&
                        (std::is_integral<D>::value || std::is_enum<D>::value), bool>::type
/*bool*/ SkTFitsIn(S src) {
    // Ensure that is_signed and is_unsigned are passed the arithmetic underlyng types of enums.
    using Sa = typename sk_strip_enum<S>::type;
    using Da = typename sk_strip_enum<D>::type;

    // SkTFitsIn() is used in public headers, so needs to be written targeting at most C++11.
    return

    // E.g. (int8_t)(uint8_t) int8_t(-1) == -1, but the uint8_t == 255, not -1.
    (std::is_signed<Sa>::value && std::is_unsigned<Da>::value && sizeof(Sa) <= sizeof(Da)) ?
        (S)0 <= src :

    // E.g. (uint8_t)(int8_t) uint8_t(255) == 255, but the int8_t == -1.
    (std::is_signed<Da>::value && std::is_unsigned<Sa>::value && sizeof(Da) <= sizeof(Sa)) ?
        src <= (S)std::numeric_limits<Da>::max() :

#if !defined(SK_DEBUG) && !defined(__MSVC_RUNTIME_CHECKS )
    // Correct (simple) version. This trips up MSVC's /RTCc run-time checking.
    (S)(D)src == src;
#else
    // More complex version that's safe with /RTCc. Used in all debug builds, for coverage.
    (std::is_signed<Sa>::value) ?
        (intmax_t)src >= (intmax_t)std::numeric_limits<Da>::min() &&
        (intmax_t)src <= (intmax_t)std::numeric_limits<Da>::max() :

    // std::is_unsigned<S> ?
        (uintmax_t)src <= (uintmax_t)std::numeric_limits<Da>::max();
#endif
}

struct SkRect;

/** \struct SkIRect
    SkIRect holds four 32-bit integer coordinates describing the upper and
    lower bounds of a rectangle. SkIRect may be created from outer bounds or
    from position, width, and height. SkIRect describes an area; if its right
    is less than or equal to its left, or if its bottom is less than or equal to
    its top, it is considered empty.
*/
struct SK_API SkIRect {
    int32_t fLeft   = 0; //!< smaller x-axis bounds
    int32_t fTop    = 0; //!< smaller y-axis bounds
    int32_t fRight  = 0; //!< larger x-axis bounds
    int32_t fBottom = 0; //!< larger y-axis bounds

    /** Returns constructed SkIRect set to (0, 0, 0, 0).
        Many other rectangles are empty; if left is equal to or greater than right,
        or if top is equal to or greater than bottom. Setting all members to zero
        is a convenience, but does not designate a special empty rectangle.

        @return  bounds (0, 0, 0, 0)
    */
    [[nodiscard]] static constexpr SkIRect MakeEmpty() {
        return SkIRect{0, 0, 0, 0};
    }

    /** Returns constructed SkIRect set to (0, 0, w, h). Does not validate input; w or h
        may be negative.

        @param w  width of constructed SkIRect
        @param h  height of constructed SkIRect
        @return   bounds (0, 0, w, h)
    */
    [[nodiscard]] static constexpr SkIRect MakeWH(int32_t w, int32_t h) {
        return SkIRect{0, 0, w, h};
    }

    /** Returns constructed SkIRect set to (0, 0, size.width(), size.height()).
        Does not validate input; size.width() or size.height() may be negative.

        @param size  values for SkIRect width and height
        @return      bounds (0, 0, size.width(), size.height())
    */
    [[nodiscard]] static constexpr SkIRect MakeSize(const SkISize& size) {
        return SkIRect{0, 0, size.fWidth, size.fHeight};
    }

    /** Returns constructed SkIRect set to (pt.x(), pt.y(), pt.x() + size.width(),
        pt.y() + size.height()). Does not validate input; size.width() or size.height() may be
        negative.

        @param pt    values for SkIRect fLeft and fTop
        @param size  values for SkIRect width and height
        @return      bounds at pt with width and height of size
    */
    [[nodiscard]] static constexpr SkIRect MakePtSize(SkIPoint pt, SkISize size) {
        return MakeXYWH(pt.x(), pt.y(), size.width(), size.height());
    }

    /** Returns constructed SkIRect set to (l, t, r, b). Does not sort input; SkIRect may
        result in fLeft greater than fRight, or fTop greater than fBottom.

        @param l  integer stored in fLeft
        @param t  integer stored in fTop
        @param r  integer stored in fRight
        @param b  integer stored in fBottom
        @return   bounds (l, t, r, b)
    */
    [[nodiscard]] static constexpr SkIRect MakeLTRB(int32_t l, int32_t t, int32_t r, int32_t b) {
        return SkIRect{l, t, r, b};
    }

    /** Returns constructed SkIRect set to: (x, y, x + w, y + h).
        Does not validate input; w or h may be negative.

        @param x  stored in fLeft
        @param y  stored in fTop
        @param w  added to x and stored in fRight
        @param h  added to y and stored in fBottom
        @return   bounds at (x, y) with width w and height h
    */
    [[nodiscard]] static constexpr SkIRect MakeXYWH(int32_t x, int32_t y, int32_t w, int32_t h) {
        return { x, y, Sk32_sat_add(x, w), Sk32_sat_add(y, h) };
    }

    /** Returns left edge of SkIRect, if sorted.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fLeft
    */
    constexpr int32_t left() const { return fLeft; }

    /** Returns top edge of SkIRect, if sorted. Call isEmpty() to see if SkIRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fTop
    */
    constexpr int32_t top() const { return fTop; }

    /** Returns right edge of SkIRect, if sorted.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fRight
    */
    constexpr int32_t right() const { return fRight; }

    /** Returns bottom edge of SkIRect, if sorted. Call isEmpty() to see if SkIRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fBottom
    */
    constexpr int32_t bottom() const { return fBottom; }

    /** Returns left edge of SkIRect, if sorted. Call isEmpty() to see if SkIRect may be invalid,
        and sort() to reverse fLeft and fRight if needed.

        @return  fLeft
    */
    constexpr int32_t x() const { return fLeft; }

    /** Returns top edge of SkIRect, if sorted. Call isEmpty() to see if SkIRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fTop
    */
    constexpr int32_t y() const { return fTop; }

    // Experimental
    constexpr SkIPoint topLeft() const { return {fLeft, fTop}; }

    /** Returns span on the x-axis. This does not check if SkIRect is sorted, or if
        result fits in 32-bit signed integer; result may be negative.

        @return  fRight minus fLeft
    */
    constexpr int32_t width() const { return Sk32_can_overflow_sub(fRight, fLeft); }

    /** Returns span on the y-axis. This does not check if SkIRect is sorted, or if
        result fits in 32-bit signed integer; result may be negative.

        @return  fBottom minus fTop
    */
    constexpr int32_t height() const { return Sk32_can_overflow_sub(fBottom, fTop); }

    /** Returns spans on the x-axis and y-axis. This does not check if SkIRect is sorted,
        or if result fits in 32-bit signed integer; result may be negative.

        @return  SkISize (width, height)
    */
    constexpr SkISize size() const { return SkISize::Make(this->width(), this->height()); }

    /** Returns span on the x-axis. This does not check if SkIRect is sorted, so the
        result may be negative. This is safer than calling width() since width() might
        overflow in its calculation.

        @return  fRight minus fLeft cast to int64_t
    */
    constexpr int64_t width64() const { return (int64_t)fRight - (int64_t)fLeft; }

    /** Returns span on the y-axis. This does not check if SkIRect is sorted, so the
        result may be negative. This is safer than calling height() since height() might
        overflow in its calculation.

        @return  fBottom minus fTop cast to int64_t
    */
    constexpr int64_t height64() const { return (int64_t)fBottom - (int64_t)fTop; }

    /** Returns true if fLeft is equal to or greater than fRight, or if fTop is equal
        to or greater than fBottom. Call sort() to reverse rectangles with negative
        width64() or height64().

        @return  true if width64() or height64() are zero or negative
    */
    bool isEmpty64() const { return fRight <= fLeft || fBottom <= fTop; }

    /** Returns true if width() or height() are zero or negative.

        @return  true if width() or height() are zero or negative
    */
    bool isEmpty() const {
        int64_t w = this->width64();
        int64_t h = this->height64();
        if (w <= 0 || h <= 0) {
            return true;
        }
        // Return true if either exceeds int32_t
        return !SkTFitsIn<int32_t>(w | h);
    }

    /** Returns true if all members in a: fLeft, fTop, fRight, and fBottom; are
        identical to corresponding members in b.

        @param a  SkIRect to compare
        @param b  SkIRect to compare
        @return   true if members are equal
    */
    friend bool operator==(const SkIRect& a, const SkIRect& b) {
        return a.fLeft == b.fLeft && a.fTop == b.fTop &&
               a.fRight == b.fRight && a.fBottom == b.fBottom;
    }

    /** Returns true if any member in a: fLeft, fTop, fRight, and fBottom; is not
        identical to the corresponding member in b.

        @param a  SkIRect to compare
        @param b  SkIRect to compare
        @return   true if members are not equal
    */
    friend bool operator!=(const SkIRect& a, const SkIRect& b) {
        return a.fLeft != b.fLeft || a.fTop != b.fTop ||
               a.fRight != b.fRight || a.fBottom != b.fBottom;
    }

    /** Sets SkIRect to (0, 0, 0, 0).

        Many other rectangles are empty; if left is equal to or greater than right,
        or if top is equal to or greater than bottom. Setting all members to zero
        is a convenience, but does not designate a special empty rectangle.
    */
    void setEmpty() { memset(this, 0, sizeof(*this)); }

    /** Sets SkIRect to (left, top, right, bottom).
        left and right are not sorted; left is not necessarily less than right.
        top and bottom are not sorted; top is not necessarily less than bottom.

        @param left    stored in fLeft
        @param top     stored in fTop
        @param right   stored in fRight
        @param bottom  stored in fBottom
    */
    void setLTRB(int32_t left, int32_t top, int32_t right, int32_t bottom) {
        fLeft   = left;
        fTop    = top;
        fRight  = right;
        fBottom = bottom;
    }

    /** Sets SkIRect to: (x, y, x + width, y + height).
        Does not validate input; width or height may be negative.

        @param x       stored in fLeft
        @param y       stored in fTop
        @param width   added to x and stored in fRight
        @param height  added to y and stored in fBottom
    */
    void setXYWH(int32_t x, int32_t y, int32_t width, int32_t height) {
        fLeft   = x;
        fTop    = y;
        fRight  = Sk32_sat_add(x, width);
        fBottom = Sk32_sat_add(y, height);
    }

    void setWH(int32_t width, int32_t height) {
        fLeft   = 0;
        fTop    = 0;
        fRight  = width;
        fBottom = height;
    }

    void setSize(SkISize size) {
        fLeft = 0;
        fTop = 0;
        fRight = size.width();
        fBottom = size.height();
    }

    /** Returns SkIRect offset by (dx, dy).

        If dx is negative, SkIRect returned is moved to the left.
        If dx is positive, SkIRect returned is moved to the right.
        If dy is negative, SkIRect returned is moved upward.
        If dy is positive, SkIRect returned is moved downward.

        @param dx  offset added to fLeft and fRight
        @param dy  offset added to fTop and fBottom
        @return    SkIRect offset by dx and dy, with original width and height
    */
    constexpr SkIRect makeOffset(int32_t dx, int32_t dy) const {
        return {
            Sk32_sat_add(fLeft,  dx), Sk32_sat_add(fTop,    dy),
            Sk32_sat_add(fRight, dx), Sk32_sat_add(fBottom, dy),
        };
    }

    /** Returns SkIRect offset by (offset.x(), offset.y()).

        If offset.x() is negative, SkIRect returned is moved to the left.
        If offset.x() is positive, SkIRect returned is moved to the right.
        If offset.y() is negative, SkIRect returned is moved upward.
        If offset.y() is positive, SkIRect returned is moved downward.

        @param offset  translation vector
        @return    SkIRect translated by offset, with original width and height
    */
    constexpr SkIRect makeOffset(SkIVector offset) const {
        return this->makeOffset(offset.x(), offset.y());
    }

    /** Returns SkIRect, inset by (dx, dy).

        If dx is negative, SkIRect returned is wider.
        If dx is positive, SkIRect returned is narrower.
        If dy is negative, SkIRect returned is taller.
        If dy is positive, SkIRect returned is shorter.

        @param dx  offset added to fLeft and subtracted from fRight
        @param dy  offset added to fTop and subtracted from fBottom
        @return    SkIRect inset symmetrically left and right, top and bottom
    */
    SkIRect makeInset(int32_t dx, int32_t dy) const {
        return {
            Sk32_sat_add(fLeft,  dx), Sk32_sat_add(fTop,    dy),
            Sk32_sat_sub(fRight, dx), Sk32_sat_sub(fBottom, dy),
        };
    }

    /** Returns SkIRect, outset by (dx, dy).

        If dx is negative, SkIRect returned is narrower.
        If dx is positive, SkIRect returned is wider.
        If dy is negative, SkIRect returned is shorter.
        If dy is positive, SkIRect returned is taller.

        @param dx  offset subtracted to fLeft and added from fRight
        @param dy  offset subtracted to fTop and added from fBottom
        @return    SkIRect outset symmetrically left and right, top and bottom
    */
    SkIRect makeOutset(int32_t dx, int32_t dy) const {
        return {
            Sk32_sat_sub(fLeft,  dx), Sk32_sat_sub(fTop,    dy),
            Sk32_sat_add(fRight, dx), Sk32_sat_add(fBottom, dy),
        };
    }

    /** Offsets SkIRect by adding dx to fLeft, fRight; and by adding dy to fTop, fBottom.

        If dx is negative, moves SkIRect returned to the left.
        If dx is positive, moves SkIRect returned to the right.
        If dy is negative, moves SkIRect returned upward.
        If dy is positive, moves SkIRect returned downward.

        @param dx  offset added to fLeft and fRight
        @param dy  offset added to fTop and fBottom
    */
    void offset(int32_t dx, int32_t dy) {
        fLeft   = Sk32_sat_add(fLeft,   dx);
        fTop    = Sk32_sat_add(fTop,    dy);
        fRight  = Sk32_sat_add(fRight,  dx);
        fBottom = Sk32_sat_add(fBottom, dy);
    }

    /** Offsets SkIRect by adding delta.fX to fLeft, fRight; and by adding delta.fY to
        fTop, fBottom.

        If delta.fX is negative, moves SkIRect returned to the left.
        If delta.fX is positive, moves SkIRect returned to the right.
        If delta.fY is negative, moves SkIRect returned upward.
        If delta.fY is positive, moves SkIRect returned downward.

        @param delta  offset added to SkIRect
    */
    void offset(const SkIPoint& delta) {
        this->offset(delta.fX, delta.fY);
    }

    /** Offsets SkIRect so that fLeft equals newX, and fTop equals newY. width and height
        are unchanged.

        @param newX  stored in fLeft, preserving width()
        @param newY  stored in fTop, preserving height()
    */
    void offsetTo(int32_t newX, int32_t newY) {
        fRight  = Sk64_pin_to_s32((int64_t)fRight + newX - fLeft);
        fBottom = Sk64_pin_to_s32((int64_t)fBottom + newY - fTop);
        fLeft   = newX;
        fTop    = newY;
    }

    /** Insets SkIRect by (dx,dy).

        If dx is positive, makes SkIRect narrower.
        If dx is negative, makes SkIRect wider.
        If dy is positive, makes SkIRect shorter.
        If dy is negative, makes SkIRect taller.

        @param dx  offset added to fLeft and subtracted from fRight
        @param dy  offset added to fTop and subtracted from fBottom
    */
    void inset(int32_t dx, int32_t dy) {
        fLeft   = Sk32_sat_add(fLeft,   dx);
        fTop    = Sk32_sat_add(fTop,    dy);
        fRight  = Sk32_sat_sub(fRight,  dx);
        fBottom = Sk32_sat_sub(fBottom, dy);
    }

    /** Outsets SkIRect by (dx, dy).

        If dx is positive, makes SkIRect wider.
        If dx is negative, makes SkIRect narrower.
        If dy is positive, makes SkIRect taller.
        If dy is negative, makes SkIRect shorter.

        @param dx  subtracted to fLeft and added from fRight
        @param dy  subtracted to fTop and added from fBottom
    */
    void outset(int32_t dx, int32_t dy)  { this->inset(-dx, -dy); }

    /** Adjusts SkIRect by adding dL to fLeft, dT to fTop, dR to fRight, and dB to fBottom.

        If dL is positive, narrows SkIRect on the left. If negative, widens it on the left.
        If dT is positive, shrinks SkIRect on the top. If negative, lengthens it on the top.
        If dR is positive, narrows SkIRect on the right. If negative, widens it on the right.
        If dB is positive, shrinks SkIRect on the bottom. If negative, lengthens it on the bottom.

        The resulting SkIRect is not checked for validity. Thus, if the resulting SkIRect left is
        greater than right, the SkIRect will be considered empty. Call sort() after this call
        if that is not the desired behavior.

        @param dL  offset added to fLeft
        @param dT  offset added to fTop
        @param dR  offset added to fRight
        @param dB  offset added to fBottom
    */
    void adjust(int32_t dL, int32_t dT, int32_t dR, int32_t dB) {
        fLeft   = Sk32_sat_add(fLeft,   dL);
        fTop    = Sk32_sat_add(fTop,    dT);
        fRight  = Sk32_sat_add(fRight,  dR);
        fBottom = Sk32_sat_add(fBottom, dB);
    }

    /** Returns true if: fLeft <= x < fRight && fTop <= y < fBottom.
        Returns false if SkIRect is empty.

        Considers input to describe constructed SkIRect: (x, y, x + 1, y + 1) and
        returns true if constructed area is completely enclosed by SkIRect area.

        @param x  test SkIPoint x-coordinate
        @param y  test SkIPoint y-coordinate
        @return   true if (x, y) is inside SkIRect
    */
    bool contains(int32_t x, int32_t y) const {
        return x >= fLeft && x < fRight && y >= fTop && y < fBottom;
    }

    /** Returns true if SkIRect contains r.
     Returns false if SkIRect is empty or r is empty.

     SkIRect contains r when SkIRect area completely includes r area.

     @param r  SkIRect contained
     @return   true if all sides of SkIRect are outside r
     */
    bool contains(const SkIRect& r) const {
        return  !r.isEmpty() && !this->isEmpty() &&     // check for empties
                fLeft <= r.fLeft && fTop <= r.fTop &&
                fRight >= r.fRight && fBottom >= r.fBottom;
    }

    /** Returns true if SkIRect contains r.
        Returns false if SkIRect is empty or r is empty.

        SkIRect contains r when SkIRect area completely includes r area.

        @param r  SkRect contained
        @return   true if all sides of SkIRect are outside r
    */
    inline bool contains(const SkRect& r) const;

    /** Returns true if SkIRect contains construction.
        Asserts if SkIRect is empty or construction is empty, and if SK_DEBUG is defined.

        Return is undefined if SkIRect is empty or construction is empty.

        @param r  SkIRect contained
        @return   true if all sides of SkIRect are outside r
    */
    bool containsNoEmptyCheck(const SkIRect& r) const {
        SkASSERT(fLeft < fRight && fTop < fBottom);
        SkASSERT(r.fLeft < r.fRight && r.fTop < r.fBottom);
        return fLeft <= r.fLeft && fTop <= r.fTop && fRight >= r.fRight && fBottom >= r.fBottom;
    }

    /** Returns true if SkIRect intersects r, and sets SkIRect to intersection.
        Returns false if SkIRect does not intersect r, and leaves SkIRect unchanged.

        Returns false if either r or SkIRect is empty, leaving SkIRect unchanged.

        @param r  limit of result
        @return   true if r and SkIRect have area in common
    */
    bool intersect(const SkIRect& r) {
        return this->intersect(*this, r);
    }

    /** Returns true if a intersects b, and sets SkIRect to intersection.
        Returns false if a does not intersect b, and leaves SkIRect unchanged.

        Returns false if either a or b is empty, leaving SkIRect unchanged.

        @param a  SkIRect to intersect
        @param b  SkIRect to intersect
        @return   true if a and b have area in common
    */
    [[nodiscard]] bool intersect(const SkIRect& a, const SkIRect& b);

    /** Returns true if a intersects b.
        Returns false if either a or b is empty, or do not intersect.

        @param a  SkIRect to intersect
        @param b  SkIRect to intersect
        @return   true if a and b have area in common
    */
    static bool Intersects(const SkIRect& a, const SkIRect& b) {
        return SkIRect{}.intersect(a, b);
    }

    /** Sets SkIRect to the union of itself and r.

     Has no effect if r is empty. Otherwise, if SkIRect is empty, sets SkIRect to r.

     @param r  expansion SkIRect

        example: https://fiddle.skia.org/c/@IRect_join_2
     */
    void join(const SkIRect& r);

    /** Swaps fLeft and fRight if fLeft is greater than fRight; and swaps
        fTop and fBottom if fTop is greater than fBottom. Result may be empty,
        and width() and height() will be zero or positive.
    */
    void sort() {
        using std::swap;
        if (fLeft > fRight) {
            swap(fLeft, fRight);
        }
        if (fTop > fBottom) {
            swap(fTop, fBottom);
        }
    }

    /** Returns SkIRect with fLeft and fRight swapped if fLeft is greater than fRight; and
        with fTop and fBottom swapped if fTop is greater than fBottom. Result may be empty;
        and width() and height() will be zero or positive.

        @return  sorted SkIRect
    */
    SkIRect makeSorted() const {
        return MakeLTRB(std::min(fLeft, fRight), std::min(fTop, fBottom),
                        std::max(fLeft, fRight), std::max(fTop, fBottom));
    }
};

/** \struct SkRect
    SkRect holds four float coordinates describing the upper and
    lower bounds of a rectangle. SkRect may be created from outer bounds or
    from position, width, and height. SkRect describes an area; if its right
    is less than or equal to its left, or if its bottom is less than or equal to
    its top, it is considered empty.
*/
struct SK_API SkRect {
    float fLeft   = 0; //!< smaller x-axis bounds
    float fTop    = 0; //!< smaller y-axis bounds
    float fRight  = 0; //!< larger x-axis bounds
    float fBottom = 0; //!< larger y-axis bounds

    /** Returns constructed SkRect set to (0, 0, 0, 0).
        Many other rectangles are empty; if left is equal to or greater than right,
        or if top is equal to or greater than bottom. Setting all members to zero
        is a convenience, but does not designate a special empty rectangle.

        @return  bounds (0, 0, 0, 0)
    */
    [[nodiscard]] static constexpr SkRect MakeEmpty() {
        return SkRect{0, 0, 0, 0};
    }

    /** Returns constructed SkRect set to float values (0, 0, w, h). Does not
        validate input; w or h may be negative.

        Passing integer values may generate a compiler warning since SkRect cannot
        represent 32-bit integers exactly. Use SkIRect for an exact integer rectangle.

        @param w  float width of constructed SkRect
        @param h  float height of constructed SkRect
        @return   bounds (0, 0, w, h)
    */
    [[nodiscard]] static constexpr SkRect MakeWH(float w, float h) {
        return SkRect{0, 0, w, h};
    }

    /** Returns constructed SkRect set to integer values (0, 0, w, h). Does not validate
        input; w or h may be negative.

        Use to avoid a compiler warning that input may lose precision when stored.
        Use SkIRect for an exact integer rectangle.

        @param w  integer width of constructed SkRect
        @param h  integer height of constructed SkRect
        @return   bounds (0, 0, w, h)
    */
    [[nodiscard]] static SkRect MakeIWH(int w, int h) {
        return {0, 0, static_cast<float>(w), static_cast<float>(h)};
    }

    /** Returns constructed SkRect set to (0, 0, size.width(), size.height()). Does not
        validate input; size.width() or size.height() may be negative.

        @param size  float values for SkRect width and height
        @return      bounds (0, 0, size.width(), size.height())
    */
    [[nodiscard]] static constexpr SkRect MakeSize(const SkSize& size) {
        return SkRect{0, 0, size.fWidth, size.fHeight};
    }

    /** Returns constructed SkRect set to (l, t, r, b). Does not sort input; SkRect may
        result in fLeft greater than fRight, or fTop greater than fBottom.

        @param l  float stored in fLeft
        @param t  float stored in fTop
        @param r  float stored in fRight
        @param b  float stored in fBottom
        @return   bounds (l, t, r, b)
    */
    [[nodiscard]] static constexpr SkRect MakeLTRB(float l, float t, float r, float b) {
        return SkRect {l, t, r, b};
    }

    /** Returns constructed SkRect set to (x, y, x + w, y + h).
        Does not validate input; w or h may be negative.

        @param x  stored in fLeft
        @param y  stored in fTop
        @param w  added to x and stored in fRight
        @param h  added to y and stored in fBottom
        @return   bounds at (x, y) with width w and height h
    */
    [[nodiscard]] static constexpr SkRect MakeXYWH(float x, float y, float w, float h) {
        return SkRect {x, y, x + w, y + h};
    }

    /** Returns constructed SkIRect set to (0, 0, size.width(), size.height()).
        Does not validate input; size.width() or size.height() may be negative.

        @param size  integer values for SkRect width and height
        @return      bounds (0, 0, size.width(), size.height())
    */
    static SkRect Make(const SkISize& size) {
        return MakeIWH(size.width(), size.height());
    }

    /** Returns constructed SkIRect set to irect, promoting integers to float.
        Does not validate input; fLeft may be greater than fRight, fTop may be greater
        than fBottom.

        @param irect  integer unsorted bounds
        @return       irect members converted to float
    */
    [[nodiscard]] static SkRect Make(const SkIRect& irect) {
        return {
            static_cast<float>(irect.fLeft), static_cast<float>(irect.fTop),
            static_cast<float>(irect.fRight), static_cast<float>(irect.fBottom)
        };
    }

    /** Returns true if fLeft is equal to or greater than fRight, or if fTop is equal
        to or greater than fBottom. Call sort() to reverse rectangles with negative
        width() or height().

        @return  true if width() or height() are zero or negative
    */
    bool isEmpty() const {
        // We write it as the NOT of a non-empty rect, so we will return true if any values
        // are NaN.
        return !(fLeft < fRight && fTop < fBottom);
    }

    /** Returns true if fLeft is equal to or less than fRight, or if fTop is equal
        to or less than fBottom. Call sort() to reverse rectangles with negative
        width() or height().

        @return  true if width() or height() are zero or positive
    */
    bool isSorted() const { return fLeft <= fRight && fTop <= fBottom; }

    /** Returns true if all values in the rectangle are finite.

        @return  true if no member is infinite or NaN
    */
    bool isFinite() const {
        float accum = 0;
        accum *= fLeft;
        accum *= fTop;
        accum *= fRight;
        accum *= fBottom;

        // accum is either NaN or it is finite (zero).
        SkASSERT(0 == accum || std::isnan(accum));

        // value==value will be true iff value is not NaN
        // TODO: is it faster to say !accum or accum==accum?
        return !std::isnan(accum);
    }

    /** Returns left edge of SkRect, if sorted. Call isSorted() to see if SkRect is valid.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fLeft
    */
    constexpr float x() const { return fLeft; }

    /** Returns top edge of SkRect, if sorted. Call isEmpty() to see if SkRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fTop
    */
    constexpr float y() const { return fTop; }

    /** Returns left edge of SkRect, if sorted. Call isSorted() to see if SkRect is valid.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fLeft
    */
    constexpr float left() const { return fLeft; }

    /** Returns top edge of SkRect, if sorted. Call isEmpty() to see if SkRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fTop
    */
    constexpr float top() const { return fTop; }

    /** Returns right edge of SkRect, if sorted. Call isSorted() to see if SkRect is valid.
        Call sort() to reverse fLeft and fRight if needed.

        @return  fRight
    */
    constexpr float right() const { return fRight; }

    /** Returns bottom edge of SkRect, if sorted. Call isEmpty() to see if SkRect may be invalid,
        and sort() to reverse fTop and fBottom if needed.

        @return  fBottom
    */
    constexpr float bottom() const { return fBottom; }

    /** Returns span on the x-axis. This does not check if SkRect is sorted, or if
        result fits in 32-bit float; result may be negative or infinity.

        @return  fRight minus fLeft
    */
    constexpr float width() const { return fRight - fLeft; }

    /** Returns span on the y-axis. This does not check if SkRect is sorted, or if
        result fits in 32-bit float; result may be negative or infinity.

        @return  fBottom minus fTop
    */
    constexpr float height() const { return fBottom - fTop; }

    /** Returns average of left edge and right edge. Result does not change if SkRect
        is sorted. Result may overflow to infinity if SkRect is far from the origin.

        @return  midpoint on x-axis
    */
    constexpr float centerX() const {
        return sk_float_midpoint(fLeft, fRight);
    }

    /** Returns average of top edge and bottom edge. Result does not change if SkRect
        is sorted.

        @return  midpoint on y-axis
    */
    constexpr float centerY() const {
        return sk_float_midpoint(fTop, fBottom);
    }

    /** Returns the point this->centerX(), this->centerY().
        @return  rectangle center
     */
    constexpr SkPoint center() const { return {this->centerX(), this->centerY()}; }

    /** Returns true if all members in a: fLeft, fTop, fRight, and fBottom; are
        equal to the corresponding members in b.

        a and b are not equal if either contain NaN. a and b are equal if members
        contain zeroes with different signs.

        @param a  SkRect to compare
        @param b  SkRect to compare
        @return   true if members are equal
    */
    friend bool operator==(const SkRect& a, const SkRect& b) {
        return a.fLeft == b.fLeft &&
               a.fTop == b.fTop &&
               a.fRight == b.fRight &&
               a.fBottom == b.fBottom;
    }

    /** Returns true if any in a: fLeft, fTop, fRight, and fBottom; does not
        equal the corresponding members in b.

        a and b are not equal if either contain NaN. a and b are equal if members
        contain zeroes with different signs.

        @param a  SkRect to compare
        @param b  SkRect to compare
        @return   true if members are not equal
    */
    friend bool operator!=(const SkRect& a, const SkRect& b) {
        return !(a == b);
    }

    /** Returns four points in quad that enclose SkRect ordered as: top-left, top-right,
        bottom-right, bottom-left.

        TODO: Consider adding parameter to control whether quad is clockwise or counterclockwise.

        @param quad  storage for corners of SkRect

        example: https://fiddle.skia.org/c/@Rect_toQuad
    */
    void toQuad(SkPoint quad[4]) const;

    /** Sets SkRect to (0, 0, 0, 0).

        Many other rectangles are empty; if left is equal to or greater than right,
        or if top is equal to or greater than bottom. Setting all members to zero
        is a convenience, but does not designate a special empty rectangle.
    */
    void setEmpty() { *this = MakeEmpty(); }

    /** Sets SkRect to src, promoting src members from integer to float.
        Very large values in src may lose precision.

        @param src  integer SkRect
    */
    void set(const SkIRect& src) {
        fLeft   = src.fLeft;
        fTop    = src.fTop;
        fRight  = src.fRight;
        fBottom = src.fBottom;
    }

    /** Sets SkRect to (left, top, right, bottom).
        left and right are not sorted; left is not necessarily less than right.
        top and bottom are not sorted; top is not necessarily less than bottom.

        @param left    stored in fLeft
        @param top     stored in fTop
        @param right   stored in fRight
        @param bottom  stored in fBottom
    */
    void setLTRB(float left, float top, float right, float bottom) {
        fLeft   = left;
        fTop    = top;
        fRight  = right;
        fBottom = bottom;
    }

    /** Sets to bounds of SkPoint array with count entries. If count is zero or smaller,
        or if SkPoint array contains an infinity or NaN, sets to (0, 0, 0, 0).

        Result is either empty or sorted: fLeft is less than or equal to fRight, and
        fTop is less than or equal to fBottom.

        @param pts    SkPoint array
        @param count  entries in array
    */
    void setBounds(const SkPoint pts[], int count) {
        (void)this->setBoundsCheck(pts, count);
    }

    /** Sets to bounds of SkPoint array with count entries. Returns false if count is
        zero or smaller, or if SkPoint array contains an infinity or NaN; in these cases
        sets SkRect to (0, 0, 0, 0).

        Result is either empty or sorted: fLeft is less than or equal to fRight, and
        fTop is less than or equal to fBottom.

        @param pts    SkPoint array
        @param count  entries in array
        @return       true if all SkPoint values are finite

        example: https://fiddle.skia.org/c/@Rect_setBoundsCheck
    */
    bool setBoundsCheck(const SkPoint pts[], int count);

    /** Sets to bounds of SkPoint pts array with count entries. If any SkPoint in pts
        contains infinity or NaN, all SkRect dimensions are set to NaN.

        @param pts    SkPoint array
        @param count  entries in array

        example: https://fiddle.skia.org/c/@Rect_setBoundsNoCheck
    */
    void setBoundsNoCheck(const SkPoint pts[], int count);

    /** Sets bounds to the smallest SkRect enclosing SkPoint p0 and p1. The result is
        sorted and may be empty. Does not check to see if values are finite.

        @param p0  corner to include
        @param p1  corner to include
    */
    void set(const SkPoint& p0, const SkPoint& p1) {
        fLeft =   std::min(p0.fX, p1.fX);
        fRight =  std::max(p0.fX, p1.fX);
        fTop =    std::min(p0.fY, p1.fY);
        fBottom = std::max(p0.fY, p1.fY);
    }

    /** Sets SkRect to (x, y, x + width, y + height).
        Does not validate input; width or height may be negative.

        @param x       stored in fLeft
        @param y       stored in fTop
        @param width   added to x and stored in fRight
        @param height  added to y and stored in fBottom
    */
    void setXYWH(float x, float y, float width, float height) {
        fLeft = x;
        fTop = y;
        fRight = x + width;
        fBottom = y + height;
    }

    /** Sets SkRect to (0, 0, width, height). Does not validate input;
        width or height may be negative.

        @param width   stored in fRight
        @param height  stored in fBottom
    */
    void setWH(float width, float height) {
        fLeft = 0;
        fTop = 0;
        fRight = width;
        fBottom = height;
    }
    void setIWH(int32_t width, int32_t height) {
        this->setWH(width, height);
    }

    /** Returns SkRect offset by (dx, dy).

        If dx is negative, SkRect returned is moved to the left.
        If dx is positive, SkRect returned is moved to the right.
        If dy is negative, SkRect returned is moved upward.
        If dy is positive, SkRect returned is moved downward.

        @param dx  added to fLeft and fRight
        @param dy  added to fTop and fBottom
        @return    SkRect offset on axes, with original width and height
    */
    constexpr SkRect makeOffset(float dx, float dy) const {
        return MakeLTRB(fLeft + dx, fTop + dy, fRight + dx, fBottom + dy);
    }

    /** Returns SkRect offset by v.

        @param v  added to rect
        @return    SkRect offset on axes, with original width and height
    */
    constexpr SkRect makeOffset(SkVector v) const { return this->makeOffset(v.x(), v.y()); }

    /** Returns SkRect, inset by (dx, dy).

        If dx is negative, SkRect returned is wider.
        If dx is positive, SkRect returned is narrower.
        If dy is negative, SkRect returned is taller.
        If dy is positive, SkRect returned is shorter.

        @param dx  added to fLeft and subtracted from fRight
        @param dy  added to fTop and subtracted from fBottom
        @return    SkRect inset symmetrically left and right, top and bottom
    */
    SkRect makeInset(float dx, float dy) const {
        return MakeLTRB(fLeft + dx, fTop + dy, fRight - dx, fBottom - dy);
    }

    /** Returns SkRect, outset by (dx, dy).

        If dx is negative, SkRect returned is narrower.
        If dx is positive, SkRect returned is wider.
        If dy is negative, SkRect returned is shorter.
        If dy is positive, SkRect returned is taller.

        @param dx  subtracted to fLeft and added from fRight
        @param dy  subtracted to fTop and added from fBottom
        @return    SkRect outset symmetrically left and right, top and bottom
    */
    SkRect makeOutset(float dx, float dy) const {
        return MakeLTRB(fLeft - dx, fTop - dy, fRight + dx, fBottom + dy);
    }

    /** Offsets SkRect by adding dx to fLeft, fRight; and by adding dy to fTop, fBottom.

        If dx is negative, moves SkRect to the left.
        If dx is positive, moves SkRect to the right.
        If dy is negative, moves SkRect upward.
        If dy is positive, moves SkRect downward.

        @param dx  offset added to fLeft and fRight
        @param dy  offset added to fTop and fBottom
    */
    void offset(float dx, float dy) {
        fLeft   += dx;
        fTop    += dy;
        fRight  += dx;
        fBottom += dy;
    }

    /** Offsets SkRect by adding delta.fX to fLeft, fRight; and by adding delta.fY to
        fTop, fBottom.

        If delta.fX is negative, moves SkRect to the left.
        If delta.fX is positive, moves SkRect to the right.
        If delta.fY is negative, moves SkRect upward.
        If delta.fY is positive, moves SkRect downward.

        @param delta  added to SkRect
    */
    void offset(const SkPoint& delta) {
        this->offset(delta.fX, delta.fY);
    }

    /** Offsets SkRect so that fLeft equals newX, and fTop equals newY. width and height
        are unchanged.

        @param newX  stored in fLeft, preserving width()
        @param newY  stored in fTop, preserving height()
    */
    void offsetTo(float newX, float newY) {
        fRight += newX - fLeft;
        fBottom += newY - fTop;
        fLeft = newX;
        fTop = newY;
    }

    /** Insets SkRect by (dx, dy).

        If dx is positive, makes SkRect narrower.
        If dx is negative, makes SkRect wider.
        If dy is positive, makes SkRect shorter.
        If dy is negative, makes SkRect taller.

        @param dx  added to fLeft and subtracted from fRight
        @param dy  added to fTop and subtracted from fBottom
    */
    void inset(float dx, float dy)  {
        fLeft   += dx;
        fTop    += dy;
        fRight  -= dx;
        fBottom -= dy;
    }

    /** Outsets SkRect by (dx, dy).

        If dx is positive, makes SkRect wider.
        If dx is negative, makes SkRect narrower.
        If dy is positive, makes SkRect taller.
        If dy is negative, makes SkRect shorter.

        @param dx  subtracted to fLeft and added from fRight
        @param dy  subtracted to fTop and added from fBottom
    */
    void outset(float dx, float dy)  { this->inset(-dx, -dy); }

    /** Returns true if SkRect intersects r, and sets SkRect to intersection.
        Returns false if SkRect does not intersect r, and leaves SkRect unchanged.

        Returns false if either r or SkRect is empty, leaving SkRect unchanged.

        @param r  limit of result
        @return   true if r and SkRect have area in common

        example: https://fiddle.skia.org/c/@Rect_intersect
    */
    bool intersect(const SkRect& r);

    /** Returns true if a intersects b, and sets SkRect to intersection.
        Returns false if a does not intersect b, and leaves SkRect unchanged.

        Returns false if either a or b is empty, leaving SkRect unchanged.

        @param a  SkRect to intersect
        @param b  SkRect to intersect
        @return   true if a and b have area in common
    */
    [[nodiscard]] bool intersect(const SkRect& a, const SkRect& b);


private:
    static bool Intersects(float al, float at, float ar, float ab,
                           float bl, float bt, float br, float bb) {
        float L = std::max(al, bl);
        float R = std::min(ar, br);
        float T = std::max(at, bt);
        float B = std::min(ab, bb);
        return L < R && T < B;
    }

public:

    /** Returns true if SkRect intersects r.
     Returns false if either r or SkRect is empty, or do not intersect.

     @param r  SkRect to intersect
     @return   true if r and SkRect have area in common
     */
    bool intersects(const SkRect& r) const {
        return Intersects(fLeft, fTop, fRight, fBottom,
                          r.fLeft, r.fTop, r.fRight, r.fBottom);
    }

    /** Returns true if a intersects b.
        Returns false if either a or b is empty, or do not intersect.

        @param a  SkRect to intersect
        @param b  SkRect to intersect
        @return   true if a and b have area in common
    */
    static bool Intersects(const SkRect& a, const SkRect& b) {
        return Intersects(a.fLeft, a.fTop, a.fRight, a.fBottom,
                          b.fLeft, b.fTop, b.fRight, b.fBottom);
    }

    /** Sets SkRect to the union of itself and r.

        Has no effect if r is empty. Otherwise, if SkRect is empty, sets
        SkRect to r.

        @param r  expansion SkRect

        example: https://fiddle.skia.org/c/@Rect_join_2
    */
    void join(const SkRect& r);

    /** Sets SkRect to the union of itself and r.

        Asserts if r is empty and SK_DEBUG is defined.
        If SkRect is empty, sets SkRect to r.

        May produce incorrect results if r is empty.

        @param r  expansion SkRect
    */
    void joinNonEmptyArg(const SkRect& r) {
        SkASSERT(!r.isEmpty());
        // if we are empty, just assign
        if (fLeft >= fRight || fTop >= fBottom) {
            *this = r;
        } else {
            this->joinPossiblyEmptyRect(r);
        }
    }

    /** Sets SkRect to the union of itself and the construction.

        May produce incorrect results if SkRect or r is empty.

        @param r  expansion SkRect
    */
    void joinPossiblyEmptyRect(const SkRect& r) {
        fLeft   = std::min(fLeft, r.left());
        fTop    = std::min(fTop, r.top());
        fRight  = std::max(fRight, r.right());
        fBottom = std::max(fBottom, r.bottom());
    }

    /** Returns true if: fLeft <= x < fRight && fTop <= y < fBottom.
        Returns false if SkRect is empty.

        @param x  test SkPoint x-coordinate
        @param y  test SkPoint y-coordinate
        @return   true if (x, y) is inside SkRect
    */
    bool contains(float x, float y) const {
        return x >= fLeft && x < fRight && y >= fTop && y < fBottom;
    }

    /** Returns true if SkRect contains r.
        Returns false if SkRect is empty or r is empty.

        SkRect contains r when SkRect area completely includes r area.

        @param r  SkRect contained
        @return   true if all sides of SkRect are outside r
    */
    bool contains(const SkRect& r) const {
        // todo: can we eliminate the this->isEmpty check?
        return  !r.isEmpty() && !this->isEmpty() &&
                fLeft <= r.fLeft && fTop <= r.fTop &&
                fRight >= r.fRight && fBottom >= r.fBottom;
    }

    /** Returns true if SkRect contains r.
        Returns false if SkRect is empty or r is empty.

        SkRect contains r when SkRect area completely includes r area.

        @param r  SkIRect contained
        @return   true if all sides of SkRect are outside r
    */
    bool contains(const SkIRect& r) const {
        // todo: can we eliminate the this->isEmpty check?
        return  !r.isEmpty() && !this->isEmpty() &&
                fLeft <= r.fLeft && fTop <= r.fTop &&
                fRight >= r.fRight && fBottom >= r.fBottom;
    }

    /** Sets SkIRect by adding 0.5 and discarding the fractional portion of SkRect
        members, using (sk_float_round2int(fLeft), sk_float_round2int(fTop),
                        sk_float_round2int(fRight), sk_float_round2int(fBottom)).

        @param dst  storage for SkIRect
    */
    void round(SkIRect* dst) const {
        SkASSERT(dst);
        dst->setLTRB(sk_float_round2int(fLeft),  sk_float_round2int(fTop),
                     sk_float_round2int(fRight), sk_float_round2int(fBottom));
    }

    /** Sets SkIRect by discarding the fractional portion of fLeft and fTop; and rounding
        up fRight and fBottom, using
        (sk_float_floor2int(fLeft), sk_float_floor2int(fTop),
         sk_float_ceil2int(fRight), sk_float_ceil2int(fBottom)).

        @param dst  storage for SkIRect
    */
    void roundOut(SkIRect* dst) const {
        SkASSERT(dst);
        dst->setLTRB(sk_float_floor2int(fLeft), sk_float_floor2int(fTop),
                     sk_float_ceil2int(fRight), sk_float_ceil2int(fBottom));
    }

    /** Sets SkRect by discarding the fractional portion of fLeft and fTop; and rounding
        up fRight and fBottom, using
        (sk_float_floor(fLeft), sk_float_floor(fTop),
         sk_float_ceil(fRight), sk_float_ceil(fBottom)).

        @param dst  storage for SkRect
    */
    void roundOut(SkRect* dst) const {
        dst->setLTRB(sk_float_floor(fLeft), sk_float_floor(fTop),
                     sk_float_ceil(fRight), sk_float_ceil(fBottom));
    }

    /** Sets SkRect by rounding up fLeft and fTop; and discarding the fractional portion
        of fRight and fBottom, using
        (sk_float_ceil2int(fLeft), sk_float_ceil2int(fTop),
         sk_float_floor2int(fRight), sk_float_floor2int(fBottom)).

        @param dst  storage for SkIRect
    */
    void roundIn(SkIRect* dst) const {
        SkASSERT(dst);
        dst->setLTRB(sk_float_ceil2int(fLeft),   sk_float_ceil2int(fTop),
                     sk_float_floor2int(fRight), sk_float_floor2int(fBottom));
    }

    /** Returns SkIRect by adding 0.5 and discarding the fractional portion of SkRect
        members, using (sk_float_round2int(fLeft), sk_float_round2int(fTop),
                        sk_float_round2int(fRight), sk_float_round2int(fBottom)).

        @return  rounded SkIRect
    */
    SkIRect round() const {
        SkIRect ir;
        this->round(&ir);
        return ir;
    }

    /** Sets SkIRect by discarding the fractional portion of fLeft and fTop; and rounding
        up fRight and fBottom, using
        (sk_float_floor2int(fLeft), sk_float_floor2int(fTop),
         sk_float_ceil2int(fRight), sk_float_ceil2int(fBottom)).

        @return  rounded SkIRect
    */
    SkIRect roundOut() const {
        SkIRect ir;
        this->roundOut(&ir);
        return ir;
    }
    /** Sets SkIRect by rounding up fLeft and fTop; and discarding the fractional portion
        of fRight and fBottom, using
        (sk_float_ceil2int(fLeft), sk_float_ceil2int(fTop),
         sk_float_floor2int(fRight), sk_float_floor2int(fBottom)).

        @return  rounded SkIRect
    */
    SkIRect roundIn() const {
        SkIRect ir;
        this->roundIn(&ir);
        return ir;
    }

    /** Swaps fLeft and fRight if fLeft is greater than fRight; and swaps
        fTop and fBottom if fTop is greater than fBottom. Result may be empty;
        and width() and height() will be zero or positive.
    */
    void sort() {
        using std::swap;
        if (fLeft > fRight) {
            swap(fLeft, fRight);
        }

        if (fTop > fBottom) {
            swap(fTop, fBottom);
        }
    }

    /** Returns SkRect with fLeft and fRight swapped if fLeft is greater than fRight; and
        with fTop and fBottom swapped if fTop is greater than fBottom. Result may be empty;
        and width() and height() will be zero or positive.

        @return  sorted SkRect
    */
    SkRect makeSorted() const {
        return MakeLTRB(std::min(fLeft, fRight), std::min(fTop, fBottom),
                        std::max(fLeft, fRight), std::max(fTop, fBottom));
    }

    /** Returns pointer to first float in SkRect, to treat it as an array with four
        entries.

        @return  pointer to fLeft
    */
    const float* asScalars() const { return &fLeft; }

    /** Writes text representation of SkRect to standard output. Set asHex to true to
        generate exact binary representations of floating point numbers.

        @param asHex  true if SkScalar values are written as hexadecimal

        example: https://fiddle.skia.org/c/@Rect_dump
    */
    void dump(bool asHex) const;

    /** Writes text representation of SkRect to standard output. The representation may be
        directly compiled as C++ code. Floating point values are written
        with limited precision; it may not be possible to reconstruct original SkRect
        from output.
    */
    void dump() const { this->dump(false); }

    /** Writes text representation of SkRect to standard output. The representation may be
        directly compiled as C++ code. Floating point values are written
        in hexadecimal to preserve their exact bit pattern. The output reconstructs the
        original SkRect.

        Use instead of dump() when submitting
    */
    void dumpHex() const { this->dump(true); }
};

inline bool SkIRect::contains(const SkRect& r) const {
    return  !r.isEmpty() && !this->isEmpty() &&     // check for empties
            fLeft <= r.fLeft && fTop <= r.fTop &&
            fRight >= r.fRight && fBottom >= r.fBottom;
}

/*
 *  Usage:  SK_MACRO_CONCAT(a, b)   to construct the symbol ab
 *
 *  SK_MACRO_CONCAT_IMPL_PRIV just exists to make this work. Do not use directly
 *
 */
#define SK_MACRO_CONCAT(X, Y)           SK_MACRO_CONCAT_IMPL_PRIV(X, Y)
#define SK_MACRO_CONCAT_IMPL_PRIV(X, Y)  X ## Y

/*
 *  Usage: SK_MACRO_APPEND_LINE(foo)    to make foo123, where 123 is the current
 *                                      line number. Easy way to construct
 *                                      unique names for local functions or
 *                                      variables.
 */
#define SK_MACRO_APPEND_LINE(name)  SK_MACRO_CONCAT(name, __LINE__)

#define SK_MACRO_APPEND_COUNTER(name) SK_MACRO_CONCAT(name, __COUNTER__)

////////////////////////////////////////////////////////////////////////////////

// Can be used to bracket data types that must be dense/packed, e.g. hash keys.
#if defined(__clang__)  // This should work on GCC too, but GCC diagnostic pop didn't seem to work!
    #define SK_BEGIN_REQUIRE_DENSE _Pragma("GCC diagnostic push") \
                                   _Pragma("GCC diagnostic error \"-Wpadded\"")
    #define SK_END_REQUIRE_DENSE   _Pragma("GCC diagnostic pop")
#else
    #define SK_BEGIN_REQUIRE_DENSE
    #define SK_END_REQUIRE_DENSE
#endif

#if defined(__clang__) && defined(__has_feature)
    // Some compilers have a preprocessor that does not appear to do short-circuit
    // evaluation as expected
    #if __has_feature(leak_sanitizer) || __has_feature(address_sanitizer)
        // Chrome had issues if we tried to include lsan_interface.h ourselves.
        // https://github.com/llvm/llvm-project/blob/10a35632d55bb05004fe3d0c2d4432bb74897ee7/compiler-rt/include/sanitizer/lsan_interface.h#L26
extern "C" {
        void __lsan_ignore_object(const void *p);
}
        #define SK_INTENTIONALLY_LEAKED(X) __lsan_ignore_object(X)
    #else
        #define SK_INTENTIONALLY_LEAKED(X) ((void)0)
    #endif
#else
    #define SK_INTENTIONALLY_LEAKED(X) ((void)0)
#endif

#define SK_INIT_TO_AVOID_WARNING    = 0

////////////////////////////////////////////////////////////////////////////////

/**
 * Defines overloaded bitwise operators to make it easier to use an enum as a
 * bitfield.
 */
#define SK_MAKE_BITFIELD_OPS(X) \
    inline X operator ~(X a) { \
        using U = std::underlying_type_t<X>; \
        return (X) (~static_cast<U>(a)); \
    } \
    inline X operator |(X a, X b) { \
        using U = std::underlying_type_t<X>; \
        return (X) (static_cast<U>(a) | static_cast<U>(b)); \
    } \
    inline X& operator |=(X& a, X b) { \
        return (a = a | b); \
    } \
    inline X operator &(X a, X b) { \
        using U = std::underlying_type_t<X>; \
        return (X) (static_cast<U>(a) & static_cast<U>(b)); \
    } \
    inline X& operator &=(X& a, X b) { \
        return (a = a & b); \
    }

#define SK_DECL_BITFIELD_OPS_FRIENDS(X) \
    friend X operator ~(X a); \
    friend X operator |(X a, X b); \
    friend X& operator |=(X& a, X b); \
    \
    friend X operator &(X a, X b); \
    friend X& operator &=(X& a, X b);

template <typename D, typename S> constexpr D SkTo(S s) {
    return SkASSERT(SkTFitsIn<D>(s)),
           static_cast<D>(s);
}

template <typename S> constexpr int8_t   SkToS8(S x)    { return SkTo<int8_t>(x);   }
template <typename S> constexpr uint8_t  SkToU8(S x)    { return SkTo<uint8_t>(x);  }
template <typename S> constexpr int16_t  SkToS16(S x)   { return SkTo<int16_t>(x);  }
template <typename S> constexpr uint16_t SkToU16(S x)   { return SkTo<uint16_t>(x); }
template <typename S> constexpr int32_t  SkToS32(S x)   { return SkTo<int32_t>(x);  }
template <typename S> constexpr uint32_t SkToU32(S x)   { return SkTo<uint32_t>(x); }
template <typename S> constexpr int64_t  SkToS64(S x)   { return SkTo<int64_t>(x);  }
template <typename S> constexpr uint64_t SkToU64(S x)   { return SkTo<uint64_t>(x); }
template <typename S> constexpr int      SkToInt(S x)   { return SkTo<int>(x);      }
template <typename S> constexpr unsigned SkToUInt(S x)  { return SkTo<unsigned>(x); }
template <typename S> constexpr size_t   SkToSizeT(S x) { return SkTo<size_t>(x);   }

/** @return false or true based on the condition
*/
template <typename T> static constexpr bool SkToBool(const T& x) {
    return (bool)x;
}

struct SkPoint3;
struct SkRSXform;
struct SkSize;

// Remove when clients are updated to live without this
#define SK_SUPPORT_LEGACY_MATRIX_RECTTORECT

/**
 *  When we transform points through a matrix containing perspective (the bottom row is something
 *  other than 0,0,1), the bruteforce math can produce confusing results (since we might divide
 *  by 0, or a negative w value). By default, methods that map rects and paths will apply
 *  perspective clipping, but this can be changed by specifying kYes to those methods.
 */
enum class SkApplyPerspectiveClip {
    kNo,    //!< Don't pre-clip the geometry before applying the (perspective) matrix
    kYes,   //!< Do pre-clip the geometry before applying the (perspective) matrix
};

/** \class SkMatrix
    SkMatrix holds a 3x3 matrix for transforming coordinates. This allows mapping
    SkPoint and vectors with translation, scaling, skewing, rotation, and
    perspective.

    SkMatrix elements are in row major order.
    SkMatrix constexpr default constructs to identity.

    SkMatrix includes a hidden variable that classifies the type of matrix to
    improve performance. SkMatrix is not thread safe unless getType() is called first.

    example: https://fiddle.skia.org/c/@Matrix_063
*/
SK_BEGIN_REQUIRE_DENSE
class SK_API SkMatrix {
public:

    /** Creates an identity SkMatrix:

            | 1 0 0 |
            | 0 1 0 |
            | 0 0 1 |
    */
    constexpr SkMatrix() : SkMatrix(1,0,0, 0,1,0, 0,0,1, kIdentity_Mask | kRectStaysRect_Mask) {}

    /** Sets SkMatrix to scale by (sx, sy). Returned matrix is:

            | sx  0  0 |
            |  0 sy  0 |
            |  0  0  1 |

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
        @return    SkMatrix with scale
    */
    [[nodiscard]] static SkMatrix Scale(SkScalar sx, SkScalar sy) {
        SkMatrix m;
        m.setScale(sx, sy);
        return m;
    }

    /** Sets SkMatrix to translate by (dx, dy). Returned matrix is:

            | 1 0 dx |
            | 0 1 dy |
            | 0 0  1 |

        @param dx  horizontal translation
        @param dy  vertical translation
        @return    SkMatrix with translation
    */
    [[nodiscard]] static SkMatrix Translate(SkScalar dx, SkScalar dy) {
        SkMatrix m;
        m.setTranslate(dx, dy);
        return m;
    }
    [[nodiscard]] static SkMatrix Translate(SkVector t) { return Translate(t.x(), t.y()); }
    [[nodiscard]] static SkMatrix Translate(SkIVector t) { return Translate(t.x(), t.y()); }

    /** Sets SkMatrix to rotate by |deg| about a pivot point at (0, 0).

        @param deg  rotation angle in degrees (positive rotates clockwise)
        @return     SkMatrix with rotation
    */
    [[nodiscard]] static SkMatrix RotateDeg(SkScalar deg) {
        SkMatrix m;
        m.setRotate(deg);
        return m;
    }
    [[nodiscard]] static SkMatrix RotateDeg(SkScalar deg, SkPoint pt) {
        SkMatrix m;
        m.setRotate(deg, pt.x(), pt.y());
        return m;
    }
    [[nodiscard]] static SkMatrix RotateRad(SkScalar rad) {
        return RotateDeg(SkRadiansToDegrees(rad));
    }

    /** Sets SkMatrix to skew by (kx, ky) about pivot point (0, 0).

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
        @return    SkMatrix with skew
    */
    [[nodiscard]] static SkMatrix Skew(SkScalar kx, SkScalar ky) {
        SkMatrix m;
        m.setSkew(kx, ky);
        return m;
    }

    /** \enum SkMatrix::ScaleToFit
        ScaleToFit describes how SkMatrix is constructed to map one SkRect to another.
        ScaleToFit may allow SkMatrix to have unequal horizontal and vertical scaling,
        or may restrict SkMatrix to square scaling. If restricted, ScaleToFit specifies
        how SkMatrix maps to the side or center of the destination SkRect.
    */
    enum ScaleToFit {
        kFill_ScaleToFit,   //!< scales in x and y to fill destination SkRect
        kStart_ScaleToFit,  //!< scales and aligns to left and top
        kCenter_ScaleToFit, //!< scales and aligns to center
        kEnd_ScaleToFit,    //!< scales and aligns to right and bottom
    };

    /** Returns SkMatrix set to scale and translate src to dst. ScaleToFit selects
        whether mapping completely fills dst or preserves the aspect ratio, and how to
        align src within dst. Returns the identity SkMatrix if src is empty. If dst is
        empty, returns SkMatrix set to:

            | 0 0 0 |
            | 0 0 0 |
            | 0 0 1 |

        @param src  SkRect to map from
        @param dst  SkRect to map to
        @param mode How to handle the mapping
        @return     SkMatrix mapping src to dst
    */
    [[nodiscard]] static SkMatrix RectToRect(const SkRect& src, const SkRect& dst,
                                             ScaleToFit mode = kFill_ScaleToFit) {
        return MakeRectToRect(src, dst, mode);
    }

    /** Sets SkMatrix to:

            | scaleX  skewX transX |
            |  skewY scaleY transY |
            |  pers0  pers1  pers2 |

        @param scaleX  horizontal scale factor
        @param skewX   horizontal skew factor
        @param transX  horizontal translation
        @param skewY   vertical skew factor
        @param scaleY  vertical scale factor
        @param transY  vertical translation
        @param pers0   input x-axis perspective factor
        @param pers1   input y-axis perspective factor
        @param pers2   perspective scale factor
        @return        SkMatrix constructed from parameters
    */
    [[nodiscard]] static SkMatrix MakeAll(SkScalar scaleX, SkScalar skewX,  SkScalar transX,
                                          SkScalar skewY,  SkScalar scaleY, SkScalar transY,
                                          SkScalar pers0, SkScalar pers1, SkScalar pers2) {
        SkMatrix m;
        m.setAll(scaleX, skewX, transX, skewY, scaleY, transY, pers0, pers1, pers2);
        return m;
    }

    /** \enum SkMatrix::TypeMask
        Enum of bit fields for mask returned by getType().
        Used to identify the complexity of SkMatrix, to optimize performance.
    */
    enum TypeMask {
        kIdentity_Mask    = 0,    //!< identity SkMatrix; all bits clear
        kTranslate_Mask   = 0x01, //!< translation SkMatrix
        kScale_Mask       = 0x02, //!< scale SkMatrix
        kAffine_Mask      = 0x04, //!< skew or rotate SkMatrix
        kPerspective_Mask = 0x08, //!< perspective SkMatrix
    };

    /** Returns a bit field describing the transformations the matrix may
        perform. The bit field is computed conservatively, so it may include
        false positives. For example, when kPerspective_Mask is set, all
        other bits are set.

        @return  kIdentity_Mask, or combinations of: kTranslate_Mask, kScale_Mask,
                 kAffine_Mask, kPerspective_Mask
    */
    TypeMask getType() const {
        if (fTypeMask & kUnknown_Mask) {
            fTypeMask = this->computeTypeMask();
        }
        // only return the public masks
        return (TypeMask)(fTypeMask & 0xF);
    }

    /** Returns true if SkMatrix is identity.  Identity matrix is:

            | 1 0 0 |
            | 0 1 0 |
            | 0 0 1 |

        @return  true if SkMatrix has no effect
    */
    bool isIdentity() const {
        return this->getType() == 0;
    }

    /** Returns true if SkMatrix at most scales and translates. SkMatrix may be identity,
        contain only scale elements, only translate elements, or both. SkMatrix form is:

            | scale-x    0    translate-x |
            |    0    scale-y translate-y |
            |    0       0         1      |

        @return  true if SkMatrix is identity; or scales, translates, or both
    */
    bool isScaleTranslate() const {
        return !(this->getType() & ~(kScale_Mask | kTranslate_Mask));
    }

    /** Returns true if SkMatrix is identity, or translates. SkMatrix form is:

            | 1 0 translate-x |
            | 0 1 translate-y |
            | 0 0      1      |

        @return  true if SkMatrix is identity, or translates
    */
    bool isTranslate() const { return !(this->getType() & ~(kTranslate_Mask)); }

    /** Returns true SkMatrix maps SkRect to another SkRect. If true, SkMatrix is identity,
        or scales, or rotates a multiple of 90 degrees, or mirrors on axes. In all
        cases, SkMatrix may also have translation. SkMatrix form is either:

            | scale-x    0    translate-x |
            |    0    scale-y translate-y |
            |    0       0         1      |

        or

            |    0     rotate-x translate-x |
            | rotate-y    0     translate-y |
            |    0        0          1      |

        for non-zero values of scale-x, scale-y, rotate-x, and rotate-y.

        Also called preservesAxisAlignment(); use the one that provides better inline
        documentation.

        @return  true if SkMatrix maps one SkRect into another
    */
    bool rectStaysRect() const {
        if (fTypeMask & kUnknown_Mask) {
            fTypeMask = this->computeTypeMask();
        }
        return (fTypeMask & kRectStaysRect_Mask) != 0;
    }

    /** Returns true SkMatrix maps SkRect to another SkRect. If true, SkMatrix is identity,
        or scales, or rotates a multiple of 90 degrees, or mirrors on axes. In all
        cases, SkMatrix may also have translation. SkMatrix form is either:

            | scale-x    0    translate-x |
            |    0    scale-y translate-y |
            |    0       0         1      |

        or

            |    0     rotate-x translate-x |
            | rotate-y    0     translate-y |
            |    0        0          1      |

        for non-zero values of scale-x, scale-y, rotate-x, and rotate-y.

        Also called rectStaysRect(); use the one that provides better inline
        documentation.

        @return  true if SkMatrix maps one SkRect into another
    */
    bool preservesAxisAlignment() const { return this->rectStaysRect(); }

    /** Returns true if the matrix contains perspective elements. SkMatrix form is:

            |       --            --              --          |
            |       --            --              --          |
            | perspective-x  perspective-y  perspective-scale |

        where perspective-x or perspective-y is non-zero, or perspective-scale is
        not one. All other elements may have any value.

        @return  true if SkMatrix is in most general form
    */
    bool hasPerspective() const {
        return SkToBool(this->getPerspectiveTypeMaskOnly() &
                        kPerspective_Mask);
    }

    /** Returns true if SkMatrix contains only translation, rotation, reflection, and
        uniform scale.
        Returns false if SkMatrix contains different scales, skewing, perspective, or
        degenerate forms that collapse to a line or point.

        Describes that the SkMatrix makes rendering with and without the matrix are
        visually alike; a transformed circle remains a circle. Mathematically, this is
        referred to as similarity of a Euclidean space, or a similarity transformation.

        Preserves right angles, keeping the arms of the angle equal lengths.

        @param tol  to be deprecated
        @return     true if SkMatrix only rotates, uniformly scales, translates

        example: https://fiddle.skia.org/c/@Matrix_isSimilarity
    */
    bool isSimilarity(SkScalar tol = SK_ScalarNearlyZero) const;

    /** Returns true if SkMatrix contains only translation, rotation, reflection, and
        scale. Scale may differ along rotated axes.
        Returns false if SkMatrix skewing, perspective, or degenerate forms that collapse
        to a line or point.

        Preserves right angles, but not requiring that the arms of the angle
        retain equal lengths.

        @param tol  to be deprecated
        @return     true if SkMatrix only rotates, scales, translates

        example: https://fiddle.skia.org/c/@Matrix_preservesRightAngles
    */
    bool preservesRightAngles(SkScalar tol = SK_ScalarNearlyZero) const;

    /** SkMatrix organizes its values in row-major order. These members correspond to
        each value in SkMatrix.
    */
    static constexpr int kMScaleX = 0; //!< horizontal scale factor
    static constexpr int kMSkewX  = 1; //!< horizontal skew factor
    static constexpr int kMTransX = 2; //!< horizontal translation
    static constexpr int kMSkewY  = 3; //!< vertical skew factor
    static constexpr int kMScaleY = 4; //!< vertical scale factor
    static constexpr int kMTransY = 5; //!< vertical translation
    static constexpr int kMPersp0 = 6; //!< input x perspective factor
    static constexpr int kMPersp1 = 7; //!< input y perspective factor
    static constexpr int kMPersp2 = 8; //!< perspective bias

    /** Affine arrays are in column-major order to match the matrix used by
        PDF and XPS.
    */
    static constexpr int kAScaleX = 0; //!< horizontal scale factor
    static constexpr int kASkewY  = 1; //!< vertical skew factor
    static constexpr int kASkewX  = 2; //!< horizontal skew factor
    static constexpr int kAScaleY = 3; //!< vertical scale factor
    static constexpr int kATransX = 4; //!< horizontal translation
    static constexpr int kATransY = 5; //!< vertical translation

    /** Returns one matrix value. Asserts if index is out of range and SK_DEBUG is
        defined.

        @param index  one of: kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY,
                      kMPersp0, kMPersp1, kMPersp2
        @return       value corresponding to index
    */
    SkScalar operator[](int index) const {
        SkASSERT((unsigned)index < 9);
        return fMat[index];
    }

    /** Returns one matrix value. Asserts if index is out of range and SK_DEBUG is
        defined.

        @param index  one of: kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY,
                      kMPersp0, kMPersp1, kMPersp2
        @return       value corresponding to index
    */
    SkScalar get(int index) const {
        SkASSERT((unsigned)index < 9);
        return fMat[index];
    }

    /** Returns one matrix value from a particular row/column. Asserts if index is out
        of range and SK_DEBUG is defined.

        @param r  matrix row to fetch
        @param c  matrix column to fetch
        @return   value at the given matrix position
    */
    SkScalar rc(int r, int c) const {
        SkASSERT(r >= 0 && r <= 2);
        SkASSERT(c >= 0 && c <= 2);
        return fMat[r*3 + c];
    }

    /** Returns scale factor multiplied by x-axis input, contributing to x-axis output.
        With mapPoints(), scales SkPoint along the x-axis.

        @return  horizontal scale factor
    */
    SkScalar getScaleX() const { return fMat[kMScaleX]; }

    /** Returns scale factor multiplied by y-axis input, contributing to y-axis output.
        With mapPoints(), scales SkPoint along the y-axis.

        @return  vertical scale factor
    */
    SkScalar getScaleY() const { return fMat[kMScaleY]; }

    /** Returns scale factor multiplied by x-axis input, contributing to y-axis output.
        With mapPoints(), skews SkPoint along the y-axis.
        Skewing both axes can rotate SkPoint.

        @return  vertical skew factor
    */
    SkScalar getSkewY() const { return fMat[kMSkewY]; }

    /** Returns scale factor multiplied by y-axis input, contributing to x-axis output.
        With mapPoints(), skews SkPoint along the x-axis.
        Skewing both axes can rotate SkPoint.

        @return  horizontal scale factor
    */
    SkScalar getSkewX() const { return fMat[kMSkewX]; }

    /** Returns translation contributing to x-axis output.
        With mapPoints(), moves SkPoint along the x-axis.

        @return  horizontal translation factor
    */
    SkScalar getTranslateX() const { return fMat[kMTransX]; }

    /** Returns translation contributing to y-axis output.
        With mapPoints(), moves SkPoint along the y-axis.

        @return  vertical translation factor
    */
    SkScalar getTranslateY() const { return fMat[kMTransY]; }

    /** Returns factor scaling input x-axis relative to input y-axis.

        @return  input x-axis perspective factor
    */
    SkScalar getPerspX() const { return fMat[kMPersp0]; }

    /** Returns factor scaling input y-axis relative to input x-axis.

        @return  input y-axis perspective factor
    */
    SkScalar getPerspY() const { return fMat[kMPersp1]; }

    /** Returns writable SkMatrix value. Asserts if index is out of range and SK_DEBUG is
        defined. Clears internal cache anticipating that caller will change SkMatrix value.

        Next call to read SkMatrix state may recompute cache; subsequent writes to SkMatrix
        value must be followed by dirtyMatrixTypeCache().

        @param index  one of: kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY,
                      kMPersp0, kMPersp1, kMPersp2
        @return       writable value corresponding to index
    */
    SkScalar& operator[](int index) {
        SkASSERT((unsigned)index < 9);
        this->setTypeMask(kUnknown_Mask);
        return fMat[index];
    }

    /** Sets SkMatrix value. Asserts if index is out of range and SK_DEBUG is
        defined. Safer than operator[]; internal cache is always maintained.

        @param index  one of: kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY,
                      kMPersp0, kMPersp1, kMPersp2
        @param value  scalar to store in SkMatrix
    */
    SkMatrix& set(int index, SkScalar value) {
        SkASSERT((unsigned)index < 9);
        fMat[index] = value;
        this->setTypeMask(kUnknown_Mask);
        return *this;
    }

    /** Sets horizontal scale factor.

        @param v  horizontal scale factor to store
    */
    SkMatrix& setScaleX(SkScalar v) { return this->set(kMScaleX, v); }

    /** Sets vertical scale factor.

        @param v  vertical scale factor to store
    */
    SkMatrix& setScaleY(SkScalar v) { return this->set(kMScaleY, v); }

    /** Sets vertical skew factor.

        @param v  vertical skew factor to store
    */
    SkMatrix& setSkewY(SkScalar v) { return this->set(kMSkewY, v); }

    /** Sets horizontal skew factor.

        @param v  horizontal skew factor to store
    */
    SkMatrix& setSkewX(SkScalar v) { return this->set(kMSkewX, v); }

    /** Sets horizontal translation.

        @param v  horizontal translation to store
    */
    SkMatrix& setTranslateX(SkScalar v) { return this->set(kMTransX, v); }

    /** Sets vertical translation.

        @param v  vertical translation to store
    */
    SkMatrix& setTranslateY(SkScalar v) { return this->set(kMTransY, v); }

    /** Sets input x-axis perspective factor, which causes mapXY() to vary input x-axis values
        inversely proportional to input y-axis values.

        @param v  perspective factor
    */
    SkMatrix& setPerspX(SkScalar v) { return this->set(kMPersp0, v); }

    /** Sets input y-axis perspective factor, which causes mapXY() to vary input y-axis values
        inversely proportional to input x-axis values.

        @param v  perspective factor
    */
    SkMatrix& setPerspY(SkScalar v) { return this->set(kMPersp1, v); }

    /** Sets all values from parameters. Sets matrix to:

            | scaleX  skewX transX |
            |  skewY scaleY transY |
            | persp0 persp1 persp2 |

        @param scaleX  horizontal scale factor to store
        @param skewX   horizontal skew factor to store
        @param transX  horizontal translation to store
        @param skewY   vertical skew factor to store
        @param scaleY  vertical scale factor to store
        @param transY  vertical translation to store
        @param persp0  input x-axis values perspective factor to store
        @param persp1  input y-axis values perspective factor to store
        @param persp2  perspective scale factor to store
    */
    SkMatrix& setAll(SkScalar scaleX, SkScalar skewX,  SkScalar transX,
                     SkScalar skewY,  SkScalar scaleY, SkScalar transY,
                     SkScalar persp0, SkScalar persp1, SkScalar persp2) {
        fMat[kMScaleX] = scaleX;
        fMat[kMSkewX]  = skewX;
        fMat[kMTransX] = transX;
        fMat[kMSkewY]  = skewY;
        fMat[kMScaleY] = scaleY;
        fMat[kMTransY] = transY;
        fMat[kMPersp0] = persp0;
        fMat[kMPersp1] = persp1;
        fMat[kMPersp2] = persp2;
        this->setTypeMask(kUnknown_Mask);
        return *this;
    }

    /** Copies nine scalar values contained by SkMatrix into buffer, in member value
        ascending order: kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY,
        kMPersp0, kMPersp1, kMPersp2.

        @param buffer  storage for nine scalar values
    */
    void get9(SkScalar buffer[9]) const {
        memcpy(buffer, fMat, 9 * sizeof(SkScalar));
    }

    /** Sets SkMatrix to nine scalar values in buffer, in member value ascending order:
        kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY, kMPersp0, kMPersp1,
        kMPersp2.

        Sets matrix to:

            | buffer[0] buffer[1] buffer[2] |
            | buffer[3] buffer[4] buffer[5] |
            | buffer[6] buffer[7] buffer[8] |

        In the future, set9 followed by get9 may not return the same values. Since SkMatrix
        maps non-homogeneous coordinates, scaling all nine values produces an equivalent
        transformation, possibly improving precision.

        @param buffer  nine scalar values
    */
    SkMatrix& set9(const SkScalar buffer[9]);

    /** Sets SkMatrix to identity; which has no effect on mapped SkPoint. Sets SkMatrix to:

            | 1 0 0 |
            | 0 1 0 |
            | 0 0 1 |

        Also called setIdentity(); use the one that provides better inline
        documentation.
    */
    SkMatrix& reset();

    /** Sets SkMatrix to identity; which has no effect on mapped SkPoint. Sets SkMatrix to:

            | 1 0 0 |
            | 0 1 0 |
            | 0 0 1 |

        Also called reset(); use the one that provides better inline
        documentation.
    */
    SkMatrix& setIdentity() { return this->reset(); }

    /** Sets SkMatrix to translate by (dx, dy).

        @param dx  horizontal translation
        @param dy  vertical translation
    */
    SkMatrix& setTranslate(SkScalar dx, SkScalar dy);

    /** Sets SkMatrix to translate by (v.fX, v.fY).

        @param v  vector containing horizontal and vertical translation
    */
    SkMatrix& setTranslate(const SkVector& v) { return this->setTranslate(v.fX, v.fY); }

    /** Sets SkMatrix to scale by sx and sy, about a pivot point at (px, py).
        The pivot point is unchanged when mapped with SkMatrix.

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
        @param px  pivot on x-axis
        @param py  pivot on y-axis
    */
    SkMatrix& setScale(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py);

    /** Sets SkMatrix to scale by sx and sy about at pivot point at (0, 0).

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
    */
    SkMatrix& setScale(SkScalar sx, SkScalar sy);

    /** Sets SkMatrix to rotate by degrees about a pivot point at (px, py).
        The pivot point is unchanged when mapped with SkMatrix.

        Positive degrees rotates clockwise.

        @param degrees  angle of axes relative to upright axes
        @param px       pivot on x-axis
        @param py       pivot on y-axis
    */
    SkMatrix& setRotate(SkScalar degrees, SkScalar px, SkScalar py);

    /** Sets SkMatrix to rotate by degrees about a pivot point at (0, 0).
        Positive degrees rotates clockwise.

        @param degrees  angle of axes relative to upright axes
    */
    SkMatrix& setRotate(SkScalar degrees);

    /** Sets SkMatrix to rotate by sinValue and cosValue, about a pivot point at (px, py).
        The pivot point is unchanged when mapped with SkMatrix.

        Vector (sinValue, cosValue) describes the angle of rotation relative to (0, 1).
        Vector length specifies scale.

        @param sinValue  rotation vector x-axis component
        @param cosValue  rotation vector y-axis component
        @param px        pivot on x-axis
        @param py        pivot on y-axis
    */
    SkMatrix& setSinCos(SkScalar sinValue, SkScalar cosValue,
                   SkScalar px, SkScalar py);

    /** Sets SkMatrix to rotate by sinValue and cosValue, about a pivot point at (0, 0).

        Vector (sinValue, cosValue) describes the angle of rotation relative to (0, 1).
        Vector length specifies scale.

        @param sinValue  rotation vector x-axis component
        @param cosValue  rotation vector y-axis component
    */
    SkMatrix& setSinCos(SkScalar sinValue, SkScalar cosValue);

    /** Sets SkMatrix to rotate, scale, and translate using a compressed matrix form.

        Vector (rsxForm.fSSin, rsxForm.fSCos) describes the angle of rotation relative
        to (0, 1). Vector length specifies scale. Mapped point is rotated and scaled
        by vector, then translated by (rsxForm.fTx, rsxForm.fTy).

        @param rsxForm  compressed SkRSXform matrix
        @return         reference to SkMatrix

        example: https://fiddle.skia.org/c/@Matrix_setRSXform
    */
    SkMatrix& setRSXform(const SkRSXform& rsxForm);

    /** Sets SkMatrix to skew by kx and ky, about a pivot point at (px, py).
        The pivot point is unchanged when mapped with SkMatrix.

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
        @param px  pivot on x-axis
        @param py  pivot on y-axis
    */
    SkMatrix& setSkew(SkScalar kx, SkScalar ky, SkScalar px, SkScalar py);

    /** Sets SkMatrix to skew by kx and ky, about a pivot point at (0, 0).

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
    */
    SkMatrix& setSkew(SkScalar kx, SkScalar ky);

    /** Sets SkMatrix to SkMatrix a multiplied by SkMatrix b. Either a or b may be this.

        Given:

                | A B C |      | J K L |
            a = | D E F |, b = | M N O |
                | G H I |      | P Q R |

        sets SkMatrix to:

                    | A B C |   | J K L |   | AJ+BM+CP AK+BN+CQ AL+BO+CR |
            a * b = | D E F | * | M N O | = | DJ+EM+FP DK+EN+FQ DL+EO+FR |
                    | G H I |   | P Q R |   | GJ+HM+IP GK+HN+IQ GL+HO+IR |

        @param a  SkMatrix on left side of multiply expression
        @param b  SkMatrix on right side of multiply expression
    */
    SkMatrix& setConcat(const SkMatrix& a, const SkMatrix& b);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from translation (dx, dy).
        This can be thought of as moving the point to be mapped before applying SkMatrix.

        Given:

                     | A B C |               | 1 0 dx |
            Matrix = | D E F |,  T(dx, dy) = | 0 1 dy |
                     | G H I |               | 0 0  1 |

        sets SkMatrix to:

                                 | A B C | | 1 0 dx |   | A B A*dx+B*dy+C |
            Matrix * T(dx, dy) = | D E F | | 0 1 dy | = | D E D*dx+E*dy+F |
                                 | G H I | | 0 0  1 |   | G H G*dx+H*dy+I |

        @param dx  x-axis translation before applying SkMatrix
        @param dy  y-axis translation before applying SkMatrix
    */
    SkMatrix& preTranslate(SkScalar dx, SkScalar dy);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from scaling by (sx, sy)
        about pivot point (px, py).
        This can be thought of as scaling about a pivot point before applying SkMatrix.

        Given:

                     | A B C |                       | sx  0 dx |
            Matrix = | D E F |,  S(sx, sy, px, py) = |  0 sy dy |
                     | G H I |                       |  0  0  1 |

        where

            dx = px - sx * px
            dy = py - sy * py

        sets SkMatrix to:

                                         | A B C | | sx  0 dx |   | A*sx B*sy A*dx+B*dy+C |
            Matrix * S(sx, sy, px, py) = | D E F | |  0 sy dy | = | D*sx E*sy D*dx+E*dy+F |
                                         | G H I | |  0  0  1 |   | G*sx H*sy G*dx+H*dy+I |

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
        @param px  pivot on x-axis
        @param py  pivot on y-axis
    */
    SkMatrix& preScale(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from scaling by (sx, sy)
        about pivot point (0, 0).
        This can be thought of as scaling about the origin before applying SkMatrix.

        Given:

                     | A B C |               | sx  0  0 |
            Matrix = | D E F |,  S(sx, sy) = |  0 sy  0 |
                     | G H I |               |  0  0  1 |

        sets SkMatrix to:

                                 | A B C | | sx  0  0 |   | A*sx B*sy C |
            Matrix * S(sx, sy) = | D E F | |  0 sy  0 | = | D*sx E*sy F |
                                 | G H I | |  0  0  1 |   | G*sx H*sy I |

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
    */
    SkMatrix& preScale(SkScalar sx, SkScalar sy);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from rotating by degrees
        about pivot point (px, py).
        This can be thought of as rotating about a pivot point before applying SkMatrix.

        Positive degrees rotates clockwise.

        Given:

                     | A B C |                        | c -s dx |
            Matrix = | D E F |,  R(degrees, px, py) = | s  c dy |
                     | G H I |                        | 0  0  1 |

        where

            c  = cos(degrees)
            s  = sin(degrees)
            dx =  s * py + (1 - c) * px
            dy = -s * px + (1 - c) * py

        sets SkMatrix to:

                                          | A B C | | c -s dx |   | Ac+Bs -As+Bc A*dx+B*dy+C |
            Matrix * R(degrees, px, py) = | D E F | | s  c dy | = | Dc+Es -Ds+Ec D*dx+E*dy+F |
                                          | G H I | | 0  0  1 |   | Gc+Hs -Gs+Hc G*dx+H*dy+I |

        @param degrees  angle of axes relative to upright axes
        @param px       pivot on x-axis
        @param py       pivot on y-axis
    */
    SkMatrix& preRotate(SkScalar degrees, SkScalar px, SkScalar py);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from rotating by degrees
        about pivot point (0, 0).
        This can be thought of as rotating about the origin before applying SkMatrix.

        Positive degrees rotates clockwise.

        Given:

                     | A B C |                        | c -s 0 |
            Matrix = | D E F |,  R(degrees, px, py) = | s  c 0 |
                     | G H I |                        | 0  0 1 |

        where

            c  = cos(degrees)
            s  = sin(degrees)

        sets SkMatrix to:

                                          | A B C | | c -s 0 |   | Ac+Bs -As+Bc C |
            Matrix * R(degrees, px, py) = | D E F | | s  c 0 | = | Dc+Es -Ds+Ec F |
                                          | G H I | | 0  0 1 |   | Gc+Hs -Gs+Hc I |

        @param degrees  angle of axes relative to upright axes
    */
    SkMatrix& preRotate(SkScalar degrees);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from skewing by (kx, ky)
        about pivot point (px, py).
        This can be thought of as skewing about a pivot point before applying SkMatrix.

        Given:

                     | A B C |                       |  1 kx dx |
            Matrix = | D E F |,  K(kx, ky, px, py) = | ky  1 dy |
                     | G H I |                       |  0  0  1 |

        where

            dx = -kx * py
            dy = -ky * px

        sets SkMatrix to:

                                         | A B C | |  1 kx dx |   | A+B*ky A*kx+B A*dx+B*dy+C |
            Matrix * K(kx, ky, px, py) = | D E F | | ky  1 dy | = | D+E*ky D*kx+E D*dx+E*dy+F |
                                         | G H I | |  0  0  1 |   | G+H*ky G*kx+H G*dx+H*dy+I |

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
        @param px  pivot on x-axis
        @param py  pivot on y-axis
    */
    SkMatrix& preSkew(SkScalar kx, SkScalar ky, SkScalar px, SkScalar py);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix constructed from skewing by (kx, ky)
        about pivot point (0, 0).
        This can be thought of as skewing about the origin before applying SkMatrix.

        Given:

                     | A B C |               |  1 kx 0 |
            Matrix = | D E F |,  K(kx, ky) = | ky  1 0 |
                     | G H I |               |  0  0 1 |

        sets SkMatrix to:

                                 | A B C | |  1 kx 0 |   | A+B*ky A*kx+B C |
            Matrix * K(kx, ky) = | D E F | | ky  1 0 | = | D+E*ky D*kx+E F |
                                 | G H I | |  0  0 1 |   | G+H*ky G*kx+H I |

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
    */
    SkMatrix& preSkew(SkScalar kx, SkScalar ky);

    /** Sets SkMatrix to SkMatrix multiplied by SkMatrix other.
        This can be thought of mapping by other before applying SkMatrix.

        Given:

                     | A B C |          | J K L |
            Matrix = | D E F |, other = | M N O |
                     | G H I |          | P Q R |

        sets SkMatrix to:

                             | A B C |   | J K L |   | AJ+BM+CP AK+BN+CQ AL+BO+CR |
            Matrix * other = | D E F | * | M N O | = | DJ+EM+FP DK+EN+FQ DL+EO+FR |
                             | G H I |   | P Q R |   | GJ+HM+IP GK+HN+IQ GL+HO+IR |

        @param other  SkMatrix on right side of multiply expression
    */
    SkMatrix& preConcat(const SkMatrix& other);

    /** Sets SkMatrix to SkMatrix constructed from translation (dx, dy) multiplied by SkMatrix.
        This can be thought of as moving the point to be mapped after applying SkMatrix.

        Given:

                     | J K L |               | 1 0 dx |
            Matrix = | M N O |,  T(dx, dy) = | 0 1 dy |
                     | P Q R |               | 0 0  1 |

        sets SkMatrix to:

                                 | 1 0 dx | | J K L |   | J+dx*P K+dx*Q L+dx*R |
            T(dx, dy) * Matrix = | 0 1 dy | | M N O | = | M+dy*P N+dy*Q O+dy*R |
                                 | 0 0  1 | | P Q R |   |      P      Q      R |

        @param dx  x-axis translation after applying SkMatrix
        @param dy  y-axis translation after applying SkMatrix
    */
    SkMatrix& postTranslate(SkScalar dx, SkScalar dy);

    /** Sets SkMatrix to SkMatrix constructed from scaling by (sx, sy) about pivot point
        (px, py), multiplied by SkMatrix.
        This can be thought of as scaling about a pivot point after applying SkMatrix.

        Given:

                     | J K L |                       | sx  0 dx |
            Matrix = | M N O |,  S(sx, sy, px, py) = |  0 sy dy |
                     | P Q R |                       |  0  0  1 |

        where

            dx = px - sx * px
            dy = py - sy * py

        sets SkMatrix to:

                                         | sx  0 dx | | J K L |   | sx*J+dx*P sx*K+dx*Q sx*L+dx+R |
            S(sx, sy, px, py) * Matrix = |  0 sy dy | | M N O | = | sy*M+dy*P sy*N+dy*Q sy*O+dy*R |
                                         |  0  0  1 | | P Q R |   |         P         Q         R |

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
        @param px  pivot on x-axis
        @param py  pivot on y-axis
    */
    SkMatrix& postScale(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py);

    /** Sets SkMatrix to SkMatrix constructed from scaling by (sx, sy) about pivot point
        (0, 0), multiplied by SkMatrix.
        This can be thought of as scaling about the origin after applying SkMatrix.

        Given:

                     | J K L |               | sx  0  0 |
            Matrix = | M N O |,  S(sx, sy) = |  0 sy  0 |
                     | P Q R |               |  0  0  1 |

        sets SkMatrix to:

                                 | sx  0  0 | | J K L |   | sx*J sx*K sx*L |
            S(sx, sy) * Matrix = |  0 sy  0 | | M N O | = | sy*M sy*N sy*O |
                                 |  0  0  1 | | P Q R |   |    P    Q    R |

        @param sx  horizontal scale factor
        @param sy  vertical scale factor
    */
    SkMatrix& postScale(SkScalar sx, SkScalar sy);

    /** Sets SkMatrix to SkMatrix constructed from rotating by degrees about pivot point
        (px, py), multiplied by SkMatrix.
        This can be thought of as rotating about a pivot point after applying SkMatrix.

        Positive degrees rotates clockwise.

        Given:

                     | J K L |                        | c -s dx |
            Matrix = | M N O |,  R(degrees, px, py) = | s  c dy |
                     | P Q R |                        | 0  0  1 |

        where

            c  = cos(degrees)
            s  = sin(degrees)
            dx =  s * py + (1 - c) * px
            dy = -s * px + (1 - c) * py

        sets SkMatrix to:

                                          |c -s dx| |J K L|   |cJ-sM+dx*P cK-sN+dx*Q cL-sO+dx+R|
            R(degrees, px, py) * Matrix = |s  c dy| |M N O| = |sJ+cM+dy*P sK+cN+dy*Q sL+cO+dy*R|
                                          |0  0  1| |P Q R|   |         P          Q          R|

        @param degrees  angle of axes relative to upright axes
        @param px       pivot on x-axis
        @param py       pivot on y-axis
    */
    SkMatrix& postRotate(SkScalar degrees, SkScalar px, SkScalar py);

    /** Sets SkMatrix to SkMatrix constructed from rotating by degrees about pivot point
        (0, 0), multiplied by SkMatrix.
        This can be thought of as rotating about the origin after applying SkMatrix.

        Positive degrees rotates clockwise.

        Given:

                     | J K L |                        | c -s 0 |
            Matrix = | M N O |,  R(degrees, px, py) = | s  c 0 |
                     | P Q R |                        | 0  0 1 |

        where

            c  = cos(degrees)
            s  = sin(degrees)

        sets SkMatrix to:

                                          | c -s dx | | J K L |   | cJ-sM cK-sN cL-sO |
            R(degrees, px, py) * Matrix = | s  c dy | | M N O | = | sJ+cM sK+cN sL+cO |
                                          | 0  0  1 | | P Q R |   |     P     Q     R |

        @param degrees  angle of axes relative to upright axes
    */
    SkMatrix& postRotate(SkScalar degrees);

    /** Sets SkMatrix to SkMatrix constructed from skewing by (kx, ky) about pivot point
        (px, py), multiplied by SkMatrix.
        This can be thought of as skewing about a pivot point after applying SkMatrix.

        Given:

                     | J K L |                       |  1 kx dx |
            Matrix = | M N O |,  K(kx, ky, px, py) = | ky  1 dy |
                     | P Q R |                       |  0  0  1 |

        where

            dx = -kx * py
            dy = -ky * px

        sets SkMatrix to:

                                         | 1 kx dx| |J K L|   |J+kx*M+dx*P K+kx*N+dx*Q L+kx*O+dx+R|
            K(kx, ky, px, py) * Matrix = |ky  1 dy| |M N O| = |ky*J+M+dy*P ky*K+N+dy*Q ky*L+O+dy*R|
                                         | 0  0  1| |P Q R|   |          P           Q           R|

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
        @param px  pivot on x-axis
        @param py  pivot on y-axis
    */
    SkMatrix& postSkew(SkScalar kx, SkScalar ky, SkScalar px, SkScalar py);

    /** Sets SkMatrix to SkMatrix constructed from skewing by (kx, ky) about pivot point
        (0, 0), multiplied by SkMatrix.
        This can be thought of as skewing about the origin after applying SkMatrix.

        Given:

                     | J K L |               |  1 kx 0 |
            Matrix = | M N O |,  K(kx, ky) = | ky  1 0 |
                     | P Q R |               |  0  0 1 |

        sets SkMatrix to:

                                 |  1 kx 0 | | J K L |   | J+kx*M K+kx*N L+kx*O |
            K(kx, ky) * Matrix = | ky  1 0 | | M N O | = | ky*J+M ky*K+N ky*L+O |
                                 |  0  0 1 | | P Q R |   |      P      Q      R |

        @param kx  horizontal skew factor
        @param ky  vertical skew factor
    */
    SkMatrix& postSkew(SkScalar kx, SkScalar ky);

    /** Sets SkMatrix to SkMatrix other multiplied by SkMatrix.
        This can be thought of mapping by other after applying SkMatrix.

        Given:

                     | J K L |           | A B C |
            Matrix = | M N O |,  other = | D E F |
                     | P Q R |           | G H I |

        sets SkMatrix to:

                             | A B C |   | J K L |   | AJ+BM+CP AK+BN+CQ AL+BO+CR |
            other * Matrix = | D E F | * | M N O | = | DJ+EM+FP DK+EN+FQ DL+EO+FR |
                             | G H I |   | P Q R |   | GJ+HM+IP GK+HN+IQ GL+HO+IR |

        @param other  SkMatrix on left side of multiply expression
    */
    SkMatrix& postConcat(const SkMatrix& other);

#ifndef SK_SUPPORT_LEGACY_MATRIX_RECTTORECT
private:
#endif
    /** Sets SkMatrix to scale and translate src SkRect to dst SkRect. stf selects whether
        mapping completely fills dst or preserves the aspect ratio, and how to align
        src within dst. Returns false if src is empty, and sets SkMatrix to identity.
        Returns true if dst is empty, and sets SkMatrix to:

            | 0 0 0 |
            | 0 0 0 |
            | 0 0 1 |

        @param src  SkRect to map from
        @param dst  SkRect to map to
        @return     true if SkMatrix can represent SkRect mapping

        example: https://fiddle.skia.org/c/@Matrix_setRectToRect
    */
    bool setRectToRect(const SkRect& src, const SkRect& dst, ScaleToFit stf);

    /** Returns SkMatrix set to scale and translate src SkRect to dst SkRect. stf selects
        whether mapping completely fills dst or preserves the aspect ratio, and how to
        align src within dst. Returns the identity SkMatrix if src is empty. If dst is
        empty, returns SkMatrix set to:

            | 0 0 0 |
            | 0 0 0 |
            | 0 0 1 |

        @param src  SkRect to map from
        @param dst  SkRect to map to
        @return     SkMatrix mapping src to dst
    */
    static SkMatrix MakeRectToRect(const SkRect& src, const SkRect& dst, ScaleToFit stf) {
        SkMatrix m;
        m.setRectToRect(src, dst, stf);
        return m;
    }
#ifndef SK_SUPPORT_LEGACY_MATRIX_RECTTORECT
public:
#endif

    /** Sets SkMatrix to map src to dst. count must be zero or greater, and four or less.

        If count is zero, sets SkMatrix to identity and returns true.
        If count is one, sets SkMatrix to translate and returns true.
        If count is two or more, sets SkMatrix to map SkPoint if possible; returns false
        if SkMatrix cannot be constructed. If count is four, SkMatrix may include
        perspective.

        @param src    SkPoint to map from
        @param dst    SkPoint to map to
        @param count  number of SkPoint in src and dst
        @return       true if SkMatrix was constructed successfully

        example: https://fiddle.skia.org/c/@Matrix_setPolyToPoly
    */
    bool setPolyToPoly(const SkPoint src[], const SkPoint dst[], int count);

    /** Sets inverse to reciprocal matrix, returning true if SkMatrix can be inverted.
        Geometrically, if SkMatrix maps from source to destination, inverse SkMatrix
        maps from destination to source. If SkMatrix can not be inverted, inverse is
        unchanged.

        @param inverse  storage for inverted SkMatrix; may be nullptr
        @return         true if SkMatrix can be inverted
    */
    [[nodiscard]] bool invert(SkMatrix* inverse) const {
        // Allow the trivial case to be inlined.
        if (this->isIdentity()) {
            if (inverse) {
                inverse->reset();
            }
            return true;
        }
        return this->invertNonIdentity(inverse);
    }

    /** Fills affine with identity values in column major order.
        Sets affine to:

            | 1 0 0 |
            | 0 1 0 |

        Affine 3 by 2 matrices in column major order are used by OpenGL and XPS.

        @param affine  storage for 3 by 2 affine matrix

        example: https://fiddle.skia.org/c/@Matrix_SetAffineIdentity
    */
    static void SetAffineIdentity(SkScalar affine[6]);

    /** Fills affine in column major order. Sets affine to:

            | scale-x  skew-x translate-x |
            | skew-y  scale-y translate-y |

        If SkMatrix contains perspective, returns false and leaves affine unchanged.

        @param affine  storage for 3 by 2 affine matrix; may be nullptr
        @return        true if SkMatrix does not contain perspective
    */
    [[nodiscard]] bool asAffine(SkScalar affine[6]) const;

    /** Sets SkMatrix to affine values, passed in column major order. Given affine,
        column, then row, as:

            | scale-x  skew-x translate-x |
            |  skew-y scale-y translate-y |

        SkMatrix is set, row, then column, to:

            | scale-x  skew-x translate-x |
            |  skew-y scale-y translate-y |
            |       0       0           1 |

        @param affine  3 by 2 affine matrix
    */
    SkMatrix& setAffine(const SkScalar affine[6]);

    /**
     *  A matrix is categorized as 'perspective' if the bottom row is not [0, 0, 1].
     *  However, for most uses (e.g. mapPoints) a bottom row of [0, 0, X] behaves like a
     *  non-perspective matrix, though it will be categorized as perspective. Calling
     *  normalizePerspective() will change the matrix such that, if its bottom row was [0, 0, X],
     *  it will be changed to [0, 0, 1] by scaling the rest of the matrix by 1/X.
     *
     *  | A B C |    | A/X B/X C/X |
     *  | D E F | -> | D/X E/X F/X |   for X != 0
     *  | 0 0 X |    |  0   0   1  |
     */
    void normalizePerspective() {
        if (fMat[8] != 1) {
            this->doNormalizePerspective();
        }
    }

    /** Maps src SkPoint array of length count to dst SkPoint array of equal or greater
        length. SkPoint are mapped by multiplying each SkPoint by SkMatrix. Given:

                     | A B C |        | x |
            Matrix = | D E F |,  pt = | y |
                     | G H I |        | 1 |

        where

            for (i = 0; i < count; ++i) {
                x = src[i].fX
                y = src[i].fY
            }

        each dst SkPoint is computed as:

                          |A B C| |x|                               Ax+By+C   Dx+Ey+F
            Matrix * pt = |D E F| |y| = |Ax+By+C Dx+Ey+F Gx+Hy+I| = ------- , -------
                          |G H I| |1|                               Gx+Hy+I   Gx+Hy+I

        src and dst may point to the same storage.

        @param dst    storage for mapped SkPoint
        @param src    SkPoint to transform
        @param count  number of SkPoint to transform

        example: https://fiddle.skia.org/c/@Matrix_mapPoints
    */
    void mapPoints(SkPoint dst[], const SkPoint src[], int count) const;

    /** Maps pts SkPoint array of length count in place. SkPoint are mapped by multiplying
        each SkPoint by SkMatrix. Given:

                     | A B C |        | x |
            Matrix = | D E F |,  pt = | y |
                     | G H I |        | 1 |

        where

            for (i = 0; i < count; ++i) {
                x = pts[i].fX
                y = pts[i].fY
            }

        each resulting pts SkPoint is computed as:

                          |A B C| |x|                               Ax+By+C   Dx+Ey+F
            Matrix * pt = |D E F| |y| = |Ax+By+C Dx+Ey+F Gx+Hy+I| = ------- , -------
                          |G H I| |1|                               Gx+Hy+I   Gx+Hy+I

        @param pts    storage for mapped SkPoint
        @param count  number of SkPoint to transform
    */
    void mapPoints(SkPoint pts[], int count) const {
        this->mapPoints(pts, pts, count);
    }

    /** Maps src SkPoint3 array of length count to dst SkPoint3 array, which must of length count or
        greater. SkPoint3 array is mapped by multiplying each SkPoint3 by SkMatrix. Given:

                     | A B C |         | x |
            Matrix = | D E F |,  src = | y |
                     | G H I |         | z |

        each resulting dst SkPoint is computed as:

                           |A B C| |x|
            Matrix * src = |D E F| |y| = |Ax+By+Cz Dx+Ey+Fz Gx+Hy+Iz|
                           |G H I| |z|

        @param dst    storage for mapped SkPoint3 array
        @param src    SkPoint3 array to transform
        @param count  items in SkPoint3 array to transform

        example: https://fiddle.skia.org/c/@Matrix_mapHomogeneousPoints
    */
    void mapHomogeneousPoints(SkPoint3 dst[], const SkPoint3 src[], int count) const;

    /**
     *  Returns homogeneous points, starting with 2D src points (with implied w = 1).
     */
    void mapHomogeneousPoints(SkPoint3 dst[], const SkPoint src[], int count) const;

    /** Returns SkPoint pt multiplied by SkMatrix. Given:

                     | A B C |        | x |
            Matrix = | D E F |,  pt = | y |
                     | G H I |        | 1 |

        result is computed as:

                          |A B C| |x|                               Ax+By+C   Dx+Ey+F
            Matrix * pt = |D E F| |y| = |Ax+By+C Dx+Ey+F Gx+Hy+I| = ------- , -------
                          |G H I| |1|                               Gx+Hy+I   Gx+Hy+I

        @param p  SkPoint to map
        @return   mapped SkPoint
    */
    SkPoint mapPoint(SkPoint pt) const {
        SkPoint result;
        this->mapXY(pt.x(), pt.y(), &result);
        return result;
    }

    /** Maps SkPoint (x, y) to result. SkPoint is mapped by multiplying by SkMatrix. Given:

                     | A B C |        | x |
            Matrix = | D E F |,  pt = | y |
                     | G H I |        | 1 |

        result is computed as:

                          |A B C| |x|                               Ax+By+C   Dx+Ey+F
            Matrix * pt = |D E F| |y| = |Ax+By+C Dx+Ey+F Gx+Hy+I| = ------- , -------
                          |G H I| |1|                               Gx+Hy+I   Gx+Hy+I

        @param x       x-axis value of SkPoint to map
        @param y       y-axis value of SkPoint to map
        @param result  storage for mapped SkPoint

        example: https://fiddle.skia.org/c/@Matrix_mapXY
    */
    void mapXY(SkScalar x, SkScalar y, SkPoint* result) const;

    /** Returns SkPoint (x, y) multiplied by SkMatrix. Given:

                     | A B C |        | x |
            Matrix = | D E F |,  pt = | y |
                     | G H I |        | 1 |

        result is computed as:

                          |A B C| |x|                               Ax+By+C   Dx+Ey+F
            Matrix * pt = |D E F| |y| = |Ax+By+C Dx+Ey+F Gx+Hy+I| = ------- , -------
                          |G H I| |1|                               Gx+Hy+I   Gx+Hy+I

        @param x  x-axis value of SkPoint to map
        @param y  y-axis value of SkPoint to map
        @return   mapped SkPoint
    */
    SkPoint mapXY(SkScalar x, SkScalar y) const {
        SkPoint result;
        this->mapXY(x,y, &result);
        return result;
    }


    /** Returns (0, 0) multiplied by SkMatrix. Given:

                     | A B C |        | 0 |
            Matrix = | D E F |,  pt = | 0 |
                     | G H I |        | 1 |

        result is computed as:

                          |A B C| |0|             C    F
            Matrix * pt = |D E F| |0| = |C F I| = -  , -
                          |G H I| |1|             I    I

        @return   mapped (0, 0)
    */
    SkPoint mapOrigin() const {
        SkScalar x = this->getTranslateX(),
                 y = this->getTranslateY();
        if (this->hasPerspective()) {
            SkScalar w = fMat[kMPersp2];
            if (w) { w = 1 / w; }
            x *= w;
            y *= w;
        }
        return {x, y};
    }

    /** Maps src vector array of length count to vector SkPoint array of equal or greater
        length. Vectors are mapped by multiplying each vector by SkMatrix, treating
        SkMatrix translation as zero. Given:

                     | A B 0 |         | x |
            Matrix = | D E 0 |,  src = | y |
                     | G H I |         | 1 |

        where

            for (i = 0; i < count; ++i) {
                x = src[i].fX
                y = src[i].fY
            }

        each dst vector is computed as:

                           |A B 0| |x|                            Ax+By     Dx+Ey
            Matrix * src = |D E 0| |y| = |Ax+By Dx+Ey Gx+Hy+I| = ------- , -------
                           |G H I| |1|                           Gx+Hy+I   Gx+Hy+I

        src and dst may point to the same storage.

        @param dst    storage for mapped vectors
        @param src    vectors to transform
        @param count  number of vectors to transform

        example: https://fiddle.skia.org/c/@Matrix_mapVectors
    */
    void mapVectors(SkVector dst[], const SkVector src[], int count) const;

    /** Maps vecs vector array of length count in place, multiplying each vector by
        SkMatrix, treating SkMatrix translation as zero. Given:

                     | A B 0 |         | x |
            Matrix = | D E 0 |,  vec = | y |
                     | G H I |         | 1 |

        where

            for (i = 0; i < count; ++i) {
                x = vecs[i].fX
                y = vecs[i].fY
            }

        each result vector is computed as:

                           |A B 0| |x|                            Ax+By     Dx+Ey
            Matrix * vec = |D E 0| |y| = |Ax+By Dx+Ey Gx+Hy+I| = ------- , -------
                           |G H I| |1|                           Gx+Hy+I   Gx+Hy+I

        @param vecs   vectors to transform, and storage for mapped vectors
        @param count  number of vectors to transform
    */
    void mapVectors(SkVector vecs[], int count) const {
        this->mapVectors(vecs, vecs, count);
    }

    /** Maps vector (dx, dy) to result. Vector is mapped by multiplying by SkMatrix,
        treating SkMatrix translation as zero. Given:

                     | A B 0 |         | dx |
            Matrix = | D E 0 |,  vec = | dy |
                     | G H I |         |  1 |

        each result vector is computed as:

                       |A B 0| |dx|                                        A*dx+B*dy     D*dx+E*dy
        Matrix * vec = |D E 0| |dy| = |A*dx+B*dy D*dx+E*dy G*dx+H*dy+I| = ----------- , -----------
                       |G H I| | 1|                                       G*dx+H*dy+I   G*dx+*dHy+I

        @param dx      x-axis value of vector to map
        @param dy      y-axis value of vector to map
        @param result  storage for mapped vector
    */
    void mapVector(SkScalar dx, SkScalar dy, SkVector* result) const {
        SkVector vec = { dx, dy };
        this->mapVectors(result, &vec, 1);
    }

    /** Returns vector (dx, dy) multiplied by SkMatrix, treating SkMatrix translation as zero.
        Given:

                     | A B 0 |         | dx |
            Matrix = | D E 0 |,  vec = | dy |
                     | G H I |         |  1 |

        each result vector is computed as:

                       |A B 0| |dx|                                        A*dx+B*dy     D*dx+E*dy
        Matrix * vec = |D E 0| |dy| = |A*dx+B*dy D*dx+E*dy G*dx+H*dy+I| = ----------- , -----------
                       |G H I| | 1|                                       G*dx+H*dy+I   G*dx+*dHy+I

        @param dx  x-axis value of vector to map
        @param dy  y-axis value of vector to map
        @return    mapped vector
    */
    SkVector mapVector(SkScalar dx, SkScalar dy) const {
        SkVector vec = { dx, dy };
        this->mapVectors(&vec, &vec, 1);
        return vec;
    }

    /** Sets dst to bounds of src corners mapped by SkMatrix.
        Returns true if mapped corners are dst corners.

        Returned value is the same as calling rectStaysRect().

        @param dst  storage for bounds of mapped SkPoint
        @param src  SkRect to map
        @param pc   whether to apply perspective clipping
        @return     true if dst is equivalent to mapped src

        example: https://fiddle.skia.org/c/@Matrix_mapRect
    */
    bool mapRect(SkRect* dst, const SkRect& src,
                 SkApplyPerspectiveClip pc = SkApplyPerspectiveClip::kYes) const;

    /** Sets rect to bounds of rect corners mapped by SkMatrix.
        Returns true if mapped corners are computed rect corners.

        Returned value is the same as calling rectStaysRect().

        @param rect  rectangle to map, and storage for bounds of mapped corners
        @param pc    whether to apply perspective clipping
        @return      true if result is equivalent to mapped rect
    */
    bool mapRect(SkRect* rect, SkApplyPerspectiveClip pc = SkApplyPerspectiveClip::kYes) const {
        return this->mapRect(rect, *rect, pc);
    }

    /** Returns bounds of src corners mapped by SkMatrix.

        @param src  rectangle to map
        @return     mapped bounds
    */
    SkRect mapRect(const SkRect& src,
                   SkApplyPerspectiveClip pc = SkApplyPerspectiveClip::kYes) const {
        SkRect dst;
        (void)this->mapRect(&dst, src, pc);
        return dst;
    }

    /** Maps four corners of rect to dst. SkPoint are mapped by multiplying each
        rect corner by SkMatrix. rect corner is processed in this order:
        (rect.fLeft, rect.fTop), (rect.fRight, rect.fTop), (rect.fRight, rect.fBottom),
        (rect.fLeft, rect.fBottom).

        rect may be empty: rect.fLeft may be greater than or equal to rect.fRight;
        rect.fTop may be greater than or equal to rect.fBottom.

        Given:

                     | A B C |        | x |
            Matrix = | D E F |,  pt = | y |
                     | G H I |        | 1 |

        where pt is initialized from each of (rect.fLeft, rect.fTop),
        (rect.fRight, rect.fTop), (rect.fRight, rect.fBottom), (rect.fLeft, rect.fBottom),
        each dst SkPoint is computed as:

                          |A B C| |x|                               Ax+By+C   Dx+Ey+F
            Matrix * pt = |D E F| |y| = |Ax+By+C Dx+Ey+F Gx+Hy+I| = ------- , -------
                          |G H I| |1|                               Gx+Hy+I   Gx+Hy+I

        @param dst   storage for mapped corner SkPoint
        @param rect  SkRect to map

        Note: this does not perform perspective clipping (as that might result in more than
              4 points, so results are suspect if the matrix contains perspective.
    */
    void mapRectToQuad(SkPoint dst[4], const SkRect& rect) const {
        // This could potentially be faster if we only transformed each x and y of the rect once.
        rect.toQuad(dst);
        this->mapPoints(dst, 4);
    }

    /** Sets dst to bounds of src corners mapped by SkMatrix. If matrix contains
        elements other than scale or translate: asserts if SK_DEBUG is defined;
        otherwise, results are undefined.

        @param dst  storage for bounds of mapped SkPoint
        @param src  SkRect to map

        example: https://fiddle.skia.org/c/@Matrix_mapRectScaleTranslate
    */
    void mapRectScaleTranslate(SkRect* dst, const SkRect& src) const;

    /** Returns geometric mean radius of ellipse formed by constructing circle of
        size radius, and mapping constructed circle with SkMatrix. The result squared is
        equal to the major axis length times the minor axis length.
        Result is not meaningful if SkMatrix contains perspective elements.

        @param radius  circle size to map
        @return        average mapped radius

        example: https://fiddle.skia.org/c/@Matrix_mapRadius
    */
    SkScalar mapRadius(SkScalar radius) const;

    /** Compares a and b; returns true if a and b are numerically equal. Returns true
        even if sign of zero values are different. Returns false if either SkMatrix
        contains NaN, even if the other SkMatrix also contains NaN.

        @param a  SkMatrix to compare
        @param b  SkMatrix to compare
        @return   true if SkMatrix a and SkMatrix b are numerically equal
    */
    friend SK_API bool operator==(const SkMatrix& a, const SkMatrix& b);

    /** Compares a and b; returns true if a and b are not numerically equal. Returns false
        even if sign of zero values are different. Returns true if either SkMatrix
        contains NaN, even if the other SkMatrix also contains NaN.

        @param a  SkMatrix to compare
        @param b  SkMatrix to compare
        @return   true if SkMatrix a and SkMatrix b are numerically not equal
    */
    friend SK_API bool operator!=(const SkMatrix& a, const SkMatrix& b) {
        return !(a == b);
    }

    /** Writes text representation of SkMatrix to standard output. Floating point values
        are written with limited precision; it may not be possible to reconstruct
        original SkMatrix from output.

        example: https://fiddle.skia.org/c/@Matrix_dump
    */
    void dump() const;

    /** Returns the minimum scaling factor of SkMatrix by decomposing the scaling and
        skewing elements.
        Returns -1 if scale factor overflows or SkMatrix contains perspective.

        @return  minimum scale factor

        example: https://fiddle.skia.org/c/@Matrix_getMinScale
    */
    SkScalar getMinScale() const;

    /** Returns the maximum scaling factor of SkMatrix by decomposing the scaling and
        skewing elements.
        Returns -1 if scale factor overflows or SkMatrix contains perspective.

        @return  maximum scale factor

        example: https://fiddle.skia.org/c/@Matrix_getMaxScale
    */
    SkScalar getMaxScale() const;

    /** Sets scaleFactors[0] to the minimum scaling factor, and scaleFactors[1] to the
        maximum scaling factor. Scaling factors are computed by decomposing
        the SkMatrix scaling and skewing elements.

        Returns true if scaleFactors are found; otherwise, returns false and sets
        scaleFactors to undefined values.

        @param scaleFactors  storage for minimum and maximum scale factors
        @return              true if scale factors were computed correctly
    */
    [[nodiscard]] bool getMinMaxScales(SkScalar scaleFactors[2]) const;

    /** Decomposes SkMatrix into scale components and whatever remains. Returns false if
        SkMatrix could not be decomposed.

        Sets scale to portion of SkMatrix that scale axes. Sets remaining to SkMatrix
        with scaling factored out. remaining may be passed as nullptr
        to determine if SkMatrix can be decomposed without computing remainder.

        Returns true if scale components are found. scale and remaining are
        unchanged if SkMatrix contains perspective; scale factors are not finite, or
        are nearly zero.

        On success: Matrix = Remaining * scale.

        @param scale      axes scaling factors; may be nullptr
        @param remaining  SkMatrix without scaling; may be nullptr
        @return           true if scale can be computed

        example: https://fiddle.skia.org/c/@Matrix_decomposeScale
    */
    bool decomposeScale(SkSize* scale, SkMatrix* remaining = nullptr) const;

    /** Returns reference to const identity SkMatrix. Returned SkMatrix is set to:

            | 1 0 0 |
            | 0 1 0 |
            | 0 0 1 |

        @return  const identity SkMatrix

        example: https://fiddle.skia.org/c/@Matrix_I
    */
    static const SkMatrix& I();

    /** Returns reference to a const SkMatrix with invalid values. Returned SkMatrix is set
        to:

            | SK_ScalarMax SK_ScalarMax SK_ScalarMax |
            | SK_ScalarMax SK_ScalarMax SK_ScalarMax |
            | SK_ScalarMax SK_ScalarMax SK_ScalarMax |

        @return  const invalid SkMatrix

        example: https://fiddle.skia.org/c/@Matrix_InvalidMatrix
    */
    static const SkMatrix& InvalidMatrix();

    /** Returns SkMatrix a multiplied by SkMatrix b.

        Given:

                | A B C |      | J K L |
            a = | D E F |, b = | M N O |
                | G H I |      | P Q R |

        sets SkMatrix to:

                    | A B C |   | J K L |   | AJ+BM+CP AK+BN+CQ AL+BO+CR |
            a * b = | D E F | * | M N O | = | DJ+EM+FP DK+EN+FQ DL+EO+FR |
                    | G H I |   | P Q R |   | GJ+HM+IP GK+HN+IQ GL+HO+IR |

        @param a  SkMatrix on left side of multiply expression
        @param b  SkMatrix on right side of multiply expression
        @return   SkMatrix computed from a times b
    */
    static SkMatrix Concat(const SkMatrix& a, const SkMatrix& b) {
        SkMatrix result;
        result.setConcat(a, b);
        return result;
    }

    friend SkMatrix operator*(const SkMatrix& a, const SkMatrix& b) {
        return Concat(a, b);
    }

    /** Sets internal cache to unknown state. Use to force update after repeated
        modifications to SkMatrix element reference returned by operator[](int index).
    */
    void dirtyMatrixTypeCache() {
        this->setTypeMask(kUnknown_Mask);
    }

    /** Initializes SkMatrix with scale and translate elements.

            | sx  0 tx |
            |  0 sy ty |
            |  0  0  1 |

        @param sx  horizontal scale factor to store
        @param sy  vertical scale factor to store
        @param tx  horizontal translation to store
        @param ty  vertical translation to store
    */
    void setScaleTranslate(SkScalar sx, SkScalar sy, SkScalar tx, SkScalar ty) {
        fMat[kMScaleX] = sx;
        fMat[kMSkewX]  = 0;
        fMat[kMTransX] = tx;

        fMat[kMSkewY]  = 0;
        fMat[kMScaleY] = sy;
        fMat[kMTransY] = ty;

        fMat[kMPersp0] = 0;
        fMat[kMPersp1] = 0;
        fMat[kMPersp2] = 1;

        int mask = 0;
        if (sx != 1 || sy != 1) {
            mask |= kScale_Mask;
        }
        if (tx != 0.0f || ty != 0.0f) {
            mask |= kTranslate_Mask;
        }
        if (sx != 0 && sy != 0) {
            mask |= kRectStaysRect_Mask;
        }
        this->setTypeMask(mask);
    }

    /** Returns true if all elements of the matrix are finite. Returns false if any
        element is infinity, or NaN.

        @return  true if matrix has only finite elements
    */
    bool isFinite() const { return SkScalarsAreFinite(fMat, 9); }

private:
    /** Set if the matrix will map a rectangle to another rectangle. This
        can be true if the matrix is scale-only, or rotates a multiple of
        90 degrees.

        This bit will be set on identity matrices
    */
    static constexpr int kRectStaysRect_Mask = 0x10;

    /** Set if the perspective bit is valid even though the rest of
        the matrix is Unknown.
    */
    static constexpr int kOnlyPerspectiveValid_Mask = 0x40;

    static constexpr int kUnknown_Mask = 0x80;

    static constexpr int kORableMasks = kTranslate_Mask |
                                        kScale_Mask |
                                        kAffine_Mask |
                                        kPerspective_Mask;

    static constexpr int kAllMasks = kTranslate_Mask |
                                     kScale_Mask |
                                     kAffine_Mask |
                                     kPerspective_Mask |
                                     kRectStaysRect_Mask;

    SkScalar        fMat[9];
    mutable int32_t fTypeMask;

    constexpr SkMatrix(SkScalar sx, SkScalar kx, SkScalar tx,
                       SkScalar ky, SkScalar sy, SkScalar ty,
                       SkScalar p0, SkScalar p1, SkScalar p2, int typeMask)
        : fMat{sx, kx, tx,
               ky, sy, ty,
               p0, p1, p2}
        , fTypeMask(typeMask) {}

    static void ComputeInv(SkScalar dst[9], const SkScalar src[9], double invDet, bool isPersp);

    uint8_t computeTypeMask() const;
    uint8_t computePerspectiveTypeMask() const;

    void setTypeMask(int mask) {
        // allow kUnknown or a valid mask
        SkASSERT(kUnknown_Mask == mask || (mask & kAllMasks) == mask ||
                 ((kUnknown_Mask | kOnlyPerspectiveValid_Mask) & mask)
                 == (kUnknown_Mask | kOnlyPerspectiveValid_Mask));
        fTypeMask = mask;
    }

    void orTypeMask(int mask) {
        SkASSERT((mask & kORableMasks) == mask);
        fTypeMask |= mask;
    }

    void clearTypeMask(int mask) {
        // only allow a valid mask
        SkASSERT((mask & kAllMasks) == mask);
        fTypeMask &= ~mask;
    }

    TypeMask getPerspectiveTypeMaskOnly() const {
        if ((fTypeMask & kUnknown_Mask) &&
            !(fTypeMask & kOnlyPerspectiveValid_Mask)) {
            fTypeMask = this->computePerspectiveTypeMask();
        }
        return (TypeMask)(fTypeMask & 0xF);
    }

    /** Returns true if we already know that the matrix is identity;
        false otherwise.
    */
    bool isTriviallyIdentity() const {
        if (fTypeMask & kUnknown_Mask) {
            return false;
        }
        return ((fTypeMask & 0xF) == 0);
    }

    inline void updateTranslateMask() {
        if ((fMat[kMTransX] != 0) | (fMat[kMTransY] != 0)) {
            fTypeMask |= kTranslate_Mask;
        } else {
            fTypeMask &= ~kTranslate_Mask;
        }
    }

    typedef void (*MapXYProc)(const SkMatrix& mat, SkScalar x, SkScalar y,
                                 SkPoint* result);

    static MapXYProc GetMapXYProc(TypeMask mask) {
        SkASSERT((mask & ~kAllMasks) == 0);
        return gMapXYProcs[mask & kAllMasks];
    }

    MapXYProc getMapXYProc() const {
        return GetMapXYProc(this->getType());
    }

    typedef void (*MapPtsProc)(const SkMatrix& mat, SkPoint dst[],
                                  const SkPoint src[], int count);

    static MapPtsProc GetMapPtsProc(TypeMask mask) {
        SkASSERT((mask & ~kAllMasks) == 0);
        return gMapPtsProcs[mask & kAllMasks];
    }

    MapPtsProc getMapPtsProc() const {
        return GetMapPtsProc(this->getType());
    }

    [[nodiscard]] bool invertNonIdentity(SkMatrix* inverse) const;

    static bool Poly2Proc(const SkPoint[], SkMatrix*);
    static bool Poly3Proc(const SkPoint[], SkMatrix*);
    static bool Poly4Proc(const SkPoint[], SkMatrix*);

    static void Identity_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);
    static void Trans_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);
    static void Scale_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);
    static void ScaleTrans_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);
    static void Rot_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);
    static void RotTrans_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);
    static void Persp_xy(const SkMatrix&, SkScalar, SkScalar, SkPoint*);

    static const MapXYProc gMapXYProcs[];

    static void Identity_pts(const SkMatrix&, SkPoint[], const SkPoint[], int);
    static void Trans_pts(const SkMatrix&, SkPoint dst[], const SkPoint[], int);
    static void Scale_pts(const SkMatrix&, SkPoint dst[], const SkPoint[], int);
    static void ScaleTrans_pts(const SkMatrix&, SkPoint dst[], const SkPoint[],
                               int count);
    static void Persp_pts(const SkMatrix&, SkPoint dst[], const SkPoint[], int);

    static void Affine_vpts(const SkMatrix&, SkPoint dst[], const SkPoint[], int);

    static const MapPtsProc gMapPtsProcs[];

    // return the number of bytes written, whether or not buffer is null
    size_t writeToMemory(void* buffer) const;
    /**
     * Reads data from the buffer parameter
     *
     * @param buffer Memory to read from
     * @param length Amount of memory available in the buffer
     * @return number of bytes read (must be a multiple of 4) or
     *         0 if there was not enough memory available
     */
    size_t readFromMemory(const void* buffer, size_t length);

    // legacy method -- still needed? why not just postScale(1/divx, ...)?
    bool postIDiv(int divx, int divy);
    void doNormalizePerspective();

    friend class SkPerspIter;
    friend class SkMatrixPriv;
    friend class SerializationTest;
};
SK_END_REQUIRE_DENSE

enum class SkPathFillType {
    /** Specifies that "inside" is computed by a non-zero sum of signed edge crossings */
    kWinding,
    /** Specifies that "inside" is computed by an odd number of edge crossings */
    kEvenOdd,
    /** Same as Winding, but draws outside of the path, rather than inside */
    kInverseWinding,
    /** Same as EvenOdd, but draws outside of the path, rather than inside */
    kInverseEvenOdd
};

static inline bool SkPathFillType_IsEvenOdd(SkPathFillType ft) {
    return (static_cast<int>(ft) & 1) != 0;
}

static inline bool SkPathFillType_IsInverse(SkPathFillType ft) {
    return (static_cast<int>(ft) & 2) != 0;
}

static inline SkPathFillType SkPathFillType_ConvertToNonInverse(SkPathFillType ft) {
    return static_cast<SkPathFillType>(static_cast<int>(ft) & 1);
}

enum class SkPathDirection {
    /** clockwise direction for adding closed contours */
    kCW,
    /** counter-clockwise direction for adding closed contours */
    kCCW,
};

enum SkPathSegmentMask {
    kLine_SkPathSegmentMask   = 1 << 0,
    kQuad_SkPathSegmentMask   = 1 << 1,
    kConic_SkPathSegmentMask  = 1 << 2,
    kCubic_SkPathSegmentMask  = 1 << 3,
};

enum class SkPathVerb {
    kMove,   //!< SkPath::RawIter returns 1 point
    kLine,   //!< SkPath::RawIter returns 2 points
    kQuad,   //!< SkPath::RawIter returns 3 points
    kConic,  //!< SkPath::RawIter returns 3 points + 1 weight
    kCubic,  //!< SkPath::RawIter returns 4 points
    kClose   //!< SkPath::RawIter returns 0 points
};

/** \class SkRefCntBase

    SkRefCntBase is the base class for objects that may be shared by multiple
    objects. When an existing owner wants to share a reference, it calls ref().
    When an owner wants to release its reference, it calls unref(). When the
    shared object's reference count goes to zero as the result of an unref()
    call, its (virtual) destructor is called. It is an error for the
    destructor to be called explicitly (or via the object going out of scope on
    the stack or calling delete) if getRefCnt() > 1.
*/
class SK_API SkRefCntBase {
public:
    /** Default construct, initializing the reference count to 1.
    */
    SkRefCntBase() : fRefCnt(1) {}

    /** Destruct, asserting that the reference count is 1.
    */
    virtual ~SkRefCntBase() {
    #ifdef SK_DEBUG
        SkASSERTF(this->getRefCnt() == 1, "fRefCnt was %d", this->getRefCnt());
        // illegal value, to catch us if we reuse after delete
        fRefCnt.store(0, std::memory_order_relaxed);
    #endif
    }

    /** May return true if the caller is the only owner.
     *  Ensures that all previous owner's actions are complete.
     */
    bool unique() const {
        if (1 == fRefCnt.load(std::memory_order_acquire)) {
            // The acquire barrier is only really needed if we return true.  It
            // prevents code conditioned on the result of unique() from running
            // until previous owners are all totally done calling unref().
            return true;
        }
        return false;
    }

    /** Increment the reference count. Must be balanced by a call to unref().
    */
    void ref() const {
        SkASSERT(this->getRefCnt() > 0);
        // No barrier required.
        (void)fRefCnt.fetch_add(+1, std::memory_order_relaxed);
    }

    /** Decrement the reference count. If the reference count is 1 before the
        decrement, then delete the object. Note that if this is the case, then
        the object needs to have been allocated via new, and not on the stack.
    */
    void unref() const {
        SkASSERT(this->getRefCnt() > 0);
        // A release here acts in place of all releases we "should" have been doing in ref().
        if (1 == fRefCnt.fetch_add(-1, std::memory_order_acq_rel)) {
            // Like unique(), the acquire is only needed on success, to make sure
            // code in internal_dispose() doesn't happen before the decrement.
            this->internal_dispose();
        }
    }

private:

#ifdef SK_DEBUG
    /** Return the reference count. Use only for debugging. */
    int32_t getRefCnt() const {
        return fRefCnt.load(std::memory_order_relaxed);
    }
#endif

    /**
     *  Called when the ref count goes to 0.
     */
    virtual void internal_dispose() const {
    #ifdef SK_DEBUG
        SkASSERT(0 == this->getRefCnt());
        fRefCnt.store(1, std::memory_order_relaxed);
    #endif
        delete this;
    }

    // The following friends are those which override internal_dispose()
    // and conditionally call SkRefCnt::internal_dispose().
    friend class SkWeakRefCnt;

    mutable std::atomic<int32_t> fRefCnt;

    SkRefCntBase(SkRefCntBase&&) = delete;
    SkRefCntBase(const SkRefCntBase&) = delete;
    SkRefCntBase& operator=(SkRefCntBase&&) = delete;
    SkRefCntBase& operator=(const SkRefCntBase&) = delete;
};

#ifdef SK_REF_CNT_MIXIN_INCLUDE
// It is the responsibility of the following include to define the type SkRefCnt.
// This SkRefCnt should normally derive from SkRefCntBase.
#else
class SK_API SkRefCnt : public SkRefCntBase {
    // "#include SK_REF_CNT_MIXIN_INCLUDE" doesn't work with this build system.
    #if defined(SK_BUILD_FOR_GOOGLE3)
    public:
        void deref() const { this->unref(); }
    #endif
};
#endif

///////////////////////////////////////////////////////////////////////////////

/** Call obj->ref() and return obj. The obj must not be nullptr.
 */
template <typename T> static inline T* SkRef(T* obj) {
    SkASSERT(obj);
    obj->ref();
    return obj;
}

/** Check if the argument is non-null, and if so, call obj->ref() and return obj.
 */
template <typename T> static inline T* SkSafeRef(T* obj) {
    if (obj) {
        obj->ref();
    }
    return obj;
}

/** Check if the argument is non-null, and if so, call obj->unref()
 */
template <typename T> static inline void SkSafeUnref(T* obj) {
    if (obj) {
        obj->unref();
    }
}

///////////////////////////////////////////////////////////////////////////////

// This is a variant of SkRefCnt that's Not Virtual, so weighs 4 bytes instead of 8 or 16.
// There's only benefit to using this if the deriving class does not otherwise need a vtable.
template <typename Derived>
class SkNVRefCnt {
public:
    SkNVRefCnt() : fRefCnt(1) {}
    ~SkNVRefCnt() {
    #ifdef SK_DEBUG
        int rc = fRefCnt.load(std::memory_order_relaxed);
        SkASSERTF(rc == 1, "NVRefCnt was %d", rc);
    #endif
    }

    // Implementation is pretty much the same as SkRefCntBase. All required barriers are the same:
    //   - unique() needs acquire when it returns true, and no barrier if it returns false;
    //   - ref() doesn't need any barrier;
    //   - unref() needs a release barrier, and an acquire if it's going to call delete.

    bool unique() const { return 1 == fRefCnt.load(std::memory_order_acquire); }
    void ref() const { (void)fRefCnt.fetch_add(+1, std::memory_order_relaxed); }
    void unref() const {
        if (1 == fRefCnt.fetch_add(-1, std::memory_order_acq_rel)) {
            // restore the 1 for our destructor's assert
            SkDEBUGCODE(fRefCnt.store(1, std::memory_order_relaxed));
            delete (const Derived*)this;
        }
    }
    void  deref() const { this->unref(); }

    // This must be used with caution. It is only valid to call this when 'threadIsolatedTestCnt'
    // refs are known to be isolated to the current thread. That is, it is known that there are at
    // least 'threadIsolatedTestCnt' refs for which no other thread may make a balancing unref()
    // call. Assuming the contract is followed, if this returns false then no other thread has
    // ownership of this. If it returns true then another thread *may* have ownership.
    bool refCntGreaterThan(int32_t threadIsolatedTestCnt) const {
        int cnt = fRefCnt.load(std::memory_order_acquire);
        // If this fails then the above contract has been violated.
        SkASSERT(cnt >= threadIsolatedTestCnt);
        return cnt > threadIsolatedTestCnt;
    }

private:
    mutable std::atomic<int32_t> fRefCnt;

    SkNVRefCnt(SkNVRefCnt&&) = delete;
    SkNVRefCnt(const SkNVRefCnt&) = delete;
    SkNVRefCnt& operator=(SkNVRefCnt&&) = delete;
    SkNVRefCnt& operator=(const SkNVRefCnt&) = delete;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 *  Shared pointer class to wrap classes that support a ref()/unref() interface.
 *
 *  This can be used for classes inheriting from SkRefCnt, but it also works for other
 *  classes that match the interface, but have different internal choices: e.g. the hosted class
 *  may have its ref/unref be thread-safe, but that is not assumed/imposed by sk_sp.
 *
 *  Declared with the trivial_abi attribute where supported so that sk_sp and types containing it
 *  may be considered as trivially relocatable by the compiler so that destroying-move operations
 *  i.e. move constructor followed by destructor can be optimized to memcpy.
 */
template <typename T> class SK_TRIVIAL_ABI sk_sp {
public:
    using element_type = T;

    constexpr sk_sp() : fPtr(nullptr) {}
    constexpr sk_sp(std::nullptr_t) : fPtr(nullptr) {}

    /**
     *  Shares the underlying object by calling ref(), so that both the argument and the newly
     *  created sk_sp both have a reference to it.
     */
    sk_sp(const sk_sp<T>& that) : fPtr(SkSafeRef(that.get())) {}
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp(const sk_sp<U>& that) : fPtr(SkSafeRef(that.get())) {}

    /**
     *  Move the underlying object from the argument to the newly created sk_sp. Afterwards only
     *  the new sk_sp will have a reference to the object, and the argument will point to null.
     *  No call to ref() or unref() will be made.
     */
    sk_sp(sk_sp<T>&& that) : fPtr(that.release()) {}
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp(sk_sp<U>&& that) : fPtr(that.release()) {}

    /**
     *  Adopt the bare pointer into the newly created sk_sp.
     *  No call to ref() or unref() will be made.
     */
    explicit sk_sp(T* obj) : fPtr(obj) {}

    /**
     *  Calls unref() on the underlying object pointer.
     */
    ~sk_sp() {
        SkSafeUnref(fPtr);
        SkDEBUGCODE(fPtr = nullptr);
    }

    sk_sp<T>& operator=(std::nullptr_t) { this->reset(); return *this; }

    /**
     *  Shares the underlying object referenced by the argument by calling ref() on it. If this
     *  sk_sp previously had a reference to an object (i.e. not null) it will call unref() on that
     *  object.
     */
    sk_sp<T>& operator=(const sk_sp<T>& that) {
        if (this != &that) {
            this->reset(SkSafeRef(that.get()));
        }
        return *this;
    }
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp<T>& operator=(const sk_sp<U>& that) {
        this->reset(SkSafeRef(that.get()));
        return *this;
    }

    /**
     *  Move the underlying object from the argument to the sk_sp. If the sk_sp previously held
     *  a reference to another object, unref() will be called on that object. No call to ref()
     *  will be made.
     */
    sk_sp<T>& operator=(sk_sp<T>&& that) {
        this->reset(that.release());
        return *this;
    }
    template <typename U,
              typename = typename std::enable_if<std::is_convertible<U*, T*>::value>::type>
    sk_sp<T>& operator=(sk_sp<U>&& that) {
        this->reset(that.release());
        return *this;
    }

    T& operator*() const {
        SkASSERT(this->get() != nullptr);
        return *this->get();
    }

    explicit operator bool() const { return this->get() != nullptr; }

    T* get() const { return fPtr; }
    T* operator->() const { return fPtr; }

    /**
     *  Adopt the new bare pointer, and call unref() on any previously held object (if not null).
     *  No call to ref() will be made.
     */
    void reset(T* ptr = nullptr) {
        // Calling fPtr->unref() may call this->~() or this->reset(T*).
        // http://wg21.cmeerw.net/lwg/issue998
        // http://wg21.cmeerw.net/lwg/issue2262
        T* oldPtr = fPtr;
        fPtr = ptr;
        SkSafeUnref(oldPtr);
    }

    /**
     *  Return the bare pointer, and set the internal object pointer to nullptr.
     *  The caller must assume ownership of the object, and manage its reference count directly.
     *  No call to unref() will be made.
     */
    [[nodiscard]] T* release() {
        T* ptr = fPtr;
        fPtr = nullptr;
        return ptr;
    }

    void swap(sk_sp<T>& that) /*noexcept*/ {
        using std::swap;
        swap(fPtr, that.fPtr);
    }

    using sk_is_trivially_relocatable = std::true_type;

private:
    T*  fPtr;
};

template <typename T> inline void swap(sk_sp<T>& a, sk_sp<T>& b) /*noexcept*/ {
    a.swap(b);
}

template <typename T, typename U> inline bool operator==(const sk_sp<T>& a, const sk_sp<U>& b) {
    return a.get() == b.get();
}
template <typename T> inline bool operator==(const sk_sp<T>& a, std::nullptr_t) /*noexcept*/ {
    return !a;
}
template <typename T> inline bool operator==(std::nullptr_t, const sk_sp<T>& b) /*noexcept*/ {
    return !b;
}

template <typename T, typename U> inline bool operator!=(const sk_sp<T>& a, const sk_sp<U>& b) {
    return a.get() != b.get();
}
template <typename T> inline bool operator!=(const sk_sp<T>& a, std::nullptr_t) /*noexcept*/ {
    return static_cast<bool>(a);
}
template <typename T> inline bool operator!=(std::nullptr_t, const sk_sp<T>& b) /*noexcept*/ {
    return static_cast<bool>(b);
}

template <typename C, typename CT, typename T>
auto operator<<(std::basic_ostream<C, CT>& os, const sk_sp<T>& sp) -> decltype(os << sp.get()) {
    return os << sp.get();
}

template <typename T, typename... Args>
sk_sp<T> sk_make_sp(Args&&... args) {
    return sk_sp<T>(new T(std::forward<Args>(args)...));
}

/*
 *  Returns a sk_sp wrapping the provided ptr AND calls ref on it (if not null).
 *
 *  This is different than the semantics of the constructor for sk_sp, which just wraps the ptr,
 *  effectively "adopting" it.
 */
template <typename T> sk_sp<T> sk_ref_sp(T* obj) {
    return sk_sp<T>(SkSafeRef(obj));
}

template <typename T> sk_sp<T> sk_ref_sp(const T* obj) {
    return sk_sp<T>(const_cast<T*>(SkSafeRef(obj)));
}

// Copyright 2022 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkTypeTraits_DEFINED
#define SkTypeTraits_DEFINED


// Trait for identifying types which are relocatable via memcpy, for container optimizations.
template<typename, typename = void>
struct sk_has_trivially_relocatable_member : std::false_type {};

// Types can declare themselves trivially relocatable with a public
//    using sk_is_trivially_relocatable = std::true_type;
template<typename T>
struct sk_has_trivially_relocatable_member<T, std::void_t<typename T::sk_is_trivially_relocatable>>
        : T::sk_is_trivially_relocatable {};

// By default, all trivially copyable types are trivially relocatable.
template <typename T>
struct sk_is_trivially_relocatable
        : std::disjunction<std::is_trivially_copyable<T>, sk_has_trivially_relocatable_member<T>>{};

// Here be some dragons: while technically not guaranteed, we count on all sane unique_ptr
// implementations to be trivially relocatable.
template <typename T>
struct sk_is_trivially_relocatable<std::unique_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool sk_is_trivially_relocatable_v = sk_is_trivially_relocatable<T>::value;

#endif  // SkTypeTraits_DEFINED

class SkData;
class SkPathRef;
class SkRRect;
class SkWStream;
enum class SkPathConvexity;
enum class SkPathFirstDirection;
struct SkPathVerbAnalysis;

// WIP -- define this locally, and fix call-sites to use SkPathBuilder (skbug.com/9000)
//#define SK_HIDE_PATH_EDIT_METHODS

/** \class SkPath
    SkPath contain geometry. SkPath may be empty, or contain one or more verbs that
    outline a figure. SkPath always starts with a move verb to a Cartesian coordinate,
    and may be followed by additional verbs that add lines or curves.
    Adding a close verb makes the geometry into a continuous loop, a closed contour.
    SkPath may contain any number of contours, each beginning with a move verb.

    SkPath contours may contain only a move verb, or may also contain lines,
    quadratic beziers, conics, and cubic beziers. SkPath contours may be open or
    closed.

    When used to draw a filled area, SkPath describes whether the fill is inside or
    outside the geometry. SkPath also describes the winding rule used to fill
    overlapping contours.

    Internally, SkPath lazily computes metrics likes bounds and convexity. Call
    SkPath::updateBoundsCache to make SkPath thread safe.
*/
class SK_API SkPath {
public:
    /**
     *  Create a new path with the specified segments.
     *
     *  The points and weights arrays are read in order, based on the sequence of verbs.
     *
     *  Move    1 point
     *  Line    1 point
     *  Quad    2 points
     *  Conic   2 points and 1 weight
     *  Cubic   3 points
     *  Close   0 points
     *
     *  If an illegal sequence of verbs is encountered, or the specified number of points
     *  or weights is not sufficient given the verbs, an empty Path is returned.
     *
     *  A legal sequence of verbs consists of any number of Contours. A contour always begins
     *  with a Move verb, followed by 0 or more segments: Line, Quad, Conic, Cubic, followed
     *  by an optional Close.
     */
    static SkPath Make(const SkPoint[],  int pointCount,
                       const uint8_t[],  int verbCount,
                       const SkScalar[], int conicWeightCount,
                       SkPathFillType, bool isVolatile = false);

    static SkPath Rect(const SkRect&, SkPathDirection = SkPathDirection::kCW,
                       unsigned startIndex = 0);
    static SkPath Oval(const SkRect&, SkPathDirection = SkPathDirection::kCW);
    static SkPath Oval(const SkRect&, SkPathDirection, unsigned startIndex);
    static SkPath Circle(SkScalar center_x, SkScalar center_y, SkScalar radius,
                         SkPathDirection dir = SkPathDirection::kCW);
    static SkPath RRect(const SkRRect&, SkPathDirection dir = SkPathDirection::kCW);
    static SkPath RRect(const SkRRect&, SkPathDirection, unsigned startIndex);
    static SkPath RRect(const SkRect& bounds, SkScalar rx, SkScalar ry,
                        SkPathDirection dir = SkPathDirection::kCW);

    static SkPath Polygon(const SkPoint pts[], int count, bool isClosed,
                          SkPathFillType = SkPathFillType::kWinding,
                          bool isVolatile = false);

    static SkPath Polygon(const std::initializer_list<SkPoint>& list, bool isClosed,
                          SkPathFillType fillType = SkPathFillType::kWinding,
                          bool isVolatile = false) {
        return Polygon(list.begin(), SkToInt(list.size()), isClosed, fillType, isVolatile);
    }

    static SkPath Line(const SkPoint a, const SkPoint b) {
        return Polygon({a, b}, false);
    }

    /** Constructs an empty SkPath. By default, SkPath has no verbs, no SkPoint, and no weights.
        FillType is set to kWinding.

        @return  empty SkPath

        example: https://fiddle.skia.org/c/@Path_empty_constructor
    */
    SkPath();

    /** Constructs a copy of an existing path.
        Copy constructor makes two paths identical by value. Internally, path and
        the returned result share pointer values. The underlying verb array, SkPoint array
        and weights are copied when modified.

        Creating a SkPath copy is very efficient and never allocates memory.
        SkPath are always copied by value from the interface; the underlying shared
        pointers are not exposed.

        @param path  SkPath to copy by value
        @return      copy of SkPath

        example: https://fiddle.skia.org/c/@Path_copy_const_SkPath
    */
    SkPath(const SkPath& path);

    /** Releases ownership of any shared data and deletes data if SkPath is sole owner.

        example: https://fiddle.skia.org/c/@Path_destructor
    */
    ~SkPath();

    /** Constructs a copy of an existing path.
        SkPath assignment makes two paths identical by value. Internally, assignment
        shares pointer values. The underlying verb array, SkPoint array and weights
        are copied when modified.

        Copying SkPath by assignment is very efficient and never allocates memory.
        SkPath are always copied by value from the interface; the underlying shared
        pointers are not exposed.

        @param path  verb array, SkPoint array, weights, and SkPath::FillType to copy
        @return      SkPath copied by value

        example: https://fiddle.skia.org/c/@Path_copy_operator
    */
    SkPath& operator=(const SkPath& path);

    /** Compares a and b; returns true if SkPath::FillType, verb array, SkPoint array, and weights
        are equivalent.

        @param a  SkPath to compare
        @param b  SkPath to compare
        @return   true if SkPath pair are equivalent
    */
    friend SK_API bool operator==(const SkPath& a, const SkPath& b);

    /** Compares a and b; returns true if SkPath::FillType, verb array, SkPoint array, and weights
        are not equivalent.

        @param a  SkPath to compare
        @param b  SkPath to compare
        @return   true if SkPath pair are not equivalent
    */
    friend bool operator!=(const SkPath& a, const SkPath& b) {
        return !(a == b);
    }

    /** Returns true if SkPath contain equal verbs and equal weights.
        If SkPath contain one or more conics, the weights must match.

        conicTo() may add different verbs depending on conic weight, so it is not
        trivial to interpolate a pair of SkPath containing conics with different
        conic weight values.

        @param compare  SkPath to compare
        @return         true if SkPath verb array and weights are equivalent

        example: https://fiddle.skia.org/c/@Path_isInterpolatable
    */
    bool isInterpolatable(const SkPath& compare) const;

    /** Interpolates between SkPath with SkPoint array of equal size.
        Copy verb array and weights to out, and set out SkPoint array to a weighted
        average of this SkPoint array and ending SkPoint array, using the formula:
        (Path Point * weight) + ending Point * (1 - weight).

        weight is most useful when between zero (ending SkPoint array) and
        one (this Point_Array); will work with values outside of this
        range.

        interpolate() returns false and leaves out unchanged if SkPoint array is not
        the same size as ending SkPoint array. Call isInterpolatable() to check SkPath
        compatibility prior to calling interpolate().

        @param ending  SkPoint array averaged with this SkPoint array
        @param weight  contribution of this SkPoint array, and
                       one minus contribution of ending SkPoint array
        @param out     SkPath replaced by interpolated averages
        @return        true if SkPath contain same number of SkPoint

        example: https://fiddle.skia.org/c/@Path_interpolate
    */
    bool interpolate(const SkPath& ending, SkScalar weight, SkPath* out) const;

    /** Returns SkPathFillType, the rule used to fill SkPath.

        @return  current SkPathFillType setting
    */
    SkPathFillType getFillType() const { return (SkPathFillType)fFillType; }

    /** Sets FillType, the rule used to fill SkPath. While there is no check
        that ft is legal, values outside of FillType are not supported.
    */
    void setFillType(SkPathFillType ft) {
        fFillType = SkToU8(ft);
    }

    /** Returns if FillType describes area outside SkPath geometry. The inverse fill area
        extends indefinitely.

        @return  true if FillType is kInverseWinding or kInverseEvenOdd
    */
    bool isInverseFillType() const { return SkPathFillType_IsInverse(this->getFillType()); }

    /** Replaces FillType with its inverse. The inverse of FillType describes the area
        unmodified by the original FillType.
    */
    void toggleInverseFillType() {
        fFillType ^= 2;
    }

    /** Returns true if the path is convex. If necessary, it will first compute the convexity.
     */
    bool isConvex() const;

    /** Returns true if this path is recognized as an oval or circle.

        bounds receives bounds of oval.

        bounds is unmodified if oval is not found.

        @param bounds  storage for bounding SkRect of oval; may be nullptr
        @return        true if SkPath is recognized as an oval or circle

        example: https://fiddle.skia.org/c/@Path_isOval
    */
    bool isOval(SkRect* bounds) const;

    /** Returns true if path is representable as SkRRect.
        Returns false if path is representable as oval, circle, or SkRect.

        rrect receives bounds of SkRRect.

        rrect is unmodified if SkRRect is not found.

        @param rrect  storage for bounding SkRect of SkRRect; may be nullptr
        @return       true if SkPath contains only SkRRect

        example: https://fiddle.skia.org/c/@Path_isRRect
    */
    bool isRRect(SkRRect* rrect) const;

    /** Sets SkPath to its initial state.
        Removes verb array, SkPoint array, and weights, and sets FillType to kWinding.
        Internal storage associated with SkPath is released.

        @return  reference to SkPath

        example: https://fiddle.skia.org/c/@Path_reset
    */
    SkPath& reset();

    /** Sets SkPath to its initial state, preserving internal storage.
        Removes verb array, SkPoint array, and weights, and sets FillType to kWinding.
        Internal storage associated with SkPath is retained.

        Use rewind() instead of reset() if SkPath storage will be reused and performance
        is critical.

        @return  reference to SkPath

        example: https://fiddle.skia.org/c/@Path_rewind
    */
    SkPath& rewind();

    /** Returns if SkPath is empty.
        Empty SkPath may have FillType but has no SkPoint, SkPath::Verb, or conic weight.
        SkPath() constructs empty SkPath; reset() and rewind() make SkPath empty.

        @return  true if the path contains no SkPath::Verb array
    */
    bool isEmpty() const;

    /** Returns if contour is closed.
        Contour is closed if SkPath SkPath::Verb array was last modified by close(). When stroked,
        closed contour draws SkPaint::Join instead of SkPaint::Cap at first and last SkPoint.

        @return  true if the last contour ends with a kClose_Verb

        example: https://fiddle.skia.org/c/@Path_isLastContourClosed
    */
    bool isLastContourClosed() const;

    /** Returns true for finite SkPoint array values between negative SK_ScalarMax and
        positive SK_ScalarMax. Returns false for any SkPoint array value of
        SK_ScalarInfinity, SK_ScalarNegativeInfinity, or SK_ScalarNaN.

        @return  true if all SkPoint values are finite
    */
    bool isFinite() const;

    /** Returns true if the path is volatile; it will not be altered or discarded
        by the caller after it is drawn. SkPath by default have volatile set false, allowing
        SkSurface to attach a cache of data which speeds repeated drawing. If true, SkSurface
        may not speed repeated drawing.

        @return  true if caller will alter SkPath after drawing
    */
    bool isVolatile() const {
        return SkToBool(fIsVolatile);
    }

    /** Specifies whether SkPath is volatile; whether it will be altered or discarded
        by the caller after it is drawn. SkPath by default have volatile set false, allowing
        Skia to attach a cache of data which speeds repeated drawing.

        Mark temporary paths, discarded or modified after use, as volatile
        to inform Skia that the path need not be cached.

        Mark animating SkPath volatile to improve performance.
        Mark unchanging SkPath non-volatile to improve repeated rendering.

        raster surface SkPath draws are affected by volatile for some shadows.
        GPU surface SkPath draws are affected by volatile for some shadows and concave geometries.

        @param isVolatile  true if caller will alter SkPath after drawing
        @return            reference to SkPath
    */
    SkPath& setIsVolatile(bool isVolatile) {
        fIsVolatile = isVolatile;
        return *this;
    }

    /** Tests if line between SkPoint pair is degenerate.
        Line with no length or that moves a very short distance is degenerate; it is
        treated as a point.

        exact changes the equality test. If true, returns true only if p1 equals p2.
        If false, returns true if p1 equals or nearly equals p2.

        @param p1     line start point
        @param p2     line end point
        @param exact  if false, allow nearly equals
        @return       true if line is degenerate; its length is effectively zero

        example: https://fiddle.skia.org/c/@Path_IsLineDegenerate
    */
    static bool IsLineDegenerate(const SkPoint& p1, const SkPoint& p2, bool exact);

    /** Tests if quad is degenerate.
        Quad with no length or that moves a very short distance is degenerate; it is
        treated as a point.

        @param p1     quad start point
        @param p2     quad control point
        @param p3     quad end point
        @param exact  if true, returns true only if p1, p2, and p3 are equal;
                      if false, returns true if p1, p2, and p3 are equal or nearly equal
        @return       true if quad is degenerate; its length is effectively zero
    */
    static bool IsQuadDegenerate(const SkPoint& p1, const SkPoint& p2,
                                 const SkPoint& p3, bool exact);

    /** Tests if cubic is degenerate.
        Cubic with no length or that moves a very short distance is degenerate; it is
        treated as a point.

        @param p1     cubic start point
        @param p2     cubic control point 1
        @param p3     cubic control point 2
        @param p4     cubic end point
        @param exact  if true, returns true only if p1, p2, p3, and p4 are equal;
                      if false, returns true if p1, p2, p3, and p4 are equal or nearly equal
        @return       true if cubic is degenerate; its length is effectively zero
    */
    static bool IsCubicDegenerate(const SkPoint& p1, const SkPoint& p2,
                                  const SkPoint& p3, const SkPoint& p4, bool exact);

    /** Returns true if SkPath contains only one line;
        SkPath::Verb array has two entries: kMove_Verb, kLine_Verb.
        If SkPath contains one line and line is not nullptr, line is set to
        line start point and line end point.
        Returns false if SkPath is not one line; line is unaltered.

        @param line  storage for line. May be nullptr
        @return      true if SkPath contains exactly one line

        example: https://fiddle.skia.org/c/@Path_isLine
    */
    bool isLine(SkPoint line[2]) const;

    /** Returns the number of points in SkPath.
        SkPoint count is initially zero.

        @return  SkPath SkPoint array length

        example: https://fiddle.skia.org/c/@Path_countPoints
    */
    int countPoints() const;

    /** Returns SkPoint at index in SkPoint array. Valid range for index is
        0 to countPoints() - 1.
        Returns (0, 0) if index is out of range.

        @param index  SkPoint array element selector
        @return       SkPoint array value or (0, 0)

        example: https://fiddle.skia.org/c/@Path_getPoint
    */
    SkPoint getPoint(int index) const;

    /** Returns number of points in SkPath. Up to max points are copied.
        points may be nullptr; then, max must be zero.
        If max is greater than number of points, excess points storage is unaltered.

        @param points  storage for SkPath SkPoint array. May be nullptr
        @param max     maximum to copy; must be greater than or equal to zero
        @return        SkPath SkPoint array length

        example: https://fiddle.skia.org/c/@Path_getPoints
    */
    int getPoints(SkPoint points[], int max) const;

    /** Returns the number of verbs: kMove_Verb, kLine_Verb, kQuad_Verb, kConic_Verb,
        kCubic_Verb, and kClose_Verb; added to SkPath.

        @return  length of verb array

        example: https://fiddle.skia.org/c/@Path_countVerbs
    */
    int countVerbs() const;

    /** Returns the number of verbs in the path. Up to max verbs are copied. The
        verbs are copied as one byte per verb.

        @param verbs  storage for verbs, may be nullptr
        @param max    maximum number to copy into verbs
        @return       the actual number of verbs in the path

        example: https://fiddle.skia.org/c/@Path_getVerbs
    */
    int getVerbs(uint8_t verbs[], int max) const;

    /** Returns the approximate byte size of the SkPath in memory.

        @return  approximate size
    */
    size_t approximateBytesUsed() const;

    /** Exchanges the verb array, SkPoint array, weights, and SkPath::FillType with other.
        Cached state is also exchanged. swap() internally exchanges pointers, so
        it is lightweight and does not allocate memory.

        swap() usage has largely been replaced by operator=(const SkPath& path).
        SkPath do not copy their content on assignment until they are written to,
        making assignment as efficient as swap().

        @param other  SkPath exchanged by value

        example: https://fiddle.skia.org/c/@Path_swap
    */
    void swap(SkPath& other);

    /** Returns minimum and maximum axes values of SkPoint array.
        Returns (0, 0, 0, 0) if SkPath contains no points. Returned bounds width and height may
        be larger or smaller than area affected when SkPath is drawn.

        SkRect returned includes all SkPoint added to SkPath, including SkPoint associated with
        kMove_Verb that define empty contours.

        @return  bounds of all SkPoint in SkPoint array
    */
    const SkRect& getBounds() const;

    /** Updates internal bounds so that subsequent calls to getBounds() are instantaneous.
        Unaltered copies of SkPath may also access cached bounds through getBounds().

        For now, identical to calling getBounds() and ignoring the returned value.

        Call to prepare SkPath subsequently drawn from multiple threads,
        to avoid a race condition where each draw separately computes the bounds.
    */
    void updateBoundsCache() const {
        // for now, just calling getBounds() is sufficient
        this->getBounds();
    }

    /** Returns minimum and maximum axes values of the lines and curves in SkPath.
        Returns (0, 0, 0, 0) if SkPath contains no points.
        Returned bounds width and height may be larger or smaller than area affected
        when SkPath is drawn.

        Includes SkPoint associated with kMove_Verb that define empty
        contours.

        Behaves identically to getBounds() when SkPath contains
        only lines. If SkPath contains curves, computed bounds includes
        the maximum extent of the quad, conic, or cubic; is slower than getBounds();
        and unlike getBounds(), does not cache the result.

        @return  tight bounds of curves in SkPath

        example: https://fiddle.skia.org/c/@Path_computeTightBounds
    */
    SkRect computeTightBounds() const;

    /** Returns true if rect is contained by SkPath.
        May return false when rect is contained by SkPath.

        For now, only returns true if SkPath has one contour and is convex.
        rect may share points and edges with SkPath and be contained.
        Returns true if rect is empty, that is, it has zero width or height; and
        the SkPoint or line described by rect is contained by SkPath.

        @param rect  SkRect, line, or SkPoint checked for containment
        @return      true if rect is contained

        example: https://fiddle.skia.org/c/@Path_conservativelyContainsRect
    */
    bool conservativelyContainsRect(const SkRect& rect) const;

    /** Grows SkPath verb array and SkPoint array to contain extraPtCount additional SkPoint.
        May improve performance and use less memory by
        reducing the number and size of allocations when creating SkPath.

        @param extraPtCount  number of additional SkPoint to allocate

        example: https://fiddle.skia.org/c/@Path_incReserve
    */
    void incReserve(int extraPtCount);

#ifdef SK_HIDE_PATH_EDIT_METHODS
private:
#endif

    /** Adds beginning of contour at SkPoint (x, y).

        @param x  x-axis value of contour start
        @param y  y-axis value of contour start
        @return   reference to SkPath

        example: https://fiddle.skia.org/c/@Path_moveTo
    */
    SkPath& moveTo(SkScalar x, SkScalar y);

    /** Adds beginning of contour at SkPoint p.

        @param p  contour start
        @return   reference to SkPath
    */
    SkPath& moveTo(const SkPoint& p) {
        return this->moveTo(p.fX, p.fY);
    }

    /** Adds beginning of contour relative to last point.
        If SkPath is empty, starts contour at (dx, dy).
        Otherwise, start contour at last point offset by (dx, dy).
        Function name stands for "relative move to".

        @param dx  offset from last point to contour start on x-axis
        @param dy  offset from last point to contour start on y-axis
        @return    reference to SkPath

        example: https://fiddle.skia.org/c/@Path_rMoveTo
    */
    SkPath& rMoveTo(SkScalar dx, SkScalar dy);

    /** Adds line from last point to (x, y). If SkPath is empty, or last SkPath::Verb is
        kClose_Verb, last point is set to (0, 0) before adding line.

        lineTo() appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.
        lineTo() then appends kLine_Verb to verb array and (x, y) to SkPoint array.

        @param x  end of added line on x-axis
        @param y  end of added line on y-axis
        @return   reference to SkPath

        example: https://fiddle.skia.org/c/@Path_lineTo
    */
    SkPath& lineTo(SkScalar x, SkScalar y);

    /** Adds line from last point to SkPoint p. If SkPath is empty, or last SkPath::Verb is
        kClose_Verb, last point is set to (0, 0) before adding line.

        lineTo() first appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.
        lineTo() then appends kLine_Verb to verb array and SkPoint p to SkPoint array.

        @param p  end SkPoint of added line
        @return   reference to SkPath
    */
    SkPath& lineTo(const SkPoint& p) {
        return this->lineTo(p.fX, p.fY);
    }

    /** Adds line from last point to vector (dx, dy). If SkPath is empty, or last SkPath::Verb is
        kClose_Verb, last point is set to (0, 0) before adding line.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kLine_Verb to verb array and line end to SkPoint array.
        Line end is last point plus vector (dx, dy).
        Function name stands for "relative line to".

        @param dx  offset from last point to line end on x-axis
        @param dy  offset from last point to line end on y-axis
        @return    reference to SkPath

        example: https://fiddle.skia.org/c/@Path_rLineTo
        example: https://fiddle.skia.org/c/@Quad_a
        example: https://fiddle.skia.org/c/@Quad_b
    */
    SkPath& rLineTo(SkScalar dx, SkScalar dy);

    /** Adds quad from last point towards (x1, y1), to (x2, y2).
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding quad.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kQuad_Verb to verb array; and (x1, y1), (x2, y2)
        to SkPoint array.

        @param x1  control SkPoint of quad on x-axis
        @param y1  control SkPoint of quad on y-axis
        @param x2  end SkPoint of quad on x-axis
        @param y2  end SkPoint of quad on y-axis
        @return    reference to SkPath

        example: https://fiddle.skia.org/c/@Path_quadTo
    */
    SkPath& quadTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2);

    /** Adds quad from last point towards SkPoint p1, to SkPoint p2.
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding quad.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kQuad_Verb to verb array; and SkPoint p1, p2
        to SkPoint array.

        @param p1  control SkPoint of added quad
        @param p2  end SkPoint of added quad
        @return    reference to SkPath
    */
    SkPath& quadTo(const SkPoint& p1, const SkPoint& p2) {
        return this->quadTo(p1.fX, p1.fY, p2.fX, p2.fY);
    }

    /** Adds quad from last point towards vector (dx1, dy1), to vector (dx2, dy2).
        If SkPath is empty, or last SkPath::Verb
        is kClose_Verb, last point is set to (0, 0) before adding quad.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array,
        if needed; then appends kQuad_Verb to verb array; and appends quad
        control and quad end to SkPoint array.
        Quad control is last point plus vector (dx1, dy1).
        Quad end is last point plus vector (dx2, dy2).
        Function name stands for "relative quad to".

        @param dx1  offset from last point to quad control on x-axis
        @param dy1  offset from last point to quad control on y-axis
        @param dx2  offset from last point to quad end on x-axis
        @param dy2  offset from last point to quad end on y-axis
        @return     reference to SkPath

        example: https://fiddle.skia.org/c/@Conic_Weight_a
        example: https://fiddle.skia.org/c/@Conic_Weight_b
        example: https://fiddle.skia.org/c/@Conic_Weight_c
        example: https://fiddle.skia.org/c/@Path_rQuadTo
    */
    SkPath& rQuadTo(SkScalar dx1, SkScalar dy1, SkScalar dx2, SkScalar dy2);

    /** Adds conic from last point towards (x1, y1), to (x2, y2), weighted by w.
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding conic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.

        If w is finite and not one, appends kConic_Verb to verb array;
        and (x1, y1), (x2, y2) to SkPoint array; and w to conic weights.

        If w is one, appends kQuad_Verb to verb array, and
        (x1, y1), (x2, y2) to SkPoint array.

        If w is not finite, appends kLine_Verb twice to verb array, and
        (x1, y1), (x2, y2) to SkPoint array.

        @param x1  control SkPoint of conic on x-axis
        @param y1  control SkPoint of conic on y-axis
        @param x2  end SkPoint of conic on x-axis
        @param y2  end SkPoint of conic on y-axis
        @param w   weight of added conic
        @return    reference to SkPath
    */
    SkPath& conicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                    SkScalar w);

    /** Adds conic from last point towards SkPoint p1, to SkPoint p2, weighted by w.
        If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to (0, 0)
        before adding conic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed.

        If w is finite and not one, appends kConic_Verb to verb array;
        and SkPoint p1, p2 to SkPoint array; and w to conic weights.

        If w is one, appends kQuad_Verb to verb array, and SkPoint p1, p2
        to SkPoint array.

        If w is not finite, appends kLine_Verb twice to verb array, and
        SkPoint p1, p2 to SkPoint array.

        @param p1  control SkPoint of added conic
        @param p2  end SkPoint of added conic
        @param w   weight of added conic
        @return    reference to SkPath
    */
    SkPath& conicTo(const SkPoint& p1, const SkPoint& p2, SkScalar w) {
        return this->conicTo(p1.fX, p1.fY, p2.fX, p2.fY, w);
    }

    /** Adds conic from last point towards vector (dx1, dy1), to vector (dx2, dy2),
        weighted by w. If SkPath is empty, or last SkPath::Verb
        is kClose_Verb, last point is set to (0, 0) before adding conic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array,
        if needed.

        If w is finite and not one, next appends kConic_Verb to verb array,
        and w is recorded as conic weight; otherwise, if w is one, appends
        kQuad_Verb to verb array; or if w is not finite, appends kLine_Verb
        twice to verb array.

        In all cases appends SkPoint control and end to SkPoint array.
        control is last point plus vector (dx1, dy1).
        end is last point plus vector (dx2, dy2).

        Function name stands for "relative conic to".

        @param dx1  offset from last point to conic control on x-axis
        @param dy1  offset from last point to conic control on y-axis
        @param dx2  offset from last point to conic end on x-axis
        @param dy2  offset from last point to conic end on y-axis
        @param w    weight of added conic
        @return     reference to SkPath
    */
    SkPath& rConicTo(SkScalar dx1, SkScalar dy1, SkScalar dx2, SkScalar dy2,
                     SkScalar w);

    /** Adds cubic from last point towards (x1, y1), then towards (x2, y2), ending at
        (x3, y3). If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to
        (0, 0) before adding cubic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kCubic_Verb to verb array; and (x1, y1), (x2, y2), (x3, y3)
        to SkPoint array.

        @param x1  first control SkPoint of cubic on x-axis
        @param y1  first control SkPoint of cubic on y-axis
        @param x2  second control SkPoint of cubic on x-axis
        @param y2  second control SkPoint of cubic on y-axis
        @param x3  end SkPoint of cubic on x-axis
        @param y3  end SkPoint of cubic on y-axis
        @return    reference to SkPath
    */
    SkPath& cubicTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
                    SkScalar x3, SkScalar y3);

    /** Adds cubic from last point towards SkPoint p1, then towards SkPoint p2, ending at
        SkPoint p3. If SkPath is empty, or last SkPath::Verb is kClose_Verb, last point is set to
        (0, 0) before adding cubic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array, if needed;
        then appends kCubic_Verb to verb array; and SkPoint p1, p2, p3
        to SkPoint array.

        @param p1  first control SkPoint of cubic
        @param p2  second control SkPoint of cubic
        @param p3  end SkPoint of cubic
        @return    reference to SkPath
    */
    SkPath& cubicTo(const SkPoint& p1, const SkPoint& p2, const SkPoint& p3) {
        return this->cubicTo(p1.fX, p1.fY, p2.fX, p2.fY, p3.fX, p3.fY);
    }

    /** Adds cubic from last point towards vector (dx1, dy1), then towards
        vector (dx2, dy2), to vector (dx3, dy3).
        If SkPath is empty, or last SkPath::Verb
        is kClose_Verb, last point is set to (0, 0) before adding cubic.

        Appends kMove_Verb to verb array and (0, 0) to SkPoint array,
        if needed; then appends kCubic_Verb to verb array; and appends cubic
        control and cubic end to SkPoint array.
        Cubic control is last point plus vector (dx1, dy1).
        Cubic end is last point plus vector (dx2, dy2).
        Function name stands for "relative cubic to".

        @param dx1  offset from last point to first cubic control on x-axis
        @param dy1  offset from last point to first cubic control on y-axis
        @param dx2  offset from last point to second cubic control on x-axis
        @param dy2  offset from last point to second cubic control on y-axis
        @param dx3  offset from last point to cubic end on x-axis
        @param dy3  offset from last point to cubic end on y-axis
        @return    reference to SkPath
    */
    SkPath& rCubicTo(SkScalar dx1, SkScalar dy1, SkScalar dx2, SkScalar dy2,
                     SkScalar dx3, SkScalar dy3);

    /** Appends arc to SkPath. Arc added is part of ellipse
        bounded by oval, from startAngle through sweepAngle. Both startAngle and
        sweepAngle are measured in degrees, where zero degrees is aligned with the
        positive x-axis, and positive sweeps extends arc clockwise.

        arcTo() adds line connecting SkPath last SkPoint to initial arc SkPoint if forceMoveTo
        is false and SkPath is not empty. Otherwise, added contour begins with first point
        of arc. Angles greater than -360 and less than 360 are treated modulo 360.

        @param oval         bounds of ellipse containing arc
        @param startAngle   starting angle of arc in degrees
        @param sweepAngle   sweep, in degrees. Positive is clockwise; treated modulo 360
        @param forceMoveTo  true to start a new contour with arc
        @return             reference to SkPath

        example: https://fiddle.skia.org/c/@Path_arcTo
    */
    SkPath& arcTo(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle, bool forceMoveTo);

    /** Appends arc to SkPath, after appending line if needed. Arc is implemented by conic
        weighted to describe part of circle. Arc is contained by tangent from
        last SkPath point to (x1, y1), and tangent from (x1, y1) to (x2, y2). Arc
        is part of circle sized to radius, positioned so it touches both tangent lines.

        If last Path Point does not start Arc, arcTo appends connecting Line to Path.
        The length of Vector from (x1, y1) to (x2, y2) does not affect Arc.

        Arc sweep is always less than 180 degrees. If radius is zero, or if
        tangents are nearly parallel, arcTo appends Line from last Path Point to (x1, y1).

        arcTo appends at most one Line and one conic.
        arcTo implements the functionality of PostScript arct and HTML Canvas arcTo.

        @param x1      x-axis value common to pair of tangents
        @param y1      y-axis value common to pair of tangents
        @param x2      x-axis value end of second tangent
        @param y2      y-axis value end of second tangent
        @param radius  distance from arc to circle center
        @return        reference to SkPath

        example: https://fiddle.skia.org/c/@Path_arcTo_2_a
        example: https://fiddle.skia.org/c/@Path_arcTo_2_b
        example: https://fiddle.skia.org/c/@Path_arcTo_2_c
    */
    SkPath& arcTo(SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2, SkScalar radius);

    /** Appends arc to SkPath, after appending line if needed. Arc is implemented by conic
        weighted to describe part of circle. Arc is contained by tangent from
        last SkPath point to p1, and tangent from p1 to p2. Arc
        is part of circle sized to radius, positioned so it touches both tangent lines.

        If last SkPath SkPoint does not start arc, arcTo() appends connecting line to SkPath.
        The length of vector from p1 to p2 does not affect arc.

        Arc sweep is always less than 180 degrees. If radius is zero, or if
        tangents are nearly parallel, arcTo() appends line from last SkPath SkPoint to p1.

        arcTo() appends at most one line and one conic.
        arcTo() implements the functionality of PostScript arct and HTML Canvas arcTo.

        @param p1      SkPoint common to pair of tangents
        @param p2      end of second tangent
        @param radius  distance from arc to circle center
        @return        reference to SkPath
    */
    SkPath& arcTo(const SkPoint p1, const SkPoint p2, SkScalar radius) {
        return this->arcTo(p1.fX, p1.fY, p2.fX, p2.fY, radius);
    }

    /** \enum SkPath::ArcSize
        Four oval parts with radii (rx, ry) start at last SkPath SkPoint and ends at (x, y).
        ArcSize and Direction select one of the four oval parts.
    */
    enum ArcSize {
        kSmall_ArcSize, //!< smaller of arc pair
        kLarge_ArcSize, //!< larger of arc pair
    };

    /** Appends arc to SkPath. Arc is implemented by one or more conics weighted to
        describe part of oval with radii (rx, ry) rotated by xAxisRotate degrees. Arc
        curves from last SkPath SkPoint to (x, y), choosing one of four possible routes:
        clockwise or counterclockwise, and smaller or larger.

        Arc sweep is always less than 360 degrees. arcTo() appends line to (x, y) if
        either radii are zero, or if last SkPath SkPoint equals (x, y). arcTo() scales radii
        (rx, ry) to fit last SkPath SkPoint and (x, y) if both are greater than zero but
        too small.

        arcTo() appends up to four conic curves.
        arcTo() implements the functionality of SVG arc, although SVG sweep-flag value
        is opposite the integer value of sweep; SVG sweep-flag uses 1 for clockwise,
        while kCW_Direction cast to int is zero.

        @param rx           radius on x-axis before x-axis rotation
        @param ry           radius on y-axis before x-axis rotation
        @param xAxisRotate  x-axis rotation in degrees; positive values are clockwise
        @param largeArc     chooses smaller or larger arc
        @param sweep        chooses clockwise or counterclockwise arc
        @param x            end of arc
        @param y            end of arc
        @return             reference to SkPath
    */
    SkPath& arcTo(SkScalar rx, SkScalar ry, SkScalar xAxisRotate, ArcSize largeArc,
                  SkPathDirection sweep, SkScalar x, SkScalar y);

    /** Appends arc to SkPath. Arc is implemented by one or more conic weighted to describe
        part of oval with radii (r.fX, r.fY) rotated by xAxisRotate degrees. Arc curves
        from last SkPath SkPoint to (xy.fX, xy.fY), choosing one of four possible routes:
        clockwise or counterclockwise,
        and smaller or larger.

        Arc sweep is always less than 360 degrees. arcTo() appends line to xy if either
        radii are zero, or if last SkPath SkPoint equals (xy.fX, xy.fY). arcTo() scales radii r to
        fit last SkPath SkPoint and xy if both are greater than zero but too small to describe
        an arc.

        arcTo() appends up to four conic curves.
        arcTo() implements the functionality of SVG arc, although SVG sweep-flag value is
        opposite the integer value of sweep; SVG sweep-flag uses 1 for clockwise, while
        kCW_Direction cast to int is zero.

        @param r            radii on axes before x-axis rotation
        @param xAxisRotate  x-axis rotation in degrees; positive values are clockwise
        @param largeArc     chooses smaller or larger arc
        @param sweep        chooses clockwise or counterclockwise arc
        @param xy           end of arc
        @return             reference to SkPath
    */
    SkPath& arcTo(const SkPoint r, SkScalar xAxisRotate, ArcSize largeArc, SkPathDirection sweep,
               const SkPoint xy) {
        return this->arcTo(r.fX, r.fY, xAxisRotate, largeArc, sweep, xy.fX, xy.fY);
    }

    /** Appends arc to SkPath, relative to last SkPath SkPoint. Arc is implemented by one or
        more conic, weighted to describe part of oval with radii (rx, ry) rotated by
        xAxisRotate degrees. Arc curves from last SkPath SkPoint to relative end SkPoint:
        (dx, dy), choosing one of four possible routes: clockwise or
        counterclockwise, and smaller or larger. If SkPath is empty, the start arc SkPoint
        is (0, 0).

        Arc sweep is always less than 360 degrees. arcTo() appends line to end SkPoint
        if either radii are zero, or if last SkPath SkPoint equals end SkPoint.
        arcTo() scales radii (rx, ry) to fit last SkPath SkPoint and end SkPoint if both are
        greater than zero but too small to describe an arc.

        arcTo() appends up to four conic curves.
        arcTo() implements the functionality of svg arc, although SVG "sweep-flag" value is
        opposite the integer value of sweep; SVG "sweep-flag" uses 1 for clockwise, while
        kCW_Direction cast to int is zero.

        @param rx           radius before x-axis rotation
        @param ry           radius before x-axis rotation
        @param xAxisRotate  x-axis rotation in degrees; positive values are clockwise
        @param largeArc     chooses smaller or larger arc
        @param sweep        chooses clockwise or counterclockwise arc
        @param dx           x-axis offset end of arc from last SkPath SkPoint
        @param dy           y-axis offset end of arc from last SkPath SkPoint
        @return             reference to SkPath
    */
    SkPath& rArcTo(SkScalar rx, SkScalar ry, SkScalar xAxisRotate, ArcSize largeArc,
                   SkPathDirection sweep, SkScalar dx, SkScalar dy);

    /** Appends kClose_Verb to SkPath. A closed contour connects the first and last SkPoint
        with line, forming a continuous loop. Open and closed contour draw the same
        with SkPaint::kFill_Style. With SkPaint::kStroke_Style, open contour draws
        SkPaint::Cap at contour start and end; closed contour draws
        SkPaint::Join at contour start and end.

        close() has no effect if SkPath is empty or last SkPath SkPath::Verb is kClose_Verb.

        @return  reference to SkPath

        example: https://fiddle.skia.org/c/@Path_close
    */
    SkPath& close();

#ifdef SK_HIDE_PATH_EDIT_METHODS
public:
#endif

    /** Approximates conic with quad array. Conic is constructed from start SkPoint p0,
        control SkPoint p1, end SkPoint p2, and weight w.
        Quad array is stored in pts; this storage is supplied by caller.
        Maximum quad count is 2 to the pow2.
        Every third point in array shares last SkPoint of previous quad and first SkPoint of
        next quad. Maximum pts storage size is given by:
        (1 + 2 * (1 << pow2)) * sizeof(SkPoint).

        Returns quad count used the approximation, which may be smaller
        than the number requested.

        conic weight determines the amount of influence conic control point has on the curve.
        w less than one represents an elliptical section. w greater than one represents
        a hyperbolic section. w equal to one represents a parabolic section.

        Two quad curves are sufficient to approximate an elliptical conic with a sweep
        of up to 90 degrees; in this case, set pow2 to one.

        @param p0    conic start SkPoint
        @param p1    conic control SkPoint
        @param p2    conic end SkPoint
        @param w     conic weight
        @param pts   storage for quad array
        @param pow2  quad count, as power of two, normally 0 to 5 (1 to 32 quad curves)
        @return      number of quad curves written to pts
    */
    static int ConvertConicToQuads(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2,
                                   SkScalar w, SkPoint pts[], int pow2);

    /** Returns true if SkPath is equivalent to SkRect when filled.
        If false: rect, isClosed, and direction are unchanged.
        If true: rect, isClosed, and direction are written to if not nullptr.

        rect may be smaller than the SkPath bounds. SkPath bounds may include kMove_Verb points
        that do not alter the area drawn by the returned rect.

        @param rect       storage for bounds of SkRect; may be nullptr
        @param isClosed   storage set to true if SkPath is closed; may be nullptr
        @param direction  storage set to SkRect direction; may be nullptr
        @return           true if SkPath contains SkRect

        example: https://fiddle.skia.org/c/@Path_isRect
    */
    bool isRect(SkRect* rect, bool* isClosed = nullptr, SkPathDirection* direction = nullptr) const;

#ifdef SK_HIDE_PATH_EDIT_METHODS
private:
#endif

    /** Adds a new contour to the path, defined by the rect, and wound in the
        specified direction. The verbs added to the path will be:

        kMove, kLine, kLine, kLine, kClose

        start specifies which corner to begin the contour:
            0: upper-left  corner
            1: upper-right corner
            2: lower-right corner
            3: lower-left  corner

        This start point also acts as the implied beginning of the subsequent,
        contour, if it does not have an explicit moveTo(). e.g.

            path.addRect(...)
            // if we don't say moveTo() here, we will use the rect's start point
            path.lineTo(...)

        @param rect   SkRect to add as a closed contour
        @param dir    SkPath::Direction to orient the new contour
        @param start  initial corner of SkRect to add
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addRect_2
     */
    SkPath& addRect(const SkRect& rect, SkPathDirection dir, unsigned start);

    SkPath& addRect(const SkRect& rect, SkPathDirection dir = SkPathDirection::kCW) {
        return this->addRect(rect, dir, 0);
    }

    SkPath& addRect(SkScalar left, SkScalar top, SkScalar right, SkScalar bottom,
                    SkPathDirection dir = SkPathDirection::kCW) {
        return this->addRect({left, top, right, bottom}, dir, 0);
    }

    /** Adds oval to path, appending kMove_Verb, four kConic_Verb, and kClose_Verb.
        Oval is upright ellipse bounded by SkRect oval with radii equal to half oval width
        and half oval height. Oval begins at (oval.fRight, oval.centerY()) and continues
        clockwise if dir is kCW_Direction, counterclockwise if dir is kCCW_Direction.

        @param oval  bounds of ellipse added
        @param dir   SkPath::Direction to wind ellipse
        @return      reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addOval
    */
    SkPath& addOval(const SkRect& oval, SkPathDirection dir = SkPathDirection::kCW);

    /** Adds oval to SkPath, appending kMove_Verb, four kConic_Verb, and kClose_Verb.
        Oval is upright ellipse bounded by SkRect oval with radii equal to half oval width
        and half oval height. Oval begins at start and continues
        clockwise if dir is kCW_Direction, counterclockwise if dir is kCCW_Direction.

        @param oval   bounds of ellipse added
        @param dir    SkPath::Direction to wind ellipse
        @param start  index of initial point of ellipse
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addOval_2
    */
    SkPath& addOval(const SkRect& oval, SkPathDirection dir, unsigned start);

    /** Adds circle centered at (x, y) of size radius to SkPath, appending kMove_Verb,
        four kConic_Verb, and kClose_Verb. Circle begins at: (x + radius, y), continuing
        clockwise if dir is kCW_Direction, and counterclockwise if dir is kCCW_Direction.

        Has no effect if radius is zero or negative.

        @param x       center of circle
        @param y       center of circle
        @param radius  distance from center to edge
        @param dir     SkPath::Direction to wind circle
        @return        reference to SkPath
    */
    SkPath& addCircle(SkScalar x, SkScalar y, SkScalar radius,
                      SkPathDirection dir = SkPathDirection::kCW);

    /** Appends arc to SkPath, as the start of new contour. Arc added is part of ellipse
        bounded by oval, from startAngle through sweepAngle. Both startAngle and
        sweepAngle are measured in degrees, where zero degrees is aligned with the
        positive x-axis, and positive sweeps extends arc clockwise.

        If sweepAngle <= -360, or sweepAngle >= 360; and startAngle modulo 90 is nearly
        zero, append oval instead of arc. Otherwise, sweepAngle values are treated
        modulo 360, and arc may or may not draw depending on numeric rounding.

        @param oval        bounds of ellipse containing arc
        @param startAngle  starting angle of arc in degrees
        @param sweepAngle  sweep, in degrees. Positive is clockwise; treated modulo 360
        @return            reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addArc
    */
    SkPath& addArc(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle);

    /** Appends SkRRect to SkPath, creating a new closed contour. SkRRect has bounds
        equal to rect; each corner is 90 degrees of an ellipse with radii (rx, ry). If
        dir is kCW_Direction, SkRRect starts at top-left of the lower-left corner and
        winds clockwise. If dir is kCCW_Direction, SkRRect starts at the bottom-left
        of the upper-left corner and winds counterclockwise.

        If either rx or ry is too large, rx and ry are scaled uniformly until the
        corners fit. If rx or ry is less than or equal to zero, addRoundRect() appends
        SkRect rect to SkPath.

        After appending, SkPath may be empty, or may contain: SkRect, oval, or SkRRect.

        @param rect  bounds of SkRRect
        @param rx    x-axis radius of rounded corners on the SkRRect
        @param ry    y-axis radius of rounded corners on the SkRRect
        @param dir   SkPath::Direction to wind SkRRect
        @return      reference to SkPath
    */
    SkPath& addRoundRect(const SkRect& rect, SkScalar rx, SkScalar ry,
                         SkPathDirection dir = SkPathDirection::kCW);

    /** Appends SkRRect to SkPath, creating a new closed contour. SkRRect has bounds
        equal to rect; each corner is 90 degrees of an ellipse with radii from the
        array.

        @param rect   bounds of SkRRect
        @param radii  array of 8 SkScalar values, a radius pair for each corner
        @param dir    SkPath::Direction to wind SkRRect
        @return       reference to SkPath
    */
    SkPath& addRoundRect(const SkRect& rect, const SkScalar radii[],
                         SkPathDirection dir = SkPathDirection::kCW);

    /** Adds rrect to SkPath, creating a new closed contour. If
        dir is kCW_Direction, rrect starts at top-left of the lower-left corner and
        winds clockwise. If dir is kCCW_Direction, rrect starts at the bottom-left
        of the upper-left corner and winds counterclockwise.

        After appending, SkPath may be empty, or may contain: SkRect, oval, or SkRRect.

        @param rrect  bounds and radii of rounded rectangle
        @param dir    SkPath::Direction to wind SkRRect
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addRRect
    */
    SkPath& addRRect(const SkRRect& rrect, SkPathDirection dir = SkPathDirection::kCW);

    /** Adds rrect to SkPath, creating a new closed contour. If dir is kCW_Direction, rrect
        winds clockwise; if dir is kCCW_Direction, rrect winds counterclockwise.
        start determines the first point of rrect to add.

        @param rrect  bounds and radii of rounded rectangle
        @param dir    SkPath::Direction to wind SkRRect
        @param start  index of initial point of SkRRect
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addRRect_2
    */
    SkPath& addRRect(const SkRRect& rrect, SkPathDirection dir, unsigned start);

    /** Adds contour created from line array, adding (count - 1) line segments.
        Contour added starts at pts[0], then adds a line for every additional SkPoint
        in pts array. If close is true, appends kClose_Verb to SkPath, connecting
        pts[count - 1] and pts[0].

        If count is zero, append kMove_Verb to path.
        Has no effect if count is less than one.

        @param pts    array of line sharing end and start SkPoint
        @param count  length of SkPoint array
        @param close  true to add line connecting contour end and start
        @return       reference to SkPath

        example: https://fiddle.skia.org/c/@Path_addPoly
    */
    SkPath& addPoly(const SkPoint pts[], int count, bool close);

    /** Adds contour created from list. Contour added starts at list[0], then adds a line
        for every additional SkPoint in list. If close is true, appends kClose_Verb to SkPath,
        connecting last and first SkPoint in list.

        If list is empty, append kMove_Verb to path.

        @param list   array of SkPoint
        @param close  true to add line connecting contour end and start
        @return       reference to SkPath
    */
    SkPath& addPoly(const std::initializer_list<SkPoint>& list, bool close) {
        return this->addPoly(list.begin(), SkToInt(list.size()), close);
    }

#ifdef SK_HIDE_PATH_EDIT_METHODS
public:
#endif

    /** \enum SkPath::AddPathMode
        AddPathMode chooses how addPath() appends. Adding one SkPath to another can extend
        the last contour or start a new contour.
    */
    enum AddPathMode {
        /** Contours are appended to the destination path as new contours.
        */
        kAppend_AddPathMode,
        /** Extends the last contour of the destination path with the first countour
            of the source path, connecting them with a line.  If the last contour is
            closed, a new empty contour starting at its start point is extended instead.
            If the destination path is empty, the result is the source path.
            The last path of the result is closed only if the last path of the source is.
        */
        kExtend_AddPathMode,
    };

    /** Appends src to SkPath, offset by (dx, dy).

        If mode is kAppend_AddPathMode, src verb array, SkPoint array, and conic weights are
        added unaltered. If mode is kExtend_AddPathMode, add line before appending
        verbs, SkPoint, and conic weights.

        @param src   SkPath verbs, SkPoint, and conic weights to add
        @param dx    offset added to src SkPoint array x-axis coordinates
        @param dy    offset added to src SkPoint array y-axis coordinates
        @param mode  kAppend_AddPathMode or kExtend_AddPathMode
        @return      reference to SkPath
    */
    SkPath& addPath(const SkPath& src, SkScalar dx, SkScalar dy,
                    AddPathMode mode = kAppend_AddPathMode);

    /** Appends src to SkPath.

        If mode is kAppend_AddPathMode, src verb array, SkPoint array, and conic weights are
        added unaltered. If mode is kExtend_AddPathMode, add line before appending
        verbs, SkPoint, and conic weights.

        @param src   SkPath verbs, SkPoint, and conic weights to add
        @param mode  kAppend_AddPathMode or kExtend_AddPathMode
        @return      reference to SkPath
    */
    SkPath& addPath(const SkPath& src, AddPathMode mode = kAppend_AddPathMode) {
        SkMatrix m;
        m.reset();
        return this->addPath(src, m, mode);
    }

    /** Appends src to SkPath, transformed by matrix. Transformed curves may have different
        verbs, SkPoint, and conic weights.

        If mode is kAppend_AddPathMode, src verb array, SkPoint array, and conic weights are
        added unaltered. If mode is kExtend_AddPathMode, add line before appending
        verbs, SkPoint, and conic weights.

        @param src     SkPath verbs, SkPoint, and conic weights to add
        @param matrix  transform applied to src
        @param mode    kAppend_AddPathMode or kExtend_AddPathMode
        @return        reference to SkPath
    */
    SkPath& addPath(const SkPath& src, const SkMatrix& matrix,
                    AddPathMode mode = kAppend_AddPathMode);

    /** Appends src to SkPath, from back to front.
        Reversed src always appends a new contour to SkPath.

        @param src  SkPath verbs, SkPoint, and conic weights to add
        @return     reference to SkPath

        example: https://fiddle.skia.org/c/@Path_reverseAddPath
    */
    SkPath& reverseAddPath(const SkPath& src);

    /** Offsets SkPoint array by (dx, dy). Offset SkPath replaces dst.
        If dst is nullptr, SkPath is replaced by offset data.

        @param dx   offset added to SkPoint array x-axis coordinates
        @param dy   offset added to SkPoint array y-axis coordinates
        @param dst  overwritten, translated copy of SkPath; may be nullptr

        example: https://fiddle.skia.org/c/@Path_offset
    */
    void offset(SkScalar dx, SkScalar dy, SkPath* dst) const;

    /** Offsets SkPoint array by (dx, dy). SkPath is replaced by offset data.

        @param dx  offset added to SkPoint array x-axis coordinates
        @param dy  offset added to SkPoint array y-axis coordinates
    */
    void offset(SkScalar dx, SkScalar dy) {
        this->offset(dx, dy, this);
    }

    /** Transforms verb array, SkPoint array, and weight by matrix.
        transform may change verbs and increase their number.
        Transformed SkPath replaces dst; if dst is nullptr, original data
        is replaced.

        @param matrix  SkMatrix to apply to SkPath
        @param dst     overwritten, transformed copy of SkPath; may be nullptr
        @param pc      whether to apply perspective clipping

        example: https://fiddle.skia.org/c/@Path_transform
    */
    void transform(const SkMatrix& matrix, SkPath* dst,
                   SkApplyPerspectiveClip pc = SkApplyPerspectiveClip::kYes) const;

    /** Transforms verb array, SkPoint array, and weight by matrix.
        transform may change verbs and increase their number.
        SkPath is replaced by transformed data.

        @param matrix  SkMatrix to apply to SkPath
        @param pc      whether to apply perspective clipping
    */
    void transform(const SkMatrix& matrix,
                   SkApplyPerspectiveClip pc = SkApplyPerspectiveClip::kYes) {
        this->transform(matrix, this, pc);
    }

    SkPath makeTransform(const SkMatrix& m,
                         SkApplyPerspectiveClip pc = SkApplyPerspectiveClip::kYes) const {
        SkPath dst;
        this->transform(m, &dst, pc);
        return dst;
    }

    SkPath makeScale(SkScalar sx, SkScalar sy) {
        return this->makeTransform(SkMatrix::Scale(sx, sy), SkApplyPerspectiveClip::kNo);
    }

    /** Returns last point on SkPath in lastPt. Returns false if SkPoint array is empty,
        storing (0, 0) if lastPt is not nullptr.

        @param lastPt  storage for final SkPoint in SkPoint array; may be nullptr
        @return        true if SkPoint array contains one or more SkPoint

        example: https://fiddle.skia.org/c/@Path_getLastPt
    */
    bool getLastPt(SkPoint* lastPt) const;

    /** Sets last point to (x, y). If SkPoint array is empty, append kMove_Verb to
        verb array and append (x, y) to SkPoint array.

        @param x  set x-axis value of last point
        @param y  set y-axis value of last point

        example: https://fiddle.skia.org/c/@Path_setLastPt
    */
    void setLastPt(SkScalar x, SkScalar y);

    /** Sets the last point on the path. If SkPoint array is empty, append kMove_Verb to
        verb array and append p to SkPoint array.

        @param p  set value of last point
    */
    void setLastPt(const SkPoint& p) {
        this->setLastPt(p.fX, p.fY);
    }

    /** \enum SkPath::SegmentMask
        SegmentMask constants correspond to each drawing Verb type in SkPath; for
        instance, if SkPath only contains lines, only the kLine_SegmentMask bit is set.
    */
    enum SegmentMask {
        kLine_SegmentMask  = kLine_SkPathSegmentMask,
        kQuad_SegmentMask  = kQuad_SkPathSegmentMask,
        kConic_SegmentMask = kConic_SkPathSegmentMask,
        kCubic_SegmentMask = kCubic_SkPathSegmentMask,
    };

    /** Returns a mask, where each set bit corresponds to a SegmentMask constant
        if SkPath contains one or more verbs of that type.
        Returns zero if SkPath contains no lines, or curves: quads, conics, or cubics.

        getSegmentMasks() returns a cached result; it is very fast.

        @return  SegmentMask bits or zero
    */
    uint32_t getSegmentMasks() const;

    /** \enum SkPath::Verb
        Verb instructs SkPath how to interpret one or more SkPoint and optional conic weight;
        manage contour, and terminate SkPath.
    */
    enum Verb {
        kMove_Verb  = static_cast<int>(SkPathVerb::kMove),
        kLine_Verb  = static_cast<int>(SkPathVerb::kLine),
        kQuad_Verb  = static_cast<int>(SkPathVerb::kQuad),
        kConic_Verb = static_cast<int>(SkPathVerb::kConic),
        kCubic_Verb = static_cast<int>(SkPathVerb::kCubic),
        kClose_Verb = static_cast<int>(SkPathVerb::kClose),
        kDone_Verb  = kClose_Verb + 1
    };

    /** \class SkPath::Iter
        Iterates through verb array, and associated SkPoint array and conic weight.
        Provides options to treat open contours as closed, and to ignore
        degenerate data.
    */
    class SK_API Iter {
    public:

        /** Initializes SkPath::Iter with an empty SkPath. next() on SkPath::Iter returns
            kDone_Verb.
            Call setPath to initialize SkPath::Iter at a later time.

            @return  SkPath::Iter of empty SkPath

        example: https://fiddle.skia.org/c/@Path_Iter_Iter
        */
        Iter();

        /** Sets SkPath::Iter to return elements of verb array, SkPoint array, and conic weight in
            path. If forceClose is true, SkPath::Iter will add kLine_Verb and kClose_Verb after each
            open contour. path is not altered.

            @param path        SkPath to iterate
            @param forceClose  true if open contours generate kClose_Verb
            @return            SkPath::Iter of path

        example: https://fiddle.skia.org/c/@Path_Iter_const_SkPath
        */
        Iter(const SkPath& path, bool forceClose);

        /** Sets SkPath::Iter to return elements of verb array, SkPoint array, and conic weight in
            path. If forceClose is true, SkPath::Iter will add kLine_Verb and kClose_Verb after each
            open contour. path is not altered.

            @param path        SkPath to iterate
            @param forceClose  true if open contours generate kClose_Verb

        example: https://fiddle.skia.org/c/@Path_Iter_setPath
        */
        void setPath(const SkPath& path, bool forceClose);

        /** Returns next SkPath::Verb in verb array, and advances SkPath::Iter.
            When verb array is exhausted, returns kDone_Verb.

            Zero to four SkPoint are stored in pts, depending on the returned SkPath::Verb.

            @param pts  storage for SkPoint data describing returned SkPath::Verb
            @return     next SkPath::Verb from verb array

        example: https://fiddle.skia.org/c/@Path_RawIter_next
        */
        Verb next(SkPoint pts[4]);

        /** Returns conic weight if next() returned kConic_Verb.

            If next() has not been called, or next() did not return kConic_Verb,
            result is undefined.

            @return  conic weight for conic SkPoint returned by next()
        */
        SkScalar conicWeight() const { return *fConicWeights; }

        /** Returns true if last kLine_Verb returned by next() was generated
            by kClose_Verb. When true, the end point returned by next() is
            also the start point of contour.

            If next() has not been called, or next() did not return kLine_Verb,
            result is undefined.

            @return  true if last kLine_Verb was generated by kClose_Verb
        */
        bool isCloseLine() const { return SkToBool(fCloseLine); }

        /** Returns true if subsequent calls to next() return kClose_Verb before returning
            kMove_Verb. if true, contour SkPath::Iter is processing may end with kClose_Verb, or
            SkPath::Iter may have been initialized with force close set to true.

            @return  true if contour is closed

        example: https://fiddle.skia.org/c/@Path_Iter_isClosedContour
        */
        bool isClosedContour() const;

    private:
        const SkPoint*  fPts;
        const uint8_t*  fVerbs;
        const uint8_t*  fVerbStop;
        const SkScalar* fConicWeights;
        SkPoint         fMoveTo;
        SkPoint         fLastPt;
        bool            fForceClose;
        bool            fNeedClose;
        bool            fCloseLine;

        Verb autoClose(SkPoint pts[2]);
    };

private:
    /** \class SkPath::RangeIter
        Iterates through a raw range of path verbs, points, and conics. All values are returned
        unaltered.

        NOTE: This class will be moved into SkPathPriv once RangeIter is removed.
    */
    class RangeIter {
    public:
        RangeIter() = default;
        RangeIter(const uint8_t* verbs, const SkPoint* points, const SkScalar* weights)
                : fVerb(verbs), fPoints(points), fWeights(weights) {
            SkDEBUGCODE(fInitialPoints = fPoints;)
        }
        bool operator!=(const RangeIter& that) const {
            return fVerb != that.fVerb;
        }
        bool operator==(const RangeIter& that) const {
            return fVerb == that.fVerb;
        }
        RangeIter& operator++() {
            auto verb = static_cast<SkPathVerb>(*fVerb++);
            fPoints += pts_advance_after_verb(verb);
            if (verb == SkPathVerb::kConic) {
                ++fWeights;
            }
            return *this;
        }
        RangeIter operator++(int) {
            RangeIter copy = *this;
            this->operator++();
            return copy;
        }
        SkPathVerb peekVerb() const {
            return static_cast<SkPathVerb>(*fVerb);
        }
        std::tuple<SkPathVerb, const SkPoint*, const SkScalar*> operator*() const {
            SkPathVerb verb = this->peekVerb();
            // We provide the starting point for beziers by peeking backwards from the current
            // point, which works fine as long as there is always a kMove before any geometry.
            // (SkPath::injectMoveToIfNeeded should have guaranteed this to be the case.)
            int backset = pts_backset_for_verb(verb);
            SkASSERT(fPoints + backset >= fInitialPoints);
            return {verb, fPoints + backset, fWeights};
        }
    private:
        constexpr static int pts_advance_after_verb(SkPathVerb verb) {
            switch (verb) {
                case SkPathVerb::kMove: return 1;
                case SkPathVerb::kLine: return 1;
                case SkPathVerb::kQuad: return 2;
                case SkPathVerb::kConic: return 2;
                case SkPathVerb::kCubic: return 3;
                case SkPathVerb::kClose: return 0;
            }
            SkUNREACHABLE;
        }
        constexpr static int pts_backset_for_verb(SkPathVerb verb) {
            switch (verb) {
                case SkPathVerb::kMove: return 0;
                case SkPathVerb::kLine: return -1;
                case SkPathVerb::kQuad: return -1;
                case SkPathVerb::kConic: return -1;
                case SkPathVerb::kCubic: return -1;
                case SkPathVerb::kClose: return -1;
            }
            SkUNREACHABLE;
        }
        const uint8_t* fVerb = nullptr;
        const SkPoint* fPoints = nullptr;
        const SkScalar* fWeights = nullptr;
        SkDEBUGCODE(const SkPoint* fInitialPoints = nullptr;)
    };
public:

    /** \class SkPath::RawIter
        Use Iter instead. This class will soon be removed and RangeIter will be made private.
    */
    class SK_API RawIter {
    public:

        /** Initializes RawIter with an empty SkPath. next() on RawIter returns kDone_Verb.
            Call setPath to initialize SkPath::Iter at a later time.

            @return  RawIter of empty SkPath
        */
        RawIter() {}

        /** Sets RawIter to return elements of verb array, SkPoint array, and conic weight in path.

            @param path  SkPath to iterate
            @return      RawIter of path
        */
        RawIter(const SkPath& path) {
            setPath(path);
        }

        /** Sets SkPath::Iter to return elements of verb array, SkPoint array, and conic weight in
            path.

            @param path  SkPath to iterate
        */
        void setPath(const SkPath&);

        /** Returns next SkPath::Verb in verb array, and advances RawIter.
            When verb array is exhausted, returns kDone_Verb.
            Zero to four SkPoint are stored in pts, depending on the returned SkPath::Verb.

            @param pts  storage for SkPoint data describing returned SkPath::Verb
            @return     next SkPath::Verb from verb array
        */
        Verb next(SkPoint[4]);

        /** Returns next SkPath::Verb, but does not advance RawIter.

            @return  next SkPath::Verb from verb array
        */
        Verb peek() const {
            return (fIter != fEnd) ? static_cast<Verb>(std::get<0>(*fIter)) : kDone_Verb;
        }

        /** Returns conic weight if next() returned kConic_Verb.

            If next() has not been called, or next() did not return kConic_Verb,
            result is undefined.

            @return  conic weight for conic SkPoint returned by next()
        */
        SkScalar conicWeight() const {
            return fConicWeight;
        }

    private:
        RangeIter fIter;
        RangeIter fEnd;
        SkScalar fConicWeight = 0;
        friend class SkPath;

    };

    /** Returns true if the point (x, y) is contained by SkPath, taking into
        account FillType.

        @param x  x-axis value of containment test
        @param y  y-axis value of containment test
        @return   true if SkPoint is in SkPath

        example: https://fiddle.skia.org/c/@Path_contains
    */
    bool contains(SkScalar x, SkScalar y) const;

    /** Writes text representation of SkPath to stream. If stream is nullptr, writes to
        standard output. Set dumpAsHex true to generate exact binary representations
        of floating point numbers used in SkPoint array and conic weights.

        @param stream      writable SkWStream receiving SkPath text representation; may be nullptr
        @param dumpAsHex   true if SkScalar values are written as hexadecimal

        example: https://fiddle.skia.org/c/@Path_dump
    */
    void dump(SkWStream* stream, bool dumpAsHex) const;

    void dump() const { this->dump(nullptr, false); }
    void dumpHex() const { this->dump(nullptr, true); }

    // Like dump(), but outputs for the SkPath::Make() factory
    void dumpArrays(SkWStream* stream, bool dumpAsHex) const;
    void dumpArrays() const { this->dumpArrays(nullptr, false); }

    /** Writes SkPath to buffer, returning the number of bytes written.
        Pass nullptr to obtain the storage size.

        Writes SkPath::FillType, verb array, SkPoint array, conic weight, and
        additionally writes computed information like SkPath::Convexity and bounds.

        Use only be used in concert with readFromMemory();
        the format used for SkPath in memory is not guaranteed.

        @param buffer  storage for SkPath; may be nullptr
        @return        size of storage required for SkPath; always a multiple of 4

        example: https://fiddle.skia.org/c/@Path_writeToMemory
    */
    size_t writeToMemory(void* buffer) const;

    /** Writes SkPath to buffer, returning the buffer written to, wrapped in SkData.

        serialize() writes SkPath::FillType, verb array, SkPoint array, conic weight, and
        additionally writes computed information like SkPath::Convexity and bounds.

        serialize() should only be used in concert with readFromMemory().
        The format used for SkPath in memory is not guaranteed.

        @return  SkPath data wrapped in SkData buffer

        example: https://fiddle.skia.org/c/@Path_serialize
    */
    sk_sp<SkData> serialize() const;

    /** Initializes SkPath from buffer of size length. Returns zero if the buffer is
        data is inconsistent, or the length is too small.

        Reads SkPath::FillType, verb array, SkPoint array, conic weight, and
        additionally reads computed information like SkPath::Convexity and bounds.

        Used only in concert with writeToMemory();
        the format used for SkPath in memory is not guaranteed.

        @param buffer  storage for SkPath
        @param length  buffer size in bytes; must be multiple of 4
        @return        number of bytes read, or zero on failure

        example: https://fiddle.skia.org/c/@Path_readFromMemory
    */
    size_t readFromMemory(const void* buffer, size_t length);

    /** (See Skia bug 1762.)
        Returns a non-zero, globally unique value. A different value is returned
        if verb array, SkPoint array, or conic weight changes.

        Setting SkPath::FillType does not change generation identifier.

        Each time the path is modified, a different generation identifier will be returned.
        SkPath::FillType does affect generation identifier on Android framework.

        @return  non-zero, globally unique value

        example: https://fiddle.skia.org/c/@Path_getGenerationID
    */
    uint32_t getGenerationID() const;

    /** Returns if SkPath data is consistent. Corrupt SkPath data is detected if
        internal values are out of range or internal storage does not match
        array dimensions.

        @return  true if SkPath data is consistent
    */
    bool isValid() const;

    using sk_is_trivially_relocatable = std::true_type;

private:
    SkPath(sk_sp<SkPathRef>, SkPathFillType, bool isVolatile, SkPathConvexity,
           SkPathFirstDirection firstDirection);

    sk_sp<SkPathRef>               fPathRef;
    int                            fLastMoveToIndex;
    mutable std::atomic<uint8_t>   fConvexity;      // SkPathConvexity
    mutable std::atomic<uint8_t>   fFirstDirection; // SkPathFirstDirection
    uint8_t                        fFillType    : 2;
    uint8_t                        fIsVolatile  : 1;

    static_assert(::sk_is_trivially_relocatable<decltype(fPathRef)>::value);

    /** Resets all fields other than fPathRef to their initial 'empty' values.
     *  Assumes the caller has already emptied fPathRef.
     *  On Android increments fGenerationID without reseting it.
     */
    void resetFields();

    /** Sets all fields other than fPathRef to the values in 'that'.
     *  Assumes the caller has already set fPathRef.
     *  Doesn't change fGenerationID or fSourcePath on Android.
     */
    void copyFields(const SkPath& that);

    size_t writeToMemoryAsRRect(void* buffer) const;
    size_t readAsRRect(const void*, size_t);
    size_t readFromMemory_EQ4Or5(const void*, size_t);

    friend class Iter;
    friend class SkPathPriv;
    friend class SkPathStroker;

    /*  Append, in reverse order, the first contour of path, ignoring path's
        last point. If no moveTo() call has been made for this contour, the
        first point is automatically set to (0,0).
    */
    SkPath& reversePathTo(const SkPath&);

    // called before we add points for lineTo, quadTo, cubicTo, checking to see
    // if we need to inject a leading moveTo first
    //
    //  SkPath path; path.lineTo(...);   <--- need a leading moveTo(0, 0)
    // SkPath path; ... path.close(); path.lineTo(...) <-- need a moveTo(previous moveTo)
    //
    inline void injectMoveToIfNeeded();

    inline bool hasOnlyMoveTos() const;

    SkPathConvexity computeConvexity() const;

    bool isValidImpl() const;
    /** Asserts if SkPath data is inconsistent.
        Debugging check intended for internal use only.
     */
#ifdef SK_DEBUG
    void validate() const;
    void validateRef() const;
#endif

    // called by stroker to see if all points (in the last contour) are equal and worthy of a cap
    bool isZeroLengthSincePoint(int startPtIndex) const;

    /** Returns if the path can return a bound at no cost (true) or will have to
        perform some computation (false).
     */
    bool hasComputedBounds() const;

    // 'rect' needs to be sorted
    void setBounds(const SkRect& rect);

    void setPt(int index, SkScalar x, SkScalar y);

    SkPath& dirtyAfterEdit();

    // Bottlenecks for working with fConvexity and fFirstDirection.
    // Notice the setters are const... these are mutable atomic fields.
    void  setConvexity(SkPathConvexity) const;

    void setFirstDirection(SkPathFirstDirection) const;
    SkPathFirstDirection getFirstDirection() const;

    /** Returns the comvexity type, computing if needed. Never returns kUnknown.
        @return  path's convexity type (convex or concave)
    */
    SkPathConvexity getConvexity() const;

    SkPathConvexity getConvexityOrUnknown() const;

    // Compares the cached value with a freshly computed one (computeConvexity())
    bool isConvexityAccurate() const;

    /** Stores a convexity type for this path. This is what will be returned if
     *  getConvexityOrUnknown() is called. If you pass kUnknown, then if getContexityType()
     *  is called, the real convexity will be computed.
     *
     *  example: https://fiddle.skia.org/c/@Path_setConvexity
     */
    void setConvexity(SkPathConvexity convexity);

    /** Shrinks SkPath verb array and SkPoint array storage to discard unused capacity.
     *  May reduce the heap overhead for SkPath known to be fully constructed.
     *
     *  NOTE: This may relocate the underlying buffers, and thus any Iterators referencing
     *        this path should be discarded after calling shrinkToFit().
     */
    void shrinkToFit();

    // Creates a new Path after the supplied arguments have been validated by
    // sk_path_analyze_verbs().
    static SkPath MakeInternal(const SkPathVerbAnalysis& analsis,
                               const SkPoint points[],
                               const uint8_t verbs[],
                               int verbCount,
                               const SkScalar conics[],
                               SkPathFillType fillType,
                               bool isVolatile);

    friend class SkAutoPathBoundsUpdate;
    friend class SkAutoDisableOvalCheck;
    friend class SkAutoDisableDirectionCheck;
    friend class SkPathBuilder;
    friend class SkPathEdgeIter;
    friend class SkPathWriter;
    friend class SkOpBuilder;
    friend class SkBench_AddPathTest; // perf test reversePathTo
    friend class PathTest_Private; // unit test reversePathTo
    friend class ForceIsRRect_Private; // unit test isRRect
    friend class FuzzPath; // for legacy access to validateRef
};

// Copyright 2022 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkAlignedStorage_DEFINED
#define SkAlignedStorage_DEFINED


template <int N, typename T> class SkAlignedSTStorage {
public:
    SkAlignedSTStorage() {}
    SkAlignedSTStorage(SkAlignedSTStorage&&) = delete;
    SkAlignedSTStorage(const SkAlignedSTStorage&) = delete;
    SkAlignedSTStorage& operator=(SkAlignedSTStorage&&) = delete;
    SkAlignedSTStorage& operator=(const SkAlignedSTStorage&) = delete;

    // Returns void* because this object does not initialize the
    // memory. Use placement new for types that require a constructor.
    void* get() { return fStorage; }
    const void* get() const { return fStorage; }

    // Act as a container of bytes because the storage is uninitialized.
    std::byte* data() { return fStorage; }
    const std::byte* data() const { return fStorage; }
    size_t size() const { return std::size(fStorage); }

private:
    alignas(T) std::byte fStorage[sizeof(T) * N];
};

#endif  // SkAlignedStorage_DEFINED

// Having this be an export works around IWYU churn related to
// https://github.com/include-what-you-use/include-what-you-use/issues/1121

// Add macro to check the lifetime of initializer_list arguments. initializer_list has a very
// short life span, and can only be used as a parameter, and not as a variable.
#if defined(__clang__) && defined(__has_cpp_attribute) && __has_cpp_attribute(clang::lifetimebound)
#define SK_CHECK_IL_LIFETIME [[clang::lifetimebound]]
#else
#define SK_CHECK_IL_LIFETIME
#endif

/**
 * SkSpan holds a reference to contiguous data of type T along with a count. SkSpan does not own
 * the data itself but is merely a reference, therefore you must take care with the lifetime of
 * the underlying data.
 *
 * SkSpan is a count and a pointer into existing array or data type that stores its data in
 * contiguous memory like std::vector. Any container that works with std::size() and std::data()
 * can be used.
 *
 * SkSpan makes a convenient parameter for a routine to accept array like things. This allows you to
 * write the routine without overloads for all different container types.
 *
 * Example:
 *     void routine(SkSpan<const int> a) { ... }
 *
 *     std::vector v = {1, 2, 3, 4, 5};
 *
 *     routine(a);
 *
 * A word of caution when working with initializer_list, initializer_lists have a lifetime that is
 * limited to the current statement. The following is correct and safe:
 *
 * Example:
 *     routine({1,2,3,4,5});
 *
 * The following is undefined, and will result in erratic execution:
 *
 * Bad Example:
 *     initializer_list l = {1, 2, 3, 4, 5};   // The data behind l dies at the ;.
 *     routine(l);
 */
template <typename T>
class SkSpan {
public:
    constexpr SkSpan() : fPtr{nullptr}, fSize{0} {}

    template <typename Integer, std::enable_if_t<std::is_integral_v<Integer>, bool> = true>
    constexpr SkSpan(T* ptr, Integer size) : fPtr{ptr}, fSize{SkToSizeT(size)} {
        SkASSERT(ptr || fSize == 0);  // disallow nullptr + a nonzero size
        SkASSERT(fSize < kMaxSize);
    }
    template <typename U, typename = std::enable_if_t<std::is_same_v<const U, T>>>
    constexpr SkSpan(const SkSpan<U>& that) : fPtr(std::data(that)), fSize(std::size(that)) {}
    constexpr SkSpan(const SkSpan& o) = default;
    template<size_t N> constexpr SkSpan(T(&a)[N]) : SkSpan(a, N) { }
    template<typename Container>
    constexpr SkSpan(Container&& c) : SkSpan(std::data(c), std::size(c)) { }
    SkSpan(std::initializer_list<T> il SK_CHECK_IL_LIFETIME)
            : SkSpan(std::data(il), std::size(il)) {}

    constexpr SkSpan& operator=(const SkSpan& that) = default;

    constexpr T& operator [] (size_t i) const {
        return fPtr[sk_collection_check_bounds(i, this->size())];
    }
    constexpr T& front() const { sk_collection_not_empty(this->empty()); return fPtr[0]; }
    constexpr T& back()  const { sk_collection_not_empty(this->empty()); return fPtr[fSize - 1]; }
    constexpr T* begin() const { return fPtr; }
    constexpr T* end() const { return fPtr + fSize; }
    constexpr auto rbegin() const { return std::make_reverse_iterator(this->end()); }
    constexpr auto rend() const { return std::make_reverse_iterator(this->begin()); }
    constexpr T* data() const { return this->begin(); }
    constexpr size_t size() const { return fSize; }
    constexpr bool empty() const { return fSize == 0; }
    constexpr size_t size_bytes() const { return fSize * sizeof(T); }
    constexpr SkSpan<T> first(size_t prefixLen) const {
        return SkSpan{fPtr, sk_collection_check_length(prefixLen, fSize)};
    }
    constexpr SkSpan<T> last(size_t postfixLen) const {
        return SkSpan{fPtr + (this->size() - postfixLen),
                      sk_collection_check_length(postfixLen, fSize)};
    }
    constexpr SkSpan<T> subspan(size_t offset) const {
        return this->subspan(offset, this->size() - offset);
    }
    constexpr SkSpan<T> subspan(size_t offset, size_t count) const {
        const size_t safeOffset = sk_collection_check_length(offset, fSize);

        // Should read offset + count > size(), but that could overflow. We know that safeOffset
        // is <= size, therefore the subtraction will not overflow.
        if (count > this->size() - safeOffset) SK_UNLIKELY {
            // The count is too large.
            SkUNREACHABLE;
        }
        return SkSpan{fPtr + safeOffset, count};
    }

private:
    static constexpr size_t kMaxSize = std::numeric_limits<size_t>::max() / sizeof(T);

    T* fPtr;
    size_t fSize;
};

template <typename Container>
SkSpan(Container&&) ->
        SkSpan<std::remove_pointer_t<decltype(std::data(std::declval<Container>()))>>;

// Copyright 2022 Google LLC.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkContainers_DEFINED
#define SkContainers_DEFINED



class SK_SPI SkContainerAllocator {
public:
    SkContainerAllocator(size_t sizeOfT, int maxCapacity)
            : fSizeOfT{sizeOfT}
            , fMaxCapacity{maxCapacity} {}

    // allocate will abort on failure. Given a capacity of 0, it will return the empty span.
    // The bytes allocated are freed using sk_free().
    SkSpan<std::byte> allocate(int capacity, double growthFactor = 1.0);

private:
    friend struct SkContainerAllocatorTestingPeer;
    // All capacity counts will be rounded up to kCapacityMultiple.
    // TODO: this is a constant from the original SkTArray code. This should be checked some how.
    static constexpr int64_t kCapacityMultiple = 8;

    // Rounds up capacity to next multiple of kCapacityMultiple and pin to fMaxCapacity.
    size_t roundUpCapacity(int64_t capacity) const;

    // Grows the capacity by growthFactor being sure to stay with in kMinBytes and fMaxCapacity.
    size_t growthFactorCapacity(int capacity, double growthFactor) const;

    const size_t fSizeOfT;
    const int64_t fMaxCapacity;
};

// sk_allocate_canfail returns the empty span on failure. Parameter size must be > 0.
SkSpan<std::byte> sk_allocate_canfail(size_t size);

// Returns the empty span if size is 0. sk_allocate_throw aborts on failure.
SkSpan<std::byte> sk_allocate_throw(size_t size);

SK_SPI void sk_report_container_overflow_and_die();
#endif  // SkContainers_DEFINED

/*
    memory wrappers to be implemented by the porting layer (platform)
*/


/** Free memory returned by sk_malloc(). It is safe to pass null. */
SK_API extern void sk_free(void*);

/**
 *  Called internally if we run out of memory. The platform implementation must
 *  not return, but should either throw an exception or otherwise exit.
 */
SK_API extern void sk_out_of_memory(void);

enum {
    /**
     *  If this bit is set, the returned buffer must be zero-initialized. If this bit is not set
     *  the buffer can be uninitialized.
     */
    SK_MALLOC_ZERO_INITIALIZE   = 1 << 0,

    /**
     *  If this bit is set, the implementation must throw/crash/quit if the request cannot
     *  be fulfilled. If this bit is not set, then it should return nullptr on failure.
     */
    SK_MALLOC_THROW             = 1 << 1,
};
/**
 *  Return a block of memory (at least 4-byte aligned) of at least the specified size.
 *  If the requested memory cannot be returned, either return nullptr or throw/exit, depending
 *  on the SK_MALLOC_THROW bit. If the allocation succeeds, the memory will be zero-initialized
 *  if the SK_MALLOC_ZERO_INITIALIZE bit was set.
 *
 *  To free the memory, call sk_free()
 */
SK_API extern void* sk_malloc_flags(size_t size, unsigned flags);

/** Same as standard realloc(), but this one never returns null on failure. It will throw
 *  if it fails.
 *  If size is 0, it will call sk_free on buffer and return null. (This behavior is implementation-
 *  defined for normal realloc. We follow what glibc does.)
 */
SK_API extern void* sk_realloc_throw(void* buffer, size_t size);

static inline void* sk_malloc_throw(size_t size) {
    return sk_malloc_flags(size, SK_MALLOC_THROW);
}

static inline void* sk_calloc_throw(size_t size) {
    return sk_malloc_flags(size, SK_MALLOC_THROW | SK_MALLOC_ZERO_INITIALIZE);
}

static inline void* sk_calloc_canfail(size_t size) {
#if defined(SK_BUILD_FOR_FUZZER)
    // To reduce the chance of OOM, pretend we can't allocate more than 200kb.
    if (size > 200000) {
        return nullptr;
    }
#endif
    return sk_malloc_flags(size, SK_MALLOC_ZERO_INITIALIZE);
}

// Performs a safe multiply count * elemSize, checking for overflow
SK_API extern void* sk_calloc_throw(size_t count, size_t elemSize);
SK_API extern void* sk_malloc_throw(size_t count, size_t elemSize);
SK_API extern void* sk_realloc_throw(void* buffer, size_t count, size_t elemSize);

/**
 *  These variants return nullptr on failure
 */
static inline void* sk_malloc_canfail(size_t size) {
#if defined(SK_BUILD_FOR_FUZZER)
    // To reduce the chance of OOM, pretend we can't allocate more than 200kb.
    if (size > 200000) {
        return nullptr;
    }
#endif
    return sk_malloc_flags(size, 0);
}
SK_API extern void* sk_malloc_canfail(size_t count, size_t elemSize);

// bzero is safer than memset, but we can't rely on it, so... sk_bzero()
static inline void sk_bzero(void* buffer, size_t size) {
    // Please c.f. sk_careful_memcpy.  It's undefined behavior to call memset(null, 0, 0).
    if (size) {
        memset(buffer, 0, size);
    }
}

/**
 *  sk_careful_memcpy() is just like memcpy(), but guards against undefined behavior.
 *
 * It is undefined behavior to call memcpy() with null dst or src, even if len is 0.
 * If an optimizer is "smart" enough, it can exploit this to do unexpected things.
 *     memcpy(dst, src, 0);
 *     if (src) {
 *         printf("%x\n", *src);
 *     }
 * In this code the compiler can assume src is not null and omit the if (src) {...} check,
 * unconditionally running the printf, crashing the program if src really is null.
 * Of the compilers we pay attention to only GCC performs this optimization in practice.
 */
static inline void* sk_careful_memcpy(void* dst, const void* src, size_t len) {
    // When we pass >0 len we had better already be passing valid pointers.
    // So we just need to skip calling memcpy when len == 0.
    if (len) {
        memcpy(dst,src,len);
    }
    return dst;
}

static inline void* sk_careful_memmove(void* dst, const void* src, size_t len) {
    // When we pass >0 len we had better already be passing valid pointers.
    // So we just need to skip calling memcpy when len == 0.
    if (len) {
        memmove(dst,src,len);
    }
    return dst;
}

static inline int sk_careful_memcmp(const void* a, const void* b, size_t len) {
    // When we pass >0 len we had better already be passing valid pointers.
    // So we just need to skip calling memcmp when len == 0.
    if (len == 0) {
        return 0;   // we treat zero-length buffers as "equal"
    }
    return memcmp(a, b, len);
}

namespace skia_private {
/** TArray<T> implements a typical, mostly std::vector-like array.
    Each T will be default-initialized on allocation, and ~T will be called on destruction.

    MEM_MOVE controls the behavior when a T needs to be moved (e.g. when the array is resized)
      - true: T will be bit-copied via memcpy.
      - false: T will be moved via move-constructors.
*/
template <typename T, bool MEM_MOVE = sk_is_trivially_relocatable_v<T>> class TArray {
public:
    using value_type = T;

    /**
     * Creates an empty array with no initial storage
     */
    TArray() : fOwnMemory(true), fCapacity{0} {}

    /**
     * Creates an empty array that will preallocate space for reserveCount elements.
     */
    explicit TArray(int reserveCount) : TArray() { this->reserve_exact(reserveCount); }

    /**
     * Copies one array to another. The new array will be heap allocated.
     */
    TArray(const TArray& that) : TArray(that.fData, that.fSize) {}

    TArray(TArray&& that) {
        if (that.fOwnMemory) {
            this->setData(that);
            that.setData({});
        } else {
            this->initData(that.fSize);
            that.move(fData);
        }
        fSize = std::exchange(that.fSize, 0);
    }

    /**
     * Creates a TArray by copying contents of a standard C array. The new
     * array will be heap allocated. Be careful not to use this constructor
     * when you really want the (void*, int) version.
     */
    TArray(const T* array, int count) {
        this->initData(count);
        this->copy(array);
    }

    /**
     * Creates a TArray by copying contents of an initializer list.
     */
    TArray(std::initializer_list<T> data) : TArray(data.begin(), data.size()) {}

    TArray& operator=(const TArray& that) {
        if (this == &that) {
            return *this;
        }
        this->clear();
        this->checkRealloc(that.size(), kExactFit);
        fSize = that.fSize;
        this->copy(that.fData);
        return *this;
    }
    TArray& operator=(TArray&& that) {
        if (this != &that) {
            this->clear();
            if (that.fOwnMemory) {
                // The storage is on the heap, so move the data pointer.
                if (fOwnMemory) {
                    sk_free(fData);
                }

                fData = std::exchange(that.fData, nullptr);

                // Can't use exchange with bitfields.
                fCapacity = that.fCapacity;
                that.fCapacity = 0;

                fOwnMemory = true;
            } else {
                // The data is stored inline in that, so move it element-by-element.
                this->checkRealloc(that.size(), kExactFit);
                that.move(fData);
            }
            fSize = std::exchange(that.fSize, 0);
        }
        return *this;
    }

    ~TArray() {
        this->destroyAll();
        if (fOwnMemory) {
            sk_free(fData);
        }
    }

    /**
     * Resets to size() = n newly constructed T objects and resets any reserve count.
     */
    void reset(int n) {
        SkASSERT(n >= 0);
        this->clear();
        this->checkRealloc(n, kExactFit);
        fSize = n;
        for (int i = 0; i < this->size(); ++i) {
            new (fData + i) T;
        }
    }

    /**
     * Resets to a copy of a C array and resets any reserve count.
     */
    void reset(const T* array, int count) {
        SkASSERT(count >= 0);
        this->clear();
        this->checkRealloc(count, kExactFit);
        fSize = count;
        this->copy(array);
    }

    /**
     * Ensures there is enough reserved space for at least n elements. This is guaranteed at least
     * until the array size grows above n and subsequently shrinks below n, any version of reset()
     * is called, or reserve() is called again.
     */
    void reserve(int n) {
        SkASSERT(n >= 0);
        if (n > this->size()) {
            this->checkRealloc(n - this->size(), kGrowing);
        }
    }

    /**
     * Ensures there is enough reserved space for exactly n elements. The same capacity guarantees
     * as above apply.
     */
    void reserve_exact(int n) {
        SkASSERT(n >= 0);
        if (n > this->size()) {
            this->checkRealloc(n - this->size(), kExactFit);
        }
    }

    void removeShuffle(int n) {
        SkASSERT(n < this->size());
        int newCount = fSize - 1;
        fSize = newCount;
        fData[n].~T();
        if (n != newCount) {
            this->move(n, newCount);
        }
    }

    // Is the array empty.
    bool empty() const { return fSize == 0; }

    /**
     * Adds one new default-initialized T value and returns it by reference. Note that the reference
     * only remains valid until the next call that adds or removes elements.
     */
    T& push_back() {
        void* newT = this->push_back_raw(1);
        return *new (newT) T;
    }

    /**
     * Adds one new T value which is copy-constructed, returning it by reference. As always,
     * the reference only remains valid until the next call that adds or removes elements.
     */
    T& push_back(const T& t) {
        T* newT;
        if (this->capacity() > fSize) SK_LIKELY {
            // Copy over the element directly.
            newT = new (fData + fSize) T(t);
        } else {
            newT = this->growAndConstructAtEnd(t);
        }

        fSize += 1;
        return *newT;
    }

    /**
     * Adds one new T value which is copy-constructed, returning it by reference.
     */
    T& push_back(T&& t) {
        T* newT;
        if (this->capacity() > fSize) SK_LIKELY {
            // Move over the element directly.
            newT = new (fData + fSize) T(std::move(t));
        } else {
            newT = this->growAndConstructAtEnd(std::move(t));
        }

        fSize += 1;
        return *newT;
    }

    /**
     *  Constructs a new T at the back of this array, returning it by reference.
     */
    template <typename... Args> T& emplace_back(Args&&... args) {
        T* newT;
        if (this->capacity() > fSize) SK_LIKELY {
            // Emplace the new element in directly.
            newT = new (fData + fSize) T(std::forward<Args>(args)...);
        } else {
            newT = this->growAndConstructAtEnd(std::forward<Args>(args)...);
        }

        fSize += 1;
        return *newT;
    }

    /**
     * Allocates n more default-initialized T values, and returns the address of
     * the start of that new range. Note: this address is only valid until the
     * next API call made on the array that might add or remove elements.
     */
    T* push_back_n(int n) {
        SkASSERT(n >= 0);
        T* newTs = TCast(this->push_back_raw(n));
        for (int i = 0; i < n; ++i) {
            new (&newTs[i]) T;
        }
        return newTs;
    }

    /**
     * Version of above that uses a copy constructor to initialize all n items
     * to the same T.
     */
    T* push_back_n(int n, const T& t) {
        SkASSERT(n >= 0);
        T* newTs = TCast(this->push_back_raw(n));
        for (int i = 0; i < n; ++i) {
            new (&newTs[i]) T(t);
        }
        return static_cast<T*>(newTs);
    }

    /**
     * Version of above that uses a copy constructor to initialize the n items
     * to separate T values.
     */
    T* push_back_n(int n, const T t[]) {
        SkASSERT(n >= 0);
        this->checkRealloc(n, kGrowing);
        T* end = this->end();
        for (int i = 0; i < n; ++i) {
            new (end + i) T(t[i]);
        }
        fSize += n;
        return end;
    }

    /**
     * Version of above that uses the move constructor to set n items.
     */
    T* move_back_n(int n, T* t) {
        SkASSERT(n >= 0);
        this->checkRealloc(n, kGrowing);
        T* end = this->end();
        for (int i = 0; i < n; ++i) {
            new (end + i) T(std::move(t[i]));
        }
        fSize += n;
        return end;
    }

    /**
     * Removes the last element. Not safe to call when size() == 0.
     */
    void pop_back() {
        sk_collection_not_empty(this->empty());
        --fSize;
        fData[fSize].~T();
    }

    /**
     * Removes the last n elements. Not safe to call when size() < n.
     */
    void pop_back_n(int n) {
        SkASSERT(n >= 0);
        SkASSERT(this->size() >= n);
        int i = fSize;
        while (i-- > fSize - n) {
            (*this)[i].~T();
        }
        fSize -= n;
    }

    /**
     * Pushes or pops from the back to resize. Pushes will be default
     * initialized.
     */
    void resize_back(int newCount) {
        SkASSERT(newCount >= 0);

        if (newCount > this->size()) {
            this->push_back_n(newCount - fSize);
        } else if (newCount < this->size()) {
            this->pop_back_n(fSize - newCount);
        }
    }

    /** Swaps the contents of this array with that array. Does a pointer swap if possible,
        otherwise copies the T values. */
    void swap(TArray& that) {
        using std::swap;
        if (this == &that) {
            return;
        }
        if (fOwnMemory && that.fOwnMemory) {
            swap(fData, that.fData);
            swap(fSize, that.fSize);

            // Can't use swap because fCapacity is a bit field.
            auto allocCount = fCapacity;
            fCapacity = that.fCapacity;
            that.fCapacity = allocCount;
        } else {
            // This could be more optimal...
            TArray copy(std::move(that));
            that = std::move(*this);
            *this = std::move(copy);
        }
    }

    T* begin() {
        return fData;
    }
    const T* begin() const {
        return fData;
    }

    // It's safe to use fItemArray + fSize because if fItemArray is nullptr then adding 0 is
    // valid and returns nullptr. See [expr.add] in the C++ standard.
    T* end() {
        if (fData == nullptr) {
            SkASSERT(fSize == 0);
        }
        return fData + fSize;
    }
    const T* end() const {
        if (fData == nullptr) {
            SkASSERT(fSize == 0);
        }
        return fData + fSize;
    }
    T* data() { return fData; }
    const T* data() const { return fData; }
    int size() const { return fSize; }
    size_t size_bytes() const { return Bytes(fSize); }
    void resize(size_t count) { this->resize_back((int)count); }

    void clear() {
        this->destroyAll();
        fSize = 0;
    }

    void shrink_to_fit() {
        if (!fOwnMemory || fSize == fCapacity) {
            return;
        }
        if (fSize == 0) {
            sk_free(fData);
            fData = nullptr;
            fCapacity = 0;
        } else {
            SkSpan<std::byte> allocation = Allocate(fSize);
            this->move(TCast(allocation.data()));
            if (fOwnMemory) {
                sk_free(fData);
            }
            this->setDataFromBytes(allocation);
        }
    }

    /**
     * Get the i^th element.
     */
    T& operator[] (int i) {
        return fData[sk_collection_check_bounds(i, this->size())];
    }

    const T& operator[] (int i) const {
        return fData[sk_collection_check_bounds(i, this->size())];
    }

    T& at(int i) { return (*this)[i]; }
    const T& at(int i) const { return (*this)[i]; }

    /**
     * equivalent to operator[](0)
     */
    T& front() {
        sk_collection_not_empty(this->empty());
        return fData[0];
    }

    const T& front() const {
        sk_collection_not_empty(this->empty());
        return fData[0];
    }

    /**
     * equivalent to operator[](size() - 1)
     */
    T& back() {
        sk_collection_not_empty(this->empty());
        return fData[fSize - 1];
    }

    const T& back() const {
        sk_collection_not_empty(this->empty());
        return fData[fSize - 1];
    }

    /**
     * equivalent to operator[](size()-1-i)
     */
    T& fromBack(int i) {
        return (*this)[fSize - i - 1];
    }

    const T& fromBack(int i) const {
        return (*this)[fSize - i - 1];
    }

    bool operator==(const TArray<T, MEM_MOVE>& right) const {
        int leftCount = this->size();
        if (leftCount != right.size()) {
            return false;
        }
        for (int index = 0; index < leftCount; ++index) {
            if (fData[index] != right.fData[index]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const TArray<T, MEM_MOVE>& right) const {
        return !(*this == right);
    }

    int capacity() const {
        return fCapacity;
    }

protected:
    // Creates an empty array that will use the passed storage block until it is insufficiently
    // large to hold the entire array.
    template <int InitialCapacity>
    TArray(SkAlignedSTStorage<InitialCapacity, T>* storage, int size = 0) {
        static_assert(InitialCapacity >= 0);
        SkASSERT(size >= 0);
        SkASSERT(storage->get() != nullptr);
        if (size > InitialCapacity) {
            this->initData(size);
        } else {
            this->setDataFromBytes(*storage);
            fSize = size;

            // setDataFromBytes always sets fOwnMemory to true, but we are actually using static
            // storage here, which shouldn't ever be freed.
            fOwnMemory = false;
        }
    }

    // Copy a C array, using pre-allocated storage if preAllocCount >= count. Otherwise, storage
    // will only be used when array shrinks to fit.
    template <int InitialCapacity>
    TArray(const T* array, int size, SkAlignedSTStorage<InitialCapacity, T>* storage)
        : TArray{storage, size}
    {
        this->copy(array);
    }

private:
    // Growth factors for checkRealloc.
    static constexpr double kExactFit = 1.0;
    static constexpr double kGrowing = 1.5;

    static constexpr int kMinHeapAllocCount = 8;
    static_assert(SkIsPow2(kMinHeapAllocCount), "min alloc count not power of two.");

    // Note for 32-bit machines kMaxCapacity will be <= SIZE_MAX. For 64-bit machines it will
    // just be INT_MAX if the sizeof(T) < 2^32.
    static constexpr int kMaxCapacity = SkToInt(std::min(SIZE_MAX / sizeof(T), (size_t)INT_MAX));

    void setDataFromBytes(SkSpan<std::byte> allocation) {
        T* data = TCast(allocation.data());
        // We have gotten extra bytes back from the allocation limit, pin to kMaxCapacity. It
        // would seem like the SkContainerAllocator should handle the divide, but it would have
        // to a full divide instruction. If done here the size is known at compile, and usually
        // can be implemented by a right shift. The full divide takes ~50X longer than the shift.
        size_t size = std::min(allocation.size() / sizeof(T), SkToSizeT(kMaxCapacity));
        this->setData(SkSpan<T>(data, size));
    }

    void setData(SkSpan<T> array) {
        fData = array.data();
        fCapacity = SkToU32(array.size());
        fOwnMemory = true;
    }

    // We disable Control-Flow Integrity sanitization (go/cfi) when casting item-array buffers.
    // CFI flags this code as dangerous because we are casting `buffer` to a T* while the buffer's
    // contents might still be uninitialized memory. When T has a vtable, this is especially risky
    // because we could hypothetically access a virtual method on fItemArray and jump to an
    // unpredictable location in memory. Of course, TArray won't actually use fItemArray in this
    // way, and we don't want to construct a T before the user requests one. There's no real risk
    // here, so disable CFI when doing these casts.
    SK_CLANG_NO_SANITIZE("cfi")
    static T* TCast(void* buffer) {
        return (T*)buffer;
    }

    static size_t Bytes(int n) {
        SkASSERT(n <= kMaxCapacity);
        return SkToSizeT(n) * sizeof(T);
    }

    static SkSpan<std::byte> Allocate(int capacity, double growthFactor = 1.0) {
        return SkContainerAllocator{sizeof(T), kMaxCapacity}.allocate(capacity, growthFactor);
    }

    void initData(int count) {
        this->setDataFromBytes(Allocate(count));
        fSize = count;
    }

    void destroyAll() {
        if (!this->empty()) {
            T* cursor = this->begin();
            T* const end = this->end();
            do {
                cursor->~T();
                cursor++;
            } while (cursor < end);
        }
    }

    /** In the following move and copy methods, 'dst' is assumed to be uninitialized raw storage.
     *  In the following move methods, 'src' is destroyed leaving behind uninitialized raw storage.
     */
    void copy(const T* src) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (!this->empty() && src != nullptr) {
                sk_careful_memcpy(fData, src, this->size_bytes());
            }
        } else {
            for (int i = 0; i < this->size(); ++i) {
                new (fData + i) T(src[i]);
            }
        }
    }

    void move(int dst, int src) {
        if constexpr (MEM_MOVE) {
            memcpy(static_cast<void*>(&fData[dst]),
                   static_cast<const void*>(&fData[src]),
                   sizeof(T));
        } else {
            new (&fData[dst]) T(std::move(fData[src]));
            fData[src].~T();
        }
    }

    void move(void* dst) {
        if constexpr (MEM_MOVE) {
            sk_careful_memcpy(dst, fData, Bytes(fSize));
        } else {
            for (int i = 0; i < this->size(); ++i) {
                new (static_cast<char*>(dst) + Bytes(i)) T(std::move(fData[i]));
                fData[i].~T();
            }
        }
    }

    // Helper function that makes space for n objects, adjusts the count, but does not initialize
    // the new objects.
    void* push_back_raw(int n) {
        this->checkRealloc(n, kGrowing);
        void* ptr = fData + fSize;
        fSize += n;
        return ptr;
    }

    template <typename... Args>
    SK_ALWAYS_INLINE T* growAndConstructAtEnd(Args&&... args) {
        SkSpan<std::byte> buffer = this->preallocateNewData(/*delta=*/1, kGrowing);
        T* newT = new (TCast(buffer.data()) + fSize) T(std::forward<Args>(args)...);
        this->installDataAndUpdateCapacity(buffer);

        return newT;
    }

    void checkRealloc(int delta, double growthFactor) {
        SkASSERT(delta >= 0);
        SkASSERT(fSize >= 0);
        SkASSERT(fCapacity >= 0);

        // Check if there are enough remaining allocated elements to satisfy the request.
        if (this->capacity() - fSize < delta) {
            // Looks like we need to reallocate.
            this->installDataAndUpdateCapacity(this->preallocateNewData(delta, growthFactor));
        }
    }

    SkSpan<std::byte> preallocateNewData(int delta, double growthFactor) {
        SkASSERT(delta >= 0);
        SkASSERT(fSize >= 0);
        SkASSERT(fCapacity >= 0);

        // Don't overflow fSize or size_t later in the memory allocation. Overflowing memory
        // allocation really only applies to fSizes on 32-bit machines; on 64-bit machines this
        // will probably never produce a check. Since kMaxCapacity is bounded above by INT_MAX,
        // this also checks the bounds of fSize.
        if (delta > kMaxCapacity - fSize) {
            sk_report_container_overflow_and_die();
        }
        const int newCount = fSize + delta;

        return Allocate(newCount, growthFactor);
    }

    void installDataAndUpdateCapacity(SkSpan<std::byte> allocation) {
        this->move(TCast(allocation.data()));
        if (fOwnMemory) {
            sk_free(fData);
        }
        this->setDataFromBytes(allocation);
        SkASSERT(fData != nullptr);
    }

    T* fData{nullptr};
    int fSize{0};
    uint32_t fOwnMemory : 1;
    uint32_t fCapacity : 31;
};

template <typename T, bool M> static inline void swap(TArray<T, M>& a, TArray<T, M>& b) {
    a.swap(b);
}

// Subclass of TArray that contains a pre-allocated memory block for the array.
template <int N, typename T, bool MEM_MOVE = sk_is_trivially_relocatable_v<T>>
class STArray : private SkAlignedSTStorage<N,T>, public TArray<T, MEM_MOVE> {
    static_assert(N > 0);
    using Storage = SkAlignedSTStorage<N,T>;

public:
    STArray()
        : Storage{}
        , TArray<T, MEM_MOVE>(this) {}  // Must use () to avoid confusion with initializer_list
                                        // when T=bool because * are convertable to bool.

    STArray(const T* array, int count)
        : Storage{}
        , TArray<T, MEM_MOVE>{array, count, this} {}

    STArray(std::initializer_list<T> data)
        : STArray{data.begin(), SkToInt(data.size())} {}

    explicit STArray(int reserveCount)
        : STArray() { this->reserve_exact(reserveCount); }

    STArray(const STArray& that)
        : STArray() { *this = that; }

    explicit STArray(const TArray<T, MEM_MOVE>& that)
        : STArray() { *this = that; }

    STArray(STArray&& that)
        : STArray() { *this = std::move(that); }

    explicit STArray(TArray<T, MEM_MOVE>&& that)
        : STArray() { *this = std::move(that); }

    STArray& operator=(const STArray& that) {
        TArray<T, MEM_MOVE>::operator=(that);
        return *this;
    }

    STArray& operator=(const TArray<T, MEM_MOVE>& that) {
        TArray<T, MEM_MOVE>::operator=(that);
        return *this;
    }

    STArray& operator=(STArray&& that) {
        TArray<T, MEM_MOVE>::operator=(std::move(that));
        return *this;
    }

    STArray& operator=(TArray<T, MEM_MOVE>&& that) {
        TArray<T, MEM_MOVE>::operator=(std::move(that));
        return *this;
    }

    // Force the use of TArray for data() and size().
    using TArray<T, MEM_MOVE>::data;
    using TArray<T, MEM_MOVE>::size;
};
}  // namespace skia_private

class SK_SPI SkTDStorage {
public:
    explicit SkTDStorage(int sizeOfT);
    SkTDStorage(const void* src, int size, int sizeOfT);

    // Copy
    SkTDStorage(const SkTDStorage& that);
    SkTDStorage& operator= (const SkTDStorage& that);

    // Move
    SkTDStorage(SkTDStorage&& that);
    SkTDStorage& operator= (SkTDStorage&& that);

    ~SkTDStorage();

    void reset();
    void swap(SkTDStorage& that);

    // Size routines
    bool empty() const { return fSize == 0; }
    void clear() { fSize = 0; }
    int size() const { return fSize; }
    void resize(int newSize);
    size_t size_bytes() const { return this->bytes(fSize); }

    // Capacity routines
    int capacity() const { return fCapacity; }
    void reserve(int newCapacity);
    void shrink_to_fit();

    void* data() { return fStorage; }
    const void* data() const { return fStorage; }

    // Deletion routines
    void erase(int index, int count);
    // Removes the entry at 'index' and replaces it with the last array element
    void removeShuffle(int index);

    // Insertion routines
    void* prepend();

    void append();
    void append(int count);
    void* append(const void* src, int count);

    void* insert(int index);
    void* insert(int index, int count, const void* src);

    void pop_back() {
        SkASSERT(fSize > 0);
        fSize--;
    }

    friend bool operator==(const SkTDStorage& a, const SkTDStorage& b);
    friend bool operator!=(const SkTDStorage& a, const SkTDStorage& b) {
        return !(a == b);
    }

private:
    size_t bytes(int n) const { return SkToSizeT(n * fSizeOfT); }
    void* address(int n) { return fStorage + this->bytes(n); }

    // Adds delta to fSize. Crash if outside [0, INT_MAX]
    int calculateSizeOrDie(int delta);

    // Move the tail of the array defined by the indexes tailStart and tailEnd to dstIndex. The
    // elements at dstIndex are overwritten by the tail.
    void moveTail(int dstIndex, int tailStart, int tailEnd);

    // Copy src into the array at dstIndex.
    void copySrc(int dstIndex, const void* src, int count);

    const int fSizeOfT;
    std::byte* fStorage{nullptr};
    int fCapacity{0};  // size of the allocation in fArray (#elements)
    int fSize{0};    // logical number of elements (fSize <= fCapacity)
};

static inline void swap(SkTDStorage& a, SkTDStorage& b) {
    a.swap(b);
}

// SkTDArray<T> implements a std::vector-like array for raw data-only objects that do not require
// construction or destruction. The constructor and destructor for T will not be called; T objects
// will always be moved via raw memcpy. Newly created T objects will contain uninitialized memory.
template <typename T> class SkTDArray {
public:
    SkTDArray() : fStorage{sizeof(T)} {}
    SkTDArray(const T src[], int count) : fStorage{src, count, sizeof(T)} { }
    SkTDArray(const std::initializer_list<T>& list) : SkTDArray(list.begin(), list.size()) {}

    // Copy
    SkTDArray(const SkTDArray<T>& src) : SkTDArray(src.data(), src.size()) {}
    SkTDArray<T>& operator=(const SkTDArray<T>& src) {
        fStorage = src.fStorage;
        return *this;
    }

    // Move
    SkTDArray(SkTDArray<T>&& src) : fStorage{std::move(src.fStorage)} {}
    SkTDArray<T>& operator=(SkTDArray<T>&& src) {
        fStorage = std::move(src.fStorage);
        return *this;
    }

    friend bool operator==(const SkTDArray<T>& a, const SkTDArray<T>& b) {
        return a.fStorage == b.fStorage;
    }
    friend bool operator!=(const SkTDArray<T>& a, const SkTDArray<T>& b) { return !(a == b); }

    void swap(SkTDArray<T>& that) {
        using std::swap;
        swap(fStorage, that.fStorage);
    }

    bool empty() const { return fStorage.empty(); }

    // Return the number of elements in the array
    int size() const { return fStorage.size(); }

    // Return the total number of elements allocated.
    // Note: capacity() - size() gives you the number of elements you can add without causing an
    // allocation.
    int capacity() const { return fStorage.capacity(); }

    // return the number of bytes in the array: count * sizeof(T)
    size_t size_bytes() const { return fStorage.size_bytes(); }

    T*       data() { return static_cast<T*>(fStorage.data()); }
    const T* data() const { return static_cast<const T*>(fStorage.data()); }
    T*       begin() { return this->data(); }
    const T* begin() const { return this->data(); }
    T*       end() { return this->data() + this->size(); }
    const T* end() const { return this->data() + this->size(); }

    T& operator[](int index) {
        return this->data()[sk_collection_check_bounds(index, this->size())];
    }
    const T& operator[](int index) const {
        return this->data()[sk_collection_check_bounds(index, this->size())];
    }

    const T& back() const {
        sk_collection_not_empty(this->empty());
        return this->data()[this->size() - 1];
    }
    T& back() {
        sk_collection_not_empty(this->empty());
        return this->data()[this->size() - 1];
    }

    void reset() {
        fStorage.reset();
    }

    void clear() {
        fStorage.clear();
    }

     // Sets the number of elements in the array.
     // If the array does not have space for count elements, it will increase
     // the storage allocated to some amount greater than that required.
     // It will never shrink the storage.
    void resize(int count) {
        fStorage.resize(count);
    }

    void reserve(int n) {
        fStorage.reserve(n);
    }

    T* append() {
        fStorage.append();
        return this->end() - 1;
    }
    T* append(int count) {
        fStorage.append(count);
        return this->end() - count;
    }
    T* append(int count, const T* src) {
        return static_cast<T*>(fStorage.append(src, count));
    }

    T* insert(int index) {
        return static_cast<T*>(fStorage.insert(index));
    }
    T* insert(int index, int count, const T* src = nullptr) {
        return static_cast<T*>(fStorage.insert(index, count, src));
    }

    void remove(int index, int count = 1) {
        fStorage.erase(index, count);
    }

    void removeShuffle(int index) {
        fStorage.removeShuffle(index);
    }

    // routines to treat the array like a stack
    void push_back(const T& v) {
        this->append();
        this->back() = v;
    }
    void pop_back() { fStorage.pop_back(); }

    void shrink_to_fit() {
        fStorage.shrink_to_fit();
    }

private:
    SkTDStorage fStorage;
};

template <typename T> static inline void swap(SkTDArray<T>& a, SkTDArray<T>& b) { a.swap(b); }

struct SkRect;


// FIXME: move everything below into the SkPath class
/**
  *  The logical operations that can be performed when combining two paths.
  */
enum SkPathOp {
    kDifference_SkPathOp,         //!< subtract the op path from the first path
    kIntersect_SkPathOp,          //!< intersect the two paths
    kUnion_SkPathOp,              //!< union (inclusive-or) the two paths
    kXOR_SkPathOp,                //!< exclusive-or the two paths
    kReverseDifference_SkPathOp,  //!< subtract the first path from the op path
};

/** Set this path to the result of applying the Op to this path and the
    specified path: this = (this op operand).
    The resulting path will be constructed from non-overlapping contours.
    The curve order is reduced where possible so that cubics may be turned
    into quadratics, and quadratics maybe turned into lines.

    Returns true if operation was able to produce a result;
    otherwise, result is unmodified.

    @param one The first operand (for difference, the minuend)
    @param two The second operand (for difference, the subtrahend)
    @param op The operator to apply.
    @param result The product of the operands. The result may be one of the
                  inputs.
    @return True if the operation succeeded.
  */
bool SK_API Op(const SkPath& one, const SkPath& two, SkPathOp op, SkPath* result);

/** Set this path to a set of non-overlapping contours that describe the
    same area as the original path.
    The curve order is reduced where possible so that cubics may
    be turned into quadratics, and quadratics maybe turned into lines.

    Returns true if operation was able to produce a result;
    otherwise, result is unmodified.

    @param path The path to simplify.
    @param result The simplified path. The result may be the input.
    @return True if simplification succeeded.
  */
bool SK_API Simplify(const SkPath& path, SkPath* result);

/** Set the resulting rectangle to the tight bounds of the path.

    @param path The path measured.
    @param result The tight bounds of the path.
    @return True if the bounds could be computed.
  */
bool SK_API TightBounds(const SkPath& path, SkRect* result);

/** Set the result with fill type winding to area equivalent to path.
    Returns true if successful. Does not detect if path contains contours which
    contain self-crossings or cross other contours; in these cases, may return
    true even though result does not fill same area as path.

    Returns true if operation was able to produce a result;
    otherwise, result is unmodified. The result may be the input.

    @param path The path typically with fill type set to even odd.
    @param result The equivalent path with fill type set to winding.
    @return True if winding path was set.
  */
bool SK_API AsWinding(const SkPath& path, SkPath* result);

/** Perform a series of path operations, optimized for unioning many paths together.
  */
class SK_API SkOpBuilder {
public:
    /** Add one or more paths and their operand. The builder is empty before the first
        path is added, so the result of a single add is (emptyPath OP path).

        @param path The second operand.
        @param _operator The operator to apply to the existing and supplied paths.
     */
    void add(const SkPath& path, SkPathOp _operator);

    /** Computes the sum of all paths and operands, and resets the builder to its
        initial state.

        @param result The product of the operands.
        @return True if the operation succeeded.
      */
    bool resolve(SkPath* result);

private:
    skia_private::TArray<SkPath> fPathRefs;
    SkTDArray<SkPathOp> fOps;

    static bool FixWinding(SkPath* path);
    static void ReversePath(SkPath* path);
    void reset();
};

class SkString;

class SK_API SkParsePath {
public:
    static bool FromSVGString(const char str[], SkPath*);

    enum class PathEncoding { Absolute, Relative };
    static SkString ToSVGString(const SkPath&, PathEncoding = PathEncoding::Absolute);
};

#ifdef SKIA_SIMPLIFY_NAMESPACE
} // namespace SKIA_SIMPLIFY_NAMESPACE
#endif

#undef TArray
