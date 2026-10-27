// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuConfig.h"
#include "TuFs.h"

#define TU_LOG_MAX  (64u * 1024u)

STATIC EFI_HANDLE  mLogImage;
STATIC BOOLEAN     mLogVerbose = TRUE;
STATIC CHAR8       *mLogBuf;
STATIC UINTN       mLogLen;
STATIC UINTN       mLogCap;

STATIC
BOOLEAN
TuLogReserve (
  IN UINTN  Need
  )
{
  UINTN  NewCap;
  CHAR8  *NewBuf;

  if (Need <= mLogCap) {
    return TRUE;
  }
  NewCap = (mLogCap == 0) ? 8192 : mLogCap;
  while (NewCap < Need) {
    if (NewCap >= TU_LOG_MAX) {
      return FALSE;
    }
    if (NewCap > (TU_LOG_MAX / 2)) {
      NewCap = TU_LOG_MAX;
    } else {
      NewCap *= 2;
    }
  }
  NewBuf = ReallocatePool (mLogCap, NewCap, mLogBuf);
  if (NewBuf == NULL) {
    return FALSE;
  }
  mLogBuf = NewBuf;
  mLogCap = NewCap;
  return TRUE;
}

STATIC
VOID
TuLogAppend (
  IN CONST CHAR8  *Text
  )
{
  UINTN  Add;

  if (Text == NULL) {
    return;
  }
  Add = AsciiStrLen (Text);
  if (Add == 0) {
    return;
  }
  if (!TuLogReserve (mLogLen + Add + 1)) {
    return;
  }
  CopyMem (mLogBuf + mLogLen, Text, Add);
  mLogLen += Add;
  mLogBuf[mLogLen] = 0;
}

STATIC
VOID
TuEmitAscii (
  IN CONST CHAR8  *Text
  )
{
  CHAR16  Wide[512];
  UINTN   Index;
  UINTN   Out;

  if (Text == NULL) {
    return;
  }
  Index = 0;
  Out   = 0;
  while (Text[Index] != 0 && Out + 1 < ARRAY_SIZE (Wide)) {
    if (Text[Index] == '\n' && (Out == 0 || Wide[Out - 1] != L'\r')) {
      Wide[Out++] = L'\r';
      if (Out + 1 >= ARRAY_SIZE (Wide)) {
        break;
      }
    }
    Wide[Out++] = (CHAR16)(UINT8)Text[Index++];
  }
  Wide[Out] = 0;
  if (mLogVerbose && gST != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, Wide);
  }
  TuLogAppend (Text);
}

STATIC
VOID
TuAppendChar (
  IN OUT CHAR8  *Buf,
  IN OUT UINTN  *Used,
  IN UINTN      Cap,
  IN CHAR8      Ch
  )
{
  if (*Used + 1 < Cap) {
    Buf[(*Used)++] = Ch;
  }
}

STATIC
VOID
TuAppendStr (
  IN OUT CHAR8       *Buf,
  IN OUT UINTN       *Used,
  IN UINTN           Cap,
  IN CONST CHAR8     *Str
  )
{
  if (Str == NULL) {
    Str = "(null)";
  }
  while (*Str != 0) {
    TuAppendChar (Buf, Used, Cap, *Str++);
  }
}

STATIC
VOID
TuAppendUnsigned (
  IN OUT CHAR8  *Buf,
  IN OUT UINTN  *Used,
  IN UINTN      Cap,
  IN UINT64     Value,
  IN UINTN      Base,
  IN BOOLEAN    Upper,
  IN BOOLEAN    Hash,
  IN UINTN      Width,
  IN CHAR8      Pad
  )
{
  CHAR8  Tmp[32];
  UINTN  N;
  CONST CHAR8 *Digits;

  Digits = Upper ? "0123456789ABCDEF" : "0123456789abcdef";
  N      = 0;
  if (Value == 0) {
    Tmp[N++] = '0';
  } else {
    while (Value != 0 && N < ARRAY_SIZE (Tmp)) {
      Tmp[N++] = Digits[Value % Base];
      Value   /= Base;
    }
  }
  if (Hash && Base == 16) {
    TuAppendChar (Buf, Used, Cap, '0');
    TuAppendChar (Buf, Used, Cap, Upper ? 'X' : 'x');
  }
  while (Width > N) {
    TuAppendChar (Buf, Used, Cap, Pad);
    Width--;
  }
  while (N > 0) {
    TuAppendChar (Buf, Used, Cap, Tmp[--N]);
  }
}

