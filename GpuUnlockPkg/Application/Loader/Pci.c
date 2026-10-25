// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "TuPci.h"

#include <IndustryStandard/Pci.h>

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
    GU_NOTE ("NVIDIA PCI %04x:%04x handle=%p unlock=%u\n",
             Vendor, Device, Handles[Index], TuPciIsSupportedDeviceId (Device));
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

VOID
TuPciConnectInput (
  VOID
  )
{
  UINTN  UsbCount;
  UINTN  TextInBefore;
  UINTN  TextInNow;
  UINTN  Wait;

  TuPrint (L"Connecting USB input (fast POST)...\r\n");
  GU_NOTE ("connect input: USB HC + ConIn\n");

  TextInBefore = CountProtocolHandles (&gEfiSimpleTextInProtocolGuid);
  UsbCount     = ConnectUsbHostControllers ();
  if (UsbCount == 0) {
    ConnectByProtocol (&gEfiPciRootBridgeIoProtocolGuid);
    UsbCount = ConnectUsbHostControllers ();
  }
  TuPrint (L"USB host controllers: %u\r\n", UsbCount);
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

EFI_STATUS
TuPciRestoreConfig (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST UINT8          Saved[256]
  )
{
  return PciIo->Pci.Write (PciIo, EfiPciIoWidthUint8, 0, 256, (VOID *)Saved);
}

EFI_STATUS
TuPciFlr (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS Status;
  UINT8      CapPtr;
  UINT8      CapId;
  UINT16     DevCtl;
  UINT16     DevSta;
  UINTN      Wait;

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
      Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, CapPtr + 8, 1, &DevCtl);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      DevCtl |= BIT15;
      Status  = PciIo->Pci.Write (PciIo, EfiPciIoWidthUint16, CapPtr + 8, 1, &DevCtl);
      if (EFI_ERROR (Status)) {
        return Status;
      }
      gBS->Stall (100000);
      for (Wait = 0; Wait < 50; Wait++) {
        PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, CapPtr + 10, 1, &DevSta);
        if ((DevSta & BIT5) == 0) {
          return EFI_SUCCESS;
        }
        gBS->Stall (20000);
      }
      return EFI_TIMEOUT;
    }
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, CapPtr + 1, 1, &CapPtr);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    CapPtr &= 0xfc;
  }
  return EFI_UNSUPPORTED;
}
