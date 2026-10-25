/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_FS_H
#define TU_FS_H

#include <Uefi.h>

EFI_STATUS
TuFsGetRoot (
  IN EFI_HANDLE             ImageHandle,
  OUT EFI_FILE_PROTOCOL     **Root
  );

EFI_STATUS
TuFsReadFile (
  IN EFI_HANDLE   ImageHandle,
  IN CONST CHAR16 *Path,
  OUT VOID        **Buffer,
  OUT UINTN       *Size
  );

EFI_STATUS
TuFsWriteFile (
  IN EFI_HANDLE   ImageHandle,
  IN CONST CHAR16 *Path,
  IN CONST VOID   *Buffer,
  IN UINTN        Size,
  IN BOOLEAN      Append
  );

BOOLEAN
TuFsFileExists (
  IN EFI_HANDLE   ImageHandle,
  IN CONST CHAR16 *Path
  );

BOOLEAN
TuFsBufferHasMagic (
  IN CONST VOID  *Buffer,
  IN UINTN       Size
  );

EFI_STATUS
TuFsReadDeviceFile (
  IN EFI_HANDLE   Device,
  IN CONST CHAR16 *Path,
  OUT VOID        **Buffer,
  OUT UINTN       *Size
  );

EFI_STATUS
TuFsWriteDeviceFile (
  IN EFI_HANDLE    Device,
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Buffer,
  IN UINTN         Size,
  IN BOOLEAN       Append
  );

#endif
