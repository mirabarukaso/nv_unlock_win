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
  TU_CONFIG     Config;
  TU_PCI_GPU    *Gpus;
  UINTN         GpuCount;
  TU_ACTION     Action;
  TU_BOOT_MODE  BootMode;
  EFI_INPUT_KEY Key;

  (VOID)SystemTable;
  (VOID)gTuLoaderMagic;

  TuConfigInitDefaults (&Config);
  (VOID)TuConfigLoad (ImageHandle, &Config);
  (VOID)TuConfigParseLoadOptions (ImageHandle, &Config);
  TuLogInit (ImageHandle, Config.Verbose);

  TuLogWide (L"\r\nGPU Unlock Loader %a (%a)\r\n", TU_LOADER_VERSION, gTuLoaderMagic);
  GU_NOTE ("loader start version=%a\n", TU_LOADER_VERSION);

  TuPciConnectInput ();

  Gpus     = NULL;
  GpuCount = 0;
  Status   = TuPciScan (&Gpus, &GpuCount);
  if (EFI_ERROR (Status)) {
    TuShow (L"PCI scan failed %x, continuing with 0 GPUs\r\n", Status);
    Gpus     = NULL;
    GpuCount = 0;
  }
  TuLogWide (L"NVIDIA display GPUs: %u\r\n", GpuCount);

  BootMode = TuBootModeDetect (ImageHandle);
  Action   = TuMenuChoose (&Config, Gpus, GpuCount);
  if (Action == TuActionInject) {
    UINTN Index;

    /*
     * TOTORONG AD103 Windows unlocker order:
     * preload bootmgfw -> disconnect/unlock/FLR -> reconnect GOP ->
     * observe selectors -> StartImage.
     */
    Status = TuChainloadPreload (ImageHandle, BootMode);
    if (EFI_ERROR (Status)) {
      GU_NOTE ("preload status=0x%lx (non-fatal; LoadImage later)\n", (UINTN)Status);
      TuLogWide (
        L"preload bootmgfw missed (%r); will LoadImage after unlock\r\n",
        Status
        );
    }

    TuLogWide (L"inject begin\r\n");
    TuLogFlush ();

    Status = TuUnlockAll (ImageHandle, &Config, Gpus, GpuCount);
    GU_NOTE ("inject finished status=%x\n", Status);
    for (Index = 0; Index < GpuCount; Index++) {
      TuPciReadFeat (&Gpus[Index]);
    }
    TuShow (L"\r\nFeat AFTER inject:\r\n");
    TuMenuPrintFeat (Gpus, GpuCount);
    TuLogFlush ();
    TuMenuWaitContinue (TU_POST_INJECT_WAIT_SEC);
  } else {
    TuChainloadClearPreload ();
    TuLogWide (L"Inject skipped. Continuing to boot.\r\n");
    GU_NOTE ("inject skipped by menu action=%u\n", (unsigned)Action);
  }

  TuPciLogHandoff (
    Gpus,
    GpuCount,
    (Action == TuActionInject) ? "inject" :
    (Action == TuActionEmergency) ? "emergency" : "skip"
    );
  TuLogWide (
    L"Boot mode: %a\r\n",
    (BootMode == TuBootModeEsp) ? "esp" :
    (BootMode == TuBootModeUsb) ? "usb" : "unmarked"
    );
  GU_NOTE (
    "boot mode=%a\n",
    (BootMode == TuBootModeEsp) ? "esp" :
    (BootMode == TuBootModeUsb) ? "usb" : "unmarked"
    );
  TuLogFlush ();
  TuPciFreeScan (Gpus);

  if (Config.NoChainload) {
    UINTN Index;

    TuChainloadClearPreload ();
    TuShow (L"--noload: not chainloading. Press any key.\r\n");
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    return EFI_SUCCESS;
  }

  Status = TuChainloadWindows (ImageHandle, BootMode);
  if (EFI_ERROR (Status)) {
    UINTN Index;

    TuShow (L"Press any key to return to firmware.\r\n");
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  }
  return Status;
}
