/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_CHAINLOAD_H
#define TU_CHAINLOAD_H

#include <Uefi.h>

typedef enum {
  TuBootModeUnmarked = 0,
  TuBootModeEsp,
  TuBootModeUsb
} TU_BOOT_MODE;

TU_BOOT_MODE
TuBootModeDetect (
  IN EFI_HANDLE  ImageHandle
  );

EFI_STATUS
TuChainloadWindows (
  IN EFI_HANDLE    ImageHandle,
  IN TU_BOOT_MODE  Mode
  );

#endif
