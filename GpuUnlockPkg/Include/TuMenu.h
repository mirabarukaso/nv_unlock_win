/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_MENU_H
#define TU_MENU_H

#include "TuConfig.h"
#include "TuPci.h"

#define TU_POST_INJECT_WAIT_SEC  20

TU_ACTION
TuMenuChoose (
  IN CONST TU_CONFIG  *Config,
  IN CONST TU_PCI_GPU *Gpus,
  IN UINTN            GpuCount
  );

VOID
TuMenuPrintFeat (
  IN CONST TU_PCI_GPU  *Gpus,
  IN UINTN             GpuCount
  );

VOID
TuMenuWaitContinue (
  IN UINTN  Seconds
  );

#endif
