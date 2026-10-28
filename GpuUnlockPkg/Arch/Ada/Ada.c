// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuArch.h"
#include "TuConfig.h"
#include "arch.h"
#include "prep.h"

/* Base Ada ID: admitted with targets=all (or an exact BDF). */
STATIC CONST UINT16 mAdaBaseIds[] = {
  GU_AD102_RTX_4090
};

typedef enum {
  AdaExpRtx4080 = 0,
  AdaExpAd103,
  AdaExpAd106,
  AdaExpRtx4090d
} ADA_EXP_FLAG;

/*
 * Experimental Ada IDs. Each needs its own config.ini opt-in. Exact ones must
 * be named by PCI BDF in targets, never via targets=all (upstream main.c).
 */
typedef struct {
  UINT16         DeviceId;
  BOOLEAN        Exact;
  ADA_EXP_FLAG   Flag;
  CONST CHAR8    *Name;
} ADA_EXPERIMENTAL;

STATIC CONST ADA_EXPERIMENTAL mAdaExperimental[] = {
  { GU_AD102_RTX_4090_D,        FALSE, AdaExpRtx4090d, "RTX 4090 D" },
  { GU_AD103_RTX_4080_SUPER,    TRUE,  AdaExpAd103,    "AD103 RTX 4080 SUPER" },
  { GU_AD103_RTX_4080,          TRUE,  AdaExpRtx4080,  "RTX 4080" },
  { GU_AD103_RTX_4070_TI_SUPER, TRUE,  AdaExpAd103,    "AD103" },
  { GU_AD106_RTX_4060_TI_16GB,  TRUE,  AdaExpAd106,    "AD106" },
};

STATIC
CONST ADA_EXPERIMENTAL *
AdaExperimental (
  IN UINT16  DeviceId
  )
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mAdaExperimental); Index++) {
    if (mAdaExperimental[Index].DeviceId == DeviceId) {
      return &mAdaExperimental[Index];
    }
  }
  return NULL;
}

STATIC
BOOLEAN
AdaBaseId (
  IN UINT16  DeviceId
  )
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mAdaBaseIds); Index++) {
    if (mAdaBaseIds[Index] == DeviceId) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC
BOOLEAN
AdaFlagEnabled (
  IN CONST TU_CONFIG  *Config,
  IN ADA_EXP_FLAG     Flag
  )
{
  switch (Flag) {
    case AdaExpRtx4080:
      return Config->Rtx4080Experimental;
    case AdaExpAd103:
      return Config->Ad103Experimental;
    case AdaExpAd106:
      return Config->Ad106Experimental;
    case AdaExpRtx4090d:
      return Config->Rtx4090dExperimental;
    default:
      return FALSE;
  }
}

BOOLEAN
TuAdaIsDeviceId (
  IN UINT16  DeviceId
  )
{
  if (AdaBaseId (DeviceId)) {
    return TRUE;
  }
  return AdaExperimental (DeviceId) != NULL;
}

STATIC
BOOLEAN
TargetIsAll (
  IN CONST CHAR8  *Targets
  )
{
  return Targets == NULL || Targets[0] == 0 || AsciiStriCmp (Targets, "all") == 0;
}

STATIC
BOOLEAN
TargetListsBdf (
  IN CONST CHAR8  *Targets,
  IN CONST CHAR8  *Name
  )
{
  CONST CHAR8  *Entry;
  CONST CHAR8  *End;
  UINTN        NameLen;
  UINTN        EntLen;

  if (TargetIsAll (Targets)) {
    return FALSE;
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

BOOLEAN
TuAdaAdmitUnlock (
  IN UINT16           DeviceId,
  IN CONST TU_CONFIG  *Config,
  IN CONST CHAR8      *BdfName
  )
{
  CONST ADA_EXPERIMENTAL  *Exp;

  if (Config == NULL || BdfName == NULL) {
    return FALSE;
  }

  Exp = AdaExperimental (DeviceId);
  if (Exp != NULL) {
    if (!AdaFlagEnabled (Config, Exp->Flag)) {
      TuLogWide (
        L"Skipping %a 10de:%04x (%a): enable the matching experimental flag in config.ini\r\n",
        BdfName,
        DeviceId,
        Exp->Name
        );
      return FALSE;
    }
    if (Exp->Exact) {
      if (TargetIsAll (Config->Targets) || !TargetListsBdf (Config->Targets, BdfName)) {
        TuLogWide (
          L"Skipping %a 10de:%04x (%a): experimental ID needs its exact PCI address in targets (not all)\r\n",
          BdfName,
          DeviceId,
          Exp->Name
          );
        return FALSE;
      }
      return TRUE;
    }
    /* Non-exact experimental (e.g. 4090 D): flag + targets=all or listed BDF. */
    if (TargetIsAll (Config->Targets) || TargetListsBdf (Config->Targets, BdfName)) {
      return TRUE;
    }
    TuLogWide (L"Skipping %a (not in targets)\r\n", BdfName);
    return FALSE;
  }

  if (!AdaBaseId (DeviceId)) {
    return FALSE;
  }
  if (TargetIsAll (Config->Targets) || TargetListsBdf (Config->Targets, BdfName)) {
    return TRUE;
  }
  TuLogWide (L"Skipping %a (not in targets)\r\n", BdfName);
  return FALSE;
}
