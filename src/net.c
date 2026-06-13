#include "net.h"
#include "terminal.h"
#include "kernel.h"
#include "fat.h"
#include "tls.h"
#include "rtl8168.h"

/* ===== UEFI function typedefs ===== */
typedef efi_status (*efi_bs_lhb_t)(u32, efi_guid*, void*, u64*, efi_handle**);
typedef efi_status (*efi_bs_cc_t)(efi_handle, efi_handle*, efi_handle*, u8);

/* ===== UEFI Protocol GUIDs ===== */
#define EFI_SIMPLE_NETWORK_PROTOCOL_GUID \
    {0xA19832B9, 0xAC25, 0x11D3, {0x9A,0x2D,0x00,0x90,0x27,0x3F,0xC1,0x4D}}

#define EFI_IP4_CONFIG2_PROTOCOL_GUID \
    {0x5B446ED1, 0xE30B, 0x4FAA, {0x87,0x1A,0x36,0x54,0xEC,0xA3,0x60,0x80}}

#define EFI_HTTP_PROTOCOL_GUID \
    {0x7A59B29B, 0x910B, 0x4251, {0xAD,0xE4,0x47,0x34,0x96,0xAA,0x58,0xEE}}

/* ===== Simple Network Protocol ===== */
typedef enum {
    EfiSimpleNetworkStopped = 0,
    EfiSimpleNetworkStarted = 1,
    EfiSimpleNetworkInitialized = 2
} efi_simple_network_state;

typedef struct {
    u32 State;
    u32 HwAddressSize;
    u32 MediaHeaderSize;
    u32 MaxPacketSize;
    u32 NvRamSize;
    u32 NvRamAccessSize;
    u8  PermanentAddress[32];
    u8  CurrentAddress[32];
    u8  BroadcastAddress[32];
    u8  StationAddress[32];
    u32 IfType;
    u8  MacAddressChangeable;
    u8  MultipleTxSupported;
    u8  MediaPresentSupported;
    u8  MediaPresent;
} efi_simple_network_mode;

typedef struct _efi_simple_network_protocol {
    efi_status (*Start)(struct _efi_simple_network_protocol*);
    efi_status (*Stop)(struct _efi_simple_network_protocol*);
    efi_status (*Initialize)(struct _efi_simple_network_protocol*, u8, u32);
    efi_status (*Reset)(struct _efi_simple_network_protocol*, u8);
    efi_status (*Shutdown)(struct _efi_simple_network_protocol*);
    efi_status (*ReceiveFilters)(struct _efi_simple_network_protocol*, u32, u32, u8, u32, u32*);
    efi_status (*StationAddress)(struct _efi_simple_network_protocol*, u8, u8*);
    efi_status (*Statistics)(struct _efi_simple_network_protocol*, u8, u64*, void*);
    efi_status (*MCastIpToMac)(struct _efi_simple_network_protocol*, u8, void*, void*);
    efi_status (*NvData)(struct _efi_simple_network_protocol*, u8, u64, u64, void*);
    efi_status (*GetStatus)(struct _efi_simple_network_protocol*, u32*, void**);
    efi_status (*Transmit)(struct _efi_simple_network_protocol*, u32, u64, void*, u32*, void*, void*, u16*);
    efi_status (*Receive)(struct _efi_simple_network_protocol*, u32*, u64*, void*, u32*, void*, void*, u16*);
    efi_event WaitForPacket;
    efi_simple_network_mode *Mode;
} efi_simple_network_protocol;

/* ===== IP4 Config2 Protocol ===== */
typedef enum {
    EfiIpConfig2Dhcp = 0,
    EfiIpConfig2Static = 1
} efi_ip4_config2_policy;

#define Ip4Config2DataTypeInterfaceInfo 0

typedef struct {
    u8  StationAddress[4];
    u8  SubnetMask[4];
    u8  GatewayAddress[4];
    u8  PrimaryDns[4];
    u8  SecondaryDns[4];
} efi_ip4_config2_interface_info;

typedef struct _efi_ip4_config2_protocol {
    efi_status (*SetData)(struct _efi_ip4_config2_protocol*, u32, u64, void*);
    efi_status (*GetData)(struct _efi_ip4_config2_protocol*, u32*, u64*, void*);
    efi_status (*RegisterDataNotify)(struct _efi_ip4_config2_protocol*, u32, efi_event);
    efi_status (*Start)(struct _efi_ip4_config2_protocol*, efi_ip4_config2_policy, efi_event*);
    efi_status (*Stop)(struct _efi_ip4_config2_protocol*);
} efi_ip4_config2_protocol;

/* ===== HTTP Protocol (UEFI 2.5+) ===== */
typedef enum {
    EfiHttpMethodGet,
    EfiHttpMethodPost,
    EfiHttpMethodPatch,
    EfiHttpMethodOptions,
    EfiHttpMethodConnect,
    EfiHttpMethodHead,
    EfiHttpMethodPut,
    EfiHttpMethodDelete,
    EfiHttpMethodTrace
} efi_http_method;

typedef struct {
    char16 *FieldName;
    char16 *FieldValue;
} efi_http_header;

typedef struct {
    efi_http_method Method;
    char16         *Url;
    void            *RequestMessage; /* unused for simple GET */
} efi_http_request_data;

typedef struct {
    u32    StatusCode; /* HTTP status like 200 */
    void  **Headers;   /* not used by us */
    u32    HeaderCount;
} efi_http_response_data;

typedef struct {
    u8      IsRequest; /* 1=request, 0=response */
    union {
        efi_http_request_data  *Request;
        efi_http_response_data *Response;
    } Data;
    efi_http_header *Headers;
    u32              HeaderCount;
    u64              BodyLength;
    void             *Body;
} efi_http_message;

typedef struct {
    efi_event        Event;
    efi_status       Status;
    efi_http_message *Message;
} efi_http_token;

typedef struct _efi_http_protocol {
    efi_status (*GetModeData)(struct _efi_http_protocol*, void*);
    efi_status (*Configure)(struct _efi_http_protocol*, void*);
    efi_status (*Request)(struct _efi_http_protocol*, efi_http_token*);
    efi_status (*Cancel)(struct _efi_http_protocol*, efi_http_token*);
    efi_status (*Response)(struct _efi_http_protocol*, efi_http_token*);
    efi_status (*Poll)(struct _efi_http_protocol*);
} efi_http_protocol;

/* ===== TLS Protocol (UEFI 2.5+) ===== */
#define EFI_TLS_CONFIGURATION_PROTOCOL_GUID \
    {0xB92B20EB, 0x6729, 0x4517, {0x90,0x11,0xC7,0xE3,0x6E,0x3D,0x3C,0xB8}}

#define EFI_TLS_PROTOCOL_GUID \
    {0xCA37CCC1, 0x9EC1, 0x4B7B, {0x8B,0x1B,0x1F,0x2E,0x17,0xD2,0x3B,0x49}}

typedef struct _efi_tls_configuration_protocol {
    efi_status (*SetData)(struct _efi_tls_configuration_protocol*, u32, void*, u32);
    efi_status (*GetData)(struct _efi_tls_configuration_protocol*, u32*, void*, u32*);
} efi_tls_configuration_protocol;

typedef enum {
    EfiTlsPacketTypeData      = 0,
    EfiTlsPacketTypeHandshake = 1,
    EfiTlsPacketTypeAlert     = 2,
} efi_tls_packet_type;

typedef struct _efi_tls_protocol {
    efi_status (*SetSessionData)(struct _efi_tls_protocol*, u32, void*, u32);
    efi_status (*GetSessionData)(struct _efi_tls_protocol*, u32, void*, u32*);
    efi_status (*Handshake)(struct _efi_tls_protocol*);
    efi_status (*ProcessPacket)(struct _efi_tls_protocol*, efi_tls_packet_type*, void*, u32, void**, u32*);
    efi_status (*Close)(struct _efi_tls_protocol*);
} efi_tls_protocol;

