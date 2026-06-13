#include "rtl8168.h"
#include "lumie.h"

/* ===== PCI config ===== */
static void pci_outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static u32 pci_inl(u16 port) {
    u32 val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static void pci_cfg_write(u8 bus, u8 dev, u8 func, u8 off, u32 val) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 0x1F) << 11) | ((u32)(func & 7) << 8) | (off & 0xFC);
    pci_outl(0xCF8, addr);
    pci_outl(0xCFC, val);
}
static u32 pci_cfg_read(u8 bus, u8 dev, u8 func, u8 off) {
    u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 0x1F) << 11) | ((u32)(func & 7) << 8) | (off & 0xFC);
    pci_outl(0xCF8, addr);
    return pci_inl(0xCFC);
}

/* ===== MMIO registers ===== */
static volatile u8  *g_rtl_mmio = NULL;

void rtl_write8(u32 reg, u8 val) {
    if (!g_rtl_mmio) return;
    *(volatile u8*)(g_rtl_mmio + reg) = val;
}
void rtl_write16(u32 reg, u16 val) {
    if (!g_rtl_mmio) return;
    *(volatile u16*)(g_rtl_mmio + reg) = val;
}
void rtl_write32(u32 reg, u32 val) {
    if (!g_rtl_mmio) return;
    *(volatile u32*)(g_rtl_mmio + reg) = val;
}
u8 rtl_read8(u32 reg) {
    if (!g_rtl_mmio) return 0;
    return *(volatile u8*)(g_rtl_mmio + reg);
}
u16 rtl_read16(u32 reg) {
    if (!g_rtl_mmio) return 0;
    return *(volatile u16*)(g_rtl_mmio + reg);
}
u32 rtl_read32(u32 reg) {
    if (!g_rtl_mmio) return 0;
    return *(volatile u32*)(g_rtl_mmio + reg);
}

/* ===== Packet buffers and descriptor rings ===== */
/* NOTE: These must be 256-byte aligned per RTL8168 requirement */
static rtl_desc g_tx_ring[RTL_TX_RING_SZ] __attribute__((aligned(256)));
static rtl_desc g_rx_ring[RTL_RX_RING_SZ] __attribute__((aligned(256)));
static u8 g_tx_buf[RTL_TX_RING_SZ][RTL_PKT_BUF_SZ] __attribute__((aligned(16)));
static u8 g_rx_buf[RTL_RX_RING_SZ][RTL_PKT_BUF_SZ] __attribute__((aligned(16)));
static int g_tx_cur = 0;
static int g_rx_cur = 0;

/* ===== Public state ===== */
u8 g_rtl_mac[6];
int g_rtl_ready = 0;

#define RTL_DELAY(x) lumie_stall(x)

/* Unlock / lock EEPROM */
static void rtl_eeprom_unlock(void) {
    rtl_write8(RTL_CFG_9346, RTL_9346_UNLOCK);
    RTL_DELAY(150);
}
static void rtl_eeprom_lock(void) {
    rtl_write8(RTL_CFG_9346, RTL_9346_LOCK);
    RTL_DELAY(150);
}

/* ===== Probe for RTL8168 ===== */
int rtl_probe(void) {
    u32 id;
    for (int bus = 0; bus < 256; bus++) {
        u32 hdr0 = pci_cfg_read((u8)bus, 0, 0, 0);
        if (hdr0 == 0xFFFFFFFF || hdr0 == 0) continue;
        for (int dev = 0; dev < 32; dev++) {
            id = pci_cfg_read((u8)bus, (u8)dev, 0, 0);
            if (id == 0xFFFFFFFF || id == 0) continue;
            u16 ven = id & 0xFFFF;
            u16 dev_id = id >> 16;
            if (ven != RTL_VENDOR_ID) continue;
            if (dev_id == RTL_DEVICE_8168 || dev_id == RTL_DEVICE_8411) {
                u8 func = 0;
                u8 mf = (pci_cfg_read((u8)bus, (u8)dev, 0, 0xC) >> 23) & 1;
                int maxf = mf ? 8 : 1;
                for (func = 0; func < maxf; func++) {
                    u32 fid = (func == 0) ? id : pci_cfg_read((u8)bus, (u8)dev, func, 0);
                    if ((fid & 0xFFFF) != RTL_VENDOR_ID) continue;
                    u32 cr = pci_cfg_read((u8)bus, (u8)dev, func, 8);
                    if ((cr & 0xFFFFFF) != 0x020000) continue;
                    u32 bar2 = pci_cfg_read((u8)bus, (u8)dev, func, 0x18);
                    if (!bar2) continue;
                    bar2 &= ~0xF;
                    return rtl_init((u8)bus, (u8)dev, func, bar2);
                }
            }
        }
    }
    return -1;
}

