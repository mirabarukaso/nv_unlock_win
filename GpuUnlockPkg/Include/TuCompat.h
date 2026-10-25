/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_COMPAT_H
#define TU_COMPAT_H

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define TURING_UNLOCK_UEFI 1
#define TURING_UNLOCK_U32_DEFINED 1

typedef UINT8  u8;
typedef UINT16 u16;
typedef UINT32 u32;
typedef UINT64 u64;
typedef BOOLEAN bool;

#ifndef true
#define true TRUE
#endif
#ifndef false
#define false FALSE
#endif

typedef UINTN size_t;

#define EINVAL      22
#define ENOENT       2
#define EIO          5
#define EAGAIN      11
#define ENOMEM      12
#define EBUSY       16
#define ENODEV      19
#define ERANGE      34
#define EBADMSG     74
#define EOVERFLOW   75
#define ETIMEDOUT  110
#define EOPNOTSUPP  95
#define EPROTO      71
#define EALREADY   114

struct firmware {
  UINTN       size;
  const UINT8 *data;
};

#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#define min_t(type, a, b) (((type)(a) < (type)(b)) ? (type)(a) : (type)(b))
#define lower_32_bits(n) ((u32)((UINT64)(n) & 0xffffffffu))
#define upper_32_bits(n) ((u32)(((UINT64)(n) >> 32) & 0xffffffffu))

#ifdef memcpy
#undef memcpy
#endif
#ifdef memset
#undef memset
#endif
#ifdef memcmp
#undef memcmp
#endif
#ifdef memchr
#undef memchr
#endif
#ifdef strlen
#undef strlen
#endif

#define memcpy(Dst, Src, Size)  CopyMem ((Dst), (Src), (Size))
#define memset(Dst, Val, Size)  SetMem ((Dst), (Size), (UINT8)(Val))
#define memcmp(A, B, Size)      CompareMem ((A), (B), (Size))
#define memchr(Buf, Val, Size)  ScanMem8 ((Buf), (Size), (UINT8)(Val))
#define strlen(Str)             AsciiStrLen ((CONST CHAR8 *)(Str))

VOID
TuLogInit (
  IN EFI_HANDLE  ImageHandle,
  IN BOOLEAN     Verbose
  );

VOID
TuLog (
  IN CONST CHAR8  *Prefix,
  IN CONST CHAR8  *Fmt,
  ...
  );

VOID
TuPrint (
  IN CONST CHAR16  *Fmt,
  ...
  );

#define GU_NOTE(fmt, ...)  TuLog ("turing_unlock: ", fmt, ##__VA_ARGS__)
#define GU_ERROR(fmt, ...) TuLog ("turing_unlock ERROR: ", fmt, ##__VA_ARGS__)

/* BaseLib has AsciiStriCmp / AsciiStrnCmp, not AsciiStrniCmp. */
static inline INTN
AsciiStrniCmp (
  IN CONST CHAR8  *FirstString,
  IN CONST CHAR8  *SecondString,
  IN UINTN        Length
  )
{
  CHAR8  First;
  CHAR8  Second;

  while (Length > 0) {
    First  = *FirstString;
    Second = *SecondString;
    if ((First >= 'a') && (First <= 'z')) {
      First = (CHAR8)(First - ('a' - 'A'));
    }
    if ((Second >= 'a') && (Second <= 'z')) {
      Second = (CHAR8)(Second - ('a' - 'A'));
    }
    if (First != Second) {
      return (INTN)First - (INTN)Second;
    }
    if (*FirstString == 0) {
      return 0;
    }
    FirstString++;
    SecondString++;
    Length--;
  }
  return 0;
}

#endif
