// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "TuPci.h"

#include <IndustryStandard/Pci.h>
#include <Library/DevicePathLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextOut.h>

BOOLEAN
TuPciIsSupportedDeviceId (
  IN UINT16  DeviceId
  )
{
  return TuTuringIsDeviceId (DeviceId) ||
         TuAmpereIsDeviceId (DeviceId) ||
         TuAdaIsDeviceId (DeviceId);
}

STATIC
UINT8
FindMmioBar (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  UINT8  Index;
  UINT64 Attr;

  for (Index = 0; Index < 6; Index++) {
    if (!EFI_ERROR (PciIo->GetBarAttributes (PciIo, Index, &Attr, NULL))) {
      if ((Attr & EFI_PCI_IO_ATTRIBUTE_MEMORY) != 0) {
        return Index;
      }
    }
  }
  return 0;
}

STATIC
EFI_STATUS
TuPciEnableMemory (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT64      Attr;

  Status = PciIo->Attributes (PciIo, EfiPciIoAttributeOperationGet, 0, &Attr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return PciIo->Attributes (
                  PciIo,
                  EfiPciIoAttributeOperationEnable,
                  EFI_PCI_IO_ATTRIBUTE_MEMORY,
                  &Attr
                  );
}

VOID
TuPciReadFeat (
  IN OUT TU_PCI_GPU  *Gpu
  )
{
  UINT32  PlmOff;
  UINT32  Ss0Off;
  UINT32  Ss1Off;
  UINT32  RoOff;

  if (Gpu == NULL || Gpu->PciIo == NULL) {
    return;
  }
  Gpu->FeatValid = FALSE;
  switch (Gpu->Arch) {
    case TuArchTuring:
      PlmOff = TU_TURING_FEAT_PLM;
      Ss0Off = TU_TURING_FEAT_SS0;
      Ss1Off = TU_TURING_FEAT_SS1;
      RoOff  = TU_TURING_FEAT_READOUT;
      break;
    case TuArchAmpere:
    case TuArchAda:
      PlmOff = TU_AMPERE_FEAT_PLM;
      Ss0Off = TU_AMPERE_FEAT_SS0;
      Ss1Off = TU_AMPERE_FEAT_SS1;
      RoOff  = TU_AMPERE_FEAT_READOUT;
      break;
    default:
      return;
  }

  (VOID)TuPciEnableMemory (Gpu->PciIo);
  Gpu->FeatPlm     = TuPciMemRead32 (Gpu->PciIo, Gpu->BarIndex, PlmOff);
  Gpu->FeatSs0     = TuPciMemRead32 (Gpu->PciIo, Gpu->BarIndex, Ss0Off);
  Gpu->FeatSs1     = TuPciMemRead32 (Gpu->PciIo, Gpu->BarIndex, Ss1Off);
  Gpu->FeatReadout = TuPciMemRead32 (Gpu->PciIo, Gpu->BarIndex, RoOff);
  Gpu->FeatValid   = TRUE;
  GU_NOTE (
    "feat 10de:%04x plm=%#x ss0=%#x ss1=%#x ro=%#x\n",
    Gpu->DeviceId,
    Gpu->FeatPlm,
    Gpu->FeatSs0,
    Gpu->FeatSs1,
    Gpu->FeatReadout
    );
}

#define TU_WPR2_LO      0x001fa824u
#define TU_WPR2_HI      0x001fa828u
#define TU_BOOT_STATE   0x001180f8u
#define TU_GSP_CPUCTL   0x00110100u
#define TU_SEC_CPUCTL   0x00840100u
#define TU_SEC_DMACTL   0x0084010cu
#define TU_SEC_MBOX0    0x00840040u
#define TU_SEC_MBOX1    0x00840044u

STATIC
UINT64
TuPciBarBase (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex
  )
{
  UINT32  Lo;
  UINT32  Hi;
  UINT16  Reg;

  Lo  = 0;
  Hi  = 0;
  Reg = (UINT16)(0x10u + (UINT16)BarIndex * 4u);
  PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, Reg, 1, &Lo);
  if ((Lo & 0x1u) != 0) {
    return 0;
  }
  if ((Lo & 0x6u) == 0x4u) {
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, (UINT32)Reg + 4u, 1, &Hi);
    return ((UINT64)Hi << 32) | (UINT64)(Lo & 0xfffffff0u);
  }
  return (UINT64)(Lo & 0xfffffff0u);
}

