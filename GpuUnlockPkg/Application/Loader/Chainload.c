// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuChainload.h"
#include "TuConfig.h"
#include "TuFs.h"
#include "TuPci.h"

#include <Guid/FileInfo.h>
#include <IndustryStandard/PeImage.h>
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
 * ESP boot: wrapper runs from bootmgfw.efi -> restore signed original
 * onto that path -> LoadImage it -> Windows CI passes -> scheduled task
 * puts the wrapper back after Windows is up. This path does not scan
 * other disks.
 *
 * USB boot: the stick has the usb-boot marker and no signed backup.
 * Nothing on the stick or on an internal disk is rewritten. NVMe and
 * SATA controllers are connected, FAT volumes other than the loader's
 * own volume are scanned, and the user picks an EFI loader. StartImage
 * uses that volume's device path.
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

/* TOTORONG AD103 handoff: LoadImage before disconnect; StartImage after reconnect. */
STATIC EFI_HANDLE  mPreloadedImage  = NULL;
STATIC EFI_HANDLE  mPreloadedDevice = NULL;

STATIC
VOID
PrintStatus (
  IN CONST CHAR16  *What,
  IN EFI_STATUS    Status
  )
{
  TuLogWide (L"  %s: %r (0x%lx)\r\n", What, Status, (UINT64)Status);
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
    TuLogWide (L"  %s: <null>\r\n", Tag);
    return;
  }
  Text = ConvertDevicePathToText (Dp, FALSE, TRUE);
  if (Text != NULL) {
    TuLogWide (L"  %s: %s\r\n", Tag, Text);
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
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath,
  IN BOOLEAN                   UseLoaderPrefix
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Prefix;
  EFI_DEVICE_PATH_PROTOCOL  *FileNode;
  EFI_DEVICE_PATH_PROTOCOL  *Result;

  /* Another volume must keep its own device path. The loader prefix
   * points at the USB (or wherever this image started) and would make
   * Windows look for the BCD there.
   */
  if (!UseLoaderPrefix) {
    Result = FileDevicePath (Device, Path);
    if (Result != NULL) {
      return Result;
    }
    return FileDevicePath (NULL, Path);
  }

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
      TuLogWide (
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
    TuLogWide (L"official bootmgfw.efi is already signed (%u bytes)\r\n", OfficialSize);
    return EFI_SUCCESS;
  }

  Status = ReadSignedBootmgr (Device, &Buffer, &Size, &From);
  if (EFI_ERROR (Status)) {
    TuLogWide (L"no signed bootmgfw backup on this volume\r\n");
    return Status;
  }

  TuLogWide (L"restoring signed bootmgfw.efi from %s (%u bytes)\r\n", From, Size);
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
      TuLogWide (L"also restored bootx64.efi\r\n");
    }
  }

  FreePool (Buffer);

  OfficialSize = 0;
  IsWrapper    = TRUE;
  Status       = InspectFile (Device, TU_MS_BOOT_OFFICIAL, &OfficialSize, &IsWrapper);
  if (EFI_ERROR (Status) || IsWrapper) {
    TuShow (L"restore verify failed: official path still looks like the wrapper\r\n");
    return EFI_LOAD_ERROR;
  }
  TuLogWide (L"restore ok: %s (%u bytes)\r\n", TU_MS_BOOT_OFFICIAL, OfficialSize);
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
      TuLogWide (L"candidate %s ", Path);
      PrintStatus (L"read", Status);
    }
    return TRUE;
  }
  Has = TuFsBufferHasMagic (Buffer, Size);
  TuLogWide (L"candidate %s (%u bytes)%s\r\n", Path, Size, Has ? L" [wrapper]" : L"");
  FreePool (Buffer);
  if (Has) {
    TuLogWide (L"  skip: this file is the wrapper itself\r\n");
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
  TuLogWide (L"  child DeviceHandle %p\r\n", Child->DeviceHandle);
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

  TuShow (L"Starting EFI loader...\r\n");
  ExitData = NULL;
  ExitSize = 0;
  Status   = gBS->StartImage (Image, &ExitSize, &ExitData);
  PrintStatus (L"StartImage returned", Status);
  return Status;
}