STATIC
VOID
TuFormat (
  OUT CHAR8        *Buf,
  IN UINTN         Cap,
  IN CONST CHAR8   *Fmt,
  IN VA_LIST       Args
  )
{
  UINTN Used;

  Used = 0;
  if (Cap == 0) {
    return;
  }
  while (Fmt != NULL && *Fmt != 0) {
    if (*Fmt != '%') {
      TuAppendChar (Buf, &Used, Cap, *Fmt++);
      continue;
    }
    Fmt++;
    {
      BOOLEAN Hash  = FALSE;
      BOOLEAN Long  = FALSE;
      BOOLEAN LLong = FALSE;
      BOOLEAN SizeT = FALSE;
      CHAR8   Pad   = ' ';
      UINTN   Width = 0;
      UINT64  Num;
      INT64   Signed;

      while (*Fmt == '#' || *Fmt == '0' || *Fmt == '-' || *Fmt == ' ') {
        if (*Fmt == '#') {
          Hash = TRUE;
        }
        if (*Fmt == '0') {
          Pad = '0';
        }
        Fmt++;
      }
      while (*Fmt >= '0' && *Fmt <= '9') {
        Width = Width * 10 + (UINTN)(*Fmt - '0');
        Fmt++;
      }
      if (*Fmt == 'l') {
        Fmt++;
        if (*Fmt == 'l') {
          LLong = TRUE;
          Fmt++;
        } else {
          Long = TRUE;
        }
      } else if (*Fmt == 'z') {
        SizeT = TRUE;
        Fmt++;
      }
      switch (*Fmt) {
      case '%':
        TuAppendChar (Buf, &Used, Cap, '%');
        break;
      case 's':
      case 'a':
        TuAppendStr (Buf, &Used, Cap, VA_ARG (Args, CHAR8 *));
        break;
      case 'c':
        TuAppendChar (Buf, &Used, Cap, (CHAR8)VA_ARG (Args, UINTN));
        break;
      case 'p':
        Hash = TRUE;
        TuAppendUnsigned (Buf, &Used, Cap, (UINT64)(UINTN)VA_ARG (Args, VOID *), 16, FALSE, TRUE, Width, Pad);
        break;
      case 'd':
      case 'i':
        if (LLong) {
          Signed = VA_ARG (Args, INT64);
        } else if (Long || SizeT) {
          Signed = (INT64)VA_ARG (Args, INTN);
        } else {
          Signed = VA_ARG (Args, int);
        }
        if (Signed < 0) {
          TuAppendChar (Buf, &Used, Cap, '-');
          Num = (UINT64)(-Signed);
        } else {
          Num = (UINT64)Signed;
        }
        TuAppendUnsigned (Buf, &Used, Cap, Num, 10, FALSE, FALSE, Width, Pad);
        break;
      case 'u':
      case 'x':
      case 'X':
        if (LLong) {
          Num = VA_ARG (Args, UINT64);
        } else if (Long || SizeT) {
          Num = (UINT64)VA_ARG (Args, UINTN);
        } else {
          Num = VA_ARG (Args, UINT32);
        }
        TuAppendUnsigned (
          Buf,
          &Used,
          Cap,
          Num,
          (*Fmt == 'u') ? 10 : 16,
          (*Fmt == 'X'),
          Hash,
          Width,
          Pad
          );
        break;
      default:
        TuAppendChar (Buf, &Used, Cap, '%');
        if (*Fmt != 0) {
          TuAppendChar (Buf, &Used, Cap, *Fmt);
        }
        break;
      }
      if (*Fmt != 0) {
        Fmt++;
      }
    }
  }
  Buf[Used < Cap ? Used : Cap - 1] = 0;
}

VOID
TuLogInit (
  IN EFI_HANDLE  ImageHandle,
  IN BOOLEAN     Verbose
  )
{
  mLogImage   = ImageHandle;
  mLogVerbose = Verbose;
  mLogLen     = 0;
  TuLogAppend ("---- turing-unlock loader ----\r\n");
}

VOID
TuLogFlush (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mLogImage == NULL || mLogBuf == NULL || mLogLen == 0) {
    return;
  }
  Status = TuFsWriteFile (mLogImage, TU_LOG_NAME, mLogBuf, mLogLen, FALSE);
  if (EFI_ERROR (Status)) {
    if (gST != NULL && gST->ConOut != NULL) {
      gST->ConOut->OutputString (gST->ConOut, L"unlock.log write failed\r\n");
    }
    return;
  }
  /* Keep the buffer. A later flush rewrites the whole file, so a boot
   * choice recorded after the first flush is still in unlock.log.
   */
}

VOID
TuLog (
  IN CONST CHAR8  *Prefix,
  IN CONST CHAR8  *Fmt,
  ...
  )
{
  CHAR8   Line[768];
  UINTN   Used;
  VA_LIST Args;

  Used = 0;
  if (Prefix != NULL) {
    TuAppendStr (Line, &Used, sizeof (Line), Prefix);
  }
  Line[Used] = 0;
  VA_START (Args, Fmt);
  TuFormat (Line + Used, sizeof (Line) - Used, Fmt, Args);
  VA_END (Args);
  TuEmitAscii (Line);
}

VOID
TuPrint (
  IN CONST CHAR16  *Fmt,
  ...
  )
{
  VA_LIST  Args;
  CHAR16   Line[512];

  VA_START (Args, Fmt);
  UnicodeVSPrint (Line, sizeof (Line), Fmt, Args);
  VA_END (Args);
  if (gST != NULL && gST->ConOut != NULL) {
    gST->ConOut->OutputString (gST->ConOut, Line);
  }
}
