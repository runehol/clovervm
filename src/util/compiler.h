#ifndef CL_UTIL_COMPILER_H
#define CL_UTIL_COMPILER_H

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#ifndef ALWAYSINLINE
#define ALWAYSINLINE inline __attribute__((always_inline))
#endif

#ifndef INLINE
#define INLINE inline
#endif

#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif

#ifndef MUSTTAIL
#define MUSTTAIL __attribute__((musttail))
#endif

#ifndef PRESERVE_NONE
#if (defined(__clang__) && __has_attribute(preserve_none)) ||                  \
    (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 16)
#define PRESERVE_NONE __attribute__((preserve_none))
#else
#define PRESERVE_NONE
#endif
#endif

#endif  // CL_UTIL_COMPILER_H
