// SPDX-License-Identifier: GPL-2.0-only
#include "TuCompat.h"
#include "TuConfig.h"
#include "TuFs.h"
#include "TuPci.h"
#include "TuUnlock.h"
#include "boot.h"
#include "diagnostic.h"
#include "gfw_eval.h"
#include "layout.h"
#include "prep.h"
#include "vbios.h"

#define GSP_BASE    0x00110000u
#define GSP_FBIF    0x00110600u
#define SEC_BASE    0x00840000u
#define SEC_FBIF    0x00840600u
#define SEC_FALCON2 0x00841000u

#define TU_FEAT_PLM     0x00409650u
#define TU_FEAT_SS0     0x00409664u
#define TU_FEAT_SS1     0x0040966cu
#define TU_FEAT_READOUT 0x00409668u
#define TU_SS0_UNLOCK   0x88888888u
#define TU_SS1_UNLOCK   0x8u

#define BOOTER_FILE     L"\\EFI\\turing-unlock\\firmware\\booter_load_tu102.bin"
#define V67_FILE        L"\\EFI\\turing-unlock\\firmware\\v67-tu102-610.43.03.bin"
#define GSP_FILE        L"\\EFI\\turing-unlock\\firmware\\gsp_tu10x.bin"
#define BOOT_FILE       L"\\EFI\\turing-unlock\\firmware\\gsp_boot_tu102.bin"
#define BOOT_DESC_FILE  L"\\EFI\\turing-unlock\\firmware\\gsp_boot_desc_tu102.bin"
#define SEC2_BL_FILE    L"\\EFI\\turing-unlock\\firmware\\sec2_bl_tu102.bin"

#define TU_V67_SIZE      0xfa00u
#define TU_SEC2_BL_SIZE  768u

struct tu_dma {
  VOID                  *Cpu;
  EFI_PHYSICAL_ADDRESS  Dma;
  UINTN                 Pages;
};

struct tu_page {
  VOID                  *Cpu;
  EFI_PHYSICAL_ADDRESS  Dma;
};

struct tu_work {
  EFI_HANDLE                         ImageHandle;
  TU_PCI_GPU                         *Gpu;
  EFI_PCI_IO_PROTOCOL                *PciIo;
  UINT8                              BarIndex;
  UINT8                              SavedCfg[256];
  BOOLEAN                            CfgSaved;
  BOOLEAN                            MasterOn;
  BOOLEAN                            GspStarted;
  BOOLEAN                            GspQuiesced;
  BOOLEAN                            SecStarted;
  BOOLEAN                            SecQuiesced;
  struct firmware                    BooterFw;
  struct firmware                    V67Fw;
  struct firmware                    GspFw;
  struct firmware                    BootFw;
  struct firmware                    DescFw;
  struct firmware                    BlFw;
  struct turing_unlock_span          ImageSpan;
  struct turing_unlock_booter_hdr    Hdr;
  struct turing_unlock_boot_desc     BootDesc;
  struct turing_unlock_radix_plan    Radix;
  struct turing_unlock_layout_in     LayoutIn;
  struct turing_unlock_layout_out    Layout;
  struct tu_dma                      BooterDma;
  struct tu_dma                      V67Dma;
  struct tu_dma                      MetaDma;
  struct tu_dma                      BootDma;
  struct tu_dma                      FwsecCode;
  struct tu_dma                      FwsecData;
  struct tu_page                     *Pages;
  u32                                PageCount;
  u32                                PagesAllocated;
  u32                                LastOff;
  u32                                LastVal;
};

STATIC
u32
rd (
  struct tu_work *w,
  u32            off
  )
{
  u32 v;

  v           = TuPciMemRead32 (w->PciIo, w->BarIndex, off);
  w->LastOff  = off;
  w->LastVal  = v;
  return v;
}

STATIC
VOID
wr (
  struct tu_work *w,
  u32            off,
  u32            val
  )
{
  w->LastOff = off;
  w->LastVal = val;
  TuPciMemWrite32 (w->PciIo, w->BarIndex, off, val);
}

STATIC
BOOLEAN
bad (
  u32 v
  )
{
  return v == ~0u || (v & 0xffff0000u) == 0xbadf0000u;
}

STATIC
VOID
dma_wmb (
  struct tu_work *w,
  VOID           *Cpu,
  UINTN          Size
  )
{
  MemoryFence ();
  if (Cpu != NULL && Size != 0) {
    WriteBackDataCacheRange (Cpu, Size);
  }
  w->PciIo->Flush (w->PciIo);
}

