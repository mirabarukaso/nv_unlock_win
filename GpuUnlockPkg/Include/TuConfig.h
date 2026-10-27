/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TU_CONFIG_H
#define TU_CONFIG_H

#include <Uefi.h>

#define TU_LOADER_MAGIC        "TURING_UNLOCK_LOADER_v1"
#define TU_LOADER_VERSION      "0.2.4"

#if defined(TU_BUILD_ARCH_TURING)
# if defined(TU_BUILD_ARCH_AMPERE) || defined(TU_BUILD_ARCH_ADA)
#  error "Build a single GPU architecture (Turing, Ampere, or Ada)."
# endif
#define TU_BUILD_ARCH_NAME     "Turing"
#elif defined(TU_BUILD_ARCH_AMPERE)
# if defined(TU_BUILD_ARCH_ADA)
#  error "Build a single GPU architecture (Turing, Ampere, or Ada)."
# endif
#define TU_BUILD_ARCH_NAME     "Ampere"
#elif defined(TU_BUILD_ARCH_ADA)
#define TU_BUILD_ARCH_NAME     "Ada"
#else
# error "Set GPU_UNLOCK_ARCH to TURING, AMPERE, or ADA (tools/Build-GpuUnlockEfi.ps1 -Arch)."
#endif
#define TU_DIR_NAME            L"\\EFI\\turing-unlock"
#define TU_CONFIG_NAME         L"\\EFI\\turing-unlock\\config.ini"
#define TU_LOG_NAME            L"\\EFI\\turing-unlock\\unlock.log"
#define TU_MS_BOOT_BACKUP      L"\\EFI\\turing-unlock\\bootmgfw_ms.efi"
#define TU_MS_BOOT_BACKUP_ALT  L"\\EFI\\Microsoft\\Boot\\bootmgfw_ms.efi"
#define TU_MS_BOOT_OFFICIAL    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
#define TU_FALLBACK_BOOT       L"\\EFI\\Boot\\bootx64.efi"
#define TU_FIRMWARE_DIR        L"\\EFI\\turing-unlock\\firmware"

typedef enum {
  TuActionSkip = 0,
  TuActionInject,
  TuActionEmergency
} TU_ACTION;

typedef struct {
  TU_ACTION    DefaultAction;
  UINTN        TimeoutSec;
  BOOLEAN      Verbose;
  BOOLEAN      NoChainload;
  BOOLEAN      ForceInject;
  BOOLEAN      ForceSkip;
  CHAR8        Targets[128];
} TU_CONFIG;

VOID
TuConfigInitDefaults (
  OUT TU_CONFIG  *Config
  );

EFI_STATUS
TuConfigLoad (
  IN EFI_HANDLE  ImageHandle,
  IN OUT TU_CONFIG  *Config
  );

EFI_STATUS
TuConfigParseLoadOptions (
  IN EFI_HANDLE     ImageHandle,
  IN OUT TU_CONFIG  *Config
  );

#endif
