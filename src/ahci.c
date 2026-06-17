#include "ahci.h"
#include "mm.h"
#include "lumie.h"

#define CMD_TAB_SIZE 256
#define PRDT_MAX 8

typedef struct {
    u64 dba;
    u32 reserved;
    u32 byte_count;
} __attribute__((packed)) ahci_prd_t;

typedef struct {
    u8 cfis[64];
    u8 acmd[16];
    u8 reserved[48];
    ahci_prd_t prdt[PRDT_MAX];
} __attribute__((packed)) ahci_cmd_table;

typedef struct {
    u32 dw0;
    u32 dw1;
    u64 ctba;
    u32 reserved[4];
} __attribute__((packed)) ahci_cmd_header;

typedef struct {
    ahci_cmd_header headers[32];
} __attribute__((packed)) ahci_cmd_list;

typedef struct {
    u8 fis[256];
} __attribute__((packed)) ahci_fis;

static void pci_outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static u32 pci_inl(u16 port) {
    u32 val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static u32 pci_cfg_read(u8 bus, u8 dev, u8 func, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 0x1F) << 11) | ((u32)(func & 7) << 8) | (off & 0xFC);
    pci_outl(0xCF8, addr);
    return pci_inl(0xCFC);
}
static void pci_cfg_write(u8 bus, u8 dev, u8 func, u8 off, u32 val) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 0x1F) << 11) | ((u32)(func & 7) << 8) | (off & 0xFC);
    pci_outl(0xCF8, addr);
    pci_outl(0xCFC, val);
}

#define AHCI_CAP     0x00
#define AHCI_GHC     0x04
#define AHCI_CAP2    0x24
#define AHCI_PORTS   0x0C
#define GHC_HR       (1 << 0)
#define GHC_AE       (1 << 31)

#define PORT_OFF(i)  (0x100 + (i) * 0x80)
#define PxCLB        0x00
#define PxCLBU       0x04
#define PxFB         0x08
#define PxFBU        0x0C
#define PxIS         0x10
#define PxIE         0x14
#define PxCMD        0x18
#define PxTFD        0x20
#define PxSIG        0x24
#define PxSSTS       0x28
#define PxSCTL       0x2C
#define PxSERR       0x30
#define PxCI         0x38
#define PxACT        0x3C

#define PxCMD_ST     (1 << 0)
#define PxCMD_FRE    (1 << 4)
#define PxCMD_FR     (1 << 14)
#define PxCMD_CR     (1 << 15)
#define PxCMD_SUD    (1 << 1)
#define PxCMD_POD    (1 << 2)

#define SIG_SATA     0x00000101

static volatile u8 *g_abar = NULL;
static int g_ahci_ready = 0;
static int g_active_port = -1;
static u64 g_sector_count = 0;
static ahci_cmd_list *g_cmd_list = NULL;
static ahci_cmd_table *g_cmd_table = NULL;
static ahci_fis *g_fis = NULL;

static u32 reg_read(u32 reg) {
    if (!g_abar) return 0;
    return *(volatile u32*)(g_abar + reg);
}

static void reg_write(u32 reg, u32 val) {
    if (!g_abar) return;
    *(volatile u32*)(g_abar + reg) = val;
}

static u32 port_reg_read(int port, u32 reg) {
    return *(volatile u32*)(g_abar + PORT_OFF(port) + reg);
}

static void port_reg_write(int port, u32 reg, u32 val) {
    *(volatile u32*)(g_abar + PORT_OFF(port) + reg) = val;
}

static void mdelay(u32 ms) {
    for (u32 i = 0; i < ms * 100000; i++)
        __asm__ volatile("pause");
}

static int ahci_find_controller(void) {
    for (u16 bus = 0; bus < 256; bus++) {
        u32 hdr0 = pci_cfg_read((u8)bus, 0, 0, 0);
        if (hdr0 == 0xFFFFFFFF) continue;
        for (u8 dev = 0; dev < 32; dev++) {
            u32 id = pci_cfg_read((u8)bus, dev, 0, 0);
            if (id == 0xFFFFFFFF) continue;
            u8 mf = (pci_cfg_read((u8)bus, dev, 0, 0xC) >> 23) & 1;
            for (u8 func = 0; func < (mf ? 8u : 1u); func++) {
                u32 idf = (func == 0) ? id : pci_cfg_read((u8)bus, dev, func, 0);
                if (idf == 0xFFFFFFFF) continue;
                u32 cr = pci_cfg_read((u8)bus, dev, func, 8);
                u8 class = (cr >> 24) & 0xFF;
                u8 subclass = (cr >> 16) & 0xFF;
                u8 prog_if = (cr >> 8) & 0xFF;
                if (class == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    u32 bar5 = pci_cfg_read((u8)bus, dev, func, 0x24);
                    u64 abar = bar5 & ~0xF;
                    if (abar == 0) continue;

                    u32 cmd = pci_cfg_read((u8)bus, dev, func, 0x04);
                    cmd |= 6;
                    pci_cfg_write((u8)bus, dev, func, 0x04, cmd);

                    g_abar = (volatile u8*)(usize)abar;
                    return 0;
                }
            }
            if (!mf) break;
        }
    }
    return -1;
}

