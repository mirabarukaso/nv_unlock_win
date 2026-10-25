// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuConfig.h"
#include "TuFs.h"

#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

STATIC
EFI_STATUS
OpenVolumeFromHandle (
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

EFI_STATUS
TuFsGetRoot (
  IN EFI_HANDLE          ImageHandle,
  OUT EFI_FILE_PROTOCOL  **Root
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return OpenVolumeFromHandle (Loaded->DeviceHandle, Root);
}

STATIC
EFI_STATUS
SplitPath (
  IN CONST CHAR16  *Path,
  OUT CHAR16       *Dir,
  IN UINTN         DirChars,
  OUT CONST CHAR16 **File
  )
{
  UINTN        Len;
  UINTN        Slash;
  UINTN        Index;

  if (Path == NULL || Path[0] != L'\\') {
    return EFI_INVALID_PARAMETER;
  }
  Len = StrLen (Path);
  Slash = 0;
  for (Index = 0; Index < Len; Index++) {
    if (Path[Index] == L'\\') {
      Slash = Index;
    }
  }
  if (Slash + 1 >= DirChars) {
    return EFI_BAD_BUFFER_SIZE;
  }
  CopyMem (Dir, Path, Slash * sizeof (CHAR16));
  Dir[Slash] = 0;
  if (Dir[0] == 0) {
    Dir[0] = L'\\';
    Dir[1] = 0;
  }
  *File = Path + Slash + 1;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EnsureDir (
  IN EFI_FILE_PROTOCOL  *Root,
  IN CONST CHAR16       *Dir
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Node;
  CHAR16             Name[128];
  UINTN              Index;
  UINTN              Start;

  if (Dir == NULL || Dir[0] == 0 || (Dir[0] == L'\\' && Dir[1] == 0)) {
    return EFI_SUCCESS;
  }
  Index = 0;
  if (Dir[0] == L'\\') {
    Index = 1;
  }
  Node = Root;
  while (Dir[Index] != 0) {
    Start = 0;
    while (Dir[Index] != 0 && Dir[Index] != L'\\' && Start + 1 < ARRAY_SIZE (Name)) {
      Name[Start++] = Dir[Index++];
    }
    Name[Start] = 0;
    {
      EFI_FILE_PROTOCOL *Next;
      Status = Node->Open (Node, &Next, Name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, EFI_FILE_DIRECTORY);
      if (EFI_ERROR (Status)) {
        Status = Node->Open (
                         Node,
                         &Next,
                         Name,
                         EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                         EFI_FILE_DIRECTORY
                         );
      }
      if (Node != Root) {
        Node->Close (Node);
      }
      if (EFI_ERROR (Status)) {
        return Status;
      }
      Node = Next;
    }
    if (Dir[Index] == L'\\') {
      Index++;
    }
  }
  if (Node != Root) {
    Node->Close (Node);
  }
  return EFI_SUCCESS;
}

EFI_STATUS
TuFsReadDeviceFile (
  IN EFI_HANDLE    Device,
  IN CONST CHAR16  *Path,
  OUT VOID         **Buffer,
  OUT UINTN        *Size
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  VOID               *Data;

  *Buffer = NULL;
  *Size   = 0;
  Status  = OpenVolumeFromHandle (Device, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) && Path[0] == L'\\') {
    Status = Root->Open (Root, &File, (CHAR16 *)(Path + 1), EFI_FILE_MODE_READ, 0);
  }
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }
  InfoSize = SIZE_OF_EFI_FILE_INFO + 256;
  Info     = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    File->Close (File);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    File->Close (File);
    Root->Close (Root);
    return Status;
  }
  Data = AllocateZeroPool ((UINTN)Info->FileSize + 1);
  if (Data == NULL) {
    FreePool (Info);
    File->Close (File);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }
  *Size  = (UINTN)Info->FileSize;
  Status = File->Read (File, Size, Data);
  FreePool (Info);
  File->Close (File);
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    FreePool (Data);
    return Status;
  }
  *Buffer = Data;
  return EFI_SUCCESS;
}