STATIC
int
poll (
  struct tu_work *w,
  u32            off,
  u32            mask,
  u32            expect,
  unsigned int   us,
  BOOLEAN        Fatal
  )
{
  UINTN Steps;
  UINTN i;
  u32   v;

  Steps = us / 50;
  if (Steps == 0) {
    Steps = 1;
  }
  for (i = 0; i < Steps; i++) {
    v = rd (w, off);
    if (bad (v)) {
      return GU_DIAG (-EIO);
    }
    if ((v & mask) == expect) {
      return 0;
    }
    gBS->Stall (50);
  }
  if (!Fatal) {
    GU_NOTE ("poll tolerated timeout: reg=%#x last=%#x want=%#x&%#x\n",
             off, v, expect, mask);
    return -ETIMEDOUT;
  }
  return GU_DIAG (-ETIMEDOUT);
}

STATIC
int
wait_gfw (
  struct tu_work *w
  )
{
  UINTN i;
  int   ret;

  for (i = 0; i < (GU_GFW_TIMEOUT_US / 1000) + 1; i++) {
    struct turing_unlock_gfw_sample sample;

    ZeroMem (&sample, sizeof (sample));
    sample.config_ok = TRUE;
    sample.plm       = rd (w, GU_GFW_PLM_OFFSET);
    sample.cpuctl    = rd (w, GU_GFW_CPUCTL_OFFSET);
    if (!bad (sample.plm) && (sample.plm & GU_GFW_PLM_LEVEL0)) {
      sample.boot = rd (w, GU_GFW_BOOT_OFFSET);
    }
    if (bad (sample.plm) || bad (sample.boot) || bad (sample.cpuctl)) {
      ret = -EAGAIN;
    } else {
      ret = turing_unlock_gfw_assess (&sample);
    }
    if (!ret) {
      GU_NOTE ("GFW boot complete: plm=%#x boot=%#x cpuctl=%#x\n",
               sample.plm, sample.boot, sample.cpuctl);
      return 0;
    }
    if (ret != -EAGAIN) {
      return GU_DIAG (ret);
    }
    gBS->Stall (1000);
  }
  return GU_DIAG (-ETIMEDOUT);
}

STATIC
int
alloc_dma (
  struct tu_work *w,
  CONST CHAR8    *Name,
  UINTN          Size,
  struct tu_dma  *Out
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Addr;

  ZeroMem (Out, sizeof (*Out));
  if (Size == 0) {
    return GU_DIAG (-EINVAL);
  }
  Out->Pages = EFI_SIZE_TO_PAGES (Size);
  Addr       = 0xFFFFFFFF;
  Status     = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, Out->Pages, &Addr);
  if (EFI_ERROR (Status)) {
    GU_ERROR ("dma alloc failed: %a %u bytes status=%x\n", Name, (unsigned)Size, Status);
    Out->Pages = 0;
    return GU_DIAG (-ENOMEM);
  }
  Out->Cpu = (VOID *)(UINTN)Addr;
  Out->Dma = Addr;
  ZeroMem (Out->Cpu, EFI_PAGES_TO_SIZE (Out->Pages));
  return 0;
}

STATIC
VOID
free_dma (
  struct tu_dma *Buf
  )
{
  if (Buf->Pages != 0 && Buf->Cpu != NULL) {
    gBS->FreePages (Buf->Dma, Buf->Pages);
  }
  ZeroMem (Buf, sizeof (*Buf));
}

STATIC
VOID
free_fw (
  struct firmware *Fw
  )
{
  if (Fw->data != NULL) {
    FreePool ((VOID *)Fw->data);
  }
  Fw->data = NULL;
  Fw->size = 0;
}

STATIC
int
load_fw (
  struct tu_work  *w,
  CONST CHAR16    *Path,
  struct firmware *Fw
  )
{
  EFI_STATUS Status;
  VOID       *Buffer;
  UINTN      Size;

  Buffer = NULL;
  Size   = 0;
  Status = TuFsReadFile (w->ImageHandle, Path, &Buffer, &Size);
  if (EFI_ERROR (Status)) {
    GU_ERROR ("firmware missing (status=%x)\n", Status);
    return GU_DIAG (-ENOENT);
  }
  Fw->data = Buffer;
  Fw->size = Size;
  GU_NOTE ("firmware loaded bytes=%u\n", (unsigned)Size);
  return 0;
}

