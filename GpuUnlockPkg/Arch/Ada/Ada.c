// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "arch.h"
#include "prep.h"

STATIC CONST UINT16 mAdaIds[] = {
  GU_AD102_RTX_4090,
  GU_AD102_RTX_4090_D,
  GU_AD103_RTX_4070_TI_SUPER,
  GU_AD106_RTX_4060_TI_16GB
};

BOOLEAN
TuAdaIsDeviceId (
  IN UINT16  DeviceId
  )
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mAdaIds); Index++) {
    if (mAdaIds[Index] == DeviceId) {
      return TRUE;
    }
  }
  return FALSE;
}
