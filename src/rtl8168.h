#ifndef RTL8168_H
#define RTL8168_H

#include "efi.h"

/* PCI vendor/device IDs */
#define RTL_VENDOR_ID  0x10EC
#define RTL_DEVICE_8168 0x8168
#define RTL_DEVICE_8411 0x8411

/* BAR2 is MMIO (bar0 = io, bar2 = mmio) */
#define RTL_BAR_MMIO 2

/* Register offsets (MMIO) */
#define RTL_IDR0       0x00
#define RTL_IDR4       0x04
#define RTL_MAR0       0x08
#define RTL_MAR4       0x0C
#define RTL_DTCCR      0x10
#define RTL_CHIP_CMD   0x37
#define RTL_TXPOLL     0x38
#define RTL_INTR_MASK  0x3C
#define RTL_INTR_STAT  0x3E
#define RTL_TX_CONFIG  0x40
#define RTL_RX_CONFIG  0x44
#define RTL_EPROM      0x50
#define RTL_CFG_9346   0x50
#define RTL_CONFIG0    0x51
#define RTL_CONFIG1    0x52
#define RTL_CONFIG2    0x53
#define RTL_CONFIG3    0x54
#define RTL_CONFIG4    0x55
#define RTL_CONFIG5    0x56
#define RTL_TIMERINT   0x58
#define RTL_MSIX       0x5A
#define RTL_MISC       0x5C
#define RTL_MCU_CMD    0x60
#define RTL_RX_MISSED  0x62
#define RTL_OCP        0x68
#define RTL_PHY_STATUS 0x6C
#define RTL_TX_START_LO 0x20
#define RTL_TX_START_HI 0x24
#define RTL_EARLY_TX_THRES 0x2C
#define RTL_RX_BYTE_CNT 0xDA
#define RTL_INTR_MIT   0xE2
#define RTL_RX_START_LO 0xE4
#define RTL_RX_START_HI 0xE8
#define RTL_RX_RING_LEN 0xEA
#define RTL_TX_RING_LEN 0xEE
#define RTL_CPLUS_CMD  0xE0

/* Cfg9346 (EEPROM) bits */
#define RTL_9346_UNLOCK 0xC0
#define RTL_9346_LOCK   0x00

/* Chip command bits */
#define RTL_CMD_TX_ENB  0x04
#define RTL_CMD_RX_ENB  0x08
#define RTL_CMD_RESET   0x10

/* TxConfig bits */
#define RTL_TXCFG_IFG1  (1 << 25)
#define RTL_TXCFG_IFG0  (1 << 24)
#define RTL_TXCFG_DMA   (7 << 8)
#define RTL_TXCFG_MXDMA_1024 (6 << 8)
#define RTL_TXCFG_MXDMA_UNLIMITED (7 << 8)

/* RxConfig bits */
#define RTL_RXCFG_RCR1       (1 << 6)
#define RTL_RXCFG_RCR2       (1 << 7)
#define RTL_RXCFG_RCR3       (1 << 8)
#define RTL_RXCFG_MXDMA_1024 (6 << 8)
#define RTL_RXCFG_NO_BUF     (1 << 11)
#define RTL_RXCFG_ACCEPT_ERR (1 << 5)
#define RTL_RXCFG_ACCEPT_RUNT (1 << 4)
#define RTL_RXCFG_ACCEPT_BROADCAST (1 << 3)
#define RTL_RXCFG_ACCEPT_MULTICAST (1 << 2)
#define RTL_RXCFG_ACCEPT_MYPHYS (1 << 1)
#define RTL_RXCFG_ACCEPT_ALLPHYS (1 << 0)

/* CPlusCmd bits */
#define RTL_CPLUS_RXVLAN   (1 << 6)
#define RTL_CPLUS_RXCHKSUM (1 << 5)
#define RTL_CPLUS_VER_MAGIC_V1 (0x03 << 12)
#define RTL_CPLUS_VER_MAGIC_V2 (0x07 << 12)

/* HW version from TxConfig[29:22] */
#define RTL_HW_VER_THRESH_G 0x40

/* Config5 bits */
#define RTL_CFG5_PHY_PWRDN (1u << 3)

/* PHY Status register */
#define RTL_PHY_LINK_UP    (1u << 0)

/* RX descriptor error bits (RTL8168C+ layout) */
#define RTL_RX_ERR_SUM     (1u << 22)
#define RTL_RX_CRC_ERR     (1u << 20)
#define RTL_RX_AE_ERR      (1u << 19)
#define RTL_RX_RUNT        (1u << 18)
#define RTL_RX_OVERSIZE    (1u << 21)
#define RTL_RX_ERR_MASK    (RTL_RX_ERR_SUM)

/* Descriptor ownership */
#define RTL_OWN_BIT    (1u << 31)
#define RTL_EOR_BIT    (1u << 30)
#define RTL_FS_BIT     (1u << 29)
#define RTL_LS_BIT     (1u << 28)
#define RTL_TX_LS_BIT  (1u << 13)
#define RTL_TX_FS_BIT  (1u << 12)

/* Descriptor ring size */
#define RTL_TX_RING_SZ  4
#define RTL_RX_RING_SZ  4
#define RTL_PKT_BUF_SZ  2048

/* Default MAC for fallback */
extern u8 g_rtl_mac[6];

/* Driver state */
extern int g_rtl_ready;

/* Descriptor structure (16 bytes) — arrays must be 256-byte aligned */
typedef struct {
    u32 status;
    u32 buf_lo;
    u32 buf_hi;
    u32 opts;
} rtl_desc;

/* Functions */
void rtl_write8(u32 reg, u8 val);
void rtl_write16(u32 reg, u16 val);
void rtl_write32(u32 reg, u32 val);
u8   rtl_read8(u32 reg);
u16  rtl_read16(u32 reg);
u32  rtl_read32(u32 reg);

int  rtl_probe(void);
int  rtl_init(u8 bus, u8 dev, u8 func, u32 bar2);
int  rtl_send(const void *buf, u32 len);
int  rtl_recv(void *buf, u32 *len);
void rtl_reset(void);

#endif
