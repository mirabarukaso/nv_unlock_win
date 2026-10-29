// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "TuConfig.h"
#include "TuUnlock.h"

#if !defined(TU_BUILD_ARCH_TURING)

BOOLEAN
TuTuringIsDeviceId (
  IN UINT16  DeviceId
  )
{
  (VOID)DeviceId;
  return FALSE;
}

INTN
TuUnlockGpu (
  IN EFI_HANDLE  ImageHandle,
  IN TU_PCI_GPU  *Gpu
  )
{
  (VOID)ImageHandle;
  (VOID)Gpu;
  return -EOPNOTSUPP;
}

#endif

#if !defined(TU_BUILD_ARCH_AMPERE)

BOOLEAN
TuAmpereIsDeviceId (
  IN UINT16  DeviceId
  )
{
  (VOID)DeviceId;
  return FALSE;
}

INTN
TuAmpereUnlockGpu (
  IN EFI_HANDLE       ImageHandle,
  IN TU_PCI_GPU       *Gpu,
  IN CONST TU_CONFIG  *Config
  )
{
  (VOID)ImageHandle;
  (VOID)Gpu;
  (VOID)Config;
  return -EOPNOTSUPP;
}

#endif

#if !defined(TU_BUILD_ARCH_ADA)

BOOLEAN
TuAdaIsDeviceId (
  IN UINT16  DeviceId
  )
{
  (VOID)DeviceId;
  return FALSE;
}

BOOLEAN
TuAdaAdmitUnlock (
  IN UINT16           DeviceId,
  IN CONST TU_CONFIG  *Config,
  IN CONST CHAR8      *BdfName
  )
{
  (VOID)DeviceId;
  (VOID)Config;
  (VOID)BdfName;
  return FALSE;
}

INTN
TuAdaUnlockGpu (
  IN EFI_HANDLE       ImageHandle,
  IN TU_PCI_GPU       *Gpu,
  IN CONST TU_CONFIG  *Config
  )
{
  (VOID)ImageHandle;
  (VOID)Gpu;
  (VOID)Config;
  return -EOPNOTSUPP;
}

#endif

TU_ARCH
TuArchFromDeviceId (
  IN UINT16  DeviceId
  )
{
  if (TuTuringIsDeviceId (DeviceId)) {
    return TuArchTuring;
  }
  if (TuAmpereIsDeviceId (DeviceId)) {
    return TuArchAmpere;
  }
  if (TuAdaIsDeviceId (DeviceId)) {
    return TuArchAda;
  }
  if (DeviceId >= 0x1e00 && DeviceId <= 0x1fff) {
    return TuArchTuring;
  }
  if (DeviceId >= 0x2200 && DeviceId <= 0x25ff) {
    return TuArchAmpere;
  }
  if (DeviceId >= 0x2600 && DeviceId <= 0x28ff) {
    return TuArchAda;
  }
  return TuArchNone;
}

CONST CHAR16 *
TuArchName (
  IN TU_ARCH  Arch
  )
{
  switch (Arch) {
    case TuArchTuring:
      return L"Turing";
    case TuArchAmpere:
      return L"Ampere";
    case TuArchAda:
      return L"Ada";
    default:
      return L"unknown";
  }
}

BOOLEAN
TuArchMatchesBuild (
  IN TU_ARCH  Arch
  )
{
#if defined(TU_BUILD_ARCH_TURING)
  return Arch == TuArchTuring;
#elif defined(TU_BUILD_ARCH_AMPERE)
  return Arch == TuArchAmpere;
#elif defined(TU_BUILD_ARCH_ADA)
  return Arch == TuArchAda;
#else
  (VOID)Arch;
  return FALSE;
#endif
}

