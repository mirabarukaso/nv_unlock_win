// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pre-driver Ada V67 unlock: FWSEC-FRTS from the board ROM, AD102 Booter with
 * the four-PLM payload, SS0/SS1 selector writes, then FLR so the stock driver
 * starts from a clean halted GSP.
 */
#include "TuCompat.h"
#include "TuConfig.h"
#include "TuFs.h"
#include "TuPci.h"
#include "TuArch.h"
#include "arch.h"
#include "diagnostic.h"
#include "gfw_eval.h"
#include "layout.h"
#include "prep.h"
#include "vbios.h"

#include <IndustryStandard/Pci.h>

#define SEC2_BASE     0x00840000u
#define SEC2_FBIF     0x00840600u
#define SEC2_FALCON2  0x00841000u
#define GSP_BASE      0x00110000u
#define GSP_FBIF      0x00110600u
#define GSP_FALCON2   0x00111000u

#define GU_SS0_UNLOCK           0x88888888u
#define GU_SS1_UNLOCK           0x8u
#define GU_AD102_MARKER         0x67b00001u
#define GU_SPEED_READOUT_MASK   0x07ffffffu
#define BOOTER_MAGIC            0x314c5442u
#define AD102_BOOT_IMAGE_SIZE   36864u
#define GSP_SIG_SIZE            4096u

#define BOOTER_FILE     L"\\EFI\\turing-unlock\\firmware\\booter_load_ad102.bin"
#define V67_FILE        L"\\EFI\\turing-unlock\\firmware\\v67-ad102-610.43.03-four-plm.bin"
#define GSP_FILE        L"\\EFI\\turing-unlock\\firmware\\gsp_ga10x.bin"
#define BOOT_FILE       L"\\EFI\\turing-unlock\\firmware\\gsp_boot_ad102.bin"
#define BOOT_DESC_FILE  L"\\EFI\\turing-unlock\\firmware\\gsp_boot_desc_ad102.bin"

struct falcon_eng {
  u32 base;
  u32 fbif;
  u32 falcon2;
};

STATIC CONST struct falcon_eng mSec2 = {
  SEC2_BASE, SEC2_FBIF, SEC2_FALCON2
};
STATIC CONST struct falcon_eng mGsp = {
  GSP_BASE, GSP_FBIF, GSP_FALCON2
};

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
  BOOLEAN                            FirmwareStarted;
  BOOLEAN                            GspStarted;
  BOOLEAN                            SecStarted;
  BOOLEAN                            SecHaltVerified;
  BOOLEAN                            Quiesced;
  BOOLEAN                            Published;
  BOOLEAN                            PayloadVerified;
  BOOLEAN                            PermissionSaved;
  BOOLEAN                            PermissionSampled;
  u32                                PermissionBefore[4];
  u32                                PermissionAfter[4];
  struct firmware                    BooterFw;
  struct firmware                    V67Fw;
  struct firmware                    GspFw;
  struct firmware                    BootFw;
  struct firmware                    DescFw;
  struct gpu_unlock_span             ImageSpan;
  struct gpu_unlock_span             SigSpan;
  struct gpu_unlock_booter_hdr       Hdr;
  struct gpu_unlock_boot_desc        BootDesc;
  struct gpu_unlock_radix_plan       Radix;
  struct gpu_unlock_layout_in        LayoutIn;
  struct gpu_unlock_layout_out       Layout;
  struct tu_dma                      BooterDma;
  struct tu_dma                      V67Dma;
  struct tu_dma                      MetaDma;
  struct tu_dma                      BootDma;
  struct tu_dma                      StockDma;
  struct tu_dma                      FwsecDma;
  struct tu_page                     *Pages;
  u32                                PageCount;
  u32                                PagesAllocated;
  u32                                BooterImageSize;
  u32                                PlmEntry;
  u32                                ChipEntry;
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
u64
falcon_dma_span (
  u32 size
  )
{
  return ((u64)size + 255) & ~255ull;
}

STATIC
BOOLEAN
falcon_dma_valid (
  u32 dest,
  u32 mem_off,
  u64 src,
  u32 size
  )
{
  u64 span = falcon_dma_span (size);

  return size != 0 && !((dest | mem_off | (u32)src) & 255) &&
         (u64)dest + span <= (1ull << 24) &&
         (u64)mem_off + span <= (1ull << 32) &&
         src < (1ull << 49) &&
         (u64)mem_off + span <= (1ull << 49) - src;
}