STATIC
EFI_STATUS
LoadCandidateOnly (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                Device,
  IN CONST CHAR16              *Path,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath,
  IN BOOLEAN                   UseLoaderPrefix,
  OUT EFI_HANDLE               *OutImage
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                Image;
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  UINTN                     Index;

  *OutImage = NULL;
  if (CandidateIsWrapper (Device, Path)) {
    return EFI_NOT_FOUND;
  }

  Dp = BuildLoadPath (Device, Path, SelfPath, UseLoaderPrefix);
  PrintDp (L"preload path", Dp);

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
    if (Dp != NULL) {
      FreePool (Dp);
    }
    *OutImage = Image;
    TuLogWide (L"preloaded %s via %s\r\n", Path, mLoadTries[Index].Name);
    return EFI_SUCCESS;
  }

  if (Dp != NULL) {
    FreePool (Dp);
  }
  return Status;
}

STATIC
EFI_STATUS
TryCandidate (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                Device,
  IN CONST CHAR16              *Path,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath,
  IN BOOLEAN                   UseLoaderPrefix
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                Image;
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  UINTN                     Index;

  if (CandidateIsWrapper (Device, Path)) {
    return EFI_NOT_FOUND;
  }

  Dp = BuildLoadPath (Device, Path, SelfPath, UseLoaderPrefix);
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
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath,
  IN BOOLEAN                   UseLoaderPrefix
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  Last;
  UINTN       Index;
  UINTN       OfficialSize;
  BOOLEAN     IsWrapper;

  OfficialSize = 0;
  IsWrapper    = FALSE;
  Status       = InspectFile (Device, TU_MS_BOOT_OFFICIAL, &OfficialSize, &IsWrapper);
  if (!EFI_ERROR (Status) && IsWrapper) {
    Status = RestoreOfficialBootmgr (Device);
    if (EFI_ERROR (Status)) {
      TuShow (L"WARNING: could not put signed bootmgfw.efi back on the BCD path.\r\n");
      TuShow (L"Windows may show 0xc0000428 (unsigned wrapper still named bootmgfw.efi).\r\n");
    }
  }

  Last = EFI_NOT_FOUND;
  for (Index = 0; Index < ARRAY_SIZE (mCandidates); Index++) {
    Status = TryCandidate (Parent, Device, mCandidates[Index], SelfPath, UseLoaderPrefix);
    if (!EFI_ERROR (Status)) {
      return Status;
    }
    if (Status != EFI_NOT_FOUND) {
      Last = Status;
    }
  }
  return Last;
}

#define TU_BOOT_TARGET_MAX  16
#define TU_BOOT_FILE_MAX    (16u * 1024u * 1024u)

typedef struct {
  EFI_HANDLE  Device;
  CHAR16      Title[48];
  CHAR16      Path[128];
  CHAR16      Where[40];
} TU_BOOT_TARGET;

STATIC
BOOLEAN
NameEq (
  IN CONST CHAR16  *Left,
  IN CONST CHAR16  *Right
  )
{
  CHAR16  A;
  CHAR16  B;

  if ((Left == NULL) || (Right == NULL)) {
    return FALSE;
  }
  while ((*Left != 0) && (*Right != 0)) {
    A = *Left;
    B = *Right;
    if ((A >= L'A') && (A <= L'Z')) {
      A = (CHAR16)(A - L'A' + L'a');
    }
    if ((B >= L'A') && (B <= L'Z')) {
      B = (CHAR16)(B - L'A' + L'a');
    }
    if (A != B) {
      return FALSE;
    }
    Left++;
    Right++;
  }
  return *Left == *Right;
}

STATIC
VOID
CopyAscii (
  IN CONST CHAR16  *In,
  OUT CHAR8        *Out,
  IN UINTN         OutSize
  )
{
  UINTN  Index;

  if ((Out == NULL) || (OutSize == 0)) {
    return;
  }
  Index = 0;
  if (In != NULL) {
    while ((In[Index] != 0) && (Index + 1 < OutSize)) {
      Out[Index] = (In[Index] < 128) ? (CHAR8)In[Index] : '?';
      Index++;
    }
  }
  Out[Index] = 0;
}