/* ===== Internal State ===== */
static efi_http_protocol *g_http = NULL;
static int  g_net_initialized = 0;
static int  g_tls_available = 0;
static u8   g_local_ip[4];
static u8   g_gateway_ip[4];
static u8   g_dns_ip[4];
static u8   g_our_mac[6];
static int  g_use_raw_http = 0;
static efi_simple_network_protocol *g_snp = NULL;
static efi_tls_protocol *g_tls_raw = NULL;

/* ===== URL Table for renet ===== */
static net_url_entry g_renet_urls[] = {
    {"gcc",       "https://mirrors.kernel.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz",   "http://mirrors.kernel.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz"},
    {"g++",       "https://mirrors.kernel.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz",   "http://mirrors.kernel.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz"},
    {"binutils",  "https://mirrors.kernel.org/gnu/binutils/binutils-2.43.tar.xz",       "http://mirrors.kernel.org/gnu/binutils/binutils-2.43.tar.xz"},
    {"make",      "https://mirrors.kernel.org/gnu/make/make-4.4.1.tar.gz",              "http://mirrors.kernel.org/gnu/make/make-4.4.1.tar.gz"},
    {"", "", ""}
};

/* ===== Helpers ===== */
static int ascii_to_utf16(const char *src, char16 *dst, int dst_max) {
    int i;
    for (i = 0; i < dst_max - 1 && src[i]; i++)
        dst[i] = (char16)(u8)src[i];
    dst[i] = 0;
    return i;
}

static void print_ip(const u8 *ip) {
    char buf[4];
    lumie_itoa(ip[0], buf, 10); term_write(buf); term_write(".");
    lumie_itoa(ip[1], buf, 10); term_write(buf); term_write(".");
    lumie_itoa(ip[2], buf, 10); term_write(buf); term_write(".");
    lumie_itoa(ip[3], buf, 10); term_write(buf);
}

/* ===== TLS init ===== */
static int tls_init(void) {
    efi_guid tls_cfg_guid = EFI_TLS_CONFIGURATION_PROTOCOL_GUID;
    efi_tls_configuration_protocol *tls_cfg = NULL;

    efi_status status = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&tls_cfg_guid, NULL, (void**)&tls_cfg);
    if (EFI_ERROR(status) || !tls_cfg) {
        return -1;
    }

    g_tls_available = 1;
    term_write(" TLS:ON");
    return 0;
}

int net_tls_available(void) {
    return g_tls_available;
}

/* ===== Force UEFI to connect PCI network controllers ===== */
static void pci_connect_all(void) {
    efi_guid pci_io_guid = {0x4CFB6450, 0x048E, 0x4F1D, {0x9C, 0x3C, 0xEA, 0x10, 0x04, 0xC1, 0x42, 0x3E}};
    efi_guid pci_rb_guid = {0x2F707EBB, 0x4A1A, 0x11D4, {0x9A, 0x38, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}};

    efi_bs_lhb_t lhb = (efi_bs_lhb_t)g_BS->LocateHandleBuffer;
    efi_bs_cc_t  cc  = (efi_bs_cc_t)g_BS->ConnectController;
    u64 cnt = 0;
    efi_handle *buf = NULL;

    /* Step 1: Connect PCI root bridges so bus driver enumerates devices */
    efi_status st = lhb(2, &pci_rb_guid, NULL, &cnt, &buf);
    if (!EFI_ERROR(st) && buf) {
        for (u64 i = 0; i < cnt; i++)
            cc(buf[i], NULL, NULL, TRUE);
        ((efi_bs_free_pool)g_BS->FreePool)(buf);
        buf = NULL; cnt = 0;
    }

    /* Step 2: Connect SNP / network drivers on all PCI devices */
    st = lhb(2, &pci_io_guid, NULL, &cnt, &buf);
    if (!EFI_ERROR(st) && buf) {
        for (u64 i = 0; i < cnt; i++)
            cc(buf[i], NULL, NULL, TRUE);
        ((efi_bs_free_pool)g_BS->FreePool)(buf);
    }
}

/* ===== PCI config space via port I/O ===== */
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

/* ===== net_init ===== */
int net_init() {
    if (g_net_initialized) return 0;
    efi_status status;
    efi_simple_network_protocol  *snp = NULL;
    efi_ip4_config2_protocol     *ip4cfg2 = NULL;
    g_snp = NULL;
    efi_guid snp_guid    = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
    efi_guid ip4cfg2_guid = EFI_IP4_CONFIG2_PROTOCOL_GUID;
    efi_guid http_guid   = EFI_HTTP_PROTOCOL_GUID;

    term_write("Net: ");

    /* RTL8168 driver ready flag */
    g_rtl_ready = 0;

    /* 0 – Force UEFI to connect PCI network drivers */
    pci_connect_all();

    /* 1 – SNP */
    status = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&snp_guid, NULL, (void**)&snp);
    if (!EFI_ERROR(status) && snp) {
        g_snp = snp;
        lumie_memcpy(g_our_mac, snp->Mode->CurrentAddress, 6);
        if (snp->Mode->State == EfiSimpleNetworkStopped) {
            status = snp->Start(snp);
            if (EFI_ERROR(status)) { term_writeln("snp-start FAIL"); goto http_try; }
        }
        if (snp->Mode->State == EfiSimpleNetworkStarted) {
            status = snp->Initialize(snp, 0, 0);
            if (EFI_ERROR(status)) { term_writeln("snp-init FAIL"); goto http_try; }
        }
        term_write("nic");
        if (snp->Mode->MediaPresent) term_write("(UP)");
        else term_write("(DOWN)");
    } else {
        term_write("no-nic");
        /* Retry: connect absolutely everything, then try SNP again */
        {
            ((efi_bs_cc_t)g_BS->ConnectController)(NULL, NULL, NULL, TRUE);
            status = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&snp_guid, NULL, (void**)&snp);
            if (!EFI_ERROR(status) && snp) {
                g_snp = snp;
                lumie_memcpy(g_our_mac, snp->Mode->CurrentAddress, 6);
            }
        }
        if (g_snp) {
            if (g_snp->Mode->State == EfiSimpleNetworkStopped) {
                status = g_snp->Start(g_snp);
                if (EFI_ERROR(status)) { term_writeln(" snp-start FAIL"); goto http_try; }
            }
            if (g_snp->Mode->State == EfiSimpleNetworkStarted) {
                status = g_snp->Initialize(g_snp, 0, 0);
                if (EFI_ERROR(status)) { term_writeln(" snp-init FAIL"); goto http_try; }
            }
            term_write("(retry)nic");
            if (g_snp->Mode->MediaPresent) term_write("(UP)");
            else term_write("(DOWN)");
        } else {
            /* Scan PCI for network controllers */
            int found_nic = 0;
            for (u16 b = 0; b < 256; b++) {
                u32 hdr0 = pci_cfg_read((u8)b, 0, 0, 0);
                if (hdr0 == 0xFFFFFFFF) continue;
                for (u8 d = 0; d < 32; d++) {
                    u32 id = pci_cfg_read((u8)b, d, 0, 0);
                    if (id == 0xFFFFFFFF) continue;
                    u8 mf = (pci_cfg_read((u8)b, d, 0, 0xC) >> 23) & 1;
                    for (u8 f = 0; f < (mf ? 8u : 1u); f++) {
                        u32 idf = (f == 0) ? id : pci_cfg_read((u8)b, d, f, 0);
                        if (idf == 0xFFFFFFFF) continue;
                        u32 cr = pci_cfg_read((u8)b, d, f, 8);
                        if (((cr >> 24) & 0xFF) == 0x02) {
                            u16 ven = idf & 0xFFFF;
                            u16 dev = idf >> 16;
                            char buf[16];
                            term_write(" pci:");
                            lumie_itoa(ven, buf, 16); term_write(buf);
                            term_write(":");
                            lumie_itoa(dev, buf, 16); term_write(buf);
                            found_nic = 1;
                        }
                    }
                    if (!mf) break;
                }
            }
            /* If we found a NIC but still don't have SNP, retry connecting */
            if (found_nic && !g_snp) {
                term_write(" reconnect");
                ((efi_bs_cc_t)g_BS->ConnectController)(NULL, NULL, NULL, TRUE);
                status = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&snp_guid, NULL, (void**)&snp);
                if (!EFI_ERROR(status) && snp) {
                    g_snp = snp;
                    lumie_memcpy(g_our_mac, snp->Mode->CurrentAddress, 6);
                    if (g_snp->Mode->State == EfiSimpleNetworkStopped) {
                        status = g_snp->Start(g_snp);
                        if (EFI_ERROR(status)) g_snp = NULL;
                    }
                    if (g_snp && g_snp->Mode->State == EfiSimpleNetworkStarted) {
                        status = g_snp->Initialize(g_snp, 0, 0);
                        if (EFI_ERROR(status)) g_snp = NULL;
                    }
                    if (g_snp) {
                        if (g_snp->Mode->MediaPresent) term_write("(UP)");
                        else term_write("(DOWN)");
                    }
                }
            }
        }
    }

    /* 2 – DHCP */
    status = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&ip4cfg2_guid, NULL, (void**)&ip4cfg2);
    if (!EFI_ERROR(status) && ip4cfg2) {
        term_write(" dhcp");
        efi_event dhcp_done = NULL;
        status = ip4cfg2->Start(ip4cfg2, EfiIpConfig2Dhcp, &dhcp_done);
        if (!EFI_ERROR(status) && dhcp_done) {
            int ok = 0;
            typedef efi_status (*efi_bs_check_event_t)(efi_event);
            efi_bs_check_event_t check_evt = (efi_bs_check_event_t)g_BS->CheckEvent;
            for (int tries = 0; tries < 200; tries++) {
                status = check_evt(dhcp_done);
                if (status == EFI_SUCCESS) { ok = 1; break; }
                lumie_stall(50000);
            }
            if (ok) {
                u32 dt = Ip4Config2DataTypeInterfaceInfo;
                u64 sz = sizeof(efi_ip4_config2_interface_info);
                efi_ip4_config2_interface_info iface;
                lumie_memset(&iface, 0, sizeof(iface));
                status = ip4cfg2->GetData(ip4cfg2, &dt, &sz, &iface);
                if (!EFI_ERROR(status) && iface.StationAddress[0] != 0) {
                    g_local_ip[0] = iface.StationAddress[0];
                    g_local_ip[1] = iface.StationAddress[1];
                    g_local_ip[2] = iface.StationAddress[2];
                    g_local_ip[3] = iface.StationAddress[3];
                    g_gateway_ip[0] = iface.GatewayAddress[0];
                    g_gateway_ip[1] = iface.GatewayAddress[1];
                    g_gateway_ip[2] = iface.GatewayAddress[2];
                    g_gateway_ip[3] = iface.GatewayAddress[3];
                    g_dns_ip[0] = iface.PrimaryDns[0];
                    g_dns_ip[1] = iface.PrimaryDns[1];
                    g_dns_ip[2] = iface.PrimaryDns[2];
                    g_dns_ip[3] = iface.PrimaryDns[3];
                    term_write(" ip="); print_ip(g_local_ip);
                    term_write(" gw="); print_ip(g_gateway_ip);
                } else {
                    term_write(" no-ip");
                }
            } else {
                term_write(" timeout");
            }
        } else {
            term_write(" no-dhcp");
        }
    }