VOID
TuPciLogHandoff (
  IN TU_PCI_GPU   *Gpus,
  IN UINTN        Count,
  IN CONST CHAR8  *Phase
  )
{
  UINTN    Index;
  UINT16   Command;
  UINT32   WprLo;
  UINT32   WprHi;
  UINT32   Boot;
  UINT32   Gsp;
  UINT32   Sec;
  UINT32   Dma;
  UINT32   Mb0;
  UINT32   Mb1;
  UINT64   Bar;

  if (Phase == NULL) {
    Phase = "?";
  }
  if (Count == 0 || Gpus == NULL) {
    GU_NOTE ("handoff phase=%a gpus=0\n", Phase);
    return;
  }
  for (Index = 0; Index < Count; Index++) {
    Command = 0;
    WprLo   = 0xffffffffu;
    WprHi   = 0xffffffffu;
    Boot    = 0xffffffffu;
    Gsp     = 0xffffffffu;
    Sec     = 0xffffffffu;
    Dma     = 0xffffffffu;
    Mb0     = 0xffffffffu;
    Mb1     = 0xffffffffu;
    Bar     = 0;
    if (Gpus[Index].PciIo == NULL) {
      GU_NOTE ("handoff phase=%a index=%u no pciio\n", Phase, (UINT32)Index);
      continue;
    }
    Gpus[Index].PciIo->Pci.Read (
                             Gpus[Index].PciIo,
                             EfiPciIoWidthUint16,
                             0x04,
                             1,
                             &Command
                             );
    Bar = TuPciBarBase (Gpus[Index].PciIo, Gpus[Index].BarIndex);
    TuPciReadFeat (&Gpus[Index]);
    if (!Gpus[Index].FeatValid) {
      GU_NOTE (
        "handoff phase=%a 10de:%04x bar=0x%llx cmd=0x%04x feat unread\n",
        Phase,
        Gpus[Index].DeviceId,
        Bar,
        (UINT32)Command
        );
      continue;
    }
    WprLo = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_WPR2_LO);
    WprHi = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_WPR2_HI);
    Boot  = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_BOOT_STATE);
    Gsp   = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_GSP_CPUCTL);
    Sec   = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_SEC_CPUCTL);
    Dma   = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_SEC_DMACTL);
    Mb0   = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_SEC_MBOX0);
    Mb1   = TuPciMemRead32 (Gpus[Index].PciIo, Gpus[Index].BarIndex, TU_SEC_MBOX1);
    GU_NOTE (
      "handoff phase=%a 10de:%04x bar=0x%llx cmd=0x%04x plm=0x%08x ss0=0x%08x ss1=0x%08x ro=0x%08x wpr2=0x%08x:0x%08x boot=0x%08x gsp=0x%08x sec=0x%08x dma=0x%08x mb=0x%08x:0x%08x\n",
      Phase,
      Gpus[Index].DeviceId,
      Bar,
      (UINT32)Command,
      Gpus[Index].FeatPlm,
      Gpus[Index].FeatSs0,
      Gpus[Index].FeatSs1,
      Gpus[Index].FeatReadout,
      WprHi,
      WprLo,
      Boot,
      Gsp,
      Sec,
      Dma,
      Mb1,
      Mb0
      );
  }
}

