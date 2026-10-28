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

  TuPrint (L"\r\nPress any key to continue (%u sec)\r\n", Remain);
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
      TuShow (L"\r\nKey pressed.\r\n");
      break;
    }
    Remain--;
  }
  if (Remain == 0) {
    TuShow (L"\r\nTimeout, continuing.\r\n");
  }
  gBS->CloseEvent (Timer);
}

STATIC CONST CHAR16 *mChoiceText[] = {
  L"Skip inject",
  L"Inject",
  L"Boot now"
};

STATIC
VOID
DrawChoiceRow (
  IN BOOLEAN       Selected,
  IN UINTN         NormalAttr,
  IN CONST CHAR16  *Text
  )
{
  if (Selected) {
    gST->ConOut->SetAttribute (gST->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
    TuPrint (L" > %s\r\n", Text);
  } else {
    gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
    TuPrint (L"   %s\r\n", Text);
  }
  gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
}

STATIC
VOID
DrawMenu (
  IN CONST TU_PCI_GPU  *Gpus,
  IN UINTN             GpuCount,
  IN UINTN             Selected,
  IN UINTN             Remain,
  IN BOOLEAN           Counting,
  IN UINTN             NormalAttr
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
  TuPrint (L"GPU Unlock Loader %a (%a)\r\n\r\n", TU_LOADER_VERSION, TU_BUILD_ARCH_NAME);
  TuPrint (L"NVIDIA display GPUs: %u\r\n", GpuCount);
  TuMenuPrintFeat (Gpus, GpuCount);
  TuPrint (L"\r\n");
  DrawChoiceRow (Selected == 0, NormalAttr, mChoiceText[0]);
  DrawChoiceRow (Selected == 1, NormalAttr, mChoiceText[1]);
  DrawChoiceRow (Selected == 2, NormalAttr, mChoiceText[2]);
  TuPrint (L"\r\nUp/Down, Enter to confirm");
  if (Counting) {
    TuPrint (L"  (%u sec)", Remain);
  }
  TuPrint (L"\r\n");
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
  UINTN          Selected;
  UINTN          NormalAttr;
  BOOLEAN        Counting;
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

  Selected = 0;
  if (Config->DefaultAction == TuActionInject) {
    Selected = 1;
  }
  NormalAttr = gST->ConOut->Mode->Attribute;
  if (NormalAttr == 0) {
    NormalAttr = EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK;
  }
  Counting = TRUE;
  while (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key) != EFI_NOT_READY) {
  }

  for (;;) {
    DrawMenu (Gpus, GpuCount, Selected, Remain, Counting, NormalAttr);
    if (Counting && (Remain == 0)) {
      break;
    }
    if (Counting) {
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
    } else {
      Index  = 0;
      Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
      if (EFI_ERROR (Status)) {
        break;
      }
    }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }
    if (Key.ScanCode == SCAN_UP) {
      Selected = (Selected == 0) ? 2 : (Selected - 1);
      Counting = FALSE;
    } else if (Key.ScanCode == SCAN_DOWN) {
      Selected = (Selected + 1) % 3;
      Counting = FALSE;
    } else if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) ||
               (Key.UnicodeChar == CHAR_LINEFEED))
    {
      break;
    }
  }

  gBS->CloseEvent (Timer);
  gST->ConOut->SetAttribute (gST->ConOut, NormalAttr);
  Choice = (Selected == 1) ? TuActionInject :
           (Selected == 2) ? TuActionEmergency : TuActionSkip;
  TuShow (
    L"\r\n%s\r\n",
    (Choice == TuActionInject) ? L"Inject" :
    (Choice == TuActionEmergency) ? L"Boot now" : L"Skip inject"
    );
  return Choice;
}
