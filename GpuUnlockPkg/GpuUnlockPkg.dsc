## @file
#  Build UnlockLoader-<Arch>.efi with EDK2 + VS2022 (BASE_NAME TuringUnlockLoader).
#
#  SPDX-License-Identifier: GPL-2.0-only
##

[Defines]
  PLATFORM_NAME                  = GpuUnlock
  PLATFORM_GUID                  = 9E4B0C71-2A58-4D06-9F11-6C3A8D25B440
  PLATFORM_VERSION               = 0.2
  DSC_SPECIFICATION              = 0x0001001B
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  # Overridden by: build -D GPU_UNLOCK_ARCH=TURING|AMPERE|ADA
  GPU_UNLOCK_ARCH                = TURING

!if $(GPU_UNLOCK_ARCH) == AMPERE
  OUTPUT_DIRECTORY               = Build/GpuUnlockAmpere
!elseif $(GPU_UNLOCK_ARCH) == ADA
  OUTPUT_DIRECTORY               = Build/GpuUnlockAda
!else
  OUTPUT_DIRECTORY               = Build/GpuUnlock
!endif

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  CacheMaintenanceLib|MdePkg/Library/BaseCacheMaintenanceLib/BaseCacheMaintenanceLib.inf
  CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

[LibraryClasses.X64]
  # MSVC /O1 turns struct foo = { 0 } into a memset() call. This is not
  # pulled in for X64 by MdeLibs.dsc.inc (only AARCH64/RISCV64/LOONGARCH64).
  NULL|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x27
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x07

[Components]
!if $(GPU_UNLOCK_ARCH) == AMPERE
  GpuUnlockPkg/Application/Loader/LoaderAmpere.inf
!elseif $(GPU_UNLOCK_ARCH) == ADA
  GpuUnlockPkg/Application/Loader/LoaderAda.inf
!else
  GpuUnlockPkg/Application/Loader/Loader.inf
!endif