http_try:
    /* 3 – HTTP Protocol (UEFI 2.5+) */
    status = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&http_guid, NULL, (void**)&g_http);
    if (EFI_ERROR(status) || !g_http) {
        term_write(" http:unavail");
        /* Fallback A: raw TCP/HTTP via SNP */
        if (g_snp && g_snp->Mode->MediaPresent && g_local_ip[0] != 0 && g_gateway_ip[0] != 0) {
            term_write(" raw-tcp");
            g_use_raw_http = 1;
            /* Try to locate TLS protocol for HTTPS support */
            {
                efi_guid tls_guid = EFI_TLS_PROTOCOL_GUID;
                efi_status tls_st = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&tls_guid, NULL, (void**)&g_tls_raw);
                if (!EFI_ERROR(tls_st) && g_tls_raw) {
                    term_write(" tls:ON");
                }
            }
            g_net_initialized = 1;
            term_writeln("");
            return 0;
        }
        /* Fallback B: raw TCP via SNP with static IP (DHCP failed) */
        if (g_snp && g_snp->Mode->MediaPresent && g_local_ip[0] == 0) {
            term_write(" raw-tcp static");
            g_local_ip[0] = 192; g_local_ip[1] = 168;
            g_local_ip[2] = 1;   g_local_ip[3] = 100;
            g_gateway_ip[0] = 192; g_gateway_ip[1] = 168;
            g_gateway_ip[2] = 1;   g_gateway_ip[3] = 1;
            g_dns_ip[0] = 8; g_dns_ip[1] = 8;
            g_dns_ip[2] = 8; g_dns_ip[3] = 8;
            g_use_raw_http = 1;
            /* Try to locate TLS protocol for HTTPS support */
            {
                efi_guid tls_guid = EFI_TLS_PROTOCOL_GUID;
                efi_status tls_st = ((efi_bs_locate_protocol)g_BS->LocateProtocol)(&tls_guid, NULL, (void**)&g_tls_raw);
                if (!EFI_ERROR(tls_st) && g_tls_raw) term_write(" tls:ON");
            }
            g_net_initialized = 1;
            term_writeln("");
            return 0;
        }
        /* Fallback C: direct RTL8168 driver (no SNP needed) */
        if (!g_snp && rtl_probe() == 0) {
            term_write(" rtl8168");
            lumie_memcpy(g_our_mac, g_rtl_mac, 6);
            if (g_local_ip[0] == 0) {
                g_local_ip[0] = 192; g_local_ip[1] = 168;
                g_local_ip[2] = 1;   g_local_ip[3] = 100;
                g_gateway_ip[0] = 192; g_gateway_ip[1] = 168;
                g_gateway_ip[2] = 1;   g_gateway_ip[3] = 1;
                g_dns_ip[0] = 8; g_dns_ip[1] = 8;
                g_dns_ip[2] = 8; g_dns_ip[3] = 8;
                term_write(" static:192.168.1.100");
            }
            g_use_raw_http = 1;
            g_net_initialized = 1;
            term_writeln("");
            return 0;
        }
        term_writeln("");
        return -1;
    }

    term_write(" http:OK");
    tls_init();
    term_writeln("");
    g_net_initialized = 1;
    return 0;
}

/* =================================================================
 * Raw TCP/HTTP via SNP (fallback when UEFI HTTP Protocol unavailable)
 * ================================================================= */