STATIC
BOOLEAN
BufferIsX64EfiApp (
  IN CONST VOID  *Buffer,
  IN UINTN       Size
  )
{
  EFI_IMAGE_DOS_HEADER    Dos;
  EFI_IMAGE_NT_HEADERS64  Nt;
  UINTN                   Off;

  if ((Buffer == NULL) || (Size < sizeof (EFI_IMAGE_DOS_HEADER))) {
    return FALSE;
  }
  CopyMem (&Dos, Buffer, sizeof (Dos));
  if (Dos.e_magic != EFI_IMAGE_DOS_SIGNATURE) {
    return FALSE;
  }
  Off = (UINTN)Dos.e_lfanew;
  if ((Off < sizeof (EFI_IMAGE_DOS_HEADER)) ||
      (Off > Size) ||
      (Size - Off < sizeof (EFI_IMAGE_NT_HEADERS64)))
  {
    return FALSE;
  }
  CopyMem (&Nt, (CONST UINT8 *)Buffer + Off, sizeof (Nt));
  if (Nt.Signature != EFI_IMAGE_NT_SIGNATURE) {
    return FALSE;
  }
  if (Nt.FileHeader.Machine != IMAGE_FILE_MACHINE_X64) {
    return FALSE;
  }
  if (Nt.OptionalHeader.Magic != EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return FALSE;
  }
  return Nt.OptionalHeader.Subsystem == EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION;
}

STATIC
EFI_STATUS
OpenFsRoot (
  IN EFI_HANDLE          Device,
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;

  Status = gBS->HandleProtocol (Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return Fs->OpenVolume (Fs, Root);
}

STATIC
BOOLEAN
ProbeBootFile (
  IN EFI_HANDLE    Device,
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  VOID               *Data;
  UINTN              Size;
  BOOLEAN            Ok;

  Status = OpenFsRoot (Device, &Root);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }
  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) && (Path[0] == L'\\')) {
    Status = Root->Open (Root, &File, (CHAR16 *)(Path + 1), EFI_FILE_MODE_READ, 0);
  }
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return FALSE;
  }
  InfoSize = SIZE_OF_EFI_FILE_INFO + (256 * sizeof (CHAR16));
  Info     = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    File->Close (File);
    Root->Close (Root);
    return FALSE;
  }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status) ||
      (Info->FileSize < sizeof (EFI_IMAGE_DOS_HEADER)) ||
      (Info->FileSize > TU_BOOT_FILE_MAX))
  {
    FreePool (Info);
    File->Close (File);
    Root->Close (Root);
    return FALSE;
  }
  Size = (UINTN)Info->FileSize;
  Data = AllocateZeroPool (Size);
  FreePool (Info);
  if (Data == NULL) {
    File->Close (File);
    Root->Close (Root);
    return FALSE;
  }
  Status = File->Read (File, &Size, Data);
  File->Close (File);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    FreePool (Data);
    return FALSE;
  }
  Ok = !TuFsBufferHasMagic (Data, Size) && BufferIsX64EfiApp (Data, Size);
  FreePool (Data);
  return Ok;
}