STATIC
int
flcn_reset (
  struct tu_work *w,
  u32            base
  )
{
  u32          engine;
  unsigned int i;
  int          ret;

  engine = rd (w, base + 0x3c0);
  if (bad (engine)) {
    return GU_DIAG (-ENODEV);
  }
  wr (w, base + 0x3c0, engine | 1);
  for (i = 0; i < 10; i++) {
    (VOID)rd (w, base + 0x3c0);
  }
  wr (w, base + 0x3c0, engine & ~1u);
  for (i = 0; i < 10; i++) {
    (VOID)rd (w, base + 0x3c0);
  }
  ret = poll (w, base + 0x10c, 0x6, 0, 1000000, TRUE);
  if (ret) {
    return GU_DIAG (ret);
  }
  wr (w, base + 0x84, rd (w, 0));
  GU_NOTE ("flcn reset done: base=%#x\n", base);
  return 0;
}

STATIC
VOID
flcn_pio_imem (
  struct tu_work *w,
  u32            base,
  u32            dst,
  const VOID     *src,
  u32            size,
  BOOLEAN        secure,
  u32            tag
  )
{
  const u32 *words = src;
  u32       i, n = size >> 2;
  u32       c = ((dst >> 2) & 0x3fu) | (((dst >> 8) & 0xffu) << 8) | (1u << 24);

  if (secure) {
    c |= 1u << 28;
  }
  wr (w, base + 0x180, c);
  tag >>= 8;
  for (i = 0; i < n; i++) {
    if (!(i & 63)) {
      wr (w, base + 0x188, tag++);
    }
    wr (w, base + 0x184, words[i]);
  }
}

STATIC
VOID
flcn_pio_dmem (
  struct tu_work *w,
  u32            base,
  u32            dst,
  const VOID     *src,
  u32            size
  )
{
  const u32 *words = src;
  u32       i, n = size >> 2;
  u32       c = ((dst >> 2) & 0x3fu) | (((dst >> 8) & 0xffu) << 8) | (1u << 24);

  wr (w, base + 0x1c0, c);
  for (i = 0; i < n; i++) {
    wr (w, base + 0x1c4, words[i]);
  }
}

struct tu_bl_dmem_desc {
  u32 reserved[4];
  u32 signature[4];
  u32 ctx_dma;
  u32 code_dma_lo;
  u32 code_dma_hi;
  u32 ns_code_off;
  u32 ns_code_size;
  u32 sec_code_off;
  u32 sec_code_size;
  u32 code_entry_point;
  u32 data_dma_lo;
  u32 data_dma_hi;
  u32 data_size;
  u32 argc;
  u32 argv;
};

