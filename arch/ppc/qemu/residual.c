/*
 * PReP residual data generation for the QEMU 40p (IBM RS/6000 7020) machine
 *
 * A PReP boot image (AIX, old Linux/PReP, NetBSD/prep, ...) receives no
 * Open Firmware device tree; instead firmware hands it a "residual data"
 * block in r3 describing the CPU, the memory map and every device on the
 * planar as PnP resource descriptors.  Without a plausible block the AIX
 * 4.3 bootstrap hangs before initialising any console.
 *
 * The device descriptions below mirror those of the FirmWorks Open
 * Firmware port for QEMU's 40p machine (cpu/ppc/prep/qemu/devices.fth,
 * pcinode.fth), whose residual data is known to boot AIX 4.3.  Values
 * OpenBIOS knows itself (memory size, memory layout, CPU, PCI devices and
 * their slots) are taken from the live machine state.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "libopenbios/ofmem.h"
#include "libc/byteorder.h"
#include "arch/common/fw_cfg.h"
#include "arch/ppc/processor.h"
#include "arch/ppc/residual.h"
#include "drivers/drivers.h"

#define OF_CODE_SIZE    0x00100000     /* firmware copy at the top of RAM */
#define PAGE_SHIFT      12

/*
 * PnP resource strings, little-endian as per the PnP ISA specification.
 * "84 xx xx 09 ..." are IBM vendor-defined large items (generic address
 * descriptors, bus attributes, bus address translations, PCI bridge slot
 * map); "75 01 ..." is the IBM chip identifier small item.
 */