STATIC
int
falcon_reset (
  struct tu_work          *w,
  const struct falcon_eng *eng
  )
{
  u32          engine, hwcfg, bcr, chip;
  unsigned int i;
  int          ret;

  GU_NOTE ("Falcon reset begin: base=%#x\n", eng->base);
  ret = poll (w, eng->base + 0xf4, 1u << 31, 1u << 31, 1000, FALSE);
  if (ret && ret != -ETIMEDOUT) {
    return GU_DIAG (ret);
  }
  engine = rd (w, eng->base + 0x3c0);
  if (bad (engine)) {
    return GU_DIAG (-ENODEV);
  }
  wr (w, eng->base + 0x3c0, engine | 1);
  for (i = 0; i < 10; i++) {
    (VOID)rd (w, eng->base + 0x3c0);
  }
  wr (w, eng->base + 0x3c0, engine & ~1u);
  for (i = 0; i < 10; i++) {
    (VOID)rd (w, eng->base + 0x3c0);
  }
  ret = poll (w, eng->base + 0xf4, 1u << 12, 0, 1000000, TRUE);
  if (ret) {
    return GU_DIAG (ret);
  }
  hwcfg = rd (w, eng->base + 0xf4);
  if (bad (hwcfg)) {
    return GU_DIAG (-EIO);
  }
  if (hwcfg & (1u << 10)) {
    bcr = rd (w, eng->falcon2 + 0x668);
    if (bad (bcr)) {
      return GU_DIAG (-ENODEV);
    }
    if (bcr & (1u << 4)) {
      wr (w, eng->falcon2 + 0x668, 0);
      ret = poll (w, eng->falcon2 + 0x668, 0x11, 1, 1000000, TRUE);
      if (ret) {
        return GU_DIAG (ret);
      }
    }
  }
  chip = rd (w, 0);
  if (bad (chip)) {
    return GU_DIAG (-ENODEV);
  }
  wr (w, eng->base + 0x84, chip);
  GU_NOTE ("Falcon reset done: base=%#x hwcfg=%#x chip=%#x\n", eng->base, hwcfg, chip);
  return 0;
}

STATIC
int
falcon_dma (
  struct tu_work *w,
  u32            base,
  u32            dest,
  u32            mem_off,
  u64            src,
  u32            size,
  u32            cmd
  )
{
  u32 done = 0;
  int ret;

  GU_NOTE ("Falcon DMA: base=%#x dest=%#x offset=%#x src=%llx size=%#x cmd=%#x\n",
           base, dest, mem_off, (unsigned long long)src, size, cmd);
  if (!falcon_dma_valid (dest, mem_off, src, size)) {
    return GU_DIAG (-EINVAL);
  }
  w->FirmwareStarted = TRUE;
  ret = poll (w, base + 0x118, 1, 0, 1000000, TRUE);
  if (ret) {
    return GU_DIAG (ret);
  }
  wr (w, base + 0x110, lower_32_bits (src >> 8));
  wr (w, base + 0x128, upper_32_bits (src >> 8) & 0x1ff);
  while (done < size) {
    ret = poll (w, base + 0x118, 1, 0, 1000000, TRUE);
    if (ret) {
      return GU_DIAG (ret);
    }
    wr (w, base + 0x114, dest);
    wr (w, base + 0x11c, mem_off);
    wr (w, base + 0x118, cmd);
    done    += 256;
    dest    += 256;
    mem_off += 256;
  }
  return GU_DIAG (poll (w, base + 0x118, 2, 2, 1000000, TRUE));
}