STATIC
VOID
FormatWhere (
  IN EFI_HANDLE  Device,
  OUT CHAR16     *Out,
  IN UINTN       OutBytes
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  CHAR16                    Bus[32];
  CHAR16                    Part[16];
  EFI_STATUS                Status;

  Bus[0]  = 0;
  Part[0] = 0;
  StrCpyS (Out, OutBytes / sizeof (CHAR16), L"disk");
  Status = gBS->HandleProtocol (Device, &gEfiDevicePathProtocolGuid, (VOID **)&Dp);
  if (EFI_ERROR (Status) || (Dp == NULL)) {
    return;
  }
  for (Node = Dp; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == HARDWARE_DEVICE_PATH) &&
        (DevicePathSubType (Node) == HW_PCI_DP) &&
        (DevicePathNodeLength (Node) >= sizeof (PCI_DEVICE_PATH)))
    {
      PCI_DEVICE_PATH  *Pci;

      Pci = (PCI_DEVICE_PATH *)Node;
      UnicodeSPrint (Bus, sizeof (Bus), L"PCI %u.%u", Pci->Device, Pci->Function);
    } else if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
               (DevicePathSubType (Node) == MSG_NVME_NAMESPACE_DP) &&
               (DevicePathNodeLength (Node) >= sizeof (NVME_NAMESPACE_DEVICE_PATH)))
    {
      NVME_NAMESPACE_DEVICE_PATH  *Nvme;

      Nvme = (NVME_NAMESPACE_DEVICE_PATH *)Node;
      UnicodeSPrint (Bus, sizeof (Bus), L"NVMe ns=%u", Nvme->NamespaceId);
    } else if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
               (DevicePathSubType (Node) == MSG_SATA_DP) &&
               (DevicePathNodeLength (Node) >= sizeof (SATA_DEVICE_PATH)))
    {
      SATA_DEVICE_PATH  *Sata;

      Sata = (SATA_DEVICE_PATH *)Node;
      UnicodeSPrint (Bus, sizeof (Bus), L"SATA port=%u", Sata->HBAPortNumber);
    } else if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
               (DevicePathSubType (Node) == MSG_ATAPI_DP) &&
               (DevicePathNodeLength (Node) >= sizeof (ATAPI_DEVICE_PATH)))
    {
      ATAPI_DEVICE_PATH  *Atapi;

      Atapi = (ATAPI_DEVICE_PATH *)Node;
      UnicodeSPrint (Bus, sizeof (Bus), L"ATAPI %u.%u", Atapi->PrimarySecondary, Atapi->SlaveMaster);
    } else if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
               (DevicePathSubType (Node) == MSG_SCSI_DP) &&
               (DevicePathNodeLength (Node) >= sizeof (SCSI_DEVICE_PATH)))
    {
      SCSI_DEVICE_PATH  *Scsi;

      Scsi = (SCSI_DEVICE_PATH *)Node;
      UnicodeSPrint (Bus, sizeof (Bus), L"SCSI pun=%u", Scsi->Pun);
    } else if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
               (DevicePathSubType (Node) == MEDIA_HARDDRIVE_DP) &&
               (DevicePathNodeLength (Node) >= sizeof (HARDDRIVE_DEVICE_PATH)))
    {
      HARDDRIVE_DEVICE_PATH  *Hd;

      Hd = (HARDDRIVE_DEVICE_PATH *)Node;
      UnicodeSPrint (Part, sizeof (Part), L"part=%u", Hd->PartitionNumber);
    }
  }
  if (Bus[0] == 0) {
    return;
  }
  if (Part[0] != 0) {
    UnicodeSPrint (Out, OutBytes, L"%s %s", Bus, Part);
  } else {
    UnicodeSPrint (Out, OutBytes, L"%s", Bus);
  }
}

STATIC
BOOLEAN
AddTarget (
  IN OUT TU_BOOT_TARGET  *List,
  IN OUT UINTN           *Count,
  IN UINTN               Max,
  IN OUT BOOLEAN         *Truncated,
  IN EFI_HANDLE          Device,
  IN CONST CHAR16        *Title,
  IN CONST CHAR16        *Path
  )
{
  TU_BOOT_TARGET  *Slot;
  CHAR8           TitleA[48];
  CHAR8           PathA[128];
  CHAR8           WhereA[40];

  if (*Count >= Max) {
    *Truncated = TRUE;
    return FALSE;
  }
  Slot         = &List[*Count];
  Slot->Device = Device;
  StrCpyS (Slot->Title, ARRAY_SIZE (Slot->Title), Title);
  StrCpyS (Slot->Path, ARRAY_SIZE (Slot->Path), Path);
  FormatWhere (Device, Slot->Where, sizeof (Slot->Where));
  (*Count)++;
  CopyAscii (Slot->Title, TitleA, sizeof (TitleA));
  CopyAscii (Slot->Path, PathA, sizeof (PathA));
  CopyAscii (Slot->Where, WhereA, sizeof (WhereA));
  GU_NOTE ("boot entry %u %a %a %a\n", (unsigned)*Count, TitleA, PathA, WhereA);
  return TRUE;
}

