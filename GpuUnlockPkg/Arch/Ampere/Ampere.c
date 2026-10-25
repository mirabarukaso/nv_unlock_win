// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"

/*
 * GA102/GA103/GA104/GA106 GPU functions from pci.ids, excluding audio
 * functions and professional (GL) parts that already run BF16 at full rate.
 * GA103 and GA106 also exclude mobile parts. Same list as
 * F:\gpu-unlock\kernel\gpu_unlock\main.c.
 */
STATIC CONST UINT16 mAmpereIds[] = {
  /* GA102 */
  0x2200, 0x2203, 0x2204, 0x2205, 0x2206, 0x2207, 0x2208, 0x220a,
  0x220d, 0x2216, 0x222b, 0x222f,
  /* GA104 */
  0x2480, 0x2482, 0x2483, 0x2484, 0x2486, 0x2487, 0x2488, 0x2489,
  0x248a, 0x248c, 0x248d, 0x248e, 0x249c, 0x249d, 0x249f, 0x24a0,
  0x24a4, 0x24ac, 0x24ad, 0x24af, 0x24bf, 0x24c0, 0x24c7, 0x24c8,
  0x24c9, 0x24dc, 0x24dd, 0x24df, 0x24e0,
  /* GA103 (untested) */
  0x2414,
  /* GA106 (validated only on RTX 3060 12GB 0x2504) */
  0x2501, 0x2503, 0x2504, 0x2505, 0x2507, 0x2508, 0x2509, 0x252f,
  0x2544
};

BOOLEAN
TuAmpereIsDeviceId (
  IN UINT16  DeviceId
  )
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mAmpereIds); Index++) {
    if (mAmpereIds[Index] == DeviceId) {
      return TRUE;
    }
  }
  return FALSE;
}
