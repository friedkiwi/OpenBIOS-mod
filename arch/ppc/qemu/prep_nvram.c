/*
 * PReP NVRAM setup for the QEMU 40p machine
 *
 * The AIX bootstrap finds the device it was booted from through the
 * "fw-boot-device" variable in the Global Environment area of the PReP
 * NVRAM (M48T59 behind ports 0x74/0x75/0x77), whose areas are described
 * by the header at offset 0.  Without it AIX cannot tell the boot type
 * and rc.boot stops with LED code 0xA06.  OpenBIOS keeps its own
 * configuration variables in RAM on this machine, so just before a PReP
 * boot image is started we lay out the header, an empty OS area and a
 * GE area naming the boot device in AIX's notation, the way the
 * FirmWorks Open Firmware port for this machine does (aixnvram.fth,
 * nvramhdr.fth):
 *
 *   /pci@80000000/pci<vendor>,<device>@<dev>,<fn>/{harddisk,cdrom}@<id>,<lun>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "libc/vsprintf.h"
#include "arch/ppc/residual.h"
#include "drivers/drivers.h"

#define NV_ADDR_LO      0x74
#define NV_ADDR_HI      0x75
#define NV_DATA         0x77

#define NV_SIZE_KB      6           /* as reported by the FirmWorks firmware */
#define NV_GE_OFFSET    0x0f8
#define NV_GE_LENGTH    0x908
#define NV_CONFIG_OFF   0x1000
#define NV_OS_OFFSET    0xa00
#define NV_OS_LENGTH    0x200

static void nv_write(unsigned int off, uint8_t val)
{
    outb(off & 0xff, NV_ADDR_LO);
    outb(off >> 8, NV_ADDR_HI);
    outb(val, NV_DATA);
}

static uint8_t rtc_read(uint8_t reg)
{
    outb(reg, 0x70);
    return inb(0x71);
}