STATIC
VOID
ScanVendorDirs (
  IN EFI_HANDLE          Device,
  IN EFI_FILE_PROTOCOL   *Root,
  IN OUT TU_BOOT_TARGET  *List,
  IN OUT UINTN           *Count,
  IN UINTN               Max,
  IN OUT BOOLEAN         *Truncated
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Dir;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  CHAR16             FilePath[160];
  CHAR16             Title[48];

  Status = Root->Open (Root, &Dir, L"\\EFI", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Status = Root->Open (Root, &Dir, L"EFI", EFI_FILE_MODE_READ, 0);
  }
  if (EFI_ERROR (Status)) {
    return;
  }
  InfoSize = SIZE_OF_EFI_FILE_INFO + (256 * sizeof (CHAR16));
  Info     = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    Dir->Close (Dir);
    return;
  }
  for (;;) {
    UINTN    ReadSize;
    CHAR16   *Name;
    BOOLEAN  Added;

    ReadSize = InfoSize;
    Status   = Dir->Read (Dir, &ReadSize, Info);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      if (ReadSize > 4096) {
        break;
      }
      FreePool (Info);
      InfoSize = ReadSize;
      Info     = AllocateZeroPool (InfoSize);
      if (Info == NULL) {
        break;
      }
      continue;
    }
    if (EFI_ERROR (Status) || (ReadSize == 0)) {
      break;
    }
    if ((Info->Attribute & EFI_FILE_DIRECTORY) == 0) {
      continue;
    }
    Name = Info->FileName;
    if ((StrLen (Name) == 0) || (StrLen (Name) > 47) ||
        NameEq (Name, L".") || NameEq (Name, L"..") ||
        NameEq (Name, L"microsoft") || NameEq (Name, L"boot") ||
        NameEq (Name, L"turing-unlock"))
    {
      continue;
    }
    Added = FALSE;
    UnicodeSPrint (FilePath, sizeof (FilePath), L"\\EFI\\%s\\shimx64.efi", Name);
    if (ProbeBootFile (Device, FilePath)) {
      AddTarget (List, Count, Max, Truncated, Device, Name, FilePath);
      Added = TRUE;
    } else {
      UnicodeSPrint (FilePath, sizeof (FilePath), L"\\EFI\\%s\\grubx64.efi", Name);
      if (ProbeBootFile (Device, FilePath)) {
        AddTarget (List, Count, Max, Truncated, Device, Name, FilePath);
        Added = TRUE;
      }
    }
    UnicodeSPrint (FilePath, sizeof (FilePath), L"\\EFI\\%s\\systemd-bootx64.efi", Name);
    if (ProbeBootFile (Device, FilePath)) {
      if (Added) {
        UnicodeSPrint (Title, sizeof (Title), L"%s systemd-boot", Name);
      } else {
        StrCpyS (Title, ARRAY_SIZE (Title), Name);
      }
      AddTarget (List, Count, Max, Truncated, Device, Title, FilePath);
    }
  }
  if (Info != NULL) {
    FreePool (Info);
  }
  Dir->Close (Dir);
}

STATIC
VOID
ScanVolume (
  IN EFI_HANDLE          Device,
  IN OUT TU_BOOT_TARGET  *List,
  IN OUT UINTN           *Count,
  IN UINTN               Max,
  IN OUT BOOLEAN         *Truncated
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  UINTN              Before;

  Before = *Count;
  if (ProbeBootFile (Device, TU_MS_BOOT_OFFICIAL)) {
    AddTarget (
      List,
      Count,
      Max,
      Truncated,
      Device,
      L"Windows Boot Manager",
      TU_MS_BOOT_OFFICIAL
      );
  }
  Status = OpenFsRoot (Device, &Root);
  if (!EFI_ERROR (Status)) {
    ScanVendorDirs (Device, Root, List, Count, Max, Truncated);
    Root->Close (Root);
  }
  if ((*Count == Before) && ProbeBootFile (Device, TU_FALLBACK_BOOT)) {
    AddTarget (
      List,
      Count,
      Max,
      Truncated,
      Device,
      L"Fallback loader",
      TU_FALLBACK_BOOT
      );
  }
}

STATIC
BOOLEAN
VolumeIsInternalDisk (
  IN EFI_HANDLE  Device
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  BOOLEAN                   Bus;
  BOOLEAN                   Part;
  EFI_STATUS                Status;

  Bus  = FALSE;
  Part = FALSE;
  Status = gBS->HandleProtocol (Device, &gEfiDevicePathProtocolGuid, (VOID **)&Dp);
  if (EFI_ERROR (Status) || (Dp == NULL)) {
    return FALSE;
  }
  for (Node = Dp; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if (DevicePathType (Node) != MESSAGING_DEVICE_PATH) {
      if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
          (DevicePathSubType (Node) == MEDIA_HARDDRIVE_DP))
      {
        Part = TRUE;
      }
      continue;
    }
    if ((DevicePathSubType (Node) == MSG_USB_DP) ||
        (DevicePathSubType (Node) == MSG_USB_CLASS_DP) ||
        (DevicePathSubType (Node) == MSG_USB_WWID_DP))
    {
      return FALSE;
    }
    if ((DevicePathSubType (Node) == MSG_NVME_NAMESPACE_DP) ||
        (DevicePathSubType (Node) == MSG_SATA_DP) ||
        (DevicePathSubType (Node) == MSG_ATAPI_DP) ||
        (DevicePathSubType (Node) == MSG_SCSI_DP))
    {
      Bus = TRUE;
    }
  }
  return Bus || Part;
}

