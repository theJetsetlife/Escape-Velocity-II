/* types.h (host tests only) - the u8..s64 names of PS4 Payload SDK libPS4/include/types.h, for a
 * host libc (the SDK header redefines wchar_t/size_t/bool, which clashes with glibc). */
#ifndef PRB_HOST_TYPES_H
#define PRB_HOST_TYPES_H
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
#endif
