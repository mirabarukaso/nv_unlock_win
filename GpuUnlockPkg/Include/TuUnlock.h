/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_UNLOCK_H
#define TU_UNLOCK_H

#include "TuConfig.h"
#include "TuPci.h"

INTN
TuUnlockGpu (
  IN EFI_HANDLE  ImageHandle,
  IN TU_PCI_GPU  *Gpu
  );

EFI_STATUS
TuUnlockAll (
  IN EFI_HANDLE      ImageHandle,
  IN CONST TU_CONFIG *Config,
  IN TU_PCI_GPU      *Gpus,
  IN UINTN           GpuCount
  );

#endif