STATIC
EFI_STATUS
CollectTargets (
  IN EFI_HANDLE          SelfDevice,
  OUT TU_BOOT_TARGET     **OutList,
  OUT UINTN              *Count,
  OUT BOOLEAN            *Truncated
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;

  *OutList   = NULL;
  *Count     = 0;
  *Truncated = FALSE;
  TuShow (L"Looking for EFI loaders...\r\n");
  TuLogWide (L"mass storage controllers connected: %u\r\n", TuPciConnectMassStorage ());
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    PrintStatus (L"LocateHandleBuffer(SFS)", Status);
    return Status;
  }
  *OutList = AllocateZeroPool (sizeof (TU_BOOT_TARGET) * TU_BOOT_TARGET_MAX);
  if (*OutList == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    if ((Handles[Index] == SelfDevice) || !VolumeIsInternalDisk (Handles[Index])) {
      continue;
    }
    ScanVolume (Handles[Index], *OutList, Count, TU_BOOT_TARGET_MAX, Truncated);
  }
  FreePool (Handles);
  GU_NOTE ("boot targets=%u\n", (unsigned)*Count);
  return EFI_SUCCESS;
}

STATIC
VOID
DrawBootRow (
  IN BOOLEAN       Selected,
  IN UINTN         NormalAttr,
  IN CONST CHAR16  *Text
  )
{
  if (Selected) {
    gST->ConOut->SetAttribute (gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
    TuPrint (L" > %s\r\n", Text);
  } else {
    gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
    TuPrint (L"   %s\r\n", Text);
  }
  gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
}

STATIC
VOID
DrawBootList (
  IN CONST TU_BOOT_TARGET  *List,
  IN UINTN                 Count,
  IN UINTN                 Selected,
  IN UINTN                 NormalAttr
  )
{
  UINTN  Index;

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
  TuPrint (L"Select a boot target\r\n");
  TuPrint (L"Up/Down, Enter to start, Esc for firmware\r\n\r\n");
  for (Index = 0; Index < Count; Index++) {
    DrawBootRow (Index == Selected, NormalAttr, List[Index].Title);
  }
  TuPrint (L"\r\n%s\r\n%s\r\n", List[Selected].Path, List[Selected].Where);
}

STATIC
INTN
PickTarget (
  IN CONST TU_BOOT_TARGET  *List,
  IN UINTN                 Count
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Index;
  UINTN          Selected;
  UINTN          NormalAttr;

  if (Count == 0) {
    return -1;
  }
  Selected   = 0;
  NormalAttr = gST->ConOut->Mode->Attribute;
  if (NormalAttr == 0) {
    NormalAttr = EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK;
  }
  while (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
  }
  for (;;) {
    DrawBootList (List, Count, Selected, NormalAttr);
    Index  = 0;
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    if (EFI_ERROR (Status)) {
      gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
      return -1;
    }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (Key.ScanCode == SCAN_ESC) {
      gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
      return -1;
    }
    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.UnicodeChar == CHAR_LINEFEED)) {
      gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
      return (INTN)Selected;
    }
    if (Key.ScanCode == SCAN_UP) {
      Selected = (Selected == 0) ? (Count - 1) : (Selected - 1);
    } else if (Key.ScanCode == SCAN_DOWN) {
      Selected = (Selected + 1) % Count;
    }
  }
}