EFI_STATUS
TuFsReadFile (
  IN EFI_HANDLE    ImageHandle,
  IN CONST CHAR16  *Path,
  OUT VOID         **Buffer,
  OUT UINTN        *Size
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return TuFsReadDeviceFile (Loaded->DeviceHandle, Path, Buffer, Size);
}

STATIC
VOID
ClearReadOnly (
  IN EFI_FILE_PROTOCOL  *File
  )
{
  EFI_FILE_INFO  *Info;
  UINTN          InfoSize;
  EFI_STATUS     Status;

  InfoSize = SIZE_OF_EFI_FILE_INFO + 256;
  Info     = AllocateZeroPool (InfoSize);
  if (Info == NULL) {
    return;
  }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (!EFI_ERROR (Status) && ((Info->Attribute & EFI_FILE_READ_ONLY) != 0)) {
    Info->Attribute &= ~EFI_FILE_READ_ONLY;
    (VOID)File->SetInfo (File, &gEfiFileInfoGuid, InfoSize, Info);
  }
  FreePool (Info);
}

EFI_STATUS
TuFsWriteDeviceFile (
  IN EFI_HANDLE    Device,
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Buffer,
  IN UINTN         Size,
  IN BOOLEAN       Append
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;
  CHAR16             Dir[128];
  CONST CHAR16       *Name;
  UINTN              WriteSize;
  UINT64             Pos;

  Status = SplitPath (Path, Dir, ARRAY_SIZE (Dir), &Name);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = OpenVolumeFromHandle (Device, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = EnsureDir (Root, Dir);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }
  Status = Root->Open (
                   Root,
                   &File,
                   (CHAR16 *)Path,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   0
                   );
  if (EFI_ERROR (Status) && Path[0] == L'\\') {
    Status = Root->Open (
                     Root,
                     &File,
                     (CHAR16 *)(Path + 1),
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                     0
                     );
  }
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }
  ClearReadOnly (File);
  if (Append) {
    Pos    = (UINT64)-1;
    Status = File->SetPosition (File, Pos);
    if (EFI_ERROR (Status)) {
      File->SetPosition (File, 0);
    }
  } else {
    File->Delete (File);
    Status = Root->Open (
                     Root,
                     &File,
                     (CHAR16 *)Path,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                     0
                     );
    if (EFI_ERROR (Status) && Path[0] == L'\\') {
      Status = Root->Open (
                       Root,
                       &File,
                       (CHAR16 *)(Path + 1),
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                       0
                       );
    }
    if (EFI_ERROR (Status)) {
      Root->Close (Root);
      return Status;
    }
  }
  WriteSize = Size;
  Status    = File->Write (File, &WriteSize, (VOID *)Buffer);
  File->Close (File);
  Root->Close (Root);
  if (!EFI_ERROR (Status) && (WriteSize != Size)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  return Status;
}

EFI_STATUS
TuFsWriteFile (
  IN EFI_HANDLE    ImageHandle,
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Buffer,
  IN UINTN         Size,
  IN BOOLEAN       Append
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;

  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return TuFsWriteDeviceFile (Loaded->DeviceHandle, Path, Buffer, Size, Append);
}

BOOLEAN
TuFsFileExists (
  IN EFI_HANDLE    ImageHandle,
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *File;

  Status = TuFsGetRoot (ImageHandle, &Root);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }
  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR (Status)) {
    File->Close (File);
  }
  Root->Close (Root);
  return !EFI_ERROR (Status);
}

BOOLEAN
TuFsBufferHasMagic (
  IN CONST VOID  *Buffer,
  IN UINTN       Size
  )
{
  CONST CHAR8  *Bytes;
  UINTN        MagicLen;
  UINTN        Index;

  if (Buffer == NULL || Size < 4) {
    return FALSE;
  }
  Bytes    = Buffer;
  MagicLen = AsciiStrLen (TU_LOADER_MAGIC);
  if (Size < MagicLen) {
    return FALSE;
  }
  for (Index = 0; Index + MagicLen <= Size; Index++) {
    if (CompareMem (Bytes + Index, TU_LOADER_MAGIC, MagicLen) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}