/* Endian helpers */
static u16 net_swap16(u16 v) { return (v >> 8) | (v << 8); }
static u32 net_swap32(u32 v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

/* Internet checksum (ones' complement) */
static u16 net_checksum(const void *buf, int len, u32 sum) {
    const u16 *p = (const u16*)buf;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const u8*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

/* Ethernet types (already in network byte order) */
#define ETH_ARP 0x0608
#define ETH_IP  0x0008

/* ARP operations */
#define ARP_REQUEST 0x0100
#define ARP_REPLY   0x0200

/* TCP flags */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* IP to u32 (network byte order) */
static u32 ip4(const u8 *ip) {
    return ((u32)ip[0] << 24) | ((u32)ip[1] << 16) | ((u32)ip[2] << 8) | (u32)ip[3];
}

/* Ethernet broadcast address */
static const u8 g_eth_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ------------------- Raw frame I/O ------------------- */
static int raw_send(const void *buf, u32 len) {
    if (g_rtl_ready) return rtl_send(buf, len);
    if (!g_snp) return -1;
    return EFI_ERROR(g_snp->Transmit(g_snp, 0, len, (void*)buf, NULL, NULL, NULL, NULL)) ? -1 : 0;
}

static int raw_recv(void *buf, u64 *len, u64 timeout_us) {
    if (g_rtl_ready) {
        u64 elapsed = 0;
        while (elapsed < timeout_us) {
            u32 rlen = 0;
            if (rtl_recv(buf, &rlen) == 0) {
                *len = rlen;
                return 0;
            }
            lumie_stall(1000);
            elapsed += 1000;
        }
        return -1;
    }
    if (!g_snp) return -1;
    u64 elapsed = 0;
    while (elapsed < timeout_us) {
        u32 hdr_sz = 0;
        u64 pkt_len = 1514;
        if (!EFI_ERROR(g_snp->Receive(g_snp, &hdr_sz, &pkt_len, buf, NULL, NULL, NULL, NULL))) {
            *len = pkt_len;
            return 0;
        }
        lumie_stall(1000);
        elapsed += 1000;
    }
    return -1;
}

/* ------------------- ARP ------------------- */
static int arp_cache_valid = 0;
static u8  arp_cache_mac[6];

static int arp_resolve(u32 target_ip, u8 *mac) {
    if (arp_cache_valid) { lumie_memcpy(mac, arp_cache_mac, 6); return 0; }

    u8 pkt[64];
    lumie_memset(pkt, 0, sizeof(pkt));
    /* Ethernet */
    lumie_memcpy(pkt, g_eth_broadcast, 6);                       /* dst = broadcast */
    lumie_memcpy(pkt + 6, g_our_mac, 6);                           /* src = our MAC */
    *(u16*)(pkt + 12) = ETH_ARP;                                   /* ethertype */
    /* ARP request */
    *(u16*)(pkt + 14) = net_swap16(1);    /* htype: Ethernet */
    *(u16*)(pkt + 16) = net_swap16(0x0800);/* ptype: IPv4 */
    *(pkt + 18) = 6;                       /* hlen */
    *(pkt + 19) = 4;                       /* plen */
    *(u16*)(pkt + 20) = ARP_REQUEST;       /* operation */
    lumie_memcpy(pkt + 22, g_our_mac, 6);  /* sender HA */
    *(u32*)(pkt + 28) = ip4(g_local_ip);   /* sender IP */
    lumie_memset(pkt + 32, 0, 6);          /* target HA */
    *(u32*)(pkt + 38) = target_ip;         /* target IP */

    if (raw_send(pkt, 42) < 0) return -1;

    for (int i = 0; i < 500; i++) {
        u64 rlen = 1514;
        u8 rbuf[1514];
        if (raw_recv(rbuf, &rlen, 10000) == 0 && rlen >= 42) {
            u16 et = *(u16*)(rbuf + 12);
            u16 op = *(u16*)(rbuf + 20);
            u32 spa = *(u32*)(rbuf + 28);
            u8 *sha = rbuf + 22;
            if (et == ETH_ARP && op == ARP_REPLY && spa == target_ip) {
                lumie_memcpy(mac, sha, 6);
                lumie_memcpy(arp_cache_mac, mac, 6);
                arp_cache_valid = 1;
                return 0;
            }
        }
    }
    return -1;
}

/* ------------------- TCP (minimal, single connection) ------------------- */
/* TCP pseudo-header */
typedef struct { u32 saddr; u32 daddr; u8 zero; u8 proto; u16 len; } __attribute__((packed)) tcp_ph_t;

/* Send a TCP segment via gateway */
static int tcp_send_seg(u32 dst_ip, u16 sport, u16 dport, u32 seq, u32 ack,
                        u8 flags, const void *data, u16 dlen, u8 *out_mac) {
    u8 buf[1514];
    u8 gw_mac[6];
    u8 *eth = buf;
    u8 *ip  = buf + 14;
    u8 *tcp = buf + 14 + 20;
    u8 *pay = tcp + 20;
    int total = 14 + 20 + 20 + dlen;
    if (total > 1514) return -1;

    if (arp_resolve(ip4(g_gateway_ip), gw_mac) < 0) return -1;
    if (out_mac) lumie_memcpy(out_mac, gw_mac, 6);

    /* Ethernet */
    lumie_memcpy(eth, gw_mac, 6);
    lumie_memcpy(eth + 6, g_our_mac, 6);
    *(u16*)(eth + 12) = ETH_IP;

    /* IP header */
    lumie_memset(ip, 0, 20);
    ip[0] = 0x45;
    *(u16*)(ip + 2) = net_swap16(20 + 20 + dlen);
    *(u16*)(ip + 4) = net_swap16(1);
    ip[8] = 64;
    ip[9] = 6;
    *(u32*)(ip + 12) = ip4(g_local_ip);
    *(u32*)(ip + 16) = dst_ip;

    /* TCP header */
    *(u16*)(tcp + 0) = net_swap16(sport);
    *(u16*)(tcp + 2) = net_swap16(dport);
    *(u32*)(tcp + 4) = net_swap32(seq);
    *(u32*)(tcp + 8) = net_swap32(ack);
    *(u16*)(tcp + 12) = net_swap16((5 << 12) | flags);
    *(u16*)(tcp + 14) = net_swap16(65535);
    *(u16*)(tcp + 18) = 0;

    if (data && dlen) lumie_memcpy(pay, data, dlen);

    /* TCP checksum */
    {
        u8 csum_buf[1520];
        tcp_ph_t ph;
        ph.saddr = *(u32*)(ip + 12);
        ph.daddr = *(u32*)(ip + 16);
        ph.zero = 0;
        ph.proto = 6;
        ph.len = net_swap16(20 + dlen);
        lumie_memcpy(csum_buf, &ph, sizeof(ph));
        lumie_memcpy(csum_buf + sizeof(ph), tcp, 20 + dlen);
        *(u16*)(tcp + 16) = net_checksum(csum_buf, sizeof(ph) + 20 + dlen, 0);
    }

    /* IP checksum */
    *(u16*)(ip + 10) = net_checksum(ip, 20, 0);

    return raw_send(buf, total);
}

/* Receive a TCP packet matching our connection. Returns TCP payload length or <0 */
static int tcp_recv_pkt(u32 dst_ip, u16 sport, u16 dport, u32 *seq, u32 *ack,
                        u8 *flags, u8 *payload, u32 *paylen, u64 timeout_us) {
    u64 elapsed = 0;
    while (elapsed < timeout_us) {
        u64 rlen = 1514;
        u8 rbuf[1514];
        if (raw_recv(rbuf, &rlen, 10000) < 0) { elapsed += 10000; continue; }
        if (rlen < 14 + 20 + 20) { elapsed += 10000; continue; }

        u16 et = *(u16*)(rbuf + 12);
        if (et != ETH_IP) continue;

        u8 *ip = rbuf + 14;
        if ((ip[0] & 0xF0) != 0x40) continue;
        if (ip[9] != 6) continue;

        u32 src_ip = *(u32*)(ip + 12);
        u32 pkt_dst = *(u32*)(ip + 16);
        if (src_ip != dst_ip || pkt_dst != ip4(g_local_ip)) continue;

        int ip_hlen = (ip[0] & 0x0F) * 4;
        u8 *tcp = rbuf + 14 + ip_hlen;
        u16 pkt_sport = net_swap16(*(u16*)(tcp + 0));
        u16 pkt_dport = net_swap16(*(u16*)(tcp + 2));
        if (pkt_sport != dport || pkt_dport != sport) continue;

        u32 pkt_seq = net_swap32(*(u32*)(tcp + 4));
        u32 pkt_ack = net_swap32(*(u32*)(tcp + 8));
        u16 doff_flags = net_swap16(*(u16*)(tcp + 12));
        u8  pkt_flags = doff_flags & 0xFF;
        int tcp_hlen = ((doff_flags >> 12) & 0x0F) * 4;

        /* Fill output */
        if (seq) *seq = pkt_seq;
        if (ack) *ack = pkt_ack;
        if (flags) *flags = pkt_flags;

        int data_len = (int)net_swap16(*(u16*)(ip + 2)) - ip_hlen - tcp_hlen;
        if (data_len > 0 && payload && paylen) {
            int copy = data_len;
            if (*paylen < (u32)copy) copy = *paylen;
            lumie_memcpy(payload, rbuf + 14 + ip_hlen + tcp_hlen, copy);
            *paylen = copy;
        } else if (paylen) {
            *paylen = 0;
        }
        return data_len;
    }
    return -1;
}

/* TCP connect: SYN → SYN-ACK → ACK. Returns 0 on success. */
static int tcp_connect(u32 dst_ip, u16 sport, u16 dport, u32 *seq, u32 *ack) {
    *seq = 0x12345678;       /* client ISN */
    *ack = 0;

    term_write(" TCP:SYN");
    if (tcp_send_seg(dst_ip, sport, dport, *seq, *ack, TCP_SYN, NULL, 0, NULL) < 0)
        return -1;

    /* Wait for SYN-ACK */
    u32 sa_seq = 0, sa_ack = 0;
    u8 sa_flags = 0;
    if (tcp_recv_pkt(dst_ip, sport, dport, &sa_seq, &sa_ack, &sa_flags, NULL, NULL, 5000000) < 0)
        return -1;
    if (!(sa_flags & TCP_SYN) || !(sa_flags & TCP_ACK))
        return -1;

    *seq = sa_ack;           /* client ISN + 1 */
    *ack = sa_seq + 1;       /* server ISN + 1 */

    term_write(" SYN-ACK");
    if (tcp_send_seg(dst_ip, sport, dport, *seq, *ack, TCP_ACK, NULL, 0, NULL) < 0)
        return -1;

    term_write(" EST");
    return 0;
}

/* TCP send data (with PSH) */
static int tcp_send_data(u32 dst_ip, u16 sport, u16 dport, u32 *seq, u32 ack,
                         const void *data, u16 dlen) {
    if (tcp_send_seg(dst_ip, sport, dport, *seq, ack, TCP_PSH | TCP_ACK, data, dlen, NULL) < 0)
        return -1;
    *seq += dlen;
    return 0;
}

/* TCP receive data. Returns payload length or <0 on error/timeout. */
static int tcp_recv_data(u32 dst_ip, u16 sport, u16 dport, u32 *seq, u32 *ack,
                         u8 *buf, u32 *bufsz, u64 timeout) {
    u32 pkt_seq = 0;
    u8  flags = 0;
    u32 plen = *bufsz;
    int ret = tcp_recv_pkt(dst_ip, sport, dport, &pkt_seq, NULL, &flags, buf, &plen, timeout);
    if (ret < 0) return -1;

    u32 expected_ack = *ack;
    u32 expected_seq = expected_ack;

    /* Only accept packets with the expected sequence number */
    if (pkt_seq != expected_seq) {
        /* Send duplicate ACK to re-sync */
        tcp_send_seg(dst_ip, sport, dport, *seq, *ack, TCP_ACK, NULL, 0, NULL);
        return 0;
    }

    /* Update ack to consume received data */
    *ack = pkt_seq + plen;
    *bufsz = plen;

    /* Send ACK for received data */
    tcp_send_seg(dst_ip, sport, dport, *seq, *ack, TCP_ACK, NULL, 0, NULL);

    return flags & TCP_FIN ? -2 : (int)plen;
}

/* TLS callback wrappers for HTTPS over raw TCP */
static u32 g_tls_dip;
static u16 g_tls_sp, g_tls_dp;
static u32 *g_tls_seq, *g_tls_ack;
static int tls_tcp_snd(const void *d,u32 l){return tcp_send_data(g_tls_dip,g_tls_sp,g_tls_dp,g_tls_seq,*g_tls_ack,d,(u16)l);}
static int tls_tcp_rcv(void *b,u32 *l,u64 t){return tcp_recv_data(g_tls_dip,g_tls_sp,g_tls_dp,g_tls_seq,g_tls_ack,(u8*)b,l,t);}

/* TCP close */
static int tcp_close(u32 dst_ip, u16 sport, u16 dport, u32 *seq, u32 *ack) {
    term_write(" FIN");
    if (tcp_send_seg(dst_ip, sport, dport, *seq, *ack, TCP_FIN | TCP_ACK, NULL, 0, NULL) < 0)
        return -1;
    *seq += 1;
    /* Wait for FIN-ACK or just ACK */
    u32 dummy, dummy2;
    u8 flags;
    for (int i = 0; i < 50; i++) {
        if (tcp_recv_pkt(dst_ip, sport, dport, &dummy, &dummy2, &flags, NULL, NULL, 100000) == 0) {
            if (flags & TCP_FIN) {
                *ack = dummy + 1;
                tcp_send_seg(dst_ip, sport, dport, *seq, *ack, TCP_ACK, NULL, 0, NULL);
                term_write("-ACK");
                break;
            }
            if (flags & TCP_ACK) {
                *ack = dummy2;
            }
        }
    }
    term_write(" CLOSED");
    return 0;
}

/* ------------------- DNS (minimal UDP) ------------------- */
/* Build a DNS A-record query for hostname, send via gateway.
   Returns resolved IP in network byte order, or 0 on failure. */
static u32 dns_resolve(const char *hostname) {
    if (!g_dns_ip[0]) return 0;

    u32 dns_ip = ip4(g_dns_ip);
    u16 sport = 0xC000 + (u16)(g_local_ip[3] << 8 | g_local_ip[2]); /* ephemeral-ish */

    u8 pkt[1514];
    u8 *eth = pkt;
    u8 *ip  = pkt + 14;
    u8 *udp = pkt + 14 + 20;
    u8 *dns = pkt + 14 + 20 + 8;
    u8 gw_mac[6];

    if (arp_resolve(ip4(g_gateway_ip), gw_mac) < 0) return 0;

    /* Build DNS query */
    u16 txid = net_swap16(0x1234);
    u16 flags = net_swap16(0x0100); /* standard query, recursion desired */
    u16 qdcount = net_swap16(1);
    u16 ancount = 0;
    u16 nscount = 0;
    u16 arcount = 0;

    lumie_memcpy(dns, &txid, 2);
    lumie_memcpy(dns + 2, &flags, 2);
    lumie_memcpy(dns + 4, &qdcount, 2);
    lumie_memcpy(dns + 6, &ancount, 2);
    lumie_memcpy(dns + 8, &nscount, 2);
    lumie_memcpy(dns + 10, &arcount, 2);

    /* Encode hostname as DNS labels */
    int dpos = 12;
    const char *h = hostname;
    while (*h) {
        const char *dot = h;
        while (*dot && *dot != '.') dot++;
        int labellen = (int)(dot - h);
        dns[dpos++] = (u8)labellen;
        lumie_memcpy(dns + dpos, h, labellen);
        dpos += labellen;
        h = *dot ? dot + 1 : dot;
    }
    dns[dpos++] = 0;
    /* QTYPE = A (1), QCLASS = IN (1) */
    *(u16*)(dns + dpos) = net_swap16(1); dpos += 2;
    *(u16*)(dns + dpos) = net_swap16(1); dpos += 2;
    int dns_len = dpos;

    /* UDP header */
    *(u16*)(udp + 0) = net_swap16(sport);
    *(u16*)(udp + 2) = net_swap16(53);
    *(u16*)(udp + 4) = net_swap16(8 + dns_len);
    *(u16*)(udp + 6) = 0;

    /* UDP checksum */
    {
        u8 csum_buf[1520];
        u32 buflen = 12 + 8 + dns_len;
        /* pseudo-header */
        *(u32*)(csum_buf + 0) = ip4(g_local_ip);
        *(u32*)(csum_buf + 4) = dns_ip;
        *(csum_buf + 8) = 0;
        *(csum_buf + 9) = 17;
        *(u16*)(csum_buf + 10) = net_swap16(8 + dns_len);
        lumie_memcpy(csum_buf + 12, udp, 8 + dns_len);
        *(u16*)(udp + 6) = net_checksum(csum_buf, buflen, 0);
    }

    /* IP header */
    lumie_memset(ip, 0, 20);
    ip[0] = 0x45;
    *(u16*)(ip + 2) = net_swap16(20 + 8 + dns_len);
    *(u16*)(ip + 4) = net_swap16(2);
    ip[8] = 64;
    ip[9] = 17;
    *(u32*)(ip + 12) = ip4(g_local_ip);
    *(u32*)(ip + 16) = dns_ip;
    *(u16*)(ip + 10) = net_checksum(ip, 20, 0);

    /* Ethernet */
    lumie_memcpy(eth, gw_mac, 6);
    lumie_memcpy(eth + 6, g_our_mac, 6);
    *(u16*)(eth + 12) = ETH_IP;

    int total = 14 + 20 + 8 + dns_len;
    if (raw_send(pkt, total) < 0) return 0;

    /* Receive DNS response */
    for (int i = 0; i < 200; i++) {
        u64 rlen = 1514;
        u8 rbuf[1514];
        if (raw_recv(rbuf, &rlen, 10000) < 0) continue;
        if (rlen < 14 + 20 + 8 + 16) continue;
        if (*(u16*)(rbuf + 12) != ETH_IP) continue;
        u8 *rip = rbuf + 14;
        if (rip[9] != 17) continue;
        u32 rsrc = *(u32*)(rip + 12);
        if (rsrc != dns_ip) continue;
        int ip_hlen = (rip[0] & 0x0F) * 4;
        u8 *rudp = rbuf + 14 + ip_hlen;
        u16 r_sport = net_swap16(*(u16*)(rudp + 0));
        u16 r_dport = net_swap16(*(u16*)(rudp + 2));
        if (r_sport != 53 || r_dport != sport) continue;
        u16 rlen16 = net_swap16(*(u16*)(rudp + 4));
        if (rlen16 < 12 + 16) continue;
        u8 *rdns = rudp + 8;
        /* Check response code (flags byte 2, lower 4 bits should be 0) */
        u16 rflags = net_swap16(*(u16*)(rdns + 2));
        if ((rflags & 0x000F) != 0) break;
        u16 r_ancount = net_swap16(*(u16*)(rdns + 6));
        if (r_ancount == 0) break;
        /* Skip question section */
        int rdpos = 12;
        while (rdpos < 200 && rdns[rdpos] != 0) rdpos += rdns[rdpos] + 1;
        rdpos += 5;
        /* Parse answer section */
        for (int a = 0; a < r_ancount && rdpos + 12 <= rlen16; a++) {
            /* Skip name pointer or labels */
            if (rdns[rdpos] & 0xC0) rdpos += 2; else { while (rdns[rdpos]) rdpos += rdns[rdpos] + 1; rdpos++; }
            u16 atype = net_swap16(*(u16*)(rdns + rdpos)); rdpos += 2;
            rdpos += 2; /* class */
            rdpos += 4; /* TTL */
            u16 rdlen = net_swap16(*(u16*)(rdns + rdpos)); rdpos += 2;
            if (atype == 1 && rdlen == 4) {
                u32 resolved = *(u32*)(rdns + rdpos);
                term_write(" DNS:");
                u8 *b = (u8*)&resolved;
                char dbuf[4];
                lumie_itoa(b[0], dbuf, 10); term_write(dbuf); term_write(".");
                lumie_itoa(b[1], dbuf, 10); term_write(dbuf); term_write(".");
                lumie_itoa(b[2], dbuf, 10); term_write(dbuf); term_write(".");
                lumie_itoa(b[3], dbuf, 10); term_write(dbuf);
                return resolved;
            }
            rdpos += rdlen;
        }
        break;
    }
    return 0;
}

/* ------------------- HTTP GET over raw TCP ------------------- */
static int http_raw_get(const char *hostname, u16 port, const char *path,
                        void **out_buf, u64 *out_size, int use_tls) {
    u32 dst_ip;
    term_write("HTTP:");
    term_write(hostname);

    /* Resolve hostname */
    {
        /* Try parsing as IP first */
        u8 ipb[4];
        int ip_ok = 1, parts = 0, val = -1;
        for (const char *p = hostname; ; p++) {
            if (*p >= '0' && *p <= '9') {
                if (val < 0) val = 0;
                val = val * 10 + (*p - '0');
            } else if (*p == '.' || *p == 0) {
                if (val < 0 || val > 255) { ip_ok = 0; break; }
                ipb[parts++] = (u8)val;
                val = -1;
                if (*p == 0) break;
            } else { ip_ok = 0; break; }
        }
        if (ip_ok && parts == 4) {
            dst_ip = ip4(ipb);
        } else {
            dst_ip = dns_resolve(hostname);
            if (!dst_ip) {
                term_writeln(" DNS:FAIL");
                return -1;
            }
        }
    }

    u16 sport = 0xB000 + (u16)(g_local_ip[3] | (g_local_ip[2] << 8));
    u32 seq = 0, ack = 0;

    /* TCP connect */
    if (tcp_connect(dst_ip, sport, port, &seq, &ack) < 0) {
        term_writeln(" TCP:FAIL");
        return -1;
    }

    /* TLS handshake (if HTTPS) */
    if (use_tls) {
        g_tls_dip = dst_ip;
        g_tls_sp = sport;
        g_tls_dp = port;
        g_tls_seq = &seq;
        g_tls_ack = &ack;
        tls_stream s;
        s.send = tls_tcp_snd;
        s.recv = tls_tcp_rcv;
        if (tls_connect(&s, hostname, port) < 0) {
            term_writeln(" TLS:FAIL");
            tcp_close(dst_ip, sport, port, &seq, &ack);
            return -1;
        }
        term_write(" HTTPS");
    }

    /* Build HTTP GET request */
    char req[2048];
    int reqlen = lumie_snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: LumieOS/0.1\r\n"
        "Connection: close\r\n"
        "\r\n", path, hostname);
    term_write(" REQ");

    /* Send request */
    int send_ok;
    if (use_tls) {
        send_ok = tls_send(NULL, req, (u16)reqlen);
    } else {
        send_ok = tcp_send_data(dst_ip, sport, port, &seq, ack, req, (u16)reqlen);
    }
    if (send_ok < 0) {
        term_writeln(" SEND:FAIL");
        return -1;
    }

    /* Receive HTTP response */
    u8 buf[1514];
    u8 *resp_buf = NULL;
    u32 resp_buf_sz = 65536;
    u32 resp_pos = 0;
    /* Allocate response buffer from pool instead of stack (avoids chkstk) */
    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(4, resp_buf_sz, (void**)&resp_buf) != 0) {
        term_writeln(" NOMEM");
        return -1;
    }
    int http_ok = 0;
    u64 content_len = 0;
    int chunked = 0;

    term_write(" RECV");
    for (int i = 0; i < 300; i++) {
        u32 plen = sizeof(buf);
        int ret;
        if (use_tls) ret = tls_recv(NULL, buf, &plen, 2000000);
        else ret = tcp_recv_data(dst_ip, sport, port, &seq, &ack, buf, &plen, 2000000);
        if (ret == -2) break; /* FIN received */
        if (ret < 0) {
            if (i == 0) { term_writeln(" TIMEOUT"); goto cleanup_http; }
            break;
        }
        if (ret == 0) continue;

        if (resp_pos + plen > resp_buf_sz) plen = resp_buf_sz - resp_pos;
        lumie_memcpy(resp_buf + resp_pos, buf, plen);
        resp_pos += plen;

        /* Once we have enough data, check HTTP status */
        if (!http_ok && resp_pos >= 12) {
            resp_buf[resp_pos] = 0;
            /* Check for "HTTP/1.X NNN" */
            if (resp_buf[0] == 'H' && resp_buf[4] == '/' && resp_buf[9] == ' ') {
                int scode = (resp_buf[10] - '0') * 100 + (resp_buf[11] - '0') * 10 + (resp_buf[12] - '0');
                if (scode == 200) {
                    http_ok = 1;
                    term_write(" 200");
                } else {
                    char ec[16];
                    lumie_snprintf(ec, sizeof(ec), " %d FAIL", scode);
                    term_writeln(ec);
                    goto cleanup_http;
                }
            }
        }

        /* Look for Content-Length */
        if (content_len == 0 && resp_pos >= 16) {
            resp_buf[resp_pos] = 0;
            char *cl = lumie_strstr((char*)resp_buf, "Content-Length: ");
            if (!cl) cl = lumie_strstr((char*)resp_buf, "content-length: ");
            if (cl) {
                cl += 16;
                content_len = 0;
                while (*cl >= '0' && *cl <= '9') { content_len = content_len * 10 + (*cl - '0'); cl++; }
            }
        }

        /* Check for chunked encoding */
        if (!chunked && resp_pos >= 16) {
            resp_buf[resp_pos] = 0;
            if (lumie_strstr((char*)resp_buf, "Transfer-Encoding: chunked") ||
                lumie_strstr((char*)resp_buf, "transfer-encoding: chunked"))
                chunked = 1;
        }

        /* Find body start (after \r\n\r\n) */
        int body_start = -1;
        resp_buf[resp_pos] = 0;
        for (u32 j = 3; j < resp_pos; j++) {
            if (resp_buf[j-3] == '\r' && resp_buf[j-2] == '\n' && resp_buf[j-1] == '\r' && resp_buf[j] == '\n') {
                body_start = j + 1;
                break;
            }
        }

        if (body_start > 0 && http_ok) {
            u32 body_len = resp_pos - body_start;
            /* Check if we have all data */
            int complete = 0;
            if (content_len > 0 && body_len >= content_len) complete = 1;
            if (chunked) {
                /* Simple check: ends with "0\r\n\r\n" */
                if (body_len >= 5 && lumie_memcmp(resp_buf + resp_pos - 5, "0\r\n\r\n", 5) == 0)
                    complete = 1;
            }
            /* Also if we got FIN, we're done */
            if (ret == -2) complete = 1;

            if (complete) {
                void *final_buf = NULL;
                u64 final_size = body_len;
                if (chunked) {
                    /* De-chunk: minimal inline dechunker */
                    u8 *out = (u8*)resp_buf + body_start;
                    u32 out_pos = 0;
                    u32 in_pos = body_start;
                    while (in_pos < resp_pos) {
                        /* Parse chunk size */
                        u32 chunk_sz = 0;
                        while (in_pos < resp_pos) {
                            char c = (char)resp_buf[in_pos++];
                            if (c >= '0' && c <= '9') chunk_sz = chunk_sz * 16 + (c - '0');
                            else if (c >= 'a' && c <= 'f') chunk_sz = chunk_sz * 16 + (c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') chunk_sz = chunk_sz * 16 + (c - 'A' + 10);
                            else if (c == '\n') break;
                        }
                        if (chunk_sz == 0) break;
                        /* Skip \r\n after chunk size line */
                        if (in_pos < resp_pos && resp_buf[in_pos] == '\r') in_pos++;
                        if (in_pos < resp_pos && resp_buf[in_pos] == '\n') in_pos++;
                        /* Copy chunk data */
                        u32 copy = chunk_sz;
                        if (out_pos + copy > resp_buf_sz) copy = resp_buf_sz - out_pos;
                        lumie_memcpy(out + out_pos, resp_buf + in_pos, copy);
                        out_pos += copy;
                        in_pos += chunk_sz;
                        /* Skip trailing \r\n */
                        if (in_pos < resp_pos && resp_buf[in_pos] == '\r') in_pos++;
                        if (in_pos < resp_pos && resp_buf[in_pos] == '\n') in_pos++;
                    }
                    final_size = out_pos;
                    /* Allocate and copy */
                    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(4, final_size, &final_buf) != 0)
                        goto cleanup_http;
                    lumie_memcpy(final_buf, out, final_size);
                } else {
                    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(4, final_size, &final_buf) != 0)
                        goto cleanup_http;
                    lumie_memcpy(final_buf, resp_buf + body_start, final_size);
                }

                *out_buf = final_buf;
                *out_size = final_size;
                term_write(" DONE");

                /* Close connection */
                if (use_tls) tls_close(NULL);
                tcp_close(dst_ip, sport, port, &seq, &ack);
                term_writeln("");
                ((efi_bs_free_pool)g_BS->FreePool)(resp_buf);
                return 0;
            }
        }
    }

    /* If we got here but have data, return what we got */
    if (http_ok && resp_pos > 0) {
        int body_start = -1;
        for (u32 j = 3; j < resp_pos; j++) {
            if (resp_buf[j-3] == '\r' && resp_buf[j-2] == '\n' && resp_buf[j-1] == '\r' && resp_buf[j] == '\n') {
                body_start = j + 1; break;
            }
        }
        if (body_start > 0) {
            u32 body_len = resp_pos - body_start;
            void *final_buf = NULL;
            if (((efi_bs_allocate_pool)g_BS->AllocatePool)(4, body_len, &final_buf) == 0) {
                lumie_memcpy(final_buf, resp_buf + body_start, body_len);
                *out_buf = final_buf;
                *out_size = body_len;
                ((efi_bs_free_pool)g_BS->FreePool)(resp_buf);
                return 0;
            }
        }
    }

    term_writeln(" FAIL");

