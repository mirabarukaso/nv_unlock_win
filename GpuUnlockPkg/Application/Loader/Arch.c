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
  IN EFI_HANDLE  ImageHandle,
  IN TU_PCI_GPU  *Gpu
  )
{
  (VOID)ImageHandle;
  (VOID)Gpu;
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

INTN
TuAdaUnlockGpu (
  IN EFI_HANDLE  ImageHandle,
  IN TU_PCI_GPU  *Gpu
  )
{
  (VOID)ImageHandle;
  (VOID)Gpu;
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

EFI_STATUS
TuUnlockAll (
  IN EFI_HANDLE       ImageHandle,
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
    if (!TargetListed (Config->Targets, Name)) {
      TuLogWide (L"Skipping %a (not in targets)\r\n", Name);
      continue;
    }
    if (!Gpus[Index].UnlockSupported) {
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
        Ret = TuAmpereUnlockGpu (ImageHandle, &Gpus[Index]);
        break;
#endif
#if defined(TU_BUILD_ARCH_ADA)
      case TuArchAda:
        Ret = TuAdaUnlockGpu (ImageHandle, &Gpus[Index]);
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