EFI_STATUS
TuPciScan (
  OUT TU_PCI_GPU  **Gpus,
  OUT UINTN       *Count
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *Handles;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                Found;
  TU_PCI_GPU           *List;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT16               Vendor;
  UINT16               Device;
  UINT32               ClassReg;

  *Gpus  = NULL;
  *Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    GU_NOTE ("PCI scan: LocateHandleBuffer status=%x (no PciIo yet?)\n", Status);
    return EFI_SUCCESS;
  }
  List = AllocateZeroPool (HandleCount * sizeof (TU_PCI_GPU));
  if (List == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }
  Found = 0;
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Vendor = 0;
    Device = 0;
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_VENDOR_ID_OFFSET, 1, &Vendor);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_DEVICE_ID_OFFSET, 1, &Device);
    if (Vendor != TU_PCI_VENDOR_NVIDIA) {
      continue;
    }
    ClassReg = 0;
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0x08, 1, &ClassReg);
    if (((ClassReg >> 24) & 0xffu) != TU_PCI_CLASS_DISPLAY) {
      continue;
    }
    List[Found].PciIo           = PciIo;
    List[Found].Handle          = Handles[Index];
    List[Found].VendorId        = Vendor;
    List[Found].DeviceId        = Device;
    List[Found].Arch            = TuArchFromDeviceId (Device);
    List[Found].BarIndex        = FindMmioBar (PciIo);
    List[Found].UnlockSupported = TuPciIsSupportedDeviceId (Device);
    PciIo->GetLocation (
             PciIo,
             &List[Found].Segment,
             &List[Found].Bus,
             &List[Found].Device,
             &List[Found].Function
             );
    /* Log BDF on every scan (skip and inject). Menu redraw is screen-only. */
    GU_NOTE (
      "NVIDIA PCI %04x:%02x:%02x.%x 10de:%04x handle=%p unlock=%u\n",
      (UINT32)List[Found].Segment,
      (UINT32)List[Found].Bus,
      (UINT32)List[Found].Device,
      (UINT32)List[Found].Function,
      Device,
      Handles[Index],
      TuPciIsSupportedDeviceId (Device)
      );
    TuLogWide (
      L"Found %04x:%02x:%02x.%x  10de:%04x  %s%s\r\n",
      List[Found].Segment,
      List[Found].Bus,
      List[Found].Device,
      List[Found].Function,
      Device,
      TuArchName (List[Found].Arch),
      List[Found].UnlockSupported ? L"" : L"  (readout-only)"
      );
    TuPciReadFeat (&List[Found]);
    Found++;
  }
  FreePool (Handles);
  if (Found == 0) {
    FreePool (List);
    return EFI_SUCCESS;
  }
  *Gpus  = List;
  *Count = Found;
  return EFI_SUCCESS;
}

VOID
TuPciFreeScan (
  IN TU_PCI_GPU  *Gpus
  )
{
  if (Gpus != NULL) {
    FreePool (Gpus);
  }
}

STATIC
UINTN
CountProtocolHandles (
  IN EFI_GUID  *Protocol
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;

  HandleCount = 0;
  Handles     = NULL;
  Status      = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return 0;
  }
  if (Handles != NULL) {
    FreePool (Handles);
  }
  return HandleCount;
}

STATIC
VOID
ConnectByProtocol (
  IN EFI_GUID  *Protocol
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       HandleCount;
  UINTN       Index;

  HandleCount = 0;
  Handles     = NULL;
  Status      = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
}

STATIC
UINTN
ConnectUsbHostControllers (
  VOID
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *Handles;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                UsbCount;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT32               ClassReg;
  UINT64               Attr;
  UINT8                BaseClass;
  UINT8                SubClass;

  UsbCount    = 0;
  HandleCount = 0;
  Handles     = NULL;
  Status      = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return 0;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (EFI_ERROR (Status)) {
      continue;
    }
    ClassReg = 0;
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0x08, 1, &ClassReg);
    BaseClass = (UINT8)((ClassReg >> 24) & 0xffu);
    SubClass  = (UINT8)((ClassReg >> 16) & 0xffu);
    if ((BaseClass != PCI_CLASS_SERIAL) || (SubClass != PCI_CLASS_SERIAL_USB)) {
      continue;
    }
    UsbCount++;
    PciIo->Attributes (
             PciIo,
             EfiPciIoAttributeOperationEnable,
             EFI_PCI_DEVICE_ENABLE,
             &Attr
             );
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
  return UsbCount;
}

UINTN
TuPciConnectMassStorage (
  VOID
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *Handles;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                DiskCount;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT32               ClassReg;
  UINT64               Attr;
  UINT8                BaseClass;

  DiskCount   = 0;
  HandleCount = 0;
  Handles     = NULL;
  Status      = gBS->LocateHandleBuffer (ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || Handles == NULL) {
    return 0;
  }
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (EFI_ERROR (Status)) {
      continue;
    }
    ClassReg = 0;
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0x08, 1, &ClassReg);
    BaseClass = (UINT8)((ClassReg >> 24) & 0xffu);
    if (BaseClass != PCI_CLASS_MASS_STORAGE) {
      continue;
    }
    DiskCount++;
    PciIo->Attributes (
             PciIo,
             EfiPciIoAttributeOperationEnable,
             EFI_PCI_DEVICE_ENABLE,
             &Attr
             );
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }
  FreePool (Handles);
  return DiskCount;
}