cleanup_http:
    if (resp_buf) ((efi_bs_free_pool)g_BS->FreePool)(resp_buf);
    return -1;
}

/* ===== Find Content-Length in response headers ===== */
static u64 parse_content_length(efi_http_header *headers, u32 count) {
    static char16 cl_name[] = L"content-length";
    for (u32 i = 0; i < count; i++) {
        if (!headers[i].FieldName) break;
        /* Convert header name to lowercase for comparison */
        char16 *fn = headers[i].FieldName;
        int match = 1;
        for (int j = 0; cl_name[j]; j++) {
            char16 c = fn[j];
            if (c >= 'A' && c <= 'Z') c += 0x20;
            if (c != cl_name[j]) { match = 0; break; }
        }
        if (!match) continue;
        /* Parse value as number */
        char16 *fv = headers[i].FieldValue;
        u64 val = 0;
        for (int j = 0; fv[j] >= '0' && fv[j] <= '9'; j++)
            val = val * 10 + (fv[j] - '0');
        return val;
    }
    return 0;
}

/* ===== net_download ===== */
int net_download(const char *url_str, void **out_buf, u64 *out_size) {
    if (!g_net_initialized) return -1;

    /* Raw TCP/HTTP fallback */
    if (g_use_raw_http) {
        /* Parse URL to extract host, port, path */
        const char *h = url_str;
        int https = 0;
        if (lumie_strncmp(h, "https://", 8) == 0) { https = 1; h += 8; }
        else if (lumie_strncmp(h, "http://", 7) == 0) { h += 7; }
        u16 port = https ? 443 : 80;
        char host[256];
        char path[1024];
        int hi = 0;
        while (hi < 255 && h[hi] && h[hi] != ':' && h[hi] != '/') {
            host[hi] = h[hi]; hi++;
        }
        host[hi] = 0;
        if (h[hi] == ':') {
            hi++;
            port = 0;
            while (hi < 1023 && h[hi] >= '0' && h[hi] <= '9') {
                port = port * 10 + (h[hi] - '0'); hi++;
            }
        }
        int pi = 0;
        if (h[hi] == '/') {
            while (h[hi] && pi < 1023) path[pi++] = h[hi++];
        } else {
            path[pi++] = '/';
        }
        path[pi] = 0;

        return http_raw_get(host, port, path, out_buf, out_size, https);
    }

    if (!g_http) return -1;

    efi_status status;
    char16 *url_wide = NULL;
    static char16 host_hdr_name[] = L"Host";
    static char16 ua_hdr_name[]   = L"User-Agent";
    static char16 ua_hdr_val[]    = L"LumieOS/0.1";
    char16 host_hdr_val[512];
    efi_http_header req_headers[4];
    efi_http_request_data req_data;
    efi_http_message req_msg;
    efi_http_response_data resp_data;
    efi_http_token token;
    efi_event event = NULL;
    u8  *body_buf = NULL;
    u64  body_size = 0;

    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(4, 2048 * sizeof(char16), (void**)&url_wide) != 0)
        goto cleanup;

    ascii_to_utf16(url_str, url_wide, 2048);

    /* Extract host from URL */
    {
        int i = 0, j;
        while (url_str[i] && url_str[i] != ':') i++;
        if (url_str[i] == ':') i += 3;
        int start = i;
        while (url_str[i] && url_str[i] != '/' && url_str[i] != ':') i++;
        int len = i - start;
        if (len > 511) len = 511;
        for (j = 0; j < len; j++)
            host_hdr_val[j] = (char16)(u8)url_str[start + j];
        host_hdr_val[j] = 0;
    }

    /* Create event */
    status = ((efi_bs_create_event)g_BS->CreateEvent)(0, 0, NULL, NULL, &event);
    if (EFI_ERROR(status)) return -1;

    /* ---- Build Request ---- */
    req_headers[0].FieldName  = host_hdr_name;
    req_headers[0].FieldValue = host_hdr_val;
    req_headers[1].FieldName  = ua_hdr_name;
    req_headers[1].FieldValue = ua_hdr_val;
    req_headers[2].FieldName  = NULL;
    req_headers[2].FieldValue = NULL;

    req_data.Method = EfiHttpMethodGet;
    req_data.Url    = url_wide;
    req_data.RequestMessage = NULL;

    req_msg.IsRequest = 1;
    req_msg.Data.Request  = &req_data;
    req_msg.Headers       = req_headers;
    req_msg.HeaderCount   = 2;
    req_msg.BodyLength    = 0;
    req_msg.Body          = NULL;

    token.Event   = event;
    token.Status  = EFI_SUCCESS;
    token.Message = &req_msg;

    /* Send GET */
    status = g_http->Request(g_http, &token);
    if (EFI_ERROR(status)) goto cleanup;

    { /* Wait */
        u64 idx;
        status = ((efi_bs_wait_for_event)g_BS->WaitForEvent)(1, &event, &idx);
        if (EFI_ERROR(status)) goto cleanup;
        if (EFI_ERROR(token.Status)) { status = token.Status; goto cleanup; }
    }

    /* ---- Receive Response (headers + body in one call) ---- */
    /* First, read response with a small header buffer to get Content-Length */
    /* We use a small initial buffer, then reallocate based on Content-Length */

    /* Initial body buffer: 8 KB for starters */
    body_size = 8192;
    status = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, body_size, (void**)&body_buf);
    if (EFI_ERROR(status)) goto cleanup;

    lumie_memset(&resp_data, 0, sizeof(resp_data));

    {
        efi_http_message resp_msg;
        resp_msg.IsRequest = 0;
        resp_msg.Data.Response = &resp_data;
        resp_msg.Headers       = NULL;
        resp_msg.HeaderCount   = 0;
        resp_msg.BodyLength    = body_size;
        resp_msg.Body          = body_buf;

        token.Event   = event;
        token.Status  = EFI_SUCCESS;
        token.Message = &resp_msg;

        status = g_http->Response(g_http, &token);
        if (EFI_ERROR(status)) goto cleanup;

        { /* Wait */
            u64 idx;
            status = ((efi_bs_wait_for_event)g_BS->WaitForEvent)(1, &event, &idx);
            if (EFI_ERROR(status)) goto cleanup;
            if (EFI_ERROR(token.Status)) { status = token.Status; goto cleanup; }
        }

        /* Check HTTP status */
        if (resp_data.StatusCode != 200) {
            char buf[64];
            lumie_strcpy(buf, "HTTP ");
            lumie_itoa(resp_data.StatusCode, buf + 5, 10);
            term_writeln(buf);
            status = EFI_ERR(1);
            goto cleanup;
        }

        /* If body is larger than initial buffer, reallocate */
        if (resp_msg.BodyLength > body_size) {
            u64 content_len = resp_msg.BodyLength;
            /* Also try Content-Length header if available */
            if (resp_msg.Headers && resp_msg.HeaderCount > 0) {
                u64 cl = parse_content_length(resp_msg.Headers, resp_msg.HeaderCount);
                if (cl > content_len) content_len = cl;
            }
            /* Allocate the real buffer */
            ((efi_bs_free_pool)g_BS->FreePool)(body_buf);
            body_size = content_len;
            status = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, body_size, (void**)&body_buf);
            if (EFI_ERROR(status)) { body_buf = NULL; goto cleanup; }

            /* Read actual body */
            efi_http_message resp2;
            resp2.IsRequest = 0;
            resp2.Data.Response = &resp_data;
            resp2.Headers       = NULL;
            resp2.HeaderCount   = 0;
            resp2.BodyLength    = body_size;
            resp2.Body          = body_buf;

            token.Event   = event;
            token.Status  = EFI_SUCCESS;
            token.Message = &resp2;

            status = g_http->Response(g_http, &token);
            if (EFI_ERROR(status)) goto cleanup;

            {
                u64 idx;
                status = ((efi_bs_wait_for_event)g_BS->WaitForEvent)(1, &event, &idx);
                if (EFI_ERROR(status)) goto cleanup;
                if (EFI_ERROR(token.Status)) { status = token.Status; goto cleanup; }
            }

            *out_buf  = body_buf;
            *out_size = resp2.BodyLength;
        } else {
            /* Body fits in initial buffer – take ownership */
            *out_buf  = body_buf;
            *out_size = resp_msg.BodyLength;
        }

        /* Show status */
        {
            char buf[64];
            lumie_strcpy(buf, "HTTP 200 (");
            lumie_itoa((i64)*out_size, buf + 10, 10);
            lumie_strcat(buf, " bytes)");
            term_writeln(buf);
        }

        ((efi_bs_close_event)g_BS->CloseEvent)(event);
        return (int)*out_size;
    }