STATIC
int
gsp_fwsec_frts (
  struct tu_work *w
  )
{
  size_t                     mapped;
  u8                         *rom = NULL;
  struct turing_unlock_fwsec fw;
  struct tu_bl_dmem_desc     bldesc;
  u32                        trans, hwcfg, imem_blks, dst_blk, cpuctl, scratch, wpr_lo, wpr_hi;
  u64                        frts = w->Layout.frts_offset;
  int                        ret;

  ZeroMem (&fw, sizeof (fw));
  ZeroMem (&bldesc, sizeof (bldesc));
  wpr_hi = rd (w, 0x1fa828);
  wpr_lo = rd (w, 0x1fa824);
  if ((wpr_hi >> 4) && (wpr_lo >> 4) == (u32)(frts >> 12)) {
    GU_NOTE ("FWSEC: WPR2 already up at %#x:%#x\n", wpr_hi, wpr_lo);
    return 0;
  }
  mapped = 1024 * 1024;
  rom    = AllocateZeroPool (mapped);
  if (rom == NULL) {
    return GU_DIAG (-ENOMEM);
  }
  ret = EFI_ERROR (TuPciMemReadBuffer (w->PciIo, w->BarIndex, 0x300000, mapped, rom)) ? -EIO : 0;
  if (ret) {
    goto out;
  }
  ret = turing_unlock_vbios_fwsec (rom, mapped, &fw);
  if (ret) {
    goto out;
  }
  if (fw.desc_version != 2) {
    ret = GU_DIAG (-EOPNOTSUPP);
    goto out;
  }
  ret = turing_unlock_fwsec_patch_frts (rom + fw.image_off, &fw, frts);
  if (ret) {
    goto out;
  }
  w->FwsecCode.Pages = 0;
  ret = alloc_dma (w, "fwsec-code", (fw.imem_size + 255u) & ~255u, &w->FwsecCode);
  if (ret) {
    goto out;
  }
  ret = alloc_dma (w, "fwsec-data", (fw.dmem_size + 255u) & ~255u, &w->FwsecData);
  if (ret) {
    goto out;
  }
  CopyMem ((u8 *)w->FwsecCode.Cpu + fw.imem_pa, rom + fw.image_off, fw.imem_size);
  CopyMem ((u8 *)w->FwsecData.Cpu + fw.dmem_pa, rom + fw.image_off + fw.data_off, fw.dmem_size);
  dma_wmb (w, w->FwsecCode.Cpu, EFI_PAGES_TO_SIZE (w->FwsecCode.Pages));
  dma_wmb (w, w->FwsecData.Cpu, EFI_PAGES_TO_SIZE (w->FwsecData.Pages));

  bldesc.ctx_dma          = 4;
  bldesc.code_dma_lo      = lower_32_bits (w->FwsecCode.Dma);
  bldesc.code_dma_hi      = upper_32_bits (w->FwsecCode.Dma);
  bldesc.ns_code_off      = fw.imem_pa;
  bldesc.ns_code_size     = fw.imem_ns_size;
  bldesc.sec_code_off     = fw.imem_sec_pa;
  bldesc.sec_code_size    = fw.imem_sec_size;
  bldesc.code_entry_point = 0;
  bldesc.data_dma_lo      = lower_32_bits (w->FwsecData.Dma);
  bldesc.data_dma_hi      = upper_32_bits (w->FwsecData.Dma);
  bldesc.data_size        = fw.dmem_size;

  ret = flcn_reset (w, GSP_BASE);
  if (ret) {
    goto out;
  }
  trans = rd (w, GSP_FBIF + 0x24);
  if (bad (trans)) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  wr (w, GSP_FBIF + 0x24, trans | (1u << 7));
  wr (w, GSP_BASE + 0x10c, 0);
  trans = rd (w, GSP_FBIF + 4 * 4);
  if (bad (trans)) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  wr (w, GSP_FBIF + 4 * 4, (trans & ~7u) | 5u);
  flcn_pio_dmem (w, GSP_BASE, 0, &bldesc, sizeof (bldesc));
  hwcfg     = rd (w, GSP_BASE + 0x108);
  imem_blks = hwcfg & 0x1ffu;
  dst_blk   = imem_blks - 2u;
  GU_NOTE ("FWSEC loader: hwcfg=%#x imem_blks=%u dst_blk=%u frts=%llx\n",
           hwcfg, imem_blks, dst_blk, (unsigned long long)frts);
  flcn_pio_imem (w, GSP_BASE, dst_blk << 8, w->BlFw.data, 0x200u, FALSE, 0xfd00u);
  wr (w, GSP_BASE + 0x104, 0xfd00u);
  wr (w, GSP_BASE + 0x40, 0);
  wr (w, GSP_BASE + 0x44, 0);
  cpuctl = rd (w, GSP_BASE + 0x100);
  if (bad (cpuctl)) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  w->GspStarted = TRUE;
  if (cpuctl & (1u << 6)) {
    wr (w, GSP_BASE + 0x130, 1u << 1);
  } else {
    wr (w, GSP_BASE + 0x100, 1u << 1);
  }
  ret     = poll (w, GSP_BASE + 0x100, 1u << 4, 1u << 4, 5000000, TRUE);
  scratch = rd (w, 0x001400 + 0x0e * 4);
  wpr_lo  = rd (w, 0x1fa824);
  wpr_hi  = rd (w, 0x1fa828);
  GU_NOTE ("FWSEC done: ret=%d scratch0e=%#x wpr2=%#x:%#x\n", ret, scratch, wpr_hi, wpr_lo);
  if (ret) {
    goto out;
  }
  if ((scratch >> 16) || !(wpr_hi >> 4) || (wpr_lo >> 4) != (u32)(frts >> 12)) {
    GU_ERROR ("FWSEC FRTS check failed: scratch=%#x wpr2=%#x:%#x want_lo=%#x\n",
              scratch, wpr_hi, wpr_lo, (u32)(frts >> 12));
    ret = GU_DIAG (-EIO);
  }
out:
  if (rom != NULL) {
    FreePool (rom);
  }
  return ret;
}