VOID
TuPciConnectInput (
  VOID
  )
{
  UINTN  UsbCount;
  UINTN  TextInBefore;
  UINTN  TextInNow;
  UINTN  Wait;

  TuLogWide (L"Connecting USB input (fast POST)...\r\n");
  GU_NOTE ("connect input: USB HC + ConIn\n");

  TextInBefore = CountProtocolHandles (&gEfiSimpleTextInProtocolGuid);
  UsbCount     = ConnectUsbHostControllers ();
  if (UsbCount == 0) {
    ConnectByProtocol (&gEfiPciRootBridgeIoProtocolGuid);
    UsbCount = ConnectUsbHostControllers ();
  }
  TuLogWide (L"USB host controllers: %u\r\n", UsbCount);
  GU_NOTE ("usb host controllers=%u textin=%u\n", UsbCount, TextInBefore);

  if (gST->ConsoleInHandle != NULL) {
    gBS->ConnectController (gST->ConsoleInHandle, NULL, NULL, TRUE);
  }
  if (gST->ConIn != NULL) {
    gST->ConIn->Reset (gST->ConIn, FALSE);
  }

  for (Wait = 0; Wait < ((UsbCount == 0) ? 3 : 15); Wait++) {
    gBS->Stall (100000);
    TextInNow = CountProtocolHandles (&gEfiSimpleTextInProtocolGuid);
    if (TextInNow > TextInBefore) {
      break;
    }
  }

  if (gST->ConsoleInHandle != NULL) {
    gBS->ConnectController (gST->ConsoleInHandle, NULL, NULL, TRUE);
  }
  if (gST->ConIn != NULL) {
    gST->ConIn->Reset (gST->ConIn, FALSE);
  }

  TextInNow = CountProtocolHandles (&gEfiSimpleTextInProtocolGuid);
  GU_NOTE (
    "connect input done usb=%u textin %u->%u wait=%u ms\n",
    UsbCount,
    TextInBefore,
    TextInNow,
    (Wait + 1) * 100
    );
}

EFI_STATUS
TuPciEnableMmioMaster (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT64      Attr;

  Status = PciIo->Attributes (PciIo, EfiPciIoAttributeOperationGet, 0, &Attr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return PciIo->Attributes (
                  PciIo,
                  EfiPciIoAttributeOperationEnable,
                  EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                  &Attr
                  );
}

EFI_STATUS
TuPciClearMaster (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  UINT64 Attr;

  return PciIo->Attributes (
                  PciIo,
                  EfiPciIoAttributeOperationDisable,
                  EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                  &Attr
                  );
}

UINT32
TuPciMemRead32 (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex,
  IN UINT32               Offset
  )
{
  UINT32 Value;

  Value = 0xffffffffu;
  PciIo->Mem.Read (PciIo, EfiPciIoWidthUint32, BarIndex, Offset, 1, &Value);
  return Value;
}

VOID
TuPciMemWrite32 (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex,
  IN UINT32               Offset,
  IN UINT32               Value
  )
{
  PciIo->Mem.Write (PciIo, EfiPciIoWidthUint32, BarIndex, Offset, 1, &Value);
}

EFI_STATUS
TuPciMemReadBuffer (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex,
  IN UINT32               Offset,
  IN UINTN                Size,
  OUT VOID                *Buffer
  )
{
  UINTN Count;

  if ((Size & 3) != 0) {
    return EFI_INVALID_PARAMETER;
  }
  Count = Size / 4;
  return PciIo->Mem.Read (PciIo, EfiPciIoWidthUint32, BarIndex, Offset, Count, Buffer);
}

EFI_STATUS
TuPciSaveConfig (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8               Saved[256]
  )
{
  return PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, 0, 256, Saved);
}

STATIC
UINT16
TuPciCommandRestoreBits (
  IN UINT16  Original,
  IN UINT16  Live
  )
{
  UINT16  Mask;

  /* Match Linux gpu_unlock_command_restore(): MEMORY|MASTER from saved image. */
  Mask = (UINT16)(EFI_PCI_COMMAND_MEMORY_SPACE | EFI_PCI_COMMAND_BUS_MASTER);
  return (UINT16)((Live & (UINT16)~Mask) | (Original & Mask));
}