int ahci_init(void) {
    if (g_ahci_ready) return 0;

    if (ahci_find_controller() < 0) return -1;
    if (!g_abar) return -1;

    g_cmd_list = (ahci_cmd_list*)kmalloc(16384);
    if (!g_cmd_list) return -1;
    lumie_memset(g_cmd_list, 0, 16384);

    g_cmd_table = (ahci_cmd_table*)kmalloc(sizeof(ahci_cmd_table));
    if (!g_cmd_table) return -1;
    lumie_memset(g_cmd_table, 0, sizeof(ahci_cmd_table));

    g_fis = (ahci_fis*)kmalloc(256);
    if (!g_fis) return -1;
    lumie_memset(g_fis, 0, 256);

    u32 ghc = reg_read(AHCI_GHC);
    ghc |= GHC_HR;
    reg_write(AHCI_GHC, ghc);
    while (reg_read(AHCI_GHC) & GHC_HR) mdelay(1);

    ghc = reg_read(AHCI_GHC);
    ghc |= GHC_AE;
    reg_write(AHCI_GHC, ghc);

    u32 cap = reg_read(AHCI_CAP);
    u32 ports_impl = reg_read(AHCI_PORTS);
    int max_ports = (cap & 0x1F) + 1;

    for (int p = 0; p < max_ports; p++) {
        if (!(ports_impl & (1 << p))) continue;

        port_reg_write(p, PxSCTL, 0x301);
        mdelay(10);
        port_reg_write(p, PxSCTL, 0);

        u32 sig = port_reg_read(p, PxSIG);
        if (sig != SIG_SATA) continue;

        u32 cmd = port_reg_read(p, PxCMD);
        cmd |= PxCMD_SUD | PxCMD_POD;
        port_reg_write(p, PxCMD, cmd);

        mdelay(10);

        u32 ssts = port_reg_read(p, PxSSTS);
        u8 ipm = (ssts >> 8) & 0x0F;
        u8 det = ssts & 0x0F;
        if (det != 3 || ipm != 1) continue;

        g_active_port = p;

        u64 clb_phys = (u64)(usize)g_cmd_list;
        port_reg_write(p, PxCLB, (u32)(clb_phys & 0xFFFFFFFF));
        port_reg_write(p, PxCLBU, (u32)(clb_phys >> 32));

        u64 fb_phys = (u64)(usize)g_fis;
        port_reg_write(p, PxFB, (u32)(fb_phys & 0xFFFFFFFF));
        port_reg_write(p, PxFBU, (u32)(fb_phys >> 32));

        port_reg_write(p, PxIS, 0xFFFFFFFF);
        port_reg_write(p, PxIE, 0);

        cmd = port_reg_read(p, PxCMD);
        cmd |= PxCMD_FRE;
        port_reg_write(p, PxCMD, cmd);
        mdelay(5);

        cmd = port_reg_read(p, PxCMD);
        cmd |= PxCMD_ST;
        port_reg_write(p, PxCMD, cmd);
        mdelay(5);

        u64 ident_buf = (u64)(usize)kmalloc(512);
        if (!ident_buf) return -1;
        lumie_memset((void*)(usize)ident_buf, 0, 512);

        lumie_memset(g_cmd_table, 0, sizeof(ahci_cmd_table));

        u8 *cfis = g_cmd_table->cfis;
        cfis[0] = 0x27;
        cfis[1] = 0x80;
        cfis[2] = 0xEC;
        cfis[3] = 0;
        cfis[4] = 0;
        cfis[5] = 0;
        cfis[6] = 0;
        cfis[7] = 0xC0;
        cfis[8] = 0;
        cfis[9] = 0;
        cfis[10] = 0;
        cfis[11] = 0;
        cfis[12] = 0;
        cfis[13] = 0;
        cfis[14] = 0;
        cfis[15] = 0;

        g_cmd_table->prdt[0].dba = ident_buf;
        g_cmd_table->prdt[0].byte_count = 512 - 1;

        lumie_memset(g_cmd_list, 0, sizeof(ahci_cmd_list));
        g_cmd_list->headers[0].dw0 = (5 << 0) | (1 << 16);
        g_cmd_list->headers[0].dw1 = (1 << 0);
        g_cmd_list->headers[0].ctba = (u64)(usize)g_cmd_table;

        port_reg_write(p, PxCI, 1);
        while (port_reg_read(p, PxCI) & 1) mdelay(1);

        u32 tfd = port_reg_read(p, PxTFD);
        if (tfd & 0x7F) {
            kfree((void*)(usize)ident_buf);
            return -1;
        }

        u16 *ident = (u16*)(usize)ident_buf;
        if (ident[83] & (1 << 10)) {
            g_sector_count = *(u64*)(ident + 100);
        } else if (ident[49] & (1 << 9)) {
            g_sector_count = *(u32*)(ident + 60);
        } else {
            g_sector_count = *(u32*)(ident + 60);
        }

        kfree((void*)(usize)ident_buf);
        g_ahci_ready = 1;
        return 0;
    }

    return -1;
}