/* CRC-16-CCITT (poly 0x1021, init 0xffff) as used for the PReP NVRAM header */
static uint16_t crc_ccitt(uint16_t crc, const uint8_t *buf, unsigned int len)
{
    unsigned int i;

    while (len--) {
        crc ^= *buf++ << 8;
        for (i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

/*
 * The SCSI driver names both disks and CD-ROMs "sd"; tell them apart
 * through the disk and cdrom aliases it registers for them.
 */
static const char *sd_kind(const char *nodepath)
{
    static const char *const cd[] = { "cdrom", "cdrom0", "cdrom1", "cdrom2", "cdrom3" };
    static const char *const hd[] = { "disk", "disk0", "disk1", "disk2", "disk3" };
    phandle_t aliases = find_dev("/aliases");
    const char *p;
    unsigned int i;
    int len;

    for (i = 0; i < sizeof(cd) / sizeof(cd[0]); i++) {
        p = get_property(aliases, cd[i], &len);
        if (p && strcmp(p, nodepath) == 0) {
            return "cdrom";
        }
        p = get_property(aliases, hd[i], &len);
        if (p && strcmp(p, nodepath) == 0) {
            return "harddisk";
        }
    }
    return NULL;
}

/*
 * Convert the Open Firmware boot path (e.g. /pci@80000000/lsi53c810@1/sd@2:1)
 * to the AIX device name.  Returns 0 if the path is not a SCSI disk or
 * CD-ROM behind a PCI adapter.
 */
static int aix_boot_name(const char *bootpath, char *buf, unsigned int len)
{
    char path[256], *p, *dev, *unit, *end;
    phandle_t ph;
    uint32_t vendor, device, reg;
    int plen;
    const char *kind;

    strncpy(path, bootpath, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if ((p = strchr(path, ':')) != NULL) {
        *p = 0;                                 /* drop the arguments */
    }
    kind = sd_kind(path);

    /* /pci@80000000 / <adapter> / <disk|cdrom>@<id> */
    if (path[0] != '/' || (p = strchr(path + 1, '/')) == NULL) {
        return 0;
    }
    p++;
    if ((dev = strchr(p, '/')) == NULL) {
        return 0;
    }
    *dev++ = 0;                                 /* path: adapter node */
    if (strchr(dev, '/') != NULL) {
        return 0;                               /* deeper trees: not on 40p */
    }

    ph = find_dev(path);
    if (!ph || !get_property(ph, "vendor-id", &plen)) {
        return 0;
    }
    vendor = get_int_property(ph, "vendor-id", &plen);
    device = get_int_property(ph, "device-id", &plen);
    reg = get_int_property(ph, "reg", &plen);

    if ((unit = strchr(dev, '@')) == NULL) {
        return 0;
    }
    *unit++ = 0;
    if (!kind) {
        if (strcmp(dev, "cdrom") == 0) {
            kind = "cdrom";
        } else if (strcmp(dev, "disk") == 0) {
            kind = "harddisk";
        } else {
            return 0;
        }
    }
    for (end = unit; *end && *end != ','; end++) {
        ;
    }
    *end = 0;

    snprintf(buf, len, "/pci@80000000/pci%x,%x@%x,%x/%s@%s,0",
             vendor, device, (reg >> 11) & 0x1f, (reg >> 8) & 7, kind, unit);
    return 1;
}

void prep_nvram_setup(void)
{
    uint8_t hdr[NV_OS_OFFSET];
    char aixname[128], ge[64 + sizeof(aixname)];
    const char *bootpath;
    unsigned int i, gelen;
    int len;
    uint16_t crc;

    bootpath = get_property(find_dev("/chosen"), "bootpath", &len);
    if (!bootpath || !aix_boot_name(bootpath, aixname, sizeof(aixname))) {
        printk("No AIX name for boot device %s\n", bootpath ? bootpath : "");
        aixname[0] = 0;
    }

    /* Header and Global Environment area, unused bytes 0xff */
    memset(hdr, 0xff, sizeof(hdr));
    hdr[0] = NV_SIZE_KB >> 8;
    hdr[1] = NV_SIZE_KB & 0xff;
    hdr[2] = 1;                                 /* version */
    hdr[3] = 0;                                 /* revision / last OS: firmware */

    /* Timestamps (BCD yyyy mm dd hh mm ss 00) from the RTC */
    for (i = 0x34; i < 0x5c; i += 8) {
        hdr[i] = 0x20;
        hdr[i + 1] = rtc_read(0x09);
        hdr[i + 2] = rtc_read(0x08);
        hdr[i + 3] = rtc_read(0x07);
        hdr[i + 4] = rtc_read(0x04);
        hdr[i + 5] = rtc_read(0x02);
        hdr[i + 6] = rtc_read(0x00);
        hdr[i + 7] = 0;
    }

    /* Area descriptors */
#define PUT32(off, val) do { \
        hdr[off] = ((val) >> 24) & 0xff; hdr[(off) + 1] = ((val) >> 16) & 0xff; \
        hdr[(off) + 2] = ((val) >> 8) & 0xff; hdr[(off) + 3] = (val) & 0xff; } while (0)
    PUT32(0xc4, NV_GE_OFFSET);  PUT32(0xc8, NV_GE_LENGTH);
    PUT32(0xd4, NV_CONFIG_OFF); PUT32(0xd8, 0);
    PUT32(0xe8, NV_OS_OFFSET);  PUT32(0xec, NV_OS_LENGTH);
#undef PUT32

    /* GE area: "name=value" strings, NUL terminated */
    gelen = 0;
    if (aixname[0]) {
        gelen = snprintf(ge, sizeof(ge), "fw-boot-device=%s", aixname) + 1;
        memcpy(hdr + NV_GE_OFFSET, ge, gelen);
    }

    /* CRC1 covers bytes 0-3 and 9 up to the OS area */
    crc = crc_ccitt(0xffff, hdr, 4);
    crc = crc_ccitt(crc, hdr + 9, NV_OS_OFFSET - 9);
    hdr[4] = crc >> 8;
    hdr[5] = crc;

    for (i = 0; i < sizeof(hdr); i++) {
        nv_write(i, hdr[i]);
    }
    for (i = NV_OS_OFFSET; i < NV_OS_OFFSET + NV_OS_LENGTH; i++) {
        nv_write(i, 0xff);
    }

    if (aixname[0]) {
        printk("PReP NVRAM: fw-boot-device=%s\n", aixname);
    }
}