STATIC
EFI_STATUS
ChainloadFromDiskList (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                SelfDevice,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath
  )
{
  EFI_STATUS      Status;
  TU_BOOT_TARGET  *List;
  TU_BOOT_TARGET  Chosen;
  UINTN           Count;
  BOOLEAN         Truncated;
  INTN            Pick;
  CHAR8           TitleA[48];
  CHAR8           PathA[128];

  Status = CollectTargets (SelfDevice, &List, &Count, &Truncated);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Truncated) {
    TuLogWide (L"Boot list stopped at %u entries.\r\n", TU_BOOT_TARGET_MAX);
  }
  TuLogFlush ();
  if (Count == 0) {
    TuShow (L"No bootable EFI loader found on NVMe or SATA FAT partitions.\r\n");
    FreePool (List);
    return EFI_NOT_FOUND;
  }
  Pick = PickTarget (List, Count);
  if (Pick < 0) {
    GU_NOTE ("boot choice=esc\n");
    TuLogFlush ();
    TuShow (L"\r\nNo target selected.\r\n");
    FreePool (List);
    return EFI_ABORTED;
  }
  Chosen = List[Pick];
  FreePool (List);
  CopyAscii (Chosen.Title, TitleA, sizeof (TitleA));
  CopyAscii (Chosen.Path, PathA, sizeof (PathA));
  GU_NOTE ("boot choice %u %a %a\n", (unsigned)Pick + 1, TitleA, PathA);
  TuLogFlush ();
  TuPrint (L"\r\n");
  return TryCandidate (Parent, Chosen.Device, Chosen.Path, SelfPath, FALSE);
}

TU_BOOT_MODE
TuBootModeDetect (
  IN EFI_HANDLE  ImageHandle
  )
{
  UINTN  Index;

  /* Install-GpuUnlockBoot.ps1 leaves the signed bootmgfw next to the loader. */
  for (Index = 0; Index < ARRAY_SIZE (mSignedSources); Index++) {
    if (TuFsFileExists (ImageHandle, mSignedSources[Index])) {
      return TuBootModeEsp;
    }
  }
  /* New-GpuUnlockUsb.ps1 writes this marker and does not copy a bootmgfw backup. */
  if (TuFsFileExists (ImageHandle, TU_USB_BOOT_MARK)) {
    return TuBootModeUsb;
  }
  return TuBootModeUnmarked;
}

VOID
TuChainloadClearPreload (
  VOID
  )
{
  if (mPreloadedImage != NULL) {
    gBS->UnloadImage (mPreloadedImage);
    mPreloadedImage  = NULL;
    mPreloadedDevice = NULL;
    GU_NOTE ("preload cleared\n");
  }
}

BOOLEAN
TuChainloadHasPreload (
  VOID
  )
{
  return mPreloadedImage != NULL;
}

STATIC
EFI_STATUS
PreloadFromDevice (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                Device,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath,
  IN BOOLEAN                   UseLoaderPrefix,
  IN BOOLEAN                   RestoreOfficial
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  Last;
  EFI_HANDLE  Image;
  UINTN       Index;
  UINTN       OfficialSize;
  BOOLEAN     IsWrapper;

  if (RestoreOfficial) {
    OfficialSize = 0;
    IsWrapper    = FALSE;
    Status       = InspectFile (Device, TU_MS_BOOT_OFFICIAL, &OfficialSize, &IsWrapper);
    if (!EFI_ERROR (Status) && IsWrapper) {
      Status = RestoreOfficialBootmgr (Device);
      if (EFI_ERROR (Status)) {
        TuLogWide (L"preload: could not restore signed bootmgfw before LoadImage\r\n");
      }
    }
  }

  Last = EFI_NOT_FOUND;
  for (Index = 0; Index < ARRAY_SIZE (mCandidates); Index++) {
    Status = LoadCandidateOnly (
               Parent,
               Device,
               mCandidates[Index],
               SelfPath,
               UseLoaderPrefix,
               &Image
               );
    if (!EFI_ERROR (Status) && (Image != NULL)) {
      mPreloadedImage  = Image;
      mPreloadedDevice = Device;
      GU_NOTE ("preload ok candidate=%u\n", (unsigned)Index);
      return EFI_SUCCESS;
    }
    Last = Status;
  }
  return Last;
}

STATIC
EFI_STATUS
PreloadFromInternalWindows (
  IN EFI_HANDLE                Parent,
  IN EFI_HANDLE                SelfDevice,
  IN EFI_DEVICE_PATH_PROTOCOL  *SelfPath
  )
{
  EFI_STATUS      Status;
  TU_BOOT_TARGET  *List;
  UINTN           Count;
  BOOLEAN         Truncated;
  UINTN           Index;

  Status = CollectTargets (SelfDevice, &List, &Count, &Truncated);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Count == 0) {
    FreePool (List);
    return EFI_NOT_FOUND;
  }

  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Count; Index++) {
    if (StrCmp (List[Index].Title, L"Windows Boot Manager") != 0) {
      continue;
    }
    Status = LoadCandidateOnly (
               Parent,
               List[Index].Device,
               List[Index].Path,
               SelfPath,
               FALSE,
               &mPreloadedImage
               );
    if (!EFI_ERROR (Status) && (mPreloadedImage != NULL)) {
      mPreloadedDevice = List[Index].Device;
      GU_NOTE ("preload ok from internal Windows Boot Manager\n");
      FreePool (List);
      return EFI_SUCCESS;
    }
  }

  FreePool (List);
  mPreloadedImage  = NULL;
  mPreloadedDevice = NULL;
  return Status;
}

