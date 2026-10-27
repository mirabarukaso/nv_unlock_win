// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "TuChainload.h"
#include "TuConfig.h"
#include "TuFs.h"
#include "TuMenu.h"
#include "TuPci.h"
#include "TuUnlock.h"

CONST CHAR8  gTuLoaderMagic[] = TU_LOADER_MAGIC;

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  TU_CONFIG   Config;
  TU_PCI_GPU  *Gpus;
  UINTN       GpuCount;
  TU_ACTION   Action;
  EFI_INPUT_KEY Key;

  (VOID)SystemTable;
  (VOID)gTuLoaderMagic;

  TuConfigInitDefaults (&Config);
  (VOID)TuConfigLoad (ImageHandle, &Config);
  (VOID)TuConfigParseLoadOptions (ImageHandle, &Config);
  TuLogInit (ImageHandle, Config.Verbose);

  TuPrint (L"\r\nGPU Unlock Loader %a (%a)\r\n", TU_LOADER_VERSION, gTuLoaderMagic);
  GU_NOTE ("loader start version=%a\n", TU_LOADER_VERSION);

  TuPciConnectInput ();

  Gpus     = NULL;
  GpuCount = 0;
  Status   = TuPciScan (&Gpus, &GpuCount);
  if (EFI_ERROR (Status)) {
    TuPrint (L"PCI scan failed %x, continuing with 0 GPUs\r\n", Status);
    Gpus     = NULL;
    GpuCount = 0;
  }
  TuPrint (L"NVIDIA display GPUs: %u\r\n", GpuCount);

  Action = TuMenuChoose (&Config, Gpus, GpuCount);
  if (Action == TuActionInject) {
    UINTN Index;

    Status = TuUnlockAll (ImageHandle, &Config, Gpus, GpuCount);
    GU_NOTE ("inject finished status=%x\n", Status);
    for (Index = 0; Index < GpuCount; Index++) {
      TuPciReadFeat (&Gpus[Index]);
    }
    TuPrint (L"\r\nFeat AFTER inject:\r\n");
    TuMenuPrintFeat (Gpus, GpuCount);
    TuMenuWaitContinue (TU_POST_INJECT_WAIT_SEC);
  } else {
    TuPrint (L"Inject skipped. Continuing to Windows.\r\n");
    GU_NOTE ("inject skipped by menu action=%u\n", (unsigned)Action);
  }

  TuPciLogHandoff (
    Gpus,
    GpuCount,
    (Action == TuActionInject) ? "inject" :
    (Action == TuActionEmergency) ? "emergency" : "skip"
    );
  TuLogFlush ();
  TuPciFreeScan (Gpus);

  if (Config.NoChainload) {
    UINTN Index;

    TuPrint (L"--noload: not chainloading. Press any key.\r\n");
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    return EFI_SUCCESS;
  }

  Status = TuChainloadWindows (ImageHandle);
  if (EFI_ERROR (Status)) {
    UINTN Index;

    TuPrint (L"Press any key to return to firmware.\r\n");
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  }
  return Status;
}
