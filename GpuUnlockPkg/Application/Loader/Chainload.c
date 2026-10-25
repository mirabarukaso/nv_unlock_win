// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuChainload.h"
#include "TuConfig.h"
#include "TuFs.h"

#include <Library/DevicePathLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

/*
 * Windows Boot Manager and winload verify \EFI\Microsoft\Boot\bootmgfw.efi
 * (BCD path + self-image). That filename must be the signed Microsoft
 * binary when StartImage runs, otherwise recovery shows 0xc0000428.
 *
 * Boot cycle: wrapper runs from bootmgfw.efi -> restore signed original
 * onto that path -> LoadImage it -> Windows CI passes -> scheduled task
 * puts the wrapper back after Windows is up.
 */
STATIC CONST CHAR16 *mCandidates[] = {
  TU_MS_BOOT_OFFICIAL,
  TU_MS_BOOT_BACKUP_ALT,
  TU_MS_BOOT_BACKUP,
  L"\\EFI\\turing-unlock\\backup\\bootmgfw.efi",
  TU_FALLBACK_BOOT
};

STATIC CONST CHAR16 *mSignedSources[] = {
  TU_MS_BOOT_BACKUP_ALT,
  TU_MS_BOOT_BACKUP,
  L"\\EFI\\turing-unlock\\backup\\bootmgfw.efi"
};

typedef struct {
  BOOLEAN       UseBuffer;
  BOOLEAN       BootPolicy;
  CONST CHAR16  *Name;
} TU_LOAD_TRY;

STATIC CONST TU_LOAD_TRY mLoadTries[] = {
  { FALSE, FALSE, L"LoadImage(path)" },
  { FALSE, TRUE,  L"LoadImage(path, boot)" },
  { TRUE,  FALSE, L"LoadImage(buffer)" },
  { TRUE,  TRUE,  L"LoadImage(buffer, boot)" }
};

/* Wrapper is ~70KB; Microsoft bootmgfw is typically >1MB. */
#define TU_MIN_SIGNED_BOOTMGR  0x20000u

STATIC
VOID
PrintStatus (
  IN CONST CHAR16  *What,
  IN EFI_STATUS    Status
  )
{
  TuPrint (L"  %s: %r (0x%lx)\r\n", What, Status, (UINT64)Status);
}

STATIC
VOID
PrintDp (
  IN CONST CHAR16              *Tag,
  IN EFI_DEVICE_PATH_PROTOCOL  *Dp
  )
{
  CHAR16  *Text;

  if (Dp == NULL) {
    TuPrint (L"  %s: <null>\r\n", Tag);
    return;
  }
  Text = ConvertDevicePathToText (Dp, FALSE, TRUE);
  if (Text != NULL) {
    TuPrint (L"  %s: %s\r\n", Tag, Text);
    FreePool (Text);
  }
}

STATIC
EFI_DEVICE_PATH_PROTOCOL *
StripFileNodes (
  IN EFI_DEVICE_PATH_PROTOCOL  *Src
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_DEVICE_PATH_PROTOCOL  *KeepEnd;
  EFI_DEVICE_PATH_PROTOCOL  *Copy;
  UINTN                     PrefixSize;

  if (Src == NULL) {
    return NULL;
  }

  KeepEnd = Src;
  for (Node = Src; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) != MEDIA_DEVICE_PATH) ||
        (DevicePathSubType (Node) != MEDIA_FILEPATH_DP))
    {
      KeepEnd = NextDevicePathNode (Node);
    }
  }

  PrefixSize = (UINTN)KeepEnd - (UINTN)Src;
  Copy       = AllocatePool (PrefixSize + END_DEVICE_PATH_LENGTH);
  if (Copy == NULL) {
    return NULL;
  }
  CopyMem (Copy, Src, PrefixSize);
  SetDevicePathEndNode ((EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Copy + PrefixSize));
  return Copy;
}