STATIC
BOOLEAN
TargetListed (
  CONST CHAR8  *Targets,
  CONST CHAR8  *Name
  )
{
  CONST CHAR8  *Entry;
  CONST CHAR8  *End;
  UINTN        NameLen;
  UINTN        EntLen;

  if (Targets == NULL || Targets[0] == 0 || AsciiStriCmp (Targets, "all") == 0) {
    return TRUE;
  }
  NameLen = AsciiStrLen (Name);
  for (Entry = Targets; *Entry != 0; Entry = *End ? End + 1 : End) {
    End = Entry;
    while (*End != 0 && *End != ',') {
      End++;
    }
    EntLen = (UINTN)(End - Entry);
    if (EntLen == NameLen && AsciiStrniCmp (Entry, Name, EntLen) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

#if defined(TU_BUILD_ARCH_AMPERE)
/*
 * 0.2.13: RTX 3050 is temporarily not supported. GA106 3050 IDs (2507/2508)
 * are still on the Ampere list from upstream but are skipped here; to
 * re-enable later, remove them from this table. GA107/laptop 3050 IDs are
 * not on the list and are covered by the range-only skip in TuUnlockAll.
 */
STATIC CONST UINT16 mAmpereBlockedIds[] = { 0x2507, 0x2508 };

STATIC
BOOLEAN
AmpereBlocked (
  UINT16  DeviceId
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mAmpereBlockedIds); Index++) {
    if (mAmpereBlockedIds[Index] == DeviceId) {
      return TRUE;
    }
  }
  return FALSE;
}
#endif

EFI_STATUS
TuUnlockAll (  IN EFI_HANDLE       ImageHandle,
  IN CONST TU_CONFIG  *Config,
  IN TU_PCI_GPU       *Gpus,
  IN UINTN            GpuCount
  )
{
  UINTN  Index;
  INTN   Ret;
  INTN   Failed;
  CHAR8  Name[32];

  if (GpuCount == 0) {
    TuShow (L"Inject: no supported GPU. No MMIO writes.\r\n");
    GU_NOTE ("inject skipped: gpu_count=0\n");
    return EFI_SUCCESS;
  }

  Failed = 0;
  for (Index = 0; Index < GpuCount; Index++) {
    AsciiSPrint (
      Name,
      sizeof (Name),
      "%04x:%02x:%02x.%x",
      (UINT32)Gpus[Index].Segment,
      (UINT32)Gpus[Index].Bus,
      (UINT32)Gpus[Index].Device,
      (UINT32)Gpus[Index].Function
      );
    if (!TuArchMatchesBuild (Gpus[Index].Arch)) {
      TuLogWide (
        L"Skipping %a: this loader was built for %a only\r\n",
        Name,
        TU_BUILD_ARCH_NAME
        );
      continue;
    }
#if defined(TU_BUILD_ARCH_ADA)
    if (Gpus[Index].Arch == TuArchAda) {
      if (!TuAdaAdmitUnlock (Gpus[Index].DeviceId, Config, Name)) {
        continue;
      }
    } else
#endif
    if (!TargetListed (Config->Targets, Name)) {
      TuLogWide (L"Skipping %a (not in targets)\r\n", Name);
      continue;
    }
#if defined(TU_BUILD_ARCH_AMPERE)
    if (Gpus[Index].Arch == TuArchAmpere && AmpereBlocked (Gpus[Index].DeviceId)) {
      TuLogWide (
        L"Skipping %a 10de:%04x (RTX 3050 not supported)\r\n",
        Name,
        Gpus[Index].DeviceId
        );
      GU_NOTE ("skip blocked Ampere 10de:%04x\n", Gpus[Index].DeviceId);
      continue;
    }
#endif
    if (!Gpus[Index].UnlockSupported) {
      /* Ada experimental / unknown IDs must go through TuAdaAdmitUnlock; do
       * not force-inject a readout-only Ada BAR onto AD102 assets. */
      if (Gpus[Index].Arch == TuArchAda) {
        TuLogWide (
          L"Skipping %a 10de:%04x (Ada ID not admitted)\r\n",
          Name,
          Gpus[Index].DeviceId
          );
        GU_NOTE ("skip unadmitted Ada 10de:%04x\n", Gpus[Index].DeviceId);
        continue;
      }
      /* Ampere: range-only match (0x2200-0x25ff, not on the explicit list)
       * covers GA107 / laptop RTX 3050 and other untested parts. Do not
       * force-inject them (0.2.13). Turing keeps range force-inject below:
       * old parts, no reported harm (e.g. TITAN RTX 1e02). */
      if (Gpus[Index].Arch == TuArchAmpere) {
        TuLogWide (
          L"Skipping %a 10de:%04x (unlisted Ampere ID; 3050/laptop parts not supported)\r\n",
          Name,
          Gpus[Index].DeviceId
          );
        GU_NOTE ("skip unlisted Ampere 10de:%04x\n", Gpus[Index].DeviceId);
        continue;
      }
      TuLogWide (
        L"FORCING inject on readout-only %a 10de:%04x\r\n",
        Name,
        Gpus[Index].DeviceId
        );
      GU_NOTE ("force inject 10de:%04x\n", Gpus[Index].DeviceId);
    }
    TuLogWide (
      L"Unlocking %a 10de:%04x (%s)\r\n",
      Name,
      Gpus[Index].DeviceId,
      TuArchName (Gpus[Index].Arch)
      );
    switch (Gpus[Index].Arch) {
#if defined(TU_BUILD_ARCH_TURING)
      case TuArchTuring:
        Ret = TuUnlockGpu (ImageHandle, &Gpus[Index]);
        break;
#endif
#if defined(TU_BUILD_ARCH_AMPERE)
      case TuArchAmpere:
        Ret = TuAmpereUnlockGpu (ImageHandle, &Gpus[Index], Config);
        break;
#endif
#if defined(TU_BUILD_ARCH_ADA)
      case TuArchAda:
        Ret = TuAdaUnlockGpu (ImageHandle, &Gpus[Index], Config);
        break;
#endif
      default:
        TuLogWide (
          L"Skipping %a: this loader was built for %a only\r\n",
          Name,
          TU_BUILD_ARCH_NAME
          );
        continue;
    }
    GU_NOTE ("unlock 10de:%04x result=%d\n", Gpus[Index].DeviceId, (int)Ret);
    if (Ret) {
      TuShow (L"Unlock failed (%d)\r\n", Ret);
      if (!Failed) {
        Failed = Ret;
      }
    } else {
      TuShow (L"Unlock completed.\r\n");
    }
  }
  if (Failed) {
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