STATIC
int
sec_hs_fire (
  struct tu_work *w
  )
{
  const struct turing_unlock_booter_hdr *h = &w->Hdr;
  u32 trans, cpuctl = 0, plm = 0, mb0, mb1, saw_running = 0;
  UINTN i;
  int ret;

  ret = flcn_reset (w, SEC_BASE);
  if (ret) {
    return GU_DIAG (ret);
  }
  ret = poll (w, SEC_BASE + 0x10c, 0x6, 0, 1000000, TRUE);
  if (ret) {
    return GU_DIAG (ret);
  }
  trans = rd (w, SEC_FBIF + 0x24);
  if (bad (trans)) {
    return GU_DIAG (-EIO);
  }
  wr (w, SEC_FBIF + 0x24, trans | (1u << 7));
  wr (w, SEC_BASE + 0x10c, 0);
  trans = rd (w, SEC_FBIF);
  if (bad (trans)) {
    return GU_DIAG (-ENODEV);
  }
  wr (w, SEC_FBIF, (trans & ~7u) | 5u);
  trans = rd (w, SEC_FBIF);
  if (bad (trans) || (trans & 7u) != 5u) {
    return GU_DIAG (-EIO);
  }
  dma_wmb (w, w->BooterDma.Cpu, TU_BOOTER_IMAGE_SIZE);

  flcn_pio_imem (w, SEC_BASE, 0, (u8 *)w->BooterDma.Cpu + h->os_code_off, h->os_code_size, FALSE, h->os_code_off);
  flcn_pio_imem (w, SEC_BASE, (h->os_code_size + 255u) & ~255u,
                 (u8 *)w->BooterDma.Cpu + h->app_code_off, h->app_code_size, TRUE, h->app_code_off);
  flcn_pio_dmem (w, SEC_BASE, 0, (u8 *)w->BooterDma.Cpu + h->os_data_off, h->os_data_size);

  wr (w, SEC_BASE + 0x104, 0);
  wr (w, SEC_BASE + 0x40, lower_32_bits (w->MetaDma.Dma));
  wr (w, SEC_BASE + 0x44, upper_32_bits (w->MetaDma.Dma));
  cpuctl = rd (w, SEC_BASE + 0x100);
  if (bad (cpuctl)) {
    return GU_DIAG (-EIO);
  }
  GU_NOTE ("sec fire: cpuctl=%#x meta=%llx\n", cpuctl, (unsigned long long)w->MetaDma.Dma);
  w->SecStarted = TRUE;
  if (cpuctl & (1u << 6)) {
    wr (w, SEC_BASE + 0x130, 1u << 1);
  } else {
    wr (w, SEC_BASE + 0x100, 1u << 1);
  }
  for (i = 0; i < 5000000 / 2; i++) {
    cpuctl = rd (w, SEC_BASE + 0x100);
    if (bad (cpuctl)) {
      return GU_DIAG (-EIO);
    }
    if (cpuctl & (1u << 4)) {
      break;
    }
    if (cpuctl != 0x20) {
      saw_running = 1;
    }
    gBS->Stall (2);
    if (i + 1 == 5000000 / 2) {
      GU_ERROR ("sec fire timeout: cpuctl=%#x ever_ran=%u\n", cpuctl, saw_running);
      return GU_DIAG (-ETIMEDOUT);
    }
  }
  plm = rd (w, TU_FEAT_PLM);
  mb0 = rd (w, SEC_BASE + 0x40);
  mb1 = rd (w, SEC_BASE + 0x44);
  GU_NOTE ("sec done: halted=%u plm=%#x mailbox=%#x:%#x\n",
           !!(cpuctl & (1u << 4)), plm, mb1, mb0);
  if (plm != ~0u || mb0) {
    GU_ERROR ("sec fire failed: halted=%u mb0=%#x plm=%#x\n",
              !!(cpuctl & (1u << 4)), mb0, plm);
    return -EIO;
  }
  return 0;
}