STATIC
EFI_STATUS
TuPciFindExpCap (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8               *CapPtrOut
  )
{
  EFI_STATUS  Status;
  UINT8       CapPtr;
  UINT8       CapId;

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, PCI_CAPBILITY_POINTER_OFFSET, 1, &CapPtr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  CapPtr &= 0xfc;
  while (CapPtr >= 0x40 && CapPtr != 0xff) {
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, CapPtr, 1, &CapId);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (CapId == EFI_PCI_CAPABILITY_ID_PCIEXP) {
      *CapPtrOut = CapPtr;
      return EFI_SUCCESS;
    }
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, CapPtr + 1, 1, &CapPtr);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    CapPtr &= 0xfc;
  }
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
TuPciWaitTrpndClear (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                CapPtr
  )
{
  EFI_STATUS  Status;
  UINT16      DevSta;
  UINTN       Wait;

  for (Wait = 0; Wait < 50; Wait++) {
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, CapPtr + 10, 1, &DevSta);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (DevSta == 0xffff) {
      return EFI_DEVICE_ERROR;
    }
    if ((DevSta & BIT5) == 0) {
      return EFI_SUCCESS;
    }
    gBS->Stall (20000);
  }
  GU_ERROR ("PCIe DevSta TRPND still set: %#x\n", (UINT32)DevSta);
  return EFI_TIMEOUT;
}

/*
 * Selective post-FLR restore (Linux unlock_610 / pci_restore_state intent):
 * restore BARs + COMMAND via MEMORY|MASTER from the pre-BME saved image,
 * verify identity/BARs/COMMAND (MEMORY on, BME off), wait for TRPND clear.
 * Do not blast all 256 config bytes (status is RW1C; blind write is risky).
 */