cleanup:
    if (url_wide) ((efi_bs_free_pool)g_BS->FreePool)(url_wide);
    if (body_buf) ((efi_bs_free_pool)g_BS->FreePool)(body_buf);
    if (event)    ((efi_bs_close_event)g_BS->CloseEvent)(event);
    *out_buf = NULL;
    *out_size = 0;
    return -1;
}

/* ===== renet command ===== */
static int do_download_and_save(const char *src_name, const char *url) {
    term_write("Downloading "); term_write(src_name);
    term_write(" from "); term_writeln(url);

    void *data = NULL;
    u64   size = 0;
    int   ret  = net_download(url, &data, &size);
    if (ret < 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Download failed");
        term_set_fg(LUMIE_LIGHTGRAY);
        return -1;
    }

    /* Save to filesystem */
    int result = fat_write_file(src_name, data, (u32)size);

    /* Free pool */
    if (data) ((efi_bs_free_pool)g_BS->FreePool)(data);

    if (result == 0) {
        char msg[128];
        lumie_strcpy(msg, "Saved ");
        lumie_strcat(msg, src_name);
        char sz[32];
        lumie_itoa((i64)size, sz, 10);
        lumie_strcat(msg, " (");
        lumie_strcat(msg, sz);
        lumie_strcat(msg, " bytes)");
        term_set_fg(LUMIE_LIGHTGREEN);
        term_writeln(msg);
    } else {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Failed to save (read-only filesystem?)");
    }
    term_set_fg(LUMIE_LIGHTGRAY);
    return result;
}

