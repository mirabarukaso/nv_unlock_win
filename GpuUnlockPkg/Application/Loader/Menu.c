// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "TuConfig.h"
#include "TuMenu.h"

VOID
TuMenuPrintFeat (
  IN CONST TU_PCI_GPU  *Gpus,
  IN UINTN             GpuCount
  )
{
  UINTN Index;

  if (GpuCount == 0) {
    TuPrint (L"  (none)\r\n");
    return;
  }
  for (Index = 0; Index < GpuCount; Index++) {
    TuPrint (
      L"  %04x:%02x:%02x.%x  10de:%04x  %s%s\r\n",
      Gpus[Index].Segment,
      Gpus[Index].Bus,
      Gpus[Index].Device,
      Gpus[Index].Function,
      Gpus[Index].DeviceId,
      TuArchName (Gpus[Index].Arch),
      Gpus[Index].UnlockSupported ? L"" : L"  (I forces)"
      );
    if (Gpus[Index].FeatValid) {
      TuPrint (
        L"    PLM=%08x  SS0=%08x  SS1=%08x  RO=%08x%s\r\n",
        Gpus[Index].FeatPlm,
        Gpus[Index].FeatSs0,
        Gpus[Index].FeatSs1,
        Gpus[Index].FeatReadout,
        (Gpus[Index].FeatSs0 == TU_SS0_FULL && Gpus[Index].FeatSs1 == TU_SS1_FULL)
          ? L"  [full SS]"
          : L""
        );
    } else {
      TuPrint (L"    feat unread\r\n");
    }
  }
}

VOID
TuMenuWaitContinue (
  IN UINTN  Seconds
  )
{
  EFI_STATUS     Status;
  EFI_EVENT      Events[2];
  EFI_EVENT      Timer;
  UINTN          Index;
  EFI_INPUT_KEY  Key;
  UINTN          Remain;

  Remain = Seconds;
  if (Remain == 0) {
    return;
  }

  while (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_NOT_READY) {
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer);
  if (EFI_ERROR (Status)) {
    return;
  }

  TuPrint (L"\r\nPress any key to continue to Windows (%u sec)\r\n", Remain);
  while (Remain > 0) {
    TuPrint (L"\r  %u...   ", Remain);
    gBS->SetTimer (Timer, TimerRelative, 10000000);
    Events[0] = gST->ConIn->WaitForKey;
    Events[1] = Timer;
    Status    = gBS->WaitForEvent (2, Events, &Index);
    if (EFI_ERROR (Status)) {
      break;
    }
    if (Index == 0) {
      gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      TuPrint (L"\r\nKey pressed.\r\n");
      break;
    }
    Remain--;
  }
  if (Remain == 0) {
    TuPrint (L"\r\nTimeout, continuing.\r\n");
  }
  gBS->CloseEvent (Timer);
}

STATIC
VOID
DrawMenu (
  IN CONST TU_CONFIG   *Config,
  IN CONST TU_PCI_GPU  *Gpus,
  IN UINTN             GpuCount,
  IN UINTN             Remain
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  TuPrint (L"GPU Unlock Loader %a (%a)\r\n", TU_LOADER_VERSION, TU_BUILD_ARCH_NAME);
  TuPrint (L"================================\r\n\r\n");
  TuPrint (L"NVIDIA display GPUs: %u  (feat read-only until I)\r\n", GpuCount);
  TuMenuPrintFeat (Gpus, GpuCount);
  TuPrint (L"\r\n");
  TuPrint (L"  [Enter]  Continue WITHOUT inject   (default)\r\n");
  TuPrint (L"  [I]      Inject (force same-arch IDs) then show regs\r\n");
  TuPrint (L"  [R]      Emergency chainload Windows now\r\n");
  TuPrint (L"\r\nDefault in %u sec: SKIP inject\r\n", Remain);
  (VOID)Config;
}

TU_ACTION
TuMenuChoose (
  IN CONST TU_CONFIG   *Config,
  IN CONST TU_PCI_GPU  *Gpus,
  IN UINTN             GpuCount
  )
{
  EFI_STATUS     Status;
  EFI_EVENT      Events[2];
  EFI_EVENT      Timer;
  UINTN          Index;
  EFI_INPUT_KEY  Key;
  UINTN          Remain;
  TU_ACTION      Choice;

  if (Config->ForceSkip) {
    return TuActionSkip;
  }
  if (Config->ForceInject) {
    return TuActionInject;
  }

  Remain = Config->TimeoutSec;
  if (Remain == 0) {
    return Config->DefaultAction;
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer);
  if (EFI_ERROR (Status)) {
    return Config->DefaultAction;
  }

  Choice = Config->DefaultAction;
  while (Remain > 0) {
    DrawMenu (Config, Gpus, GpuCount, Remain);
    gBS->SetTimer (Timer, TimerRelative, 10000000);
    Events[0] = gST->ConIn->WaitForKey;
    Events[1] = Timer;
    Status    = gBS->WaitForEvent (2, Events, &Index);
    if (EFI_ERROR (Status)) {
      break;
    }
    if (Index == 1) {
      Remain--;
      continue;
    }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.ScanCode == SCAN_ESC) {
      Choice = TuActionSkip;
      break;
    }
    if (Key.UnicodeChar == L'i' || Key.UnicodeChar == L'I') {
      Choice = TuActionInject;
      break;
    }
    if (Key.UnicodeChar == L'r' || Key.UnicodeChar == L'R') {
      Choice = TuActionEmergency;
      break;
    }
  }

  gBS->CloseEvent (Timer);
  if (Remain == 0) {
    Choice = Config->DefaultAction;
  }
  TuPrint (L"\r\nSelected: %s\r\n",
           Choice == TuActionInject ? L"INJECT" :
           Choice == TuActionEmergency ? L"EMERGENCY" : L"SKIP");
  return Choice;
}