EFI_STATUS
TuPciRestoreConfig (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST UINT8          Saved[256]
  )
{
  EFI_STATUS  Status;
  UINT32      Identity;
  UINT32      SavedId;
  UINT32      Bar;
  UINT32      ExpectedBar;
  UINT16      Command;
  UINT16      SavedCmd;
  UINT16      Want;
  UINT8       CapPtr;
  UINT8       Byte;
  UINT32      Index;

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, 1, &Identity);
  if (EFI_ERROR (Status) || Identity == 0xffffffffu) {
    GU_ERROR ("post-FLR config inaccessible before restore\n");
    return EFI_DEVICE_ERROR;
  }
  SavedId = (UINT32)Saved[0] |
            ((UINT32)Saved[1] << 8) |
            ((UINT32)Saved[2] << 16) |
            ((UINT32)Saved[3] << 24);
  if (Identity != SavedId) {
    GU_ERROR (
      "post-FLR identity mismatch: actual=%#x expected=%#x (skip restore)\n",
      Identity,
      SavedId
      );
    return EFI_DEVICE_ERROR;
  }

  for (Index = 0; Index < 6; Index++) {
    ExpectedBar = (UINT32)Saved[0x10 + Index * 4] |
                  ((UINT32)Saved[0x11 + Index * 4] << 8) |
                  ((UINT32)Saved[0x12 + Index * 4] << 16) |
                  ((UINT32)Saved[0x13 + Index * 4] << 24);
    Status = PciIo->Pci.Write (
                         PciIo,
                         EfiPciIoWidthUint32,
                         (UINT32)(0x10u + Index * 4u),
                         1,
                         &ExpectedBar
                         );
    if (EFI_ERROR (Status)) {
      GU_ERROR ("post-FLR BAR%u write failed: 0x%lx\n", (UINT32)Index, (UINTN)Status);
      return Status;
    }
  }

  ExpectedBar = (UINT32)Saved[0x30] |
                ((UINT32)Saved[0x31] << 8) |
                ((UINT32)Saved[0x32] << 16) |
                ((UINT32)Saved[0x33] << 24);
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint32, 0x30, 1, &ExpectedBar);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Cache Line Size + Latency Timer (do not touch Status at 0x06). */
  Byte = Saved[0x0c];
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint8, 0x0c, 1, &Byte);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Byte = Saved[0x0d];
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint8, 0x0d, 1, &Byte);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Byte = Saved[0x3c];
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint8, 0x3c, 1, &Byte);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Byte = Saved[0x3d];
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint8, 0x3d, 1, &Byte);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_COMMAND_OFFSET, 1, &Command);
  if (EFI_ERROR (Status) || Command == 0xffff) {
    return EFI_DEVICE_ERROR;
  }
  SavedCmd = (UINT16)(Saved[PCI_COMMAND_OFFSET] | ((UINT16)Saved[PCI_COMMAND_OFFSET + 1] << 8));
  Want     = TuPciCommandRestoreBits (SavedCmd, Command);
  /* Linux post-FLR path keeps BME off until handoff containment is verified. */
  Want = (UINT16)(Want & (UINT16)~EFI_PCI_COMMAND_BUS_MASTER);
  if (!(Want & EFI_PCI_COMMAND_MEMORY_SPACE) && (SavedCmd & EFI_PCI_COMMAND_MEMORY_SPACE)) {
    Want = (UINT16)(Want | EFI_PCI_COMMAND_MEMORY_SPACE);
  }
  Status = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint16, PCI_COMMAND_OFFSET, 1, &Want);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, PCI_COMMAND_OFFSET, 1, &Command);
  if (EFI_ERROR (Status) || Command == 0xffff) {
    return EFI_DEVICE_ERROR;
  }
  GU_NOTE (
    "restore PCI command: actual=%#x saved=%#x mask=%#x\n",
    Command,
    SavedCmd,
    (unsigned)(EFI_PCI_COMMAND_MEMORY_SPACE | EFI_PCI_COMMAND_BUS_MASTER)
    );
  if ((Command & EFI_PCI_COMMAND_BUS_MASTER) != 0 ||
      (Command & EFI_PCI_COMMAND_MEMORY_SPACE) == 0)
  {
    GU_ERROR (
      "post-FLR COMMAND bad: actual=%#x (need MEMORY=1 MASTER=0)\n",
      Command
      );
    return EFI_DEVICE_ERROR;
  }

  for (Index = 0; Index < 6; Index++) {
    ExpectedBar = (UINT32)Saved[0x10 + Index * 4] |
                  ((UINT32)Saved[0x11 + Index * 4] << 8) |
                  ((UINT32)Saved[0x12 + Index * 4] << 16) |
                  ((UINT32)Saved[0x13 + Index * 4] << 24);
    Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint32,
                        (UINT32)(0x10u + Index * 4u),
                        1,
                        &Bar
                        );
    if (EFI_ERROR (Status) || Bar != ExpectedBar) {
      GU_ERROR (
        "post-FLR BAR%u: actual=%#x expected=%#x\n",
        (UINT32)Index,
        Bar,
        ExpectedBar
        );
      return EFI_DEVICE_ERROR;
    }
  }

  Status = TuPciFindExpCap (PciIo, &CapPtr);
  if (!EFI_ERROR (Status)) {
    Status = TuPciWaitTrpndClear (PciIo, CapPtr);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}


STATIC
BOOLEAN
TuPciDevicePathPrefix (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *Prefix,
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *Path
  )
{
  UINTN  PrefixSize;

  if (Prefix == NULL || Path == NULL) {
    return FALSE;
  }
  PrefixSize = GetDevicePathSize (Prefix);
  if (PrefixSize < END_DEVICE_PATH_LENGTH) {
    return FALSE;
  }
  /* Compare all nodes except the end node of Prefix. */
  PrefixSize -= END_DEVICE_PATH_LENGTH;
  if (PrefixSize == 0) {
    return FALSE;
  }
  return CompareMem (Prefix, Path, PrefixSize) == 0;
}

STATIC
BOOLEAN
TuPciHandleUnderGpu (
  IN EFI_HANDLE  GpuHandle,
  IN EFI_HANDLE  Candidate
  )
{
  EFI_STATUS                 Status;
  EFI_DEVICE_PATH_PROTOCOL   *GpuPath;
  EFI_DEVICE_PATH_PROTOCOL   *CandPath;

  if (Candidate == NULL) {
    return FALSE;
  }
  if (Candidate == GpuHandle) {
    return TRUE;
  }
  Status = gBS->HandleProtocol (
                  GpuHandle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&GpuPath
                  );
  if (EFI_ERROR (Status) || GpuPath == NULL) {
    return FALSE;
  }
  Status = gBS->HandleProtocol (
                  Candidate,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&CandPath
                  );
  if (EFI_ERROR (Status) || CandPath == NULL) {
    return FALSE;
  }
  return TuPciDevicePathPrefix (GpuPath, CandPath);
}