int net_renet_download(const char *name) {
    if (!name || name[0] == 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: renet <name|url>");
        term_set_fg(LUMIE_LIGHTCYAN);
        term_writeln("Known names:");
        for (int i = 0; g_renet_urls[i].name[0]; i++) {
            term_write("  "); term_writeln(g_renet_urls[i].name);
        }
        term_set_fg(LUMIE_LIGHTGRAY);
        return -1;
    }

    /* Direct URL */
    if (lumie_strncmp(name, "http://", 7) == 0 || lumie_strncmp(name, "https://", 8) == 0) {
        /* Extract filename from URL */
        char fname[256];
        int last = -1;
        for (int i = 0; name[i]; i++) if (name[i] == '/') last = i;
        if (last >= 0) lumie_strcpy(fname, name + last + 1);
        else lumie_strcpy(fname, "downloaded.bin");
        for (int i = 0; fname[i]; i++) { if (fname[i] == '?') { fname[i] = 0; break; } }
        if (fname[0] == 0) lumie_strcpy(fname, "downloaded.bin");
        return do_download_and_save(fname, name);
    }

    /* Look up in URL table */
    for (int i = 0; g_renet_urls[i].name[0]; i++) {
        if (lumie_strcmp(name, g_renet_urls[i].name) == 0) {
            const char *url = (g_tls_available || g_use_raw_http) ? g_renet_urls[i].https_url : g_renet_urls[i].http_url;
            return do_download_and_save(name, url);
        }
    }

    term_set_fg(LUMIE_LIGHTRED);
    term_write("Unknown: "); term_writeln(name);
    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("Known names:");
    for (int i = 0; g_renet_urls[i].name[0]; i++) {
        term_write("  "); term_writeln(g_renet_urls[i].name);
    }
    term_set_fg(LUMIE_LIGHTGRAY);
    return -1;
}