EFI_STATUS
TuChainloadPreload (
  IN EFI_HANDLE    ImageHandle,
  IN TU_BOOT_MODE  Mode
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  EFI_DEVICE_PATH_PROTOCOL   *SelfPath;

  TuChainloadClearPreload ();

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) {
    PrintStatus (L"LoadedImage(preload)", Status);
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

  TuLogWide (L"preload Windows Boot Manager before disconnect/unlock\r\n");
  GU_NOTE ("preload begin mode=%u\n", (unsigned)Mode);

  if (Mode != TuBootModeUsb) {
    Status = PreloadFromDevice (
               ImageHandle,
               Loaded->DeviceHandle,
               SelfPath,
               TRUE,
               TRUE
               );
    if (!EFI_ERROR (Status)) {
      TuShow (L"Windows Boot Manager preloaded.\r\n");
      return EFI_SUCCESS;
    }
    if (Mode == TuBootModeEsp) {
      TuLogWide (L"ESP preload failed; chainload will retry LoadImage later\r\n");
      return Status;
    }
  }

  Status = PreloadFromInternalWindows (ImageHandle, Loaded->DeviceHandle, SelfPath);
  if (!EFI_ERROR (Status)) {
    TuShow (L"Windows Boot Manager preloaded from internal disk.\r\n");
    return EFI_SUCCESS;
  }

  TuLogWide (L"preload missed; unlock continues, chainload will LoadImage later\r\n");
  GU_NOTE ("preload missed status=0x%lx\n", (UINTN)Status);
  return Status;
}

EFI_STATUS
TuChainloadWindows (
  IN EFI_HANDLE    ImageHandle,
  IN TU_BOOT_MODE  Mode
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  EFI_DEVICE_PATH_PROTOCOL   *SelfPath;
  EFI_HANDLE                 Image;

  if (mPreloadedImage != NULL) {
    Image = mPreloadedImage;
    mPreloadedImage  = NULL;
    mPreloadedDevice = NULL;
    GU_NOTE ("chainload: StartImage preloaded Windows Boot Manager\n");
    TuLogWide (L"Starting preloaded Windows Boot Manager...\r\n");
    Status = StartWindowsImage (Image);
    if (!EFI_ERROR (Status)) {
      return Status;
    }
    PrintStatus (L"StartImage(preloaded)", Status);
    gBS->UnloadImage (Image);
    TuLogWide (L"preloaded StartImage failed; falling back to LoadImage path\r\n");
  }

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
  TuLogWide (L"chainload from device %p\r\n", Loaded->DeviceHandle);

  if (Mode != TuBootModeUsb) {
    /* ESP restores signed bootmgfw on this volume, then starts it.
     * Unmarked tries this volume the same way before offering the list.
     * USB never writes bootmgfw and never boots the loader volume.
     */
    Status = TryDevice (ImageHandle, Loaded->DeviceHandle, SelfPath, TRUE);
    if (!EFI_ERROR (Status)) {
      return Status;
    }
    if (Mode == TuBootModeEsp) {
      TuShow (L"chainload FAILED: could not LoadImage/StartImage Windows Boot Manager.\r\n");
      TuShow (L"From recovery: copy EFI\\turing-unlock\\bootmgfw_ms.efi over EFI\\Microsoft\\Boot\\bootmgfw.efi\r\n");
      return EFI_NOT_FOUND;
    }
    TuLogWide (L"This volume has no boot loader.\r\n");
  }

  /* Mass storage only. A full PCI connect rebinds the NVIDIA GOP.
   * Class 0x01 covers NVMe and SATA. USB host controllers stay as they are.
   */
  Status = ChainloadFromDiskList (ImageHandle, Loaded->DeviceHandle, SelfPath);
  if (!EFI_ERROR (Status) || (Status == EFI_ABORTED)) {
    return Status;
  }
  TuShow (L"chainload FAILED: could not start a selected EFI loader.\r\n");
  return Status;
}