STATIC
VOID
TuPciDisconnectOne (
  IN EFI_HANDLE    Controller,
  IN CONST CHAR8   *Label
  )
{
  EFI_STATUS  Status;

  if (Controller == NULL) {
    return;
  }
  Status = gBS->DisconnectController (Controller, NULL, NULL);
  GU_NOTE (
    "pre-FLR: DisconnectController %a handle=%p status=0x%lx\n",
    Label,
    Controller,
    (UINTN)Status
    );
}

/*
 * Best-effort console/GOP detach before Ada/Ampere FLR on a primary GPU.
 * Mirrors Latence AD104 stages (TextOut -> GraphicsConsole/GOP -> PCI agent)
 * without the MSI-specific graph: disconnect matching TextOut/GOP children,
 * then disconnect drivers bound to the GPU PCI handle. PciIo installed by the
 * PCI bus remains available via HandleProtocol.
 */
EFI_STATUS
TuPciDisconnectDisplay (
  IN EFI_HANDLE  GpuHandle
  )
{
  EFI_STATUS                           Status;
  EFI_HANDLE                           *Handles;
  UINTN                                Count;
  UINTN                                Index;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *OpenInfo;
  UINTN                                OpenCount;
  UINTN                                OpenIndex;
  BOOLEAN                              ConOnGpu;
  UINTN                                ChildCount;

  if (GpuHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  /*
   * Latence AD104 / AD103-2705 Windows ports (#39, #92): release console/GOP
   * ownership before PCIe FLR on the primary GPU. Working 2705 boots blank the
   * console ~1s at FLR/GOP reconnect - not a machine power-cycle. Also detach
   * gopless display children (BY_CHILD without GraphicsOutput); a pinned GOP
   * path alone misses boards with multiple ACPI display children.
   *
   * DisconnectController(Controller, NULL, NULL) drops every driver bound to
   * that controller. PciIo installed by the PCI bus stays via HandleProtocol.
   */
  GU_NOTE ("pre-FLR: driver_release TextOut/GOP/display-children begin\n");
  ConOnGpu   = FALSE;
  ChildCount = 0;

  /* Stage 1: SimpleTextOut under this GPU (GraphicsConsole / splitter children). */
  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleTextOutProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (!EFI_ERROR (Status) && Handles != NULL) {
    for (Index = 0; Index < Count; Index++) {
      if (TuPciHandleUnderGpu (GpuHandle, Handles[Index])) {
        if (gST != NULL && gST->ConsoleOutHandle == Handles[Index]) {
          ConOnGpu = TRUE;
        }
        TuPciDisconnectOne (Handles[Index], "TextOut");
      }
    }
    FreePool (Handles);
  }

  /* Stage 2: GraphicsOutput under this GPU. */
  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiGraphicsOutputProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (!EFI_ERROR (Status) && Handles != NULL) {
    for (Index = 0; Index < Count; Index++) {
      if (TuPciHandleUnderGpu (GpuHandle, Handles[Index])) {
        if (gST != NULL && gST->ConsoleOutHandle == Handles[Index]) {
          ConOnGpu = TRUE;
        }
        TuPciDisconnectOne (Handles[Index], "GOP");
      }
    }
    FreePool (Handles);
  }

  /* Stage 2b: every PciIo BY_CHILD (includes gopless display children). */
  OpenInfo  = NULL;
  OpenCount = 0;
  Status = gBS->OpenProtocolInformation (
                  GpuHandle,
                  &gEfiPciIoProtocolGuid,
                  &OpenInfo,
                  &OpenCount
                  );
  if (!EFI_ERROR (Status) && OpenInfo != NULL) {
    for (OpenIndex = 0; OpenIndex < OpenCount; OpenIndex++) {
      if ((OpenInfo[OpenIndex].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) != 0) {
        ChildCount++;
        TuPciDisconnectOne (OpenInfo[OpenIndex].ControllerHandle, "display-child");
      }
    }
    FreePool (OpenInfo);
  }

  /* Stage 3: detach NVIDIA (and any other) drivers bound to the GPU PCI handle. */
  TuPciDisconnectOne (GpuHandle, "PCI");

  if (ConOnGpu && gST != NULL) {
    /* Avoid OutputString into a dead framebuffer; file log remains. */
    GU_NOTE ("pre-FLR: console was on unlock GPU; clearing ConOut pointer\n");
    gST->ConOut = NULL;
  }

  /* Brief settle before Initiate FLR (working 2705 path blanks ~1s around FLR). */
  gBS->Stall (100000);

  GU_NOTE (
    "pre-FLR: driver_release done con_on_gpu=%u display_children=%u\n",
    ConOnGpu,
    ChildCount
    );
  return EFI_SUCCESS;
}

/*
 * TOTORONG AD103 Windows unlocker / working #92 handoff: after FLR, reconnect
 * the GPU so NVIDIA GOP (and console children) bind again before StartImage.
 * Best-effort; unlock selectors are observed separately by the Ada path.
 */
EFI_STATUS
TuPciReconnectDisplay (
  IN EFI_HANDLE  GpuHandle
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Out;

  if (GpuHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  GU_NOTE ("post-FLR: reconnect GOP begin handle=%p\n", GpuHandle);
  Status = gBS->ConnectController (GpuHandle, NULL, NULL, TRUE);
  GU_NOTE (
    "post-FLR: ConnectController GPU status=0x%lx\n",
    (UINTN)Status
    );

  /* Recursive connect above should rebind children; nudge console if cleared. */
  if ((gST != NULL) && (gST->ConOut == NULL)) {
    Out = NULL;
    if ((gST->ConsoleOutHandle != NULL) &&
        !EFI_ERROR (
           gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiSimpleTextOutProtocolGuid,
                  (VOID **)&Out
                  )) &&
        (Out != NULL))
    {
      gST->ConOut = Out;
      GU_NOTE ("post-FLR: restored ConOut from ConsoleOutHandle\n");
    } else {
      Handles = NULL;
      Count   = 0;
      Status  = gBS->LocateHandleBuffer (
                       ByProtocol,
                       &gEfiSimpleTextOutProtocolGuid,
                       NULL,
                       &Count,
                       &Handles
                       );
      if (!EFI_ERROR (Status) && (Handles != NULL) && (Count > 0)) {
        for (Index = 0; Index < Count; Index++) {
          Out = NULL;
          if (!EFI_ERROR (
                 gBS->HandleProtocol (
                        Handles[Index],
                        &gEfiSimpleTextOutProtocolGuid,
                        (VOID **)&Out
                        )) &&
              (Out != NULL))
          {
            gST->ConsoleOutHandle = Handles[Index];
            gST->ConOut           = Out;
            GU_NOTE ("post-FLR: rebound ConOut handle=%p\n", Handles[Index]);
            break;
          }
        }
        FreePool (Handles);
      }
    }
  }

  gBS->Stall (100000);
  GU_NOTE ("post-FLR: reconnect GOP done\n");
  return EFI_SUCCESS;
}

EFI_STATUS
TuPciFlr (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT8       CapPtr;
  UINT16      DevCtl;
  UINT32      DevCap;
  UINT32      Identity;
  UINTN       Wait;

  Status = TuPciFindExpCap (PciIo, &CapPtr);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Device Capabilities +4: Function Level Reset Capable is bit 28. */
  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, CapPtr + 4, 1, &DevCap);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if ((DevCap & BIT28) == 0) {
    GU_ERROR ("PCIe FLR not supported by DEVCAP=%#x\n", DevCap);
    return EFI_UNSUPPORTED;
  }

  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, CapPtr + 8, 1, &DevCtl);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  DevCtl |= BIT15; /* Initiate Function Level Reset */
  Status  = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint16, CapPtr + 8, 1, &DevCtl);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Linux pcie_flr waits >=100ms then until config space returns. */
  gBS->Stall (100000);
  Status = TuPciWaitTrpndClear (PciIo, CapPtr);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Wait = 0; Wait < 50; Wait++) {
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint32, 0, 1, &Identity);
    if (!EFI_ERROR (Status) && Identity != 0xffffffffu && Identity != 0) {
      GU_NOTE ("PCIe FLR complete: identity=%#x\n", Identity);
      return EFI_SUCCESS;
    }
    gBS->Stall (20000);
  }
  GU_ERROR ("PCIe FLR config space did not return\n");
  return EFI_TIMEOUT;
}