STATIC
EFI_DEVICE_PATH_PROTOCOL *
BuildLoadPath (
  IN EFI_HANDLE                Device,
  IN CONST CHAR16              *Path,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Prefix;
  EFI_DEVICE_PATH_PROTOCOL  *FileNode;
  EFI_DEVICE_PATH_PROTOCOL  *Result;

  FileNode = FileDevicePath (NULL, Path);
  Prefix   = StripFileNodes (SelfPath);
  /* LoadedImage->FilePath is often file-only. Need a hardware/media prefix. */
  if ((Prefix != NULL) && !IsDevicePathEnd (Prefix) && (FileNode != NULL)) {
    Result = AppendDevicePath (Prefix, FileNode);
    FreePool (Prefix);
    FreePool (FileNode);
    if (Result != NULL) {
      return Result;
    }
  } else {
    if (Prefix != NULL) {
      FreePool (Prefix);
    }
    if (FileNode != NULL) {
      FreePool (FileNode);
    }
  }

  Result = FileDevicePath (Device, Path);
  if (Result != NULL) {
    return Result;
  }
  return FileDevicePath (NULL, Path);
}

STATIC
EFI_STATUS
InspectFile (
  IN EFI_HANDLE    Device,
  IN CONST CHAR16  *Path,
  OUT UINTN        *Size OPTIONAL,
  OUT BOOLEAN      *IsWrapper OPTIONAL
  )
{
  VOID        *Buffer;
  UINTN       FileSize;
  EFI_STATUS  Status;

  Buffer   = NULL;
  FileSize = 0;
  Status   = TuFsReadDeviceFile (Device, Path, &Buffer, &FileSize);
  if (Size != NULL) {
    *Size = FileSize;
  }
  if (EFI_ERROR (Status)) {
    if (IsWrapper != NULL) {
      *IsWrapper = FALSE;
    }
    return Status;
  }
  if (IsWrapper != NULL) {
    *IsWrapper = TuFsBufferHasMagic (Buffer, FileSize);
  }
  FreePool (Buffer);
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
BufferLooksSigned (
  IN CONST VOID  *Buffer,
  IN UINTN       Size
  )
{
  if ((Buffer == NULL) || (Size < TU_MIN_SIGNED_BOOTMGR)) {
    return FALSE;
  }
  return !TuFsBufferHasMagic (Buffer, Size);
}

STATIC
EFI_STATUS
ReadSignedBootmgr (
  IN EFI_HANDLE     Device,
  OUT VOID          **Buffer,
  OUT UINTN         *Size,
  OUT CONST CHAR16  **FromPath
  )
{
  EFI_STATUS  Status;
  UINTN       Index;

  *Buffer   = NULL;
  *Size     = 0;
  *FromPath = NULL;

  for (Index = 0; Index < ARRAY_SIZE (mSignedSources); Index++) {
    Status = TuFsReadDeviceFile (Device, mSignedSources[Index], Buffer, Size);
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (!BufferLooksSigned (*Buffer, *Size)) {
      TuPrint (
        L"  skip signed source %s (%u bytes, wrapper or too small)\r\n",
        mSignedSources[Index],
        *Size
        );
      FreePool (*Buffer);
      *Buffer = NULL;
      *Size   = 0;
      continue;
    }
    *FromPath = mSignedSources[Index];
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
RestoreOfficialBootmgr (
  IN EFI_HANDLE  Device
  )
{
  EFI_STATUS    Status;
  VOID          *Buffer;
  UINTN         Size;
  CONST CHAR16  *From;
  UINTN         OfficialSize;
  BOOLEAN       IsWrapper;

  OfficialSize = 0;
  IsWrapper    = TRUE;
  Status       = InspectFile (Device, TU_MS_BOOT_OFFICIAL, &OfficialSize, &IsWrapper);
  if (!EFI_ERROR (Status) && !IsWrapper) {
    TuPrint (L"official bootmgfw.efi is already signed (%u bytes)\r\n", OfficialSize);
    return EFI_SUCCESS;
  }

  Status = ReadSignedBootmgr (Device, &Buffer, &Size, &From);
  if (EFI_ERROR (Status)) {
    TuPrint (L"no signed bootmgfw backup on this volume\r\n");
    return Status;
  }

  TuPrint (L"restoring signed bootmgfw.efi from %s (%u bytes)\r\n", From, Size);
  Status = TuFsWriteDeviceFile (Device, TU_MS_BOOT_OFFICIAL, Buffer, Size, FALSE);
  if (EFI_ERROR (Status)) {
    PrintStatus (L"restore write", Status);
    FreePool (Buffer);
    return Status;
  }

  IsWrapper = FALSE;
  Status    = InspectFile (Device, TU_FALLBACK_BOOT, NULL, &IsWrapper);
  if (!EFI_ERROR (Status) && IsWrapper) {
    Status = TuFsWriteDeviceFile (Device, TU_FALLBACK_BOOT, Buffer, Size, FALSE);
    if (EFI_ERROR (Status)) {
      PrintStatus (L"restore bootx64", Status);
    } else {
      TuPrint (L"also restored bootx64.efi\r\n");
    }
  }

  FreePool (Buffer);

  OfficialSize = 0;
  IsWrapper    = TRUE;
  Status       = InspectFile (Device, TU_MS_BOOT_OFFICIAL, &OfficialSize, &IsWrapper);
  if (EFI_ERROR (Status) || IsWrapper) {
    TuPrint (L"restore verify failed: official path still looks like the wrapper\r\n");
    return EFI_LOAD_ERROR;
  }
  TuPrint (L"restore ok: %s (%u bytes)\r\n", TU_MS_BOOT_OFFICIAL, OfficialSize);
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
CandidateIsWrapper (
  IN EFI_HANDLE    Device,
  IN CONST CHAR16  *Path
  )
{
  VOID        *Buffer;
  UINTN       Size;
  BOOLEAN     Has;
  EFI_STATUS  Status;

  Buffer = NULL;
  Size   = 0;
  Status = TuFsReadDeviceFile (Device, Path, &Buffer, &Size);
  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_FOUND) {
      TuPrint (L"candidate %s ", Path);
      PrintStatus (L"read", Status);
    }
    return TRUE;
  }
  Has = TuFsBufferHasMagic (Buffer, Size);
  TuPrint (L"candidate %s (%u bytes)%s\r\n", Path, Size, Has ? L" [wrapper]" : L"");
  FreePool (Buffer);
  if (Has) {
    TuPrint (L"  skip: this file is the wrapper itself\r\n");
  }
  return Has;
}

STATIC
EFI_STATUS
LoadFromPath (
  IN EFI_HANDLE                Parent,
  IN EFI_DEVICE_PATH_PROTOCOL  *Dp,
  IN BOOLEAN                   BootPolicy,
  OUT EFI_HANDLE               *Image
  )
{
  *Image = NULL;
  if (Dp == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  return gBS->LoadImage (BootPolicy, Parent, Dp, NULL, 0, Image);
}

STATIC
EFI_STATUS
LoadFromBuffer (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                Device,
  IN CONST CHAR16              *Path,
  IN EFI_DEVICE_PATH_PROTOCOL  *Dp,
  IN BOOLEAN                   BootPolicy,
  OUT EFI_HANDLE               *Image
  )
{
  EFI_STATUS  Status;
  VOID        *Buffer;
  UINTN       Size;

  *Image = NULL;
  Buffer = NULL;
  Size   = 0;
  Status = TuFsReadDeviceFile (Device, Path, &Buffer, &Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->LoadImage (BootPolicy, Parent, Dp, Buffer, Size, Image);
  FreePool (Buffer);
  return Status;
}

STATIC
VOID
FixupWindowsImage (
  IN EFI_HANDLE  Image,
  IN EFI_HANDLE  Device
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Child;

  Status = gBS->HandleProtocol (Image, &gEfiLoadedImageProtocolGuid, (VOID **)&Child);
  if (EFI_ERROR (Status)) {
    return;
  }
  if (Child->DeviceHandle == NULL) {
    Child->DeviceHandle = Device;
  }
  PrintDp (L"child FilePath", Child->FilePath);
  TuPrint (L"  child DeviceHandle %p\r\n", Child->DeviceHandle);
}

STATIC
EFI_STATUS
StartWindowsImage (
  IN EFI_HANDLE  Image
  )
{
  EFI_STATUS  Status;
  CHAR16      *ExitData;
  UINTN       ExitSize;

  TuPrint (L"Starting Windows Boot Manager...\r\n");
  ExitData = NULL;
  ExitSize = 0;
  Status   = gBS->StartImage (Image, &ExitSize, &ExitData);
  PrintStatus (L"StartImage returned", Status);
  return Status;
}

STATIC
EFI_STATUS
TryCandidate (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                Device,
  IN CONST CHAR16              *Path,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                Image;
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  UINTN                     Index;

  if (CandidateIsWrapper (Device, Path)) {
    return EFI_NOT_FOUND;
  }

  Dp = BuildLoadPath (Device, Path, SelfPath);
  PrintDp (L"device path", Dp);

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < ARRAY_SIZE (mLoadTries); Index++) {
    if (mLoadTries[Index].UseBuffer) {
      Status = LoadFromBuffer (
                 Parent,
                 Device,
                 Path,
                 Dp,
                 mLoadTries[Index].BootPolicy,
                 &Image
                 );
    } else {
      Status = LoadFromPath (Parent, Dp, mLoadTries[Index].BootPolicy, &Image);
    }
    if (EFI_ERROR (Status)) {
      PrintStatus (mLoadTries[Index].Name, Status);
      continue;
    }

    FixupWindowsImage (Image, Device);
    Status = StartWindowsImage (Image);
    if (EFI_ERROR (Status)) {
      gBS->UnloadImage (Image);
      continue;
    }
    if (Dp != NULL) {
      FreePool (Dp);
    }
    return Status;
  }

  if (Dp != NULL) {
    FreePool (Dp);
  }
  return Status;
}

STATIC
EFI_STATUS
TryDevice (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                Device,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  Last;
  UINTN       Index;

  Status = RestoreOfficialBootmgr (Device);
  if (EFI_ERROR (Status)) {
    TuPrint (L"WARNING: could not put signed bootmgfw.efi back on the BCD path.\r\n");
    TuPrint (L"Windows may show 0xc0000428 (unsigned wrapper still named bootmgfw.efi).\r\n");
  }

  Last = EFI_NOT_FOUND;
  for (Index = 0; Index < ARRAY_SIZE (mCandidates); Index++) {
    Status = TryCandidate (Parent, Device, mCandidates[Index], SelfPath);
    if (!EFI_ERROR (Status)) {
      return Status;
    }
    if (Status != EFI_NOT_FOUND) {
      Last = Status;
    }
  }
  return Last;
}

EFI_STATUS
TuChainloadWindows (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  EFI_DEVICE_PATH_PROTOCOL   *SelfPath;
  EFI_HANDLE                 *Handles;
  UINTN                      HandleCount;
  UINTN                      Index;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) {
    PrintStatus (L"LoadedImage", Status);
    return Status;
  }

  SelfPath = NULL;
  (VOID)gBS->HandleProtocol (
               ImageHandle,
               &gEfiLoadedImageDevicePathProtocolGuid,
               (VOID **)&SelfPath
               );
  if (SelfPath == NULL) {
    SelfPath = Loaded->FilePath;
  }
  PrintDp (L"self path", SelfPath);
  TuPrint (L"chainload from device %p\r\n", Loaded->DeviceHandle);

  Status = TryDevice (ImageHandle, Loaded->DeviceHandle, SelfPath);
  if (!EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    PrintStatus (L"LocateHandleBuffer(SFS)", Status);
    goto fail;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    if (Handles[Index] == Loaded->DeviceHandle) {
      continue;
    }
    TuPrint (L"chainload try other FS handle %p\r\n", Handles[Index]);
    Status = TryDevice (ImageHandle, Handles[Index], SelfPath);
    if (!EFI_ERROR (Status)) {
      FreePool (Handles);
      return Status;
    }
  }
  FreePool (Handles);

fail:
  TuPrint (L"chainload FAILED: could not LoadImage/StartImage Windows Boot Manager.\r\n");
  TuPrint (L"From recovery: copy EFI\\turing-unlock\\bootmgfw_ms.efi over EFI\\Microsoft\\Boot\\bootmgfw.efi\r\n");
  return EFI_NOT_FOUND;
}
