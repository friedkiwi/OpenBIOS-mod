/*
 * residual.h
 *
 * PReP residual data structures, as passed in r3 to a PReP boot image.
 *
 * The layout follows the PowerPC Reference Platform specification and
 * matches the residual data produced by the FirmWorks Open Firmware
 * for the QEMU 40p machine (cpu/ppc/prep/residdef.fth), which is known
 * to boot AIX 4.3.  All multi-byte fields are big-endian; the PnP
 * resource descriptors in the heap are little-endian as per the
 * Plug and Play ISA specification.
 *
 * Copyright (c) 2004-2005 Jocelyn Mayer
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License V2
 *   as published by the Free Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef PPC_RESIDUAL_H
#define PPC_RESIDUAL_H

#define RES_MAX_CPUS    32
#define RES_MAX_SEGS    64
#define RES_MAX_MEMS    64
#define RES_MAX_DEVS    256
#define RES_PNP_HEAP    (32 * RES_MAX_DEVS * 2)

/* Memory segment usage bits */
#define RES_MEM_FW_STACK      0x0001
#define RES_MEM_FW_HEAP       0x0002
#define RES_MEM_FW_CODE       0x0004
#define RES_MEM_BOOT_IMAGE    0x0008
#define RES_MEM_FREE          0x0010
#define RES_MEM_UNPOPULATED   0x0020
#define RES_MEM_ISA_IO        0x0040
#define RES_MEM_PCI_CONFIG    0x0080
#define RES_MEM_PCI           0x0100
#define RES_MEM_SYSTEM_REGS   0x0200
#define RES_MEM_SYSTEM_IO     0x0400
#define RES_MEM_IO_MEMORY     0x0800
#define RES_MEM_UNPOP_ROM     0x1000
#define RES_MEM_ROM           0x2000

/* Device bus ids */
#define RES_BUS_ISA           0x01
#define RES_BUS_PCI           0x04
#define RES_BUS_PROCESSOR     0x80

/* Device flags */
#define RES_DEV_OUTPUT        0x0001
#define RES_DEV_INPUT         0x0002
#define RES_DEV_CONSOLE_OUT   0x0004
#define RES_DEV_CONSOLE_IN    0x0008
#define RES_DEV_REMOVABLE     0x0010
#define RES_DEV_READ_ONLY     0x0020
#define RES_DEV_POWER_MANAGED 0x0040
#define RES_DEV_DISABLEABLE   0x0080
#define RES_DEV_CONFIGURABLE  0x0100
#define RES_DEV_BOOTABLE      0x0200
#define RES_DEV_DOCK          0x0400
#define RES_DEV_STATIC        0x0800
#define RES_DEV_FAILED        0x1000
#define RES_DEV_INTEGRATED    0x2000
#define RES_DEV_ENABLED       0x4000

typedef struct res_cpu_t {
    uint32_t pvr;
    uint8_t cpu_num;        /* MP interrupt cookie, 0 for UP */
    uint8_t state;          /* 0 good, 1 running FW, 2 inactive, 3 failed */
    uint8_t reserved[2];
} res_cpu_t;

typedef struct res_seg_t {
    uint32_t usage;
    uint32_t base_page;
    uint32_t page_count;
} res_seg_t;

typedef struct res_device_t {
    uint32_t bus_id;
    uint32_t dev_id;        /* bus-specific: PnP EISA id or PCI dev<<16|vendor */
    uint32_t serial;        /* logical unit number, -1 if none */
    uint32_t flags;
    uint8_t base_type;
    uint8_t sub_type;
    uint8_t interface;
    uint8_t spare;
    uint32_t bus_access;    /* PCI: bus#:devfn:0000 */
    uint32_t allocated_offset;   /* offsets into the PnP heap */
    uint32_t possible_offset;
    uint32_t compatible_offset;
} res_device_t;

typedef struct residual_t {
    uint32_t length;
    uint8_t version;
    uint8_t revision;
    uint16_t ec;

    /* Vital product data */
    uint8_t model[32];
    uint8_t serial[16];
    uint8_t reserved0[48];
    uint32_t fw_supplier;
    uint32_t fw_supports;
    uint32_t nvram_size;
    uint32_t nsimm_slots;
    uint16_t endian_switch_method;
    uint16_t spread_io_method;
    uint32_t smp_iar;
    uint32_t ram_err_log_offset;
    uint8_t reserved1[8];
    uint32_t processor_hz;
    uint32_t processor_bus_hz;
    uint8_t reserved2[4];
    uint32_t timebase_divisor;
    uint32_t word_width;
    uint32_t page_size;
    uint32_t coherence_block_size;
    uint32_t granule_size;
    uint32_t cache_size;
    uint32_t cache_attrib;
    uint32_t cache_assoc;
    uint32_t cache_line_size;
    uint32_t icache_size;
    uint32_t icache_assoc;
    uint32_t icache_line_size;
    uint32_t dcache_size;
    uint32_t dcache_assoc;
    uint32_t dcache_line_size;
    uint32_t tlb_size;
    uint32_t tlb_attrib;
    uint32_t tlb_assoc;
    uint32_t itlb_size;
    uint32_t itlb_assoc;
    uint32_t dtlb_size;
    uint32_t dtlb_assoc;
    uint32_t extended_vpd;

    /* Processors */
    uint16_t max_cpus;
    uint16_t actual_cpus;
    res_cpu_t cpus[RES_MAX_CPUS];

    /* Memory */
    uint32_t total_memory;
    uint32_t good_memory;
    uint32_t actual_segs;
    res_seg_t segs[RES_MAX_SEGS];
    uint32_t actual_mems;
    uint32_t mems[RES_MAX_MEMS];   /* SIMM sizes in MB */

    /* Devices */
    uint32_t actual_devices;
    res_device_t devices[RES_MAX_DEVS];
    uint8_t pnp_heap[RES_PNP_HEAP];
} residual_t;

/* Build the residual data for the loaded PReP boot image */
residual_t *residual_build(uint32_t load_base, uint32_t load_size);

/* Lay out the PReP NVRAM header and name the boot device for AIX */
void prep_nvram_setup(void);

#endif /* PPC_RESIDUAL_H */
