/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_PCI_H
#define TU_PCI_H

#include <Uefi.h>
#include <Protocol/PciIo.h>

#define TU_PCI_VENDOR_NVIDIA  0x10DE
#define TU_PCI_CLASS_DISPLAY  0x03

#define TU_TURING_FEAT_PLM      0x00409650u
#define TU_TURING_FEAT_SS0      0x00409664u
#define TU_TURING_FEAT_SS1      0x0040966cu
#define TU_TURING_FEAT_READOUT  0x00409668u
#define TU_AMPERE_FEAT_PLM      0x00823804u
#define TU_AMPERE_FEAT_SS0      0x0082381cu
#define TU_AMPERE_FEAT_SS1      0x00823820u
#define TU_AMPERE_FEAT_READOUT  0x00823818u
#define TU_SS0_FULL             0x88888888u
#define TU_SS1_FULL             0x8u

typedef enum {
  TuArchNone = 0,
  TuArchTuring,
  TuArchAmpere,
  TuArchAda
} TU_ARCH;

typedef struct {
  EFI_PCI_IO_PROTOCOL  *PciIo;
  EFI_HANDLE           Handle;
  UINT16               VendorId;
  UINT16               DeviceId;
  UINTN                Segment;
  UINTN                Bus;
  UINTN                Device;
  UINTN                Function;
  UINT8                BarIndex;
  TU_ARCH              Arch;
  BOOLEAN              UnlockSupported;
  BOOLEAN              FeatValid;
  UINT32               FeatPlm;
  UINT32               FeatSs0;
  UINT32               FeatSs1;
  UINT32               FeatReadout;
} TU_PCI_GPU;

BOOLEAN
TuPciIsSupportedDeviceId (
  IN UINT16  DeviceId
  );

EFI_STATUS
TuPciScan (
  OUT TU_PCI_GPU  **Gpus,
  OUT UINTN       *Count
  );

VOID
TuPciFreeScan (
  IN TU_PCI_GPU  *Gpus
  );

EFI_STATUS
TuPciEnableMmioMaster (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  );

EFI_STATUS
TuPciClearMaster (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  );

VOID
TuPciReadFeat (
  IN OUT TU_PCI_GPU  *Gpu
  );

VOID
TuPciLogHandoff (
  IN TU_PCI_GPU    *Gpus,
  IN UINTN         Count,
  IN CONST CHAR8   *Phase
  );

VOID
TuPciConnectInput (
  VOID
  );

UINTN
TuPciConnectMassStorage (
  VOID
  );

UINT32
TuPciMemRead32 (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex,
  IN UINT32               Offset
  );

VOID
TuPciMemWrite32 (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex,
  IN UINT32               Offset,
  IN UINT32               Value
  );

EFI_STATUS
TuPciMemReadBuffer (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT8                BarIndex,
  IN UINT32               Offset,
  IN UINTN                Size,
  OUT VOID                *Buffer
  );

EFI_STATUS
TuPciFlr (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  );

EFI_STATUS
TuPciSaveConfig (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINT8               Saved[256]
  );

EFI_STATUS
TuPciRestoreConfig (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST UINT8          Saved[256]
  );

#endif
