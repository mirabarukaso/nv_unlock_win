/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TURING_UNLOCK_BOOT_H
#define TURING_UNLOCK_BOOT_H

#include "TuCompat.h"

#define TU_BOOTER_IMAGE_SIZE  59136u
#define TU_BOOTER_SIG_TOTAL   16u

struct turing_unlock_booter_hdr;
struct turing_unlock_boot_desc;

int
turing_unlock_booter_parse (
  const struct firmware           *fw,
  struct turing_unlock_booter_hdr *hdr
  );

int
turing_unlock_boot_desc_v4 (
  const u8                      *data,
  size_t                        size,
  u32                           image_size,
  struct turing_unlock_boot_desc *out
  );

bool
turing_unlock_fb_range_ready (
  u32 range
  );

#endif
