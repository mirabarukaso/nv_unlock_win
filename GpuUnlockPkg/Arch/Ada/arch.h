/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GPU_UNLOCK_ARCH_610_H
#define GPU_UNLOCK_ARCH_610_H

#include "TuCompat.h"

enum gpu_unlock_booter_arch {
  GU_BOOTER_GA10X = 0,
  GU_BOOTER_AD10X = 1
};

/*
 * Ada PCI IDs. Admission lives in Ada.c / config.ini; Unlock.c only routes
 * every listed ID onto the pinned AD102-family assets (same as upstream
 * gpu-unlock-ada ad102_family()). Do not invent per-ID fuse/SM/VBIOS tables.
 */
#define GU_AD102_RTX_4090           0x2684u
#define GU_AD102_RTX_4090_D         0x2685u
#define GU_AD103_RTX_4080_SUPER     0x2702u
#define GU_AD103_RTX_4080           0x2704u
/* Exact experimental AD103 canary; other Ada PCI IDs need their own opt-in. */
#define GU_AD103_RTX_4070_TI_SUPER  0x2705u
/* Exact experimental AD106 target, untested on silicon. */
#define GU_AD106_RTX_4060_TI_16GB   0x2805u

#endif