STATIC
int
build_radix (
  struct tu_work *w
  )
{
  const struct turing_unlock_radix_plan *r = &w->Radix;
  u64  *addrs;
  u32  i, level, first = 0;
  int  ret = 0;

  w->PageCount = (u32)(r->table_pages + r->pages[3]);
  w->Pages     = AllocateZeroPool ((UINTN)w->PageCount * sizeof (*w->Pages));
  addrs        = AllocateZeroPool ((UINTN)w->PageCount * sizeof (*addrs));
  if (w->Pages == NULL || addrs == NULL) {
    if (addrs != NULL) {
      FreePool (addrs);
    }
    return GU_DIAG (-ENOMEM);
  }
  for (i = 0; i < w->PageCount; i++) {
    EFI_PHYSICAL_ADDRESS Addr = 0xFFFFFFFF;
    EFI_STATUS           St;

    St = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, 1, &Addr);
    if (EFI_ERROR (St)) {
      ret = GU_DIAG (-ENOMEM);
      goto out;
    }
    w->Pages[i].Cpu = (VOID *)(UINTN)Addr;
    w->Pages[i].Dma = Addr;
    w->PagesAllocated++;
    addrs[i] = Addr;
    ZeroMem (w->Pages[i].Cpu, GU_RADIX_PAGE);
    if (i >= r->table_pages) {
      u64    off = (u64)(i - r->table_pages) * GU_RADIX_PAGE;
      size_t n   = min_t (u64, GU_RADIX_PAGE, w->ImageSpan.size - off);

      CopyMem (w->Pages[i].Cpu, w->GspFw.data + w->ImageSpan.offset + off, n);
    }
    dma_wmb (w, w->Pages[i].Cpu, GU_RADIX_PAGE);
  }
  for (level = 0; level < 3; level++) {
    u32 child = first + (u32)r->pages[level];

    for (i = 0; i < r->pages[level]; i++) {
      u32 at = i * (GU_RADIX_PAGE / 8);
      u32 n  = (u32)min_t (u64, GU_RADIX_PAGE / 8, r->pages[level + 1] - at);

      ret = turing_unlock_radix_page (w->Pages[first + i].Cpu, addrs + child + at, n);
      if (ret) {
        goto out;
      }
      dma_wmb (w, w->Pages[first + i].Cpu, GU_RADIX_PAGE);
    }
    first = child;
  }
out:
  FreePool (addrs);
  return ret;
}

STATIC
int
tu_flr (
  struct tu_work *w
  )
{
  EFI_STATUS Status;

  if (w->MasterOn) {
    TuPciClearMaster (w->PciIo);
    w->MasterOn = FALSE;
  }
  Status = TuPciFlr (w->PciIo);
  if (EFI_ERROR (Status)) {
    return GU_DIAG (-EIO);
  }
  if (w->CfgSaved) {
    TuPciRestoreConfig (w->PciIo, w->SavedCfg);
  }
  w->GspQuiesced = w->GspStarted;
  w->SecQuiesced = w->SecStarted;
  return 0;
}

STATIC
BOOLEAN
firmware_quiesced (
  const struct tu_work *w
  )
{
  return (!w->GspStarted || w->GspQuiesced) &&
         (!w->SecStarted || w->SecQuiesced);
}