static int ahci_port_io(int port, u8 cmd, u32 lba, u32 count, u64 buf_phys, int is_write) {
    (void)is_write;
    if (!g_ahci_ready || port < 0) return -1;

    lumie_memset(g_cmd_table, 0, sizeof(ahci_cmd_table));

    u8 *cfis = g_cmd_table->cfis;
    cfis[0] = 0x27;
    cfis[1] = 0x80;
    cfis[2] = cmd;
    cfis[3] = 0;
    cfis[4] = (u8)(lba >> 0);
    cfis[5] = (u8)(lba >> 8);
    cfis[6] = (u8)(lba >> 16);
    cfis[7] = 0x40 | (u8)((lba >> 24) & 0x0F);
    cfis[8] = (u8)((u64)lba >> 32);
    cfis[9] = (u8)((u64)lba >> 40);
    cfis[10] = 0;
    cfis[11] = 0;
    cfis[12] = (u8)(count >> 0);
    cfis[13] = (u8)(count >> 8);
    cfis[14] = 0;
    cfis[15] = 0;

    u32 bytes = count * 512;
    int prd_entries = (bytes + 0x400000 - 1) / 0x400000;
    if (prd_entries > PRDT_MAX) return -1;
    if (prd_entries < 1) prd_entries = 1;

    u32 remaining = bytes;
    for (int i = 0; i < prd_entries; i++) {
        g_cmd_table->prdt[i].dba = buf_phys + (u64)i * 0x400000;
        u32 bc = (remaining > 0x400000) ? 0x400000 : remaining;
        g_cmd_table->prdt[i].byte_count = bc - 1;
        remaining -= bc;
    }

    lumie_memset(g_cmd_list, 0, sizeof(ahci_cmd_list));
    g_cmd_list->headers[0].dw0 = (5 << 0) | (prd_entries << 16);
    g_cmd_list->headers[0].dw1 = (prd_entries << 0);
    g_cmd_list->headers[0].ctba = (u64)(usize)g_cmd_table;

    port_reg_write(port, PxIS, 0xFFFFFFFF);

    port_reg_write(port, PxCI, 1);
    while (port_reg_read(port, PxCI) & 1) mdelay(1);

    u32 tfd = port_reg_read(port, PxTFD);
    if (tfd & 0x7F) return -1;

    return 0;
}

int ahci_read_sectors(u32 lba, u32 count, void *buffer) {
    if (!g_ahci_ready || g_active_port < 0) return -1;
    u64 buf_phys = (u64)(usize)buffer;
    return ahci_port_io(g_active_port, 0x25, lba, count, buf_phys, 0);
}

int ahci_write_sectors(u32 lba, u32 count, const void *buffer) {
    if (!g_ahci_ready || g_active_port < 0) return -1;
    u64 buf_phys = (u64)(usize)buffer;
    return ahci_port_io(g_active_port, 0x35, lba, count, buf_phys, 1);
}

u64 ahci_get_sector_count(void) {
    return g_sector_count;
}

int ahci_is_ready(void) {
    return g_ahci_ready;
}