static const uint8_t pnp_host_bridge[] = {
    0x84, 0x15, 0x00, 0x09, 0x01, 0x20, 0x00, 0x00, 0xf8, 0x0c, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x01, 0x4d, 0x24, 0x01, 0x00, 0x84, 0x5d, 0x00, 0x03, 0xf8, 0x0c,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x0c, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x08, 0x01, 0x00, 0x0d, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0x10, 0x01, 0x00, 0x0f, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03, 0x18, 0x01, 0x00, 0x0f, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x04, 0x20, 0x01, 0x00, 0x0f, 0x00,
    0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x05, 0x28, 0x01, 0x00, 0x0f, 0x00,
    0x0f, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x84, 0x06, 0x00, 0x06, 0x55, 0xa0,
    0xfc, 0x01, 0x06, 0x84, 0x1d, 0x00, 0x05, 0x01, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x84,
    0x1d, 0x00, 0x05, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x1d, 0x00, 0x05, 0x01,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3e, 0x00,
    0x00, 0x00, 0x00, 0x84, 0x1d, 0x00, 0x05, 0x01, 0x01, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t pnp_isa_bridge[] = {
    0x75, 0x01, 0x24, 0x4d, 0x00, 0x81, 0x47, 0x01, 0x61, 0x00, 0x61, 0x00,
    0x01, 0x01, 0x47, 0x01, 0x98, 0x03, 0x98, 0x03, 0x01, 0x02, 0x47, 0x01,
    0x00, 0x08, 0x00, 0x08, 0x01, 0x08, 0x84, 0x06, 0x00, 0x06, 0x90, 0xe2,
    0x7d, 0x00, 0x03, 0x84, 0x23, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07,
    0x00, 0x08, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x0c, 0x00, 0x0d,
    0x00, 0x0e, 0x00, 0x0f, 0x00, 0x84, 0x1d, 0x00, 0x05, 0x00, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x84, 0x1d, 0x00, 0x05, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t pnp_nvram[] = {
    0x47, 0x01, 0x74, 0x00, 0x74, 0x00, 0x01, 0x02, 0x47, 0x01, 0x77, 0x00,
    0x77, 0x00, 0x01, 0x01, 0x75, 0x01, 0x24, 0x4d, 0x00, 0x8f,
};

static const uint8_t pnp_rtc[] = {
    0x4b, 0x70, 0x00, 0x02, 0x4b, 0x10, 0x00, 0x01, 0x4b, 0x12, 0x00, 0x01,
    0x75, 0x01, 0x24, 0x4d, 0x00, 0x8f, 0x76, 0x00, 0x01, 0xf8, 0x1f, 0x00,
    0x00,
};

static const uint8_t pnp_serial2[] = {
    0x22, 0x08, 0x00, 0x84, 0x15, 0x00, 0x09, 0x01, 0x0b, 0x00, 0x00, 0xf8,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

static const uint8_t pnp_serial1[] = {
    0x22, 0x10, 0x00, 0x84, 0x15, 0x00, 0x09, 0x01, 0x0b, 0x00, 0x00, 0xf8,
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

static const uint8_t pnp_timer[] = {
    0x22, 0x01, 0x00, 0x47, 0x01, 0x40, 0x00, 0x40, 0x00, 0x01, 0x04, 0x47,
    0x01, 0x61, 0x00, 0x61, 0x00, 0x01, 0x01, 0x75, 0x01, 0x24, 0x4d, 0x10,
    0x8f,
};

static const uint8_t pnp_pic[] = {
    0x22, 0x04, 0x00, 0x47, 0x01, 0x20, 0x00, 0x20, 0x00, 0x01, 0x02, 0x47,
    0x01, 0xa0, 0x00, 0xa0, 0x00, 0x01, 0x02, 0x47, 0x01, 0xd0, 0x04, 0xd0,
    0x04, 0x01, 0x02, 0x84, 0x15, 0x00, 0x09, 0x03, 0x20, 0x00, 0x00, 0xf0,
    0xff, 0xff, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

static const uint8_t pnp_dma[] = {
    0x47, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x47, 0x01, 0x80, 0x00,
    0x80, 0x00, 0x01, 0x20, 0x47, 0x01, 0xc0, 0x00, 0xc0, 0x00, 0x01, 0x20,
    0x47, 0x01, 0x0a, 0x04, 0x0a, 0x04, 0x01, 0x01, 0x47, 0x01, 0x0b, 0x04,
    0x0b, 0x04, 0x01, 0x01, 0x47, 0x01, 0x10, 0x04, 0x10, 0x04, 0x01, 0x30,
    0x47, 0x01, 0x81, 0x04, 0x81, 0x04, 0x01, 0x0b, 0x47, 0x01, 0xd6, 0x04,
    0xd6, 0x04, 0x01, 0x01, 0x2a, 0x10, 0x83,
};

static const uint8_t pnp_mouse[] = {
    0x22, 0x00, 0x10, 0x84, 0x15, 0x00, 0x09, 0x01, 0x10, 0x00, 0x00, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x84, 0x15, 0x00, 0x09, 0x01, 0x10, 0x00, 0x00, 0x64,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

static const uint8_t pnp_keyboard[] = {
    0x22, 0x02, 0x00, 0x84, 0x15, 0x00, 0x09, 0x01, 0x10, 0x00, 0x00, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x84, 0x15, 0x00, 0x09, 0x01, 0x10, 0x00, 0x00, 0x64,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

typedef struct res_dev_desc {
    uint32_t bus_id;
    uint32_t dev_id;
    uint32_t serial;
    uint32_t flags;
    uint8_t base_type, sub_type, interface;
    uint32_t bus_access;
    const uint8_t *pnp;
    unsigned int pnp_len;
} res_dev_desc_t;

#define PNP(a)   a, sizeof(a)

/* Devices behind the i82378 ISA bridge / PC87312 Super I/O of the 40p */
static const res_dev_desc_t isa_devices[] = {
    /* M48T59 NVRAM at ports 74-77 (IBM0008) */
    { RES_BUS_ISA, 0x244d0008, -1, 0x2800, 0x08, 0x05, 0x00, 0, PNP(pnp_nvram) },
    /* MC146818 RTC at port 70 (PNP0B00) */
    { RES_BUS_ISA, 0x41d00b00, -1, 0x2800, 0x08, 0x03, 0x01, 0, PNP(pnp_rtc) },
    /* Serial port 2 at 2f8, IRQ 3 (PNP0501) */
    { RES_BUS_ISA, 0x41d00501, 2, 0x21c3, 0x07, 0x00, 0x04, 0, PNP(pnp_serial2) },
    /* Serial port 1 at 3f8, IRQ 4 (PNP0501) */
    { RES_BUS_ISA, 0x41d00501, 1, 0x21c3, 0x07, 0x00, 0x04, 0, PNP(pnp_serial1) },
    /* 8254 timer at port 40, IRQ 0 (PNP0100) */
    { RES_BUS_ISA, 0x41d00100, -1, 0x2800, 0x08, 0x02, 0x01, 0, PNP(pnp_timer) },
    /* 8259 interrupt controllers at 20/a0, IRQ 2 (PNP0000) */
    { RES_BUS_ISA, 0x41d00000, -1, 0x2800, 0x08, 0x00, 0x01, 0, PNP(pnp_pic) },
    /* 8237 DMA controllers (PNP0200) */
    { RES_BUS_ISA, 0x41d00200, -1, 0x2800, 0x08, 0x01, 0x01, 0, PNP(pnp_dma) },
    /* PS/2 mouse on the 8042, IRQ 12 (PNP0F03) */
    { RES_BUS_ISA, 0x41d00f03, -1, 0x2892, 0x09, 0x02, 0x00, 0, PNP(pnp_mouse) },
    /* PS/2 keyboard on the 8042, IRQ 1 (PNP0303) */
    { RES_BUS_ISA, 0x41d00303, -1, 0x289a, 0x09, 0x00, 0x00, 0, PNP(pnp_keyboard) },
};

static residual_t *res;
static unsigned int pnp_used;

static uint32_t pnp_add(const uint8_t *data, unsigned int len)
{
    uint32_t off = pnp_used;

    if (pnp_used + len + 1 > RES_PNP_HEAP) {
        return 0;
    }
    if (len) {
        memcpy(res->pnp_heap + pnp_used, data, len);
    }
    pnp_used += len;
    res->pnp_heap[pnp_used++] = 0x78;  /* PnP end tag, no checksum */

    return off;
}

static void dev_add(const res_dev_desc_t *d)
{
    res_device_t *dev;

    if (res->actual_devices >= RES_MAX_DEVS) {
        return;
    }
    dev = &res->devices[res->actual_devices++];
    dev->bus_id = d->bus_id;
    dev->dev_id = d->dev_id;
    dev->serial = d->serial;
    dev->flags = d->flags;
    dev->base_type = d->base_type;
    dev->sub_type = d->sub_type;
    dev->interface = d->interface;
    dev->spare = 0;
    dev->bus_access = d->bus_access;
    dev->allocated_offset = pnp_add(d->pnp, d->pnp_len);
    dev->possible_offset = pnp_add(NULL, 0);
    dev->compatible_offset = pnp_add(NULL, 0);
}

static void seg_add(uint32_t usage, uint32_t start, uint32_t end)
{
    res_seg_t *seg;

    if (start == end || res->actual_segs >= RES_MAX_SEGS) {
        return;
    }
    seg = &res->segs[res->actual_segs++];
    seg->usage = usage;
    seg->base_page = start >> PAGE_SHIFT;
    seg->page_count = (end - start) >> PAGE_SHIFT;
}

static uint32_t cpu_prop(phandle_t ph, const char *name, uint32_t def)
{
    int len;
    uint32_t *p;

    p = (uint32_t *)get_property(ph, name, &len);
    if (!p || len != sizeof(*p)) {
        return def;
    }
    return *p;
}

static void residual_add_cpu(void)
{
    phandle_t ph = dt_iterate_type(0, "cpu");
    uint32_t pvr = mfpvr();
    uint32_t isize, dsize, isets, dsets, iline, dline, tlbsize, tlbsets;
    uint32_t cpu_hz, bus_hz, tb_hz;

    isize = cpu_prop(ph, "i-cache-size", 0x4000);
    dsize = cpu_prop(ph, "d-cache-size", 0x4000);
    isets = cpu_prop(ph, "i-cache-sets", 0x80);
    dsets = cpu_prop(ph, "d-cache-sets", 0x80);
    iline = cpu_prop(ph, "i-cache-block-size", 0x20);
    dline = cpu_prop(ph, "d-cache-block-size", 0x20);
    tlbsize = cpu_prop(ph, "tlb-size", 0x80);   /* entries per TLB */
    tlbsets = cpu_prop(ph, "tlb-sets", 0x40);

    cpu_hz = fw_cfg_read_i32(FW_CFG_PPC_CLOCKFREQ);
    if (!cpu_hz) {
        cpu_hz = 100 * 1000 * 1000;
    }
    bus_hz = fw_cfg_read_i32(FW_CFG_PPC_BUSFREQ);
    if (!bus_hz) {
        bus_hz = 400 * 1000 * 1000;
    }
    /* QEMU's 40p time base: 7.8125 MHz RTC on the 601, 100 MHz otherwise */
    tb_hz = ((pvr >> 16) == 1) ? 7812500 : 100 * 1000 * 1000;

    res->processor_hz = cpu_hz;
    res->processor_bus_hz = bus_hz;
    res->timebase_divisor = (uint32_t)(((uint64_t)bus_hz * 1000) / tb_hz);
    res->word_width = 32;
    res->page_size = 4096;
    res->coherence_block_size = iline;
    res->granule_size = iline;
    res->cache_size = (isize + dsize) >> 10;
    res->cache_attrib = 1;              /* split I/D caches */
    res->cache_assoc = 0;
    res->cache_line_size = 0;
    res->icache_size = isize >> 10;
    res->icache_assoc = isize / (iline * isets);
    res->icache_line_size = iline;
    res->dcache_size = dsize >> 10;
    res->dcache_assoc = dsize / (dline * dsets);
    res->dcache_line_size = dline;
    res->tlb_size = tlbsize * 2;
    res->tlb_attrib = 1;                /* split I/D TLBs */
    res->tlb_assoc = 0;
    res->itlb_size = tlbsize;
    res->itlb_assoc = tlbsize / tlbsets;
    res->dtlb_size = tlbsize;
    res->dtlb_assoc = tlbsize / tlbsets;
    res->extended_vpd = 0;

    res->max_cpus = 1;
    res->actual_cpus = 1;
    res->cpus[0].pvr = pvr;
    res->cpus[0].cpu_num = 0;
    res->cpus[0].state = 0;
}

static void residual_add_memory(uint32_t load_base, uint32_t load_size)
{
    ofmem_t *ofmem = ofmem_arch_get_private();
    uint32_t ramsize = ofmem->ramsize;
    uint32_t fw_heap = (uint32_t)(uintptr_t)ofmem;      /* malloc arena */
    uint32_t fw_stack = ofmem_arch_get_heap_top();
    uint32_t hash = mfsdr1() & SDR1_HTABORG_MASK;
    uint32_t load_end;

    load_size = (load_size + 0xfff) & ~0xfff;
    load_end = load_base + load_size;

    res->total_memory = ramsize;
    res->good_memory = ramsize;

    /* RAM, in the same order as the FirmWorks firmware reports it */
    seg_add(RES_MEM_FW_CODE,    0x00000000, 0x00004000);  /* exception vectors */
    seg_add(RES_MEM_FREE,       0x00004000, load_base);
    seg_add(RES_MEM_BOOT_IMAGE, load_base,  load_end);
    seg_add(RES_MEM_FREE,       load_end,   fw_heap);
    seg_add(RES_MEM_FW_HEAP,    fw_heap,    fw_stack);
    seg_add(RES_MEM_FW_STACK,   fw_stack,   hash);
    seg_add(RES_MEM_FW_CODE,    hash,       ramsize);     /* HTAB + firmware */

    /* The rest of the PReP address map */
    seg_add(RES_MEM_UNPOPULATED,                     ramsize,    0x80000000);
    seg_add(RES_MEM_SYSTEM_IO | RES_MEM_ISA_IO,      0x80000000, 0x80800000);
    seg_add(RES_MEM_SYSTEM_IO | RES_MEM_PCI_CONFIG,  0x80800000, 0x81000000);
    seg_add(RES_MEM_SYSTEM_IO | RES_MEM_PCI,         0x81000000, 0xbf800000);
    seg_add(RES_MEM_SYSTEM_IO | RES_MEM_SYSTEM_REGS, 0xbf800000, 0xc0000000);
    seg_add(RES_MEM_IO_MEMORY,                       0xc0000000, 0xff000000);
    seg_add(RES_MEM_UNPOP_ROM,                       0xff000000, 0xfff00000);
    seg_add(RES_MEM_ROM,                             0xfff00000, 0x00000000);

    res->actual_mems = 1;
    res->mems[0] = ramsize >> 20;
    res->nsimm_slots = 1;
}

static void residual_add_pci(void)
{
    phandle_t pci, ph;
    res_dev_desc_t d;
    uint32_t *reg;
    int len;
    uint32_t vendor, device, class, devfn, bus;

    /* Raven host bridge (IBM0A03 on the processor bus) */
    memset(&d, 0, sizeof(d));
    d.bus_id = RES_BUS_PROCESSOR;
    d.dev_id = 0x41d00a03;
    d.serial = -1;
    d.flags = 0x2800;
    d.base_type = 0x06; d.sub_type = 0x04; d.interface = 0x01;
    d.pnp = pnp_host_bridge; d.pnp_len = sizeof(pnp_host_bridge);
    dev_add(&d);

    pci = find_dev("/pci");
    if (!pci) {
        return;
    }

    PUSH(pci);
    fword("child");
    ph = POP();
    for (; ph; PUSH(ph), fword("peer"), ph = POP()) {
        reg = (uint32_t *)get_property(ph, "reg", &len);
        if (!reg || len < 4 || !get_property(ph, "vendor-id", &len)) {
            continue;
        }
        vendor = get_int_property(ph, "vendor-id", &len);
        device = get_int_property(ph, "device-id", &len);
        class = get_int_property(ph, "class-code", &len);
        bus = (reg[0] >> 16) & 0xff;
        devfn = (reg[0] >> 8) & 0xff;

        memset(&d, 0, sizeof(d));
        d.bus_id = RES_BUS_PCI;
        d.dev_id = (device << 16) | vendor;
        d.serial = -1;
        d.flags = 0x4180;      /* enabled, configurable, disableable */
        d.base_type = class >> 16;
        d.sub_type = class >> 8;
        d.interface = class;
        d.bus_access = (bus << 24) | (devfn << 16);

        switch (class >> 8) {
        case 0x0601:           /* i82378 PCI-ISA bridge (IBM0A00) */
            d.dev_id = 0x41d00a00;
            d.flags = 0x2800;
            d.pnp = pnp_isa_bridge; d.pnp_len = sizeof(pnp_isa_bridge);
            break;
        case 0x0100:           /* SCSI controller: IBM001B, bootable */
            if (vendor == 0x1000) {
                d.dev_id = 0x244d001b;
            }
            d.flags = 0x6380;
            break;
        case 0x0300:           /* display adapter: console output */
        case 0x0301:
        case 0x0380:
            d.flags = 0x0184;
            break;
        default:
            break;
        }
        dev_add(&d);
    }
}

static void residual_add_isa(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(isa_devices) / sizeof(isa_devices[0]); i++) {
        dev_add(&isa_devices[i]);
    }
}

residual_t *residual_build(uint32_t load_base, uint32_t load_size)
{
    static const char model[] = "QEMU PReP/40p";

    res = malloc(sizeof(residual_t));
    if (!res) {
        return NULL;
    }
    memset(res, 0, sizeof(*res));
    pnp_used = 0;

    res->length = sizeof(residual_t);
    res->version = 0;
    res->revision = 1;
    res->ec = 0;

    memset(res->model, ' ', sizeof(res->model));
    memcpy(res->model, model, sizeof(model) - 1);
    res->model[sizeof(res->model) - 1] = 0;
    memset(res->serial, ' ', sizeof(res->serial));

    res->fw_supplier = 2;
    res->fw_supports = 0xdc6;
    res->nvram_size = 0x1c00;
    res->endian_switch_method = 1;      /* port 0x92 / Eagle style */
    res->spread_io_method = 0;

    residual_add_cpu();
    residual_add_memory(load_base, load_size);
    residual_add_pci();
    residual_add_isa();

    return res;
}