INTN
TuUnlockGpu (
  IN EFI_HANDLE  ImageHandle,
  IN TU_PCI_GPU  *Gpu
  )
{
  struct tu_work *w;
  u32            plm, ss0, ss1, readout, wpr_lo, wpr_hi, boot_state;
  int            ret;
  UINTN          i;

  w = AllocateZeroPool (sizeof (*w));
  if (w == NULL) {
    return -ENOMEM;
  }
  w->ImageHandle = ImageHandle;
  w->Gpu         = Gpu;
  w->PciIo       = Gpu->PciIo;
  w->BarIndex    = Gpu->BarIndex;

  ret = load_fw (w, BOOTER_FILE, &w->BooterFw);
  if (ret) {
    goto out;
  }
  ret = load_fw (w, V67_FILE, &w->V67Fw);
  if (ret) {
    goto out;
  }
  if (w->V67Fw.size != TU_V67_SIZE) {
    ret = GU_DIAG (-EBADMSG);
    goto out;
  }
  ret = load_fw (w, GSP_FILE, &w->GspFw);
  if (ret) {
    goto out;
  }
  ret = load_fw (w, BOOT_FILE, &w->BootFw);
  if (ret) {
    goto out;
  }
  ret = load_fw (w, BOOT_DESC_FILE, &w->DescFw);
  if (ret) {
    goto out;
  }
  ret = load_fw (w, SEC2_BL_FILE, &w->BlFw);
  if (ret) {
    goto out;
  }
  if (w->BlFw.size != TU_SEC2_BL_SIZE) {
    ret = GU_DIAG (-EBADMSG);
    goto out;
  }
  ret = turing_unlock_booter_parse (&w->BooterFw, &w->Hdr);
  if (ret) {
    goto out;
  }
  ret = turing_unlock_elf_section (w->GspFw.data, w->GspFw.size, ".fwimage", &w->ImageSpan);
  if (ret) {
    goto out;
  }
  if (!w->ImageSpan.size || w->ImageSpan.size > (128ull << 20)) {
    ret = GU_DIAG (-EINVAL);
    goto out;
  }
  ret = turing_unlock_boot_desc_v4 (w->DescFw.data, w->DescFw.size, (u32)w->BootFw.size, &w->BootDesc);
  if (ret) {
    goto out;
  }
  ret = turing_unlock_radix_plan (w->ImageSpan.size, &w->Radix);
  if (ret) {
    goto out;
  }

  if (EFI_ERROR (TuPciSaveConfig (w->PciIo, w->SavedCfg))) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  w->CfgSaved = TRUE;
  if (EFI_ERROR (TuPciEnableMmioMaster (w->PciIo))) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  w->MasterOn = TRUE;

  plm        = rd (w, TU_FEAT_PLM);
  ss0        = rd (w, TU_FEAT_SS0);
  ss1        = rd (w, TU_FEAT_SS1);
  wpr_lo     = rd (w, 0x1fa824);
  wpr_hi     = rd (w, 0x1fa828);
  boot_state = rd (w, 0x1180f8);
  GU_NOTE ("entry: plm=%#x ss0=%#x ss1=%#x wpr2=%#x:%#x boot=%#x\n",
           plm, ss0, ss1, wpr_hi, wpr_lo, boot_state);
  if ((wpr_hi >> 4) || (boot_state & 0xfff00000u)) {
    GU_NOTE ("reset required, doing PCIe FLR first\n");
    ret = tu_flr (w);
    if (ret) {
      goto out;
    }
    gBS->Stall (50000);
    if (EFI_ERROR (TuPciEnableMmioMaster (w->PciIo))) {
      ret = GU_DIAG (-EIO);
      goto out;
    }
    w->MasterOn = TRUE;
  }

  {
    u32     range_reg = 0, vga_reg, mag, scale;
    u64     fb_bytes;
    UINTN   wait;

    for (wait = 0; wait < 150; wait++) {
      range_reg = rd (w, 0x100ce0);
      if (turing_unlock_fb_range_ready (range_reg)) {
        break;
      }
      gBS->Stall (20000);
    }
    if (!turing_unlock_fb_range_ready (range_reg)) {
      ret = GU_DIAG (-ETIMEDOUT);
      goto out;
    }
    ret = wait_gfw (w);
    if (ret) {
      goto out;
    }
    vga_reg = rd (w, 0x625f04);
    if (bad (vga_reg)) {
      ret = GU_DIAG (-EIO);
      goto out;
    }
    mag      = (range_reg >> 4) & 0x3fu;
    scale    = range_reg & 0xfu;
    fb_bytes = (u64)mag << (scale + 20);
    if (range_reg & (1u << 30)) {
      fb_bytes = fb_bytes / 16 * 15;
    }
    GU_NOTE ("fb layout regs: range=%#x vga=%#x\n", range_reg, vga_reg);
    w->LayoutIn.usable_fb     = fb_bytes;
    w->LayoutIn.vga_base      = fb_bytes - (1ull << 20);
    if (vga_reg & (1u << 3)) {
      w->LayoutIn.vga_base = (u64)(vga_reg >> 8) << 16;
      if (w->LayoutIn.vga_base < fb_bytes - (1ull << 20)) {
        w->LayoutIn.vga_base = fb_bytes - 0x20000ull;
      }
    }
    w->LayoutIn.mmu_lock_valid = FALSE;
    w->LayoutIn.frts_size      = 1ull << 20;
    w->LayoutIn.boot_size      = w->BootDesc.image_size;
    w->LayoutIn.image_size     = w->ImageSpan.size;
    w->LayoutIn.nonwpr_size    = 1ull << 20;
    w->LayoutIn.heap_request   = 88ull << 20;
    ret = turing_unlock_layout (&w->LayoutIn, &w->Layout);
    if (ret) {
      goto out;
    }
  }

  ret = gsp_fwsec_frts (w);
  if (ret) {
    goto out;
  }
  ret = flcn_reset (w, GSP_BASE);
  if (ret) {
    goto out;
  }
  w->GspQuiesced = w->GspStarted;

  ret = alloc_dma (w, "booter", TU_BOOTER_IMAGE_SIZE, &w->BooterDma);
  if (ret) {
    goto out;
  }
  CopyMem (w->BooterDma.Cpu, w->BooterFw.data + 72, TU_BOOTER_IMAGE_SIZE);
  ret = turing_unlock_booter_patch (
          w->BooterDma.Cpu,
          TU_BOOTER_IMAGE_SIZE,
          &w->Hdr,
          w->BooterFw.data + 72 + TU_BOOTER_IMAGE_SIZE,
          TU_BOOTER_SIG_TOTAL,
          0
          );
  if (ret) {
    goto out;
  }
  dma_wmb (w, w->BooterDma.Cpu, TU_BOOTER_IMAGE_SIZE);
  ret = alloc_dma (w, "v67", TU_V67_SIZE, &w->V67Dma);
  if (ret) {
    goto out;
  }
  CopyMem (w->V67Dma.Cpu, w->V67Fw.data, TU_V67_SIZE);
  dma_wmb (w, w->V67Dma.Cpu, TU_V67_SIZE);
  ret = alloc_dma (w, "meta", GU_WPR_META_SIZE, &w->MetaDma);
  if (ret) {
    goto out;
  }
  ret = alloc_dma (w, "boot", w->BootDesc.image_size, &w->BootDma);
  if (ret) {
    goto out;
  }
  CopyMem (w->BootDma.Cpu, w->BootFw.data, w->BootDesc.image_size);
  dma_wmb (w, w->BootDma.Cpu, w->BootDesc.image_size);
  ret = build_radix (w);
  if (ret) {
    goto out;
  }
  {
    struct turing_unlock_wpr_args args;

    ZeroMem (&args, sizeof (args));
    args.fb_size        = w->LayoutIn.usable_fb;
    args.vga_offset     = w->LayoutIn.vga_base;
    args.frts_size      = w->LayoutIn.frts_size;
    args.signature_dma  = w->V67Dma.Dma;
    args.signature_size = TU_V67_SIZE;
    args.radix_dma      = w->Pages[0].Dma;
    args.image_size     = w->ImageSpan.size;
    args.boot_dma       = w->BootDma.Dma;
    args.boot           = w->BootDesc;
    ret = turing_unlock_wpr_meta_pack (w->MetaDma.Cpu, &w->Layout, &args);
    if (ret) {
      goto out;
    }
    dma_wmb (w, w->MetaDma.Cpu, GU_WPR_META_SIZE);
  }

  ret = sec_hs_fire (w);
  if (ret) {
    goto out;
  }
  w->SecQuiesced = w->SecStarted;
  plm = rd (w, TU_FEAT_PLM);
  if (plm != ~0u) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  wr (w, TU_FEAT_SS0, TU_SS0_UNLOCK);
  wr (w, TU_FEAT_SS1, TU_SS1_UNLOCK);
  ss0     = rd (w, TU_FEAT_SS0);
  ss1     = rd (w, TU_FEAT_SS1);
  readout = rd (w, TU_FEAT_READOUT);
  GU_NOTE ("unlock result: ss0=%#x ss1=%#x readout=%#x\n", ss0, ss1, readout);
  if (ss0 != TU_SS0_UNLOCK || ss1 != TU_SS1_UNLOCK || readout) {
    ret = GU_DIAG (-EIO);
  } else {
    GU_NOTE ("TU102 FEAT UNLOCKED\n");
  }

out:
  if (ret && (w->GspStarted || w->SecStarted)) {
    (VOID)tu_flr (w);
  }
  if (!firmware_quiesced (w)) {
    GU_ERROR ("firmware did not stop; leaving DMA until power off\n");
    if (!ret) {
      ret = -EBUSY;
    }
    FreePool (w);
    return ret;
  }
  if (w->MasterOn) {
    TuPciClearMaster (w->PciIo);
    w->MasterOn = FALSE;
  }
  if (w->Pages != NULL) {
    for (i = 0; i < w->PagesAllocated; i++) {
      gBS->FreePages (w->Pages[i].Dma, 1);
    }
    FreePool (w->Pages);
  }
  free_dma (&w->BooterDma);
  free_dma (&w->V67Dma);
  free_dma (&w->MetaDma);
  free_dma (&w->BootDma);
  free_dma (&w->FwsecCode);
  free_dma (&w->FwsecData);
  free_fw (&w->BooterFw);
  free_fw (&w->V67Fw);
  free_fw (&w->GspFw);
  free_fw (&w->BootFw);
  free_fw (&w->DescFw);
  free_fw (&w->BlFw);
  FreePool (w);
  return ret;
}
