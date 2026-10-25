// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"

STATIC CONST UINT16 mTuringIds[] = {
  0x1e03, 0x1e04, 0x1e07, 0x1e2d, 0x1e2e,
  0x1e81, 0x1e82, 0x1e84, 0x1e87, 0x1e89,
  0x1ec2, 0x1ec7,
  0x1f02, 0x1f03, 0x1f06, 0x1f07, 0x1f08, 0x1f42, 0x1f47
};

BOOLEAN
TuTuringIsDeviceId (
  IN UINT16  DeviceId
  )
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mTuringIds); Index++) {
    if (mTuringIds[Index] == DeviceId) {
      return TRUE;
    }
  }
  return FALSE;
}