STATIC
int
falcon_hs (
  struct tu_work          *w,
  const struct falcon_eng *eng,
  u32                     code_off,
  u32                     code_size,
  u32                     code_va,
  u32                     code_pa,
  u32                     data_off,
  u32                     data_size,
  u32                     data_pa,
  u32                     sig_dmem,
  u32                     engine_mask,
  u32                     ucode_id,
  u64                     image,
  u32                     mailbox0,
  u32                     mailbox1,
  BOOLEAN                 WaitPlm
  )
{
  u32 trans, cpuctl;
  u64 code_src;
  int ret;

  if (image > ~0ull - code_off || image + code_off < code_va ||
      image > ~0ull - data_off)
  {
    return GU_DIAG (-ERANGE);
  }
  code_src = image + code_off - code_va;
  if (!falcon_dma_valid (code_pa, code_va, code_src, code_size) ||
      (data_size && !falcon_dma_valid (data_pa, 0, image + data_off, data_size)))
  {
    return GU_DIAG (-EINVAL);
  }
  ret = falcon_reset (w, eng);
  if (ret) {
    return GU_DIAG (ret);
  }
  /* SEC2 consumes the V67 DMA copy. A changed buffer is not the pinned payload. */
  if (WaitPlm &&
      (w->V67Dma.Cpu == NULL || w->V67Fw.data == NULL ||
       w->V67Fw.size != GU_V67_SIZE ||
       CompareMem (w->V67Dma.Cpu, w->V67Fw.data, GU_V67_SIZE) != 0))
  {
    GU_ERROR ("AD102 payload copy changed before SEC2 start\n");
    return GU_DIAG (-EIO);
  }
  trans = rd (w, eng->fbif + 0x24);
  if (bad (trans)) {
    return GU_DIAG (-EIO);
  }
  wr (w, eng->fbif + 0x24, trans | (1u << 7));
  wr (w, eng->base + 0x10c, 0);
  trans = rd (w, eng->fbif);
  if (bad (trans)) {
    return GU_DIAG (-ENODEV);
  }
  wr (w, eng->fbif, (trans & ~7u) | 5u);
  dma_wmb (w, NULL, 0);
  ret = falcon_dma (w, eng->base, code_pa, code_va, code_src, code_size,
                    gpu_unlock_dma_cmd (TRUE));
  if (ret) {
    return GU_DIAG (ret);
  }
  if (data_size) {
    ret = falcon_dma (w, eng->base, data_pa, 0, image + data_off, data_size,
                      gpu_unlock_dma_cmd (FALSE));
    if (ret) {
      return GU_DIAG (ret);
    }
  }
  wr (w, eng->falcon2 + 0x210, sig_dmem);
  wr (w, eng->falcon2 + 0x19c, engine_mask);
  wr (w, eng->falcon2 + 0x198, ucode_id & 0xff);
  wr (w, eng->falcon2 + 0x180, 1);
  wr (w, eng->base + 0x104, code_va);
  wr (w, eng->base + 0x40, mailbox0);
  wr (w, eng->base + 0x44, mailbox1);
  cpuctl = rd (w, eng->base + 0x100);
  if (bad (cpuctl)) {
    return GU_DIAG (-EIO);
  }
  GU_NOTE ("Falcon start: base=%#x cpuctl=%#x code_va=%#x mailbox=%#x:%#x\n",
           eng->base, cpuctl, code_va, mailbox1, mailbox0);
  if (eng->base == SEC2_BASE) {
    w->SecStarted      = TRUE;
    w->SecHaltVerified = FALSE;
  }
  if (eng->base == GSP_BASE) {
    w->GspStarted = TRUE;
  }
  if (cpuctl & (1u << 6)) {
    wr (w, eng->base + 0x130, 1u << 1);
  } else {
    wr (w, eng->base + 0x100, 1u << 1);
  }
  if (WaitPlm) {
    UINTN i;
    u32   plm, engine, marker;

    for (i = 0; i < 5000000 / 50; i++) {
      engine = rd (w, eng->base + 0x100);
      plm    = rd (w, GU_FEAT_PLM_OFFSET);
      marker = rd (w, eng->base + 0x40);
      if (bad (engine)) {
        return GU_DIAG (-ENODEV);
      }
      if (plm == ~0u && w->PlmEntry != ~0u && marker == GU_AD102_MARKER) {
        return 0;
      }
      if (engine & (1u << 4)) {
        GU_ERROR ("early PLM failed: cpuctl=%#x plm=%#x marker=%#x entry=%#x\n",
                  engine, plm, marker, w->PlmEntry);
        return GU_DIAG (-EIO);
      }
      gBS->Stall (50);
    }
    return GU_DIAG (-ETIMEDOUT);
  }
  return GU_DIAG (poll (w, eng->base + 0x100, 1u << 4, 1u << 4, 5000000, TRUE));
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
    struct gpu_unlock_gfw_sample sample;

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
      ret = gpu_unlock_gfw_assess (&sample);
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
parse_booter (
  struct tu_work *w
  )
{
  const u8 *data;
  u32      magic, version, image_size, sig_bytes;

  data = w->BooterFw.data;
  if (w->BooterFw.size < 72) {
    return GU_DIAG (-EINVAL);
  }
  memcpy (&magic, data, 4);
  memcpy (&version, data + 4, 4);
  if (magic != BOOTER_MAGIC || version != 1) {
    return GU_DIAG (-EINVAL);
  }
  memcpy (&w->Hdr, data + 8, sizeof (w->Hdr));
  memcpy (&image_size, data + 8 + sizeof (w->Hdr), 4);
  memcpy (&sig_bytes, data + 12 + sizeof (w->Hdr), 4);
  if (image_size > w->BooterFw.size - 72u ||
      sig_bytes > w->BooterFw.size - 72u - image_size ||
      w->BooterFw.size != 72u + image_size + sig_bytes)
  {
    return GU_DIAG (-EINVAL);
  }
  if (!gpu_unlock_booter_geometry_match (&w->Hdr, image_size, sig_bytes) ||
      gpu_unlock_booter_validate (GU_BOOTER_AD10X, &w->Hdr, image_size, sig_bytes))
  {
    return GU_DIAG (-EINVAL);
  }
  w->BooterImageSize = image_size;
  GU_NOTE ("booter geometry ok: image=%u sig=%u patch_loc=%#x\n",
           image_size, sig_bytes, w->Hdr.patch_loc);
  return 0;
}

STATIC
int
patch_booter (
  struct tu_work *w
  )
{
  u32 fuse_raw, fuse_ver;
  u32 index = w->Hdr.ucode_id - 1;

  fuse_raw = rd (w, GU_FUSE_SEC2_UCODE1 + index * 4);
  if (bad (fuse_raw)) {
    return GU_DIAG (-EIO);
  }
  fuse_ver = gpu_unlock_fuse_version (fuse_raw);
  GU_NOTE ("Booter signature: fuse_raw=%#x version=%u signatures=%u size=%u\n",
           fuse_raw, fuse_ver, w->Hdr.num_sigs, w->Hdr.sig_bytes);
  return gpu_unlock_booter_patch (
           w->BooterDma.Cpu,
           w->BooterImageSize,
           &w->Hdr,
           w->BooterFw.data + 72 + w->BooterImageSize,
           w->Hdr.sig_bytes,
           fuse_ver
           );
}

STATIC
int
build_radix (
  struct tu_work *w
  )
{
  const struct gpu_unlock_radix_plan *r = &w->Radix;
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

      ret = gpu_unlock_radix_page (w->Pages[first + i].Cpu, addrs + child + at, n);
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
gsp_fwsec_frts (
  struct tu_work *w
  )
{
  size_t                 mapped;
  u8                     *rom = NULL;
  struct gpu_unlock_fwsec fw;
  u32                    fuse_raw, version, scratch, wpr_lo, wpr_hi, sig_at;
  u64                    frts = w->Layout.frts_offset;
  int                    ret;

  ZeroMem (&fw, sizeof (fw));
  mapped = 1024 * 1024;
  rom    = AllocateZeroPool (mapped);
  if (rom == NULL) {
    return GU_DIAG (-ENOMEM);
  }
  ret = EFI_ERROR (TuPciMemReadBuffer (w->PciIo, w->BarIndex, 0x300000, mapped, rom)) ? -EIO : 0;
  if (ret) {
    goto out;
  }
  ret = gpu_unlock_vbios_fwsec (rom, mapped, &fw);
  if (ret) {
    goto out;
  }
  if (fw.engine_mask != 0x400) {
    GU_ERROR ("FWSEC: unsupported GSP engine mask=%#x\n", fw.engine_mask);
    ret = GU_DIAG (-EOPNOTSUPP);
    goto out;
  }
  ret = alloc_dma (w, "fwsec", fw.image_size, &w->FwsecDma);
  if (ret) {
    goto out;
  }
  CopyMem (w->FwsecDma.Cpu, rom + fw.image_off, fw.image_size);
  ret = gpu_unlock_fwsec_patch_frts (w->FwsecDma.Cpu, &fw, frts);
  if (ret) {
    goto out;
  }
  if (!fw.ucode_id) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  fuse_raw = rd (w, 0x008241c0u + (fw.ucode_id - 1) * 4);
  if (bad (fuse_raw)) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  version = gpu_unlock_fuse_version (fuse_raw);
  GU_NOTE ("FWSEC fuse: raw=%#x version=%u\n", fuse_raw, version);
  sig_at = 0;
  ret    = gpu_unlock_fwsec_sig_offset (fw.sig_versions, version, 384, &sig_at);
  if (ret) {
    goto out;
  }
  CopyMem ((u8 *)w->FwsecDma.Cpu + fw.data_off + fw.pkc_off, rom + fw.sig_off + sig_at, 384);
  dma_wmb (w, w->FwsecDma.Cpu, EFI_PAGES_TO_SIZE (w->FwsecDma.Pages));
  if (falcon_dma_span (fw.imem_size) > fw.image_size ||
      (u64)fw.data_off + falcon_dma_span (fw.dmem_size) > fw.image_size)
  {
    ret = GU_DIAG (-EINVAL);
    goto out;
  }
  ret = falcon_hs (
          w, &mGsp, 0, fw.imem_size, fw.imem_va, fw.imem_pa,
          fw.data_off, fw.dmem_size, fw.dmem_pa, fw.pkc_off,
          fw.engine_mask, fw.ucode_id, w->FwsecDma.Dma, 0, 0, FALSE
          );
  if (ret) {
    goto out;
  }
  scratch = rd (w, 0x00001400u + 0x0e * 4);
  GU_NOTE ("FWSEC result: scratch=%#x\n", scratch);
  if (scratch >> 16) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  wpr_lo = rd (w, 0x001fa824);
  wpr_hi = rd (w, 0x001fa828);
  GU_NOTE ("FWSEC WPR2: low=%#x high=%#x want_lo=%#x\n",
           wpr_lo, wpr_hi, (u32)(frts >> 12));
  if (!(wpr_hi >> 4) || (wpr_hi >> 4) < (wpr_lo >> 4) ||
      ((u64)(wpr_hi >> 4) << 12) >= w->Layout.wpr_end ||
      (wpr_lo >> 4) != (u32)(frts >> 12))
  {
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
publish_v67 (
  struct tu_work *w
  )
{
  struct gpu_unlock_wpr_args args;
  int                        ret;

  ZeroMem (&args, sizeof (args));
  args.fb_size        = w->LayoutIn.usable_fb;
  args.vga_offset     = w->LayoutIn.vga_base;
  args.frts_size      = w->LayoutIn.frts_size;
  args.signature_dma  = w->V67Dma.Dma;
  args.signature_size = GU_V67_SIZE;
  args.radix_dma      = w->Pages[0].Dma;
  args.image_size     = w->ImageSpan.size;
  args.boot_dma       = w->BootDma.Dma;
  args.boot           = w->BootDesc;
  ret = gpu_unlock_wpr_meta_pack (w->MetaDma.Cpu, &w->Layout, &args);
  if (ret) {
    return ret;
  }
  dma_wmb (w, w->MetaDma.Cpu, GU_WPR_META_SIZE);
  w->Published = TRUE;
  return 0;
}

STATIC
VOID
put_le64 (
  u8  *dest,
  u64 value
  )
{
  unsigned int i;

  for (i = 0; i < 8; i++) {
    dest[i] = (u8)(value >> (8 * i));
  }
}

STATIC
int
restore_stock_sig (
  struct tu_work *w
  )
{
  if (!w->MetaDma.Cpu || !w->StockDma.Cpu || !w->SecHaltVerified) {
    return GU_DIAG (-EBUSY);
  }
  put_le64 ((u8 *)w->MetaDma.Cpu + 72, w->StockDma.Dma);
  put_le64 ((u8 *)w->MetaDma.Cpu + 80, w->StockDma.Pages ? w->SigSpan.size : 0);
  dma_wmb (w, w->MetaDma.Cpu, GU_WPR_META_SIZE);
  return 0;
}

STATIC CONST struct {
  CONST CHAR8  *Name;
  u32          Offset;
  u32          Expected;
  BOOLEAN      Required;
} mAd102Perm[] = {
  { "WPR_CFG", 0x001fa7ccu, 0xfffff0ffu, TRUE  },
  { "FBPA",    0x009a0148u, 0xffffffffu, FALSE },
  { "WPR",     0x001fa7c4u, 0xffffffffu, TRUE  },
  { "FEAT",    GU_FEAT_PLM_OFFSET, 0xffffffffu, TRUE },
};

/* All-ones is a real PLM value. Only a badf prefix, a failed read, or a
 * PCI/chip identity mismatch makes a sample invalid. Required values are
 * compared by the caller after the payload halts, not on the first sample.
 */
STATIC
int
ad102_sample_permissions (
  struct tu_work *w,
  CONST CHAR8    *Phase,
  u32            *Values,
  BOOLEAN        Before
  )
{
  EFI_STATUS  Status;
  UINT32      Identity;
  UINT32      Expect;
  u32         Chip;
  BOOLEAN     Accessible;
  UINTN       Index;
  int         ret;

  Identity = 0;
  Expect   = 0x10deu | ((UINT32)w->Gpu->DeviceId << 16);
  Status   = w->PciIo->Pci.Read (
                             w->PciIo,
                             EfiPciIoWidthUint32,
                             0,
                             1,
                             &Identity
                             );
  Chip       = rd (w, 0);
  Accessible = !EFI_ERROR (Status) && Identity == Expect &&
               Chip != 0 && (Chip & 0xffff0000u) != 0xbadf0000u &&
               Chip != ~0u && (Before || Chip == w->ChipEntry);
  if (Before && Accessible) {
    w->ChipEntry = Chip;
  }
  ret = 0;
  for (Index = 0; Index < ARRAY_SIZE (mAd102Perm); Index++) {
    UINT32   Got;
    EFI_STATUS ReadSt;
    u32      Previous;
    BOOLEAN  Valid;

    Previous = w->PermissionBefore[Index];
    Got      = 0;
    ReadSt   = w->PciIo->Mem.Read (
                              w->PciIo,
                              EfiPciIoWidthUint32,
                              w->BarIndex,
                              mAd102Perm[Index].Offset,
                              1,
                              &Got
                              );
    Values[Index] = Got;
    Valid = !EFI_ERROR (ReadSt) &&
            (Got & 0xffff0000u) != 0xbadf0000u &&
            Accessible;
    GU_NOTE (
      "AD102 permission %a: %a[%#x]=%#x before=%#x expected=%#x required=%u valid=%u pci=%#x chip=%#x\n",
      Phase,
      mAd102Perm[Index].Name,
      mAd102Perm[Index].Offset,
      Got,
      Previous,
      mAd102Perm[Index].Expected,
      mAd102Perm[Index].Required,
      Valid,
      Identity,
      Chip
      );
    if (!Valid) {
      ret = -ENODEV;
    }
  }
  return GU_DIAG (ret);
}

STATIC
BOOLEAN
ad102_permissions_match (
  CONST u32 *Values
  )
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mAd102Perm); Index++) {
    if (mAd102Perm[Index].Required && Values[Index] != mAd102Perm[Index].Expected) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC
int
ad102_readout (
  struct tu_work *w,
  CONST CHAR8    *Phase,
  BOOLEAN        RequireFull
  )
{
  u32 value = rd (w, GU_FEAT_READOUT_1_OFFSET);

  GU_NOTE (
    "AD102 %a: readout1=%#x valid=%u full=%u\n",
    Phase,
    value,
    !bad (value),
    !bad (value) && !(value & GU_SPEED_READOUT_MASK)
    );
  if (bad (value)) {
    return GU_DIAG (-ENODEV);
  }
  return GU_DIAG (RequireFull && (value & GU_SPEED_READOUT_MASK) ? -EIO : 0);
}

STATIC
int
write_selector (
  struct tu_work *w,
  CONST CHAR8    *Name,
  u32            Offset,
  u32            Unlock
  )
{
  u32 value = rd (w, Offset);

  if (bad (value)) {
    return GU_DIAG (-ENODEV);
  }
  GU_NOTE ("selector read: %a=%#x expected=%#x\n", Name, value, Unlock);
  if (value == Unlock) {
    return 0;
  }
  wr (w, Offset, Unlock);
  value = rd (w, Offset);
  GU_NOTE ("selector result: %a=%#x matched=%u\n", Name, value, value == Unlock);
  return GU_DIAG (value == Unlock ? 0 : -EIO);
}

STATIC
int
hold_sec2_reset (
  struct tu_work *w
  )
{
  unsigned int j;
  u32          engine;
  int          ret;

  engine = rd (w, SEC2_BASE + 0x3c0);
  if (bad (engine)) {
    return GU_DIAG (-ENODEV);
  }
  wr (w, SEC2_BASE + 0x3c0, engine | 1);
  for (j = 0; j < 10; j++) {
    engine = rd (w, SEC2_BASE + 0x3c0);
    if (bad (engine)) {
      return GU_DIAG (-EIO);
    }
  }
  ret = poll (w, SEC2_BASE + 0x3c0, 1, 1, 1000, TRUE);
  if (ret) {
    return GU_DIAG (ret);
  }
  engine = rd (w, SEC2_BASE + 0x3c0);
  wr (w, SEC2_BASE + 0x3c0, engine & ~1u);
  for (j = 0; j < 10; j++) {
    engine = rd (w, SEC2_BASE + 0x3c0);
    if (bad (engine)) {
      return GU_DIAG (-EIO);
    }
  }
  ret = poll (w, SEC2_BASE + 0x3c0, 1, 0, 1000, TRUE);
  if (ret) {
    return GU_DIAG (ret);
  }
  return GU_DIAG (poll (w, SEC2_BASE + 0xf4, 1u << 12, 0, 1000000, TRUE));
}

STATIC
int
ada_flr (
  struct tu_work *w
  )
{
  EFI_STATUS Status;
  u32        chip, value, lo, hi, ss0, ss1, plm, cpuctl;
  UINTN      i;
  int        perm;

  ss0 = rd (w, GU_FEAT_SS0_OFFSET);
  ss1 = rd (w, GU_FEAT_SS1_OFFSET);
  if (ss0 != GU_SS0_UNLOCK || ss1 != GU_SS1_UNLOCK) {
    return GU_DIAG (-EIO);
  }
  if (ad102_readout (w, "pre-FLR", TRUE)) {
    return GU_DIAG (-EIO);
  }
  chip = rd (w, 0);
  if (!chip || bad (chip)) {
    return GU_DIAG (-ENODEV);
  }
  if (w->MasterOn) {
    TuPciClearMaster (w->PciIo);
    w->MasterOn = FALSE;
  }
  GU_NOTE ("PCIe FLR begin: chip=%#x\n", chip);
  Status = TuPciFlr (w->PciIo);
  if (EFI_ERROR (Status)) {
    return GU_DIAG (-EIO);
  }
  if (w->CfgSaved) {
    TuPciRestoreConfig (w->PciIo, w->SavedCfg);
  }
  for (i = 0; i < 1000; i++) {
    value = rd (w, 0);
    if (value == chip) {
      break;
    }
    gBS->Stall (1000);
  }
  if (value != chip) {
    return GU_DIAG (-ETIMEDOUT);
  }
  if (wait_gfw (w)) {
    return GU_DIAG (-ETIMEDOUT);
  }
  if (hold_sec2_reset (w)) {
    return GU_DIAG (-EIO);
  }
  cpuctl = rd (w, GU_GFW_CPUCTL_OFFSET);
  lo     = rd (w, 0x001fa824);
  hi     = rd (w, 0x001fa828);
  ss0    = rd (w, GU_FEAT_SS0_OFFSET);
  ss1    = rd (w, GU_FEAT_SS1_OFFSET);
  plm    = rd (w, GU_FEAT_PLM_OFFSET);
  GU_NOTE (
    "post-FLR: cpuctl=%#x WPR2=%#x:%#x SS0=%#x SS1=%#x PLM=%#x\n",
    cpuctl, hi, lo, ss0, ss1, plm
    );
  if (bad (cpuctl) || !(cpuctl & GU_GFW_CPUCTL_HALTED) ||
      bad (lo) || bad (hi) || bad (ss0) || bad (ss1) ||
      (plm & 0xffff0000u) == 0xbadf0000u)
  {
    return GU_DIAG (-ENODEV);
  }
  if (hi >> 4) {
    GU_ERROR ("post-FLR WPR2 remains enabled: lo=%#x hi=%#x\n", lo, hi);
    return GU_DIAG (-EBUSY);
  }
  w->Quiesced = TRUE;
  if (ss0 != GU_SS0_UNLOCK || ss1 != GU_SS1_UNLOCK) {
    return GU_DIAG (-EIO);
  }
  perm = ad102_sample_permissions (w, "post-FLR", w->PermissionAfter, FALSE);
  if (perm) {
    return perm;
  }
  if (plm != ~0u) {
    return GU_DIAG (-EIO);
  }
  return ad102_readout (w, "post-FLR", TRUE);
}

STATIC
int
sec2_boot (
  struct tu_work *w
  )
{
  const struct gpu_unlock_booter_hdr *h = &w->Hdr;
  u32 marker, cpuctl, ss0, ss1;
  int ret, halt_ret, sample_ret;
  BOOLEAN required;

  if (!w->Published || !w->MasterOn || !w->FirmwareStarted) {
    return GU_DIAG (-EINVAL);
  }
  if (!w->PermissionSaved) {
    return GU_DIAG (-EINVAL);
  }
  ret = falcon_hs (
          w, &mSec2,
          h->app_code_off, h->app_code_size, h->app_code_off, 0,
          h->os_data_off, h->os_data_size, 0,
          h->patch_loc - h->os_data_off, h->engine_id, h->ucode_id,
          w->BooterDma.Dma,
          lower_32_bits (w->MetaDma.Dma),
          upper_32_bits (w->MetaDma.Dma),
          TRUE
          );
  halt_ret = ret ? ret : poll (w, SEC2_BASE + 0x100, 0x10, 0x10, 5000000, TRUE);
  marker   = rd (w, SEC2_BASE + 0x40);
  cpuctl   = rd (w, SEC2_BASE + 0x100);
  ss0      = rd (w, GU_FEAT_SS0_OFFSET);
  ss1      = rd (w, GU_FEAT_SS1_OFFSET);
  w->SecHaltVerified    = !bad (cpuctl) && !!(cpuctl & 0x10);
  w->PermissionSampled  = TRUE;
  sample_ret            = ad102_sample_permissions (
                            w,
                            "after-payload",
                            w->PermissionAfter,
                            FALSE
                            );
  required = ad102_permissions_match (w->PermissionAfter);
  GU_NOTE (
    "AD102 payload result: boot=%d halt=%d marker=%#x expected=%#x required=%u sample=%d cpuctl=%#x halted=%u ss0=%#x ss1=%#x\n",
    ret, halt_ret, marker, GU_AD102_MARKER, required, sample_ret,
    cpuctl, w->SecHaltVerified, ss0, ss1
    );
  if (ret) {
    return ret;
  }
  if (halt_ret) {
    return halt_ret;
  }
  if (sample_ret) {
    return sample_ret;
  }
  if (marker != GU_AD102_MARKER || !w->SecHaltVerified || !required ||
      bad (ss0) || bad (ss1))
  {
    return GU_DIAG (-EIO);
  }
  ret = ad102_readout (w, "after-payload", FALSE);
  if (ret) {
    return ret;
  }
  w->PayloadVerified = TRUE;
  return 0;
}

STATIC
int
compute_layout (
  struct tu_work *w
  )
{
  u32 mb, vga, lo, hi, plm;
  int ret;

  mb  = rd (w, 0x001183a4);
  vga = rd (w, 0x00625f04);
  plm = rd (w, 0x001fa7c8);
  lo  = rd (w, 0x001fa82c);
  hi  = rd (w, 0x001fa830);
  GU_NOTE ("layout inputs: fb_mb=%#x vga=%#x mmu_plm=%#x mmu_lo=%#x mmu_hi=%#x\n",
           mb, vga, plm, lo, hi);
  if (!mb || mb == ~0u || vga == ~0u || plm == ~0u || !(plm & 1) ||
      lo == ~0u || hi == ~0u)
  {
    return GU_DIAG (-ENODEV);
  }
  w->LayoutIn.usable_fb = (u64)mb << 20;
  if (w->LayoutIn.usable_fb < 256ull << 20) {
    return GU_DIAG (-ERANGE);
  }
  w->LayoutIn.vga_base = w->LayoutIn.usable_fb - (1ull << 20);
  if (vga & (1u << 3)) {
    w->LayoutIn.vga_base = (u64)(vga >> 8) << 16;
    if (w->LayoutIn.vga_base < w->LayoutIn.usable_fb - (1ull << 20)) {
      w->LayoutIn.vga_base = w->LayoutIn.usable_fb - 0x20000ull;
    }
  }
  if (w->LayoutIn.vga_base >= w->LayoutIn.usable_fb) {
    return GU_DIAG (-ERANGE);
  }
  w->LayoutIn.mmu_lock_low = (u64)(lo >> 4) << 12;
  {
    u64 mmu_hi  = (u64)(hi >> 4) << 12;
    u64 mmu_top = mmu_hi > ~0ull - 0xffffull ? ~0ull : (mmu_hi + 0xffffull) & ~0xffffull;

    w->LayoutIn.mmu_lock_valid = mmu_hi >= w->LayoutIn.mmu_lock_low &&
                                 (mmu_top >> 20) == (w->LayoutIn.usable_fb >> 20);
  }
  w->LayoutIn.frts_size    = 1ull << 20;
  w->LayoutIn.boot_size    = w->BootDesc.image_size;
  w->LayoutIn.image_size   = w->ImageSpan.size;
  w->LayoutIn.nonwpr_size  = 1ull << 20;
  w->LayoutIn.heap_request = 88ull << 20;
  ret = gpu_unlock_layout (&w->LayoutIn, &w->Layout);
  if (ret) {
    return ret;
  }
  if (w->Layout.nonwpr_start < w->LayoutIn.usable_fb - (256ull << 20)) {
    return GU_DIAG (-ERANGE);
  }
  return 0;
}

STATIC
BOOLEAN
firmware_quiesced (
  const struct tu_work *w
  )
{
  if (!w->FirmwareStarted) {
    return TRUE;
  }
  return w->Quiesced;
}

INTN
TuAdaUnlockGpu (
  IN EFI_HANDLE  ImageHandle,
  IN TU_PCI_GPU  *Gpu
  )
{
  struct tu_work *w;
  u32            plm, ss0, ss1, wpr_hi;
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

  if (Gpu->DeviceId != GU_AD102_RTX_4090) {
    GU_NOTE ("experimental Ada PCI ID %#x using AD102-family assets\n", Gpu->DeviceId);
    TuLogWide (L"Experimental Ada ID 10de:%04x; AD102-family payload.\r\n", Gpu->DeviceId);
  }

  ret = load_fw (w, BOOTER_FILE, &w->BooterFw);
  if (ret) {
    goto out;
  }
  ret = parse_booter (w);
  if (ret) {
    goto out;
  }
  ret = load_fw (w, V67_FILE, &w->V67Fw);
  if (ret) {
    goto out;
  }
  if (w->V67Fw.size != GU_V67_SIZE) {
    ret = GU_DIAG (-EBADMSG);
    goto out;
  }
  ret = load_fw (w, GSP_FILE, &w->GspFw);
  if (ret) {
    goto out;
  }
  ret = gpu_unlock_elf_section (w->GspFw.data, w->GspFw.size, ".fwimage", &w->ImageSpan);
  if (ret) {
    goto out;
  }
  ret = gpu_unlock_elf_section (w->GspFw.data, w->GspFw.size, ".fwsignature_ad10x", &w->SigSpan);
  if (ret) {
    goto out;
  }
  if (!w->ImageSpan.size || w->ImageSpan.size > (128ull << 20) || w->SigSpan.size != GSP_SIG_SIZE) {
    ret = GU_DIAG (-EINVAL);
    goto out;
  }
  ret = gpu_unlock_radix_plan (w->ImageSpan.size, &w->Radix);
  if (ret) {
    goto out;
  }
  ret = load_fw (w, BOOT_FILE, &w->BootFw);
  if (ret) {
    goto out;
  }
  if (w->BootFw.size != AD102_BOOT_IMAGE_SIZE) {
    ret = GU_DIAG (-EINVAL);
    goto out;
  }
  ret = load_fw (w, BOOT_DESC_FILE, &w->DescFw);
  if (ret) {
    goto out;
  }
  ret = gpu_unlock_boot_desc_parse (w->DescFw.data, w->DescFw.size, (u32)w->BootFw.size, &w->BootDesc);
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

  ret = wait_gfw (w);
  if (ret) {
    goto out;
  }
  w->PlmEntry  = rd (w, GU_FEAT_PLM_OFFSET);
  ss0          = rd (w, GU_FEAT_SS0_OFFSET);
  ss1          = rd (w, GU_FEAT_SS1_OFFSET);
  wpr_hi       = rd (w, 0x001fa828);
  w->ChipEntry = rd (w, 0);
  GU_NOTE ("entry: plm=%#x ss0=%#x ss1=%#x wpr2_hi=%#x chip=%#x\n",
           w->PlmEntry, ss0, ss1, wpr_hi, w->ChipEntry);
  if (w->PlmEntry == ~0u) {
    ret = GU_DIAG (-EALREADY);
    goto out;
  }
  if (bad (ss0) || bad (ss1) || bad (wpr_hi) || bad (w->ChipEntry) || !w->ChipEntry) {
    ret = GU_DIAG (-ENODEV);
    goto out;
  }
  if (wpr_hi >> 4) {
    GU_ERROR ("card already initialised since power on (WPR2 enabled)\n");
    ret = GU_DIAG (-EBUSY);
    goto out;
  }
  ret = ad102_sample_permissions (w, "before-firmware", w->PermissionBefore, TRUE);
  if (ret) {
    goto out;
  }
  w->PermissionSaved = TRUE;
  ret = ad102_readout (w, "before-firmware", FALSE);
  if (ret) {
    goto out;
  }

  ret = compute_layout (w);
  if (ret) {
    goto out;
  }
  ret = alloc_dma (w, "booter", w->BooterImageSize, &w->BooterDma);
  if (ret) {
    goto out;
  }
  CopyMem (w->BooterDma.Cpu, w->BooterFw.data + 72, w->BooterImageSize);
  ret = patch_booter (w);
  if (ret) {
    goto out;
  }
  dma_wmb (w, w->BooterDma.Cpu, w->BooterImageSize);
  ret = alloc_dma (w, "v67", GU_V67_SIZE, &w->V67Dma);
  if (ret) {
    goto out;
  }
  CopyMem (w->V67Dma.Cpu, w->V67Fw.data, GU_V67_SIZE);
  dma_wmb (w, w->V67Dma.Cpu, GU_V67_SIZE);
  ret = alloc_dma (w, "meta", GU_WPR_META_SIZE, &w->MetaDma);
  if (ret) {
    goto out;
  }
  ret = alloc_dma (w, "stock-signature", w->SigSpan.size, &w->StockDma);
  if (ret) {
    goto out;
  }
  CopyMem (w->StockDma.Cpu, w->GspFw.data + w->SigSpan.offset, w->SigSpan.size);
  dma_wmb (w, w->StockDma.Cpu, (UINTN)w->SigSpan.size);
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

  ret = gsp_fwsec_frts (w);
  if (ret) {
    goto out;
  }
  ret = publish_v67 (w);
  if (ret) {
    goto out;
  }
  ret = sec2_boot (w);
  if (ret) {
    goto out;
  }
  ret = restore_stock_sig (w);
  if (ret) {
    goto out;
  }
  if (!w->PayloadVerified) {
    ret = GU_DIAG (-EPROTO);
    goto out;
  }
  ret = write_selector (w, "SS0", GU_FEAT_SS0_OFFSET, GU_SS0_UNLOCK);
  if (!ret) {
    ret = write_selector (w, "SS1", GU_FEAT_SS1_OFFSET, GU_SS1_UNLOCK);
  }
  if (!ret) {
    ret = ad102_readout (w, "selectors-applied", TRUE);
  }
  if (ret) {
    goto out;
  }
  plm = rd (w, GU_FEAT_PLM_OFFSET);
  if (plm != ~0u) {
    ret = GU_DIAG (-EIO);
    goto out;
  }
  ret = ada_flr (w);
  if (!ret) {
    GU_NOTE ("AD102 FEAT UNLOCKED\n");
  }

out:
  if (w->PermissionSaved && !w->PermissionSampled) {
    (VOID)ad102_sample_permissions (w, "abort", w->PermissionAfter, FALSE);
    w->PermissionSampled = TRUE;
  }
  if (ret && w->FirmwareStarted && !w->Quiesced) {
    (VOID)TuPciFlr (w->PciIo);
    if (w->CfgSaved) {
      TuPciRestoreConfig (w->PciIo, w->SavedCfg);
    }
    w->Quiesced = TRUE;
    w->MasterOn = FALSE;
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
  free_dma (&w->StockDma);
  free_dma (&w->FwsecDma);
  free_fw (&w->BooterFw);
  free_fw (&w->V67Fw);
  free_fw (&w->GspFw);
  free_fw (&w->BootFw);
  free_fw (&w->DescFw);
  FreePool (w);
  return ret;
}
