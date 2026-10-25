// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuConfig.h"
#include "TuFs.h"

#include <Protocol/LoadedImage.h>

VOID
TuConfigInitDefaults (
  OUT TU_CONFIG  *Config
  )
{
  ZeroMem (Config, sizeof (*Config));
  Config->DefaultAction = TuActionSkip;
  Config->TimeoutSec    = 20;
  Config->Verbose       = TRUE;
  AsciiStrCpyS (Config->Targets, sizeof (Config->Targets), "all");
}

STATIC
VOID
TrimAscii (
  IN OUT CHAR8  *Line
  )
{
  UINTN Start;
  UINTN End;
  UINTN Len;

  Len = AsciiStrLen (Line);
  Start = 0;
  while (Line[Start] == ' ' || Line[Start] == '\t' || Line[Start] == '\r') {
    Start++;
  }
  End = Len;
  while (End > Start && (Line[End - 1] == ' ' || Line[End - 1] == '\t' ||
                         Line[End - 1] == '\r' || Line[End - 1] == '\n'))
  {
    End--;
  }
  Line[End] = 0;
  if (Start > 0) {
    CopyMem (Line, Line + Start, End - Start + 1);
  }
}

STATIC
VOID
ApplyKey (
  IN OUT TU_CONFIG  *Config,
  IN CONST CHAR8    *Key,
  IN CONST CHAR8    *Value
  )
{
  if (AsciiStriCmp (Key, "default") == 0) {
    if (AsciiStriCmp (Value, "inject") == 0) {
      Config->DefaultAction = TuActionInject;
    } else {
      Config->DefaultAction = TuActionSkip;
    }
  } else if (AsciiStriCmp (Key, "timeout_sec") == 0) {
    Config->TimeoutSec = AsciiStrDecimalToUintn (Value);
    if (Config->TimeoutSec > 60) {
      Config->TimeoutSec = 60;
    }
  } else if (AsciiStriCmp (Key, "verbose") == 0) {
    Config->Verbose = (Value[0] != '0');
  } else if (AsciiStriCmp (Key, "targets") == 0) {
    AsciiStrCpyS (Config->Targets, sizeof (Config->Targets), Value);
  }
}

EFI_STATUS
TuConfigLoad (
  IN EFI_HANDLE     ImageHandle,
  IN OUT TU_CONFIG  *Config
  )
{
  EFI_STATUS  Status;
  VOID        *Buffer;
  UINTN       Size;
  CHAR8       *Text;
  UINTN       Index;
  CHAR8       Line[160];
  UINTN       LineLen;
  CHAR8       *Eq;

  Status = TuFsReadFile (ImageHandle, TU_CONFIG_NAME, &Buffer, &Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Text = Buffer;
  LineLen = 0;
  for (Index = 0; Index <= Size; Index++) {
    CHAR8 Ch = (Index < Size) ? Text[Index] : '\n';
    if (Ch == '\n' || LineLen + 1 >= sizeof (Line)) {
      Line[LineLen] = 0;
      TrimAscii (Line);
      if (Line[0] != 0 && Line[0] != '#' && Line[0] != ';') {
        Eq = Line;
        while (*Eq != 0 && *Eq != '=') {
          Eq++;
        }
        if (*Eq == '=') {
          *Eq = 0;
          TrimAscii (Line);
          TrimAscii (Eq + 1);
          ApplyKey (Config, Line, Eq + 1);
        }
      }
      LineLen = 0;
    } else if (Ch != '\r') {
      Line[LineLen++] = Ch;
    }
  }
  FreePool (Buffer);
  return EFI_SUCCESS;
}

EFI_STATUS
TuConfigParseLoadOptions (
  IN EFI_HANDLE     ImageHandle,
  IN OUT TU_CONFIG  *Config
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  CHAR16                     *Opts;
  UINTN                      OptChars;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status) || Loaded->LoadOptions == NULL || Loaded->LoadOptionsSize == 0) {
    return EFI_SUCCESS;
  }
  Opts     = Loaded->LoadOptions;
  OptChars = Loaded->LoadOptionsSize / sizeof (CHAR16);
  if (OptChars == 0) {
    return EFI_SUCCESS;
  }
  if (StrStr (Opts, L"inject") != NULL) {
    Config->ForceInject = TRUE;
  }
  if (StrStr (Opts, L"skip") != NULL) {
    Config->ForceSkip = TRUE;
  }
  if (StrStr (Opts, L"noload") != NULL || StrStr (Opts, L"--noload") != NULL) {
    Config->NoChainload = TRUE;
  }
  if (StrStr (Opts, L"emergency") != NULL) {
    Config->ForceSkip = TRUE;
  }
  return EFI_SUCCESS;
}