/* ===== Initialize RTL8168 ===== */
int rtl_init(u8 bus, u8 dev, u8 func, u32 bar2) {
    g_rtl_ready = 0;

    /* Enable bus master + MMIO via PCI command register */
    u16 cmd = (u16)(pci_cfg_read(bus, dev, func, 4) & 0xFFFF);
    cmd |= 0x107; /* I/O space, Memory space, Bus Master */
    pci_cfg_write(bus, dev, func, 4, cmd);

    /* Map MMIO */
    g_rtl_mmio = (volatile u8*)(usize)bar2;

    /* Software reset */
    rtl_write8(RTL_CHIP_CMD, RTL_CMD_RESET);
    int timeout = 0;
    while (rtl_read8(RTL_CHIP_CMD) & RTL_CMD_RESET) {
        RTL_DELAY(1000);
        if (++timeout > 100) return -1;
    }

    /* Unlock EEPROM */
    rtl_eeprom_unlock();

    /* Read MAC address */
    for (int i = 0; i < 6; i++)
        g_rtl_mac[i] = rtl_read8(RTL_IDR0 + i);

    /* Set Config1: enable PM, clear wake-on-lan */
    u8 cfg1 = rtl_read8(RTL_CONFIG1);
    cfg1 &= ~0x30;
    cfg1 |= 0x01;
    rtl_write8(RTL_CONFIG1, cfg1);

    /* Power up PHY (clear power-down bit in Config5) */
    u8 cfg5 = rtl_read8(RTL_CONFIG5);
    cfg5 &= ~RTL_CFG5_PHY_PWRDN;
    rtl_write8(RTL_CONFIG5, cfg5);
    RTL_DELAY(10000);

    rtl_eeprom_lock();

    /* Wait for link up */
    {
        int link_timeout = 0;
        while (!(rtl_read8(RTL_PHY_STATUS) & RTL_PHY_LINK_UP)) {
            RTL_DELAY(10000);
            if (++link_timeout > 500) break;
        }
    }

    /* Read HW version from TxConfig before overwriting it */
    u32 txc = rtl_read32(RTL_TX_CONFIG);
    u16 hw_ver = (txc >> 22) & 0x3F;
    u16 cplus_magic = (hw_ver >= RTL_HW_VER_THRESH_G) ? RTL_CPLUS_VER_MAGIC_V2 : RTL_CPLUS_VER_MAGIC_V1;
    lumie_printf(" rtl8168(hw:0x%x,%s)", hw_ver, (hw_ver >= RTL_HW_VER_THRESH_G) ? "G+" : "C-F");

    /* Set TxConfig */
    rtl_write32(RTL_TX_CONFIG, RTL_TXCFG_MXDMA_UNLIMITED);
    /* Set RxConfig */
    rtl_write32(RTL_RX_CONFIG,
        RTL_RXCFG_MXDMA_1024 |
        RTL_RXCFG_ACCEPT_BROADCAST |
        RTL_RXCFG_ACCEPT_MYPHYS);

    /* Set CPlusCmd (magic depends on chip revision) */
    u16 cplus = rtl_read16(RTL_CPLUS_CMD);
    cplus |= cplus_magic | RTL_CPLUS_RXCHKSUM;
    rtl_write16(RTL_CPLUS_CMD, cplus);

    /* Set early TX threshold: transmit when 256 bytes accumulated */
    rtl_write8(RTL_EARLY_TX_THRES, 0x10);

    /* Set interrupt mask (none — polling only) */
    rtl_write16(RTL_INTR_MASK, 0x0000);

    /* Initialize descriptor rings */
    lumie_memset(g_tx_ring, 0, sizeof(g_tx_ring));
    lumie_memset(g_rx_ring, 0, sizeof(g_rx_ring));

    for (int i = 0; i < RTL_TX_RING_SZ; i++) {
        g_tx_ring[i].status = 0;
        g_tx_ring[i].buf_lo = (u32)(usize)g_tx_buf[i];
        g_tx_ring[i].buf_hi = 0;
        g_tx_ring[i].opts = 0;
    }
    /* Mark last TX descriptor as end-of-ring */
    g_tx_ring[RTL_TX_RING_SZ - 1].status |= RTL_EOR_BIT;

    for (int i = 0; i < RTL_RX_RING_SZ; i++) {
        lumie_memset(g_rx_buf[i], 0, RTL_PKT_BUF_SZ);
        g_rx_ring[i].status = RTL_OWN_BIT | RTL_PKT_BUF_SZ;
        g_rx_ring[i].buf_lo = (u32)(usize)g_rx_buf[i];
        g_rx_ring[i].buf_hi = 0;
        g_rx_ring[i].opts = 0;
    }
    /* Mark last RX descriptor as end-of-ring */
    g_rx_ring[RTL_RX_RING_SZ - 1].status |= RTL_EOR_BIT;

    /* Tell NIC about ring addresses */
    rtl_write32(RTL_TX_START_LO, (u32)(usize)g_tx_ring);
    rtl_write32(RTL_TX_START_HI, 0);
    rtl_write32(RTL_RX_START_LO, (u32)(usize)g_rx_ring);
    rtl_write32(RTL_RX_START_HI, 0);

    /* Set ring lengths (in units of descriptors) */
    rtl_write16(RTL_TX_RING_LEN, RTL_TX_RING_SZ);
    rtl_write16(RTL_RX_RING_LEN, RTL_RX_RING_SZ);

    g_tx_cur = 0;
    g_rx_cur = 0;

    /* Enable TX and RX */
    u8 txrx = rtl_read8(RTL_CHIP_CMD);
    txrx |= RTL_CMD_TX_ENB | RTL_CMD_RX_ENB;
    rtl_write8(RTL_CHIP_CMD, txrx);

    /* Clear any pending interrupts */
    rtl_write16(RTL_INTR_STAT, 0xFFFF);

    g_rtl_ready = 1;
    return 0;
}

