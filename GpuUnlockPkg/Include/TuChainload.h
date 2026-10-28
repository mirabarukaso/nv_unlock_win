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

/* TOTORONG AD103 / Latence: LoadImage Windows Boot Manager before disconnect
 * and unlock. TuChainloadWindows StartImages the preloaded handle when set. */
EFI_STATUS
TuChainloadPreload (
  IN EFI_HANDLE    ImageHandle,
  IN TU_BOOT_MODE  Mode
  );

BOOLEAN
TuChainloadHasPreload (
  VOID
  );

VOID
TuChainloadClearPreload (
  VOID
  );

#endif
