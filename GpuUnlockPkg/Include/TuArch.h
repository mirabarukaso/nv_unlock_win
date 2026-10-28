/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_ARCH_H
#define TU_ARCH_H

#include "TuPci.h"
#include "TuConfig.h"

TU_ARCH
TuArchFromDeviceId (
  IN UINT16  DeviceId
  );

CONST CHAR16 *
TuArchName (
  IN TU_ARCH  Arch
  );

BOOLEAN
TuArchMatchesBuild (
  IN TU_ARCH  Arch
  );

BOOLEAN
TuTuringIsDeviceId (
  IN UINT16  DeviceId
  );

BOOLEAN
TuAmpereIsDeviceId (
  IN UINT16  DeviceId
  );

BOOLEAN
TuAdaIsDeviceId (
  IN UINT16  DeviceId
  );

BOOLEAN
TuAdaAdmitUnlock (
  IN UINT16           DeviceId,
  IN CONST TU_CONFIG  *Config,
  IN CONST CHAR8      *BdfName
  );

INTN
TuAmpereUnlockGpu (
  IN EFI_HANDLE       ImageHandle,
  IN TU_PCI_GPU       *Gpu,
  IN CONST TU_CONFIG  *Config
  );

INTN
TuAdaUnlockGpu (
  IN EFI_HANDLE       ImageHandle,
  IN TU_PCI_GPU       *Gpu,
  IN CONST TU_CONFIG  *Config
  );

#endif