/* ===== Send a packet ===== */
int rtl_send(const void *buf, u32 len) {
    if (!g_rtl_ready) return -1;
    if (len > RTL_PKT_BUF_SZ) return -1;

    int idx = g_tx_cur;

    /* Wait for descriptor to be free (NIC has sent it) */
    int timeout = 10000;
    while (g_tx_ring[idx].status & RTL_OWN_BIT) {
        RTL_DELAY(10);
        if (--timeout <= 0) return -1;
    }

    /* Copy packet */
    lumie_memcpy(g_tx_buf[idx], buf, len);

    /* Set descriptor */
    g_tx_ring[idx].status = RTL_OWN_BIT | RTL_FS_BIT | RTL_LS_BIT | len;
    if (idx == RTL_TX_RING_SZ - 1)
        g_tx_ring[idx].status |= RTL_EOR_BIT;

    /* Memory barrier: ensure descriptor is visible before polling */
    __asm__ volatile("sfence" ::: "memory");
    /* Tell NIC to transmit (bit 0 = Normal Priority Queue) */
    rtl_write8(RTL_TXPOLL, 0x01);

    g_tx_cur = (idx + 1) % RTL_TX_RING_SZ;
    return 0;
}

/* ===== Receive a packet (polling) ===== */
int rtl_recv(void *buf, u32 *len) {
    if (!g_rtl_ready) return -1;

    int idx = g_rx_cur;

    /* Check if descriptor is owned by us (NIC returned it) */
    if (g_rx_ring[idx].status & RTL_OWN_BIT)
        return -1;

    u32 status = g_rx_ring[idx].status;
    u32 pkt_len = status & 0x3FFF; /* bits 13:0 = frame length */

    /* Check for RX errors */
    if (status & RTL_RX_ERR_MASK)
        goto recycle_desc;

    if (pkt_len > RTL_PKT_BUF_SZ)
        pkt_len = RTL_PKT_BUF_SZ;

    if (pkt_len < 12)
        goto recycle_desc;

    if (len) *len = pkt_len;
    if (buf && pkt_len > 0)
        lumie_memcpy(buf, g_rx_buf[idx], pkt_len);

    /* Return descriptor to NIC and advance */
    lumie_memset(g_rx_buf[idx], 0, RTL_PKT_BUF_SZ);
    g_rx_ring[idx].status = RTL_OWN_BIT | RTL_PKT_BUF_SZ;
    if (idx == RTL_RX_RING_SZ - 1)
        g_rx_ring[idx].status |= RTL_EOR_BIT;
    g_rx_cur = (idx + 1) % RTL_RX_RING_SZ;
    return 0;

recycle_desc:
    lumie_memset(g_rx_buf[idx], 0, RTL_PKT_BUF_SZ);
    g_rx_ring[idx].status = RTL_OWN_BIT | RTL_PKT_BUF_SZ;
    if (idx == RTL_RX_RING_SZ - 1)
        g_rx_ring[idx].status |= RTL_EOR_BIT;
    g_rx_cur = (idx + 1) % RTL_RX_RING_SZ;
    return -1;
}

/* ===== Reset NIC ===== */
void rtl_reset(void) {
    g_rtl_ready = 0;
    rtl_write8(RTL_CHIP_CMD, RTL_CMD_RESET);
    int timeout = 0;
    while (rtl_read8(RTL_CHIP_CMD) & RTL_CMD_RESET) {
        RTL_DELAY(1000);
        if (++timeout > 100) break;
    }
}
