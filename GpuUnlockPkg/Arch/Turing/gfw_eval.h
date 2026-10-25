/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TURING_UNLOCK_GFW_EVAL_H
#define TURING_UNLOCK_GFW_EVAL_H

#include "TuCompat.h"

#define GU_GFW_PLM_OFFSET          0x00118128u
#define GU_GFW_BOOT_OFFSET         0x00118234u
#define GU_GFW_CPUCTL_OFFSET       0x00110100u
#define GU_GFW_TIMEOUT_US          2050000u
#define GU_FEAT_PLM_OFFSET         0x00823804u
#define GU_FEAT_SS0_OFFSET         0x0082381cu
#define GU_FEAT_SS1_OFFSET         0x00823820u
#define GU_GFW_PLM_LEVEL0          0x1u
#define GU_GFW_BOOT_COMPLETED      0xffu
#define GU_GFW_CPUCTL_HALTED       0x10u
#define GU_PCI_COMMAND_IO          0x1u
#define GU_PCI_COMMAND_MEMORY      0x2u
#define GU_PCI_COMMAND_MASTER      0x4u

struct turing_unlock_gfw_sample {
  bool config_ok;
  u32  plm;
  u32  boot;
  u32  cpuctl;
};

int
turing_unlock_gfw_assess (
  const struct turing_unlock_gfw_sample *sample
  );

u16
turing_unlock_command_restore (
  u16 original,
  u16 live
  );

#endif
