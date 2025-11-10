#include <mmio.h>
#include <axonos.h>
#include <pci.h>
#include <string.h>
#include <stdio.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/tls.h>
#include <paging.h>

static uint32_t default_subnet_mask_host(void);
static uint32_t rx_cur_index = 0;
static int g_e1000_link = 0;

struct net_config g_net_config = {
    .ip = 0,
    .subnet_mask = 0,
    .gateway = 0,
    .dns = 0
};

// глобальные сетевые параметры
static uint32_t dns_cache_count = 0;
static uint16_t dns_transaction_id = 0;
static struct dns_cache_entry dns_cache[DNS_CACHE_SIZE];

volatile uint32_t* e1000_mmio = 0;
uint8_t net_mac_address[6];
uint32_t net_ip_address = 0;
uint8_t net_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint32_t net_dns_server = 0;

static uint8_t dns_query_payload[256];

static uint32_t tx_packets = 0;
static uint32_t rx_packets = 0;
static uint32_t tx_bytes = 0;
static uint32_t rx_bytes = 0;
static uint32_t tx_errors = 0;
static uint32_t rx_errors = 0;

static struct e1000_tx_desc* tx_ring = NULL;
static struct e1000_rx_desc* rx_ring = NULL;
static uint8_t* tx_buffers = NULL;
static uint8_t* rx_buffers = NULL;
static uint64_t tx_ring_phys = 0;
static uint64_t rx_ring_phys = 0;
static uint64_t tx_buffers_phys = 0;
static uint64_t rx_buffers_phys = 0;

#define DMA_POOL_SIZE (512 * 1024)
static uint8_t dma_pool[DMA_POOL_SIZE] __attribute__((aligned(4096)));
static size_t dma_pool_offset = 0;

static void* dma_alloc(size_t size, size_t align, uint64_t* phys_out) {
    size_t aligned = (dma_pool_offset + (align - 1)) & ~(align - 1);
    if (aligned + size > DMA_POOL_SIZE) {
        if (phys_out) *phys_out = 0;
        return NULL;
    }
    void* virt = dma_pool + aligned;
    dma_pool_offset = aligned + size;
    if (phys_out) {
        *phys_out = paging_virt_to_phys((uint64_t)(uintptr_t)virt);
    }
    memset(virt, 0, size);
    return virt;
}

uint16_t htons(uint16_t hostshort) {
    return (hostshort << 8) | (hostshort >> 8);
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) | ((hostlong & 0xFF00) << 8) |
           ((hostlong & 0xFF0000) >> 8) | ((hostlong & 0xFF000000) >> 24);
}

uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}

uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

uint32_t e1000_read(uint32_t reg) {
    if (!e1000_mmio) return 0;
    return *(volatile uint32_t*)((uint8_t*)e1000_mmio + reg);
}

void e1000_write(uint32_t reg, uint32_t value) {
    if (!e1000_mmio) return;
    *(volatile uint32_t*)((uint8_t*)e1000_mmio + reg) = value;
}

uint16_t calculate_checksum(const void* data, uint16_t length) {
    const uint16_t* words = (const uint16_t*)data;
    uint32_t sum = 0;
    
    for (int i = 0; i < length / 2; i++) {
        sum += words[i];
    }
    
    if (length % 2) {
        sum += ((const uint8_t*)data)[length - 1] << 8;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~(uint16_t)sum;
}

uint32_t parse_ip(const char* ip_str) {
    uint32_t ip = 0;
    const char* p = ip_str;
    int parts[4];
    int part_count = 0;
    
    while (*p && part_count < 4) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        
        parts[part_count] = 0;
        while (*p >= '0' && *p <= '9') {
            parts[part_count] = parts[part_count] * 10 + (*p - '0');
            p++;
        }
        part_count++;
    }
    
    if (part_count == 4) {
        uint32_t net = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        ip = ntohl(net);
    }
    
    return ip;
}

int dns_encode_name(const char* name, uint8_t* buffer) {
    int pos = 0;
    const char* start = name;
    
    while (*name) {
        if (*name == '.') {
            buffer[pos++] = name - start;
            memcpy(buffer + pos, start, name - start);
            pos += name - start;
            start = name + 1;
        }
        name++;
    }
    
    if (name > start) {
        buffer[pos++] = name - start;
        memcpy(buffer + pos, start, name - start);
        pos += name - start;
    }
    
    buffer[pos++] = 0;
    return pos;
}

static const uint8_t* dns_skip_name(const uint8_t* base, const uint8_t* ptr, const uint8_t* end) {
    (void)base;
    const uint8_t* p = ptr;
    while (p < end) {
        uint8_t len = *p;
        if (len == 0) {
            return p + 1;
        }
        if ((len & 0xC0) == 0xC0) {
            if (p + 1 >= end) return NULL;
            return p + 2;
        }
        p++;
        if (p + len > end) return NULL;
        p += len;
    }
    return NULL;
}

uint32_t dns_resolve(const char* hostname) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return 0;
    }
    
    for (uint32_t i = 0; i < dns_cache_count; i++) {
        if (strcmp(dns_cache[i].hostname, hostname) == 0) {
            return dns_cache[i].ip;
        }
    }
    
    uint32_t ip = parse_ip(hostname);
    if (ip != 0) {
        kprintf("DNS skip (literal) %s -> %d.%d.%d.%d\n", hostname,
               (htonl(ip) >> 24) & 0xFF, (htonl(ip) >> 16) & 0xFF,
               (htonl(ip) >> 8) & 0xFF, htonl(ip) & 0xFF);
        return ip;
    }
    
    kprintf("Resolving %s via DNS...\n", hostname);
    
    memset(dns_query_payload, 0, sizeof(dns_query_payload));
    struct dns_header* dns = (struct dns_header*)dns_query_payload;
    dns->id = htons(++dns_transaction_id);
    dns->flags = htons(0x0100);
    dns->qdcount = htons(1);
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;
    
    uint8_t* question = dns_query_payload + sizeof(struct dns_header);
    int name_len = dns_encode_name(hostname, question);
    struct dns_question* dns_q = (struct dns_question*)(question + name_len);
    dns_q->qtype = htons(1);
    dns_q->qclass = htons(1);
    
    uint16_t dns_length = (uint16_t)(sizeof(struct dns_header) + name_len + sizeof(struct dns_question));
    if (!udp_send(net_dns_server, UDP_DEFAULT_LOCAL_PORT, 53, dns_query_payload, dns_length)) {
        kprintf("Failed to send DNS query\n");
    } else {
        uint8_t response[512];
        int received = udp_wait(UDP_DEFAULT_LOCAL_PORT, net_dns_server, 53,
                                 response, sizeof(response), UDP_WAIT_DEFAULT_ATTEMPTS);
        if (received >= (int)sizeof(struct dns_header)) {
            struct dns_header* resp = (struct dns_header*)response;
            if (resp->id == dns->id) {
                const uint8_t* ptr = response + sizeof(struct dns_header);
                const uint8_t* end = response + received;
                uint16_t qdcount = ntohs(resp->qdcount);
                uint16_t ancount = ntohs(resp->ancount);

                for (uint16_t qi = 0; qi < qdcount; qi++) {
                    ptr = dns_skip_name(response, ptr, end);
                    if (!ptr || ptr + 4 > end) {
                        ptr = NULL;
                        break;
                    }
                    ptr += 4;
                }

                if (ptr) {
                    for (uint16_t ai = 0; ai < ancount; ai++) {
                        const uint8_t* name_end = dns_skip_name(response, ptr, end);
                        if (!name_end || name_end + 10 > end) {
                            ptr = NULL;
                            break;
                        }

                        uint16_t type = ntohs(*(const uint16_t*)(name_end + 0));
                        uint16_t cls = ntohs(*(const uint16_t*)(name_end + 2));
                        uint32_t ttl = ntohl(*(const uint32_t*)(name_end + 4));
                        uint16_t rdlength = ntohs(*(const uint16_t*)(name_end + 8));
                        const uint8_t* rdata = name_end + 10;

                        if (rdata + rdlength > end) {
                            ptr = NULL;
                            break;
                        }

                        if (type == 1 && cls == 1 && rdlength == 4) {
                            uint32_t resolved_ip = ntohl(*(const uint32_t*)rdata);
                            kprintf("DNS resolved host-order %s -> %d.%d.%d.%d\n", hostname,
                                   (htonl(resolved_ip) >> 24) & 0xFF, (htonl(resolved_ip) >> 16) & 0xFF,
                                   (htonl(resolved_ip) >> 8) & 0xFF, htonl(resolved_ip) & 0xFF);
                            if (dns_cache_count < DNS_CACHE_SIZE) {
                                struct dns_cache_entry* entry = &dns_cache[dns_cache_count++];
                                strncpy(entry->hostname, hostname, sizeof(entry->hostname) - 1);
                                entry->hostname[sizeof(entry->hostname) - 1] = '\0';
                                entry->ip = resolved_ip;
                                entry->timestamp = ttl;
                            }

                            uint32_t print_ip = htonl(resolved_ip);
                            kprintf("Resolved %s -> %d.%d.%d.%d\n", hostname,
                                   (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
                                   (print_ip >> 8) & 0xFF, print_ip & 0xFF);
                            return resolved_ip;
                        }

                        ptr = rdata + rdlength;
                    }
                }
            }
        }
    }
    
    if (strcmp(hostname, "google.com") == 0) return ntohl(0x4A7D461F);
    if (strcmp(hostname, "cloudflare.com") == 0) return ntohl(0x01010101);
    if (strcmp(hostname, "github.com") == 0) return ntohl(0xC01F0F0F);
    
    kprintf("DNS resolution failed for: %s\n", hostname);
    return 0;
}

static void apply_net_config_defaults(void) {
    net_ip_address = g_net_config.ip;
    net_dns_server = g_net_config.dns ? g_net_config.dns : parse_ip("10.0.2.3");
}

static void e1000_init_tx(void) {
    e1000_write(E1000_TCTL, 0);
    
    uint64_t tx_phys = tx_ring_phys ? tx_ring_phys : paging_virt_to_phys((uint64_t)(uintptr_t)tx_ring);
    if (!tx_phys) {
        kprintf("Failed to translate tx_ring to physical address\n");
        return;
    }
    e1000_write(E1000_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
    e1000_write(E1000_TDBAH, (uint32_t)(tx_phys >> 32));
    e1000_write(E1000_TDLEN, E1000_TX_RING_SIZE * sizeof(struct e1000_tx_desc));
    
    for (int i = 0; i < E1000_TX_RING_SIZE; i++) {
        uint8_t* buf = tx_buffers + (size_t)i * E1000_TX_BUFFER_SIZE;
        uint64_t buf_phys = tx_buffers_phys ? (tx_buffers_phys + (uint64_t)i * E1000_TX_BUFFER_SIZE)
                                            : paging_virt_to_phys((uint64_t)(uintptr_t)buf);
        tx_ring[i].buffer_addr = buf_phys;
        tx_ring[i].length = 0;
        tx_ring[i].cso = 0;
        tx_ring[i].cmd = 0;
        tx_ring[i].status = E1000_TXD_STAT_DD;
        tx_ring[i].css = 0;
        tx_ring[i].special = 0;
        memset(buf, 0, E1000_TX_BUFFER_SIZE);
    }
    
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    
    e1000_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << 4) | (0x40 << 12));
    
    e1000_write(E1000_TIPG, 0x0060200A);
    
    kprintf("TX ring: %d descriptors\n", E1000_TX_RING_SIZE);
}

static void e1000_init_rx(void) {
    e1000_write(E1000_RCTL, 0);
    
    uint64_t rx_phys = rx_ring_phys ? rx_ring_phys : paging_virt_to_phys((uint64_t)(uintptr_t)rx_ring);
    if (!rx_phys) {
        kprintf("Failed to translate rx_ring to physical address\n");
        return;
    }
    e1000_write(E1000_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
    e1000_write(E1000_RDBAH, (uint32_t)(rx_phys >> 32));
    e1000_write(E1000_RDLEN, E1000_RX_RING_SIZE * sizeof(struct e1000_rx_desc));
    
    for (int i = 0; i < E1000_RX_RING_SIZE; i++) {
        uint8_t* buf = rx_buffers + (size_t)i * E1000_RX_BUFFER_SIZE;
        uint64_t buf_phys = rx_buffers_phys ? (rx_buffers_phys + (uint64_t)i * E1000_RX_BUFFER_SIZE)
                                            : paging_virt_to_phys((uint64_t)(uintptr_t)buf);
        rx_ring[i].buffer_addr = buf_phys;
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;
        memset(buf, 0, E1000_RX_BUFFER_SIZE);
    }
    
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_RX_RING_SIZE - 1);
    rx_cur_index = 0;
    
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_LPE |
                     E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_SZ_2048;
    e1000_write(E1000_RCTL, rctl);
    
    kprintf("RX ring: %d descriptors\n", E1000_RX_RING_SIZE);
}

static void e1000_reset(void) {
    kprintf("Resetting E1000...\n");
    
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);
    
    int timeout = 1000000;
    while (timeout-- > 0) {
        if (!(e1000_read(E1000_CTRL) & E1000_CTRL_RST)) {
            uint32_t ctrl = e1000_read(E1000_CTRL);
            ctrl |= E1000_CTRL_SLU;
            e1000_write(E1000_CTRL, ctrl);
            kprintf("Reset complete\n");
            return;
        }
    }
    
    kprintf("Reset timeout\n");
}

static void e1000_configure_tx(void) {
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << E1000_TCTL_CT_SHIFT) | (0x40 << E1000_TCTL_COLD_SHIFT);
    e1000_write(E1000_TCTL, tctl);
    e1000_write(E1000_TIPG, 0x0060200A);
}

static void e1000_configure_rx(void) {
    uint32_t rctl = E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_LBM_NO |
                    E1000_RCTL_SBP | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_DPF;
    rctl |= (0 << E1000_RCTL_BSIZE_SHIFT);
    rctl &= ~E1000_RCTL_BSEX;
    e1000_write(E1000_RCTL, rctl);

    e1000_write(E1000_RDTR, 0);
    e1000_write(E1000_RADV, 0);

    uint32_t rxdctl = (1 << E1000_RXDCTL_PTHRESH_SHIFT) |
                      (1 << E1000_RXDCTL_HTHRESH_SHIFT) |
                      (1 << E1000_RXDCTL_WTHRESH_SHIFT) |
                      E1000_RXDCTL_QUEUE_ENABLE;
    e1000_write(E1000_RXDCTL, rxdctl);
    int guard = 100000;
    while (!(e1000_read(E1000_RXDCTL) & E1000_RXDCTL_QUEUE_ENABLE) && guard-- > 0) {
        asm volatile("pause");
    }

    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_RX_RING_SIZE - 1);

    rctl |= E1000_RCTL_EN;
    e1000_write(E1000_RCTL, rctl);
}

static void e1000_configure_flow_control(void) {
    e1000_write(E1000_FCAL, 0x00C28001);
    e1000_write(E1000_FCAH, 0x00000100);
    e1000_write(E1000_FCTTV, 0x00000600);
    e1000_write(E1000_FCRTL, 0x00004000);
    e1000_write(E1000_FCRTH, 0x00004080);
}

static void e1000_clear_mta(void) {
    for (int i = 0; i < 128; i++) {
        e1000_write(E1000_MTA + i * 4, 0);
    }
}

static void e1000_prepare_descriptors(void) {
    if (!tx_ring || !rx_ring || !tx_buffers || !rx_buffers) return;
    for (int i = 0; i < E1000_TX_RING_SIZE; i++) {
        uint8_t* buf = tx_buffers + (size_t)i * E1000_TX_BUFFER_SIZE;
        uint64_t buf_phys = tx_buffers_phys ? (tx_buffers_phys + (uint64_t)i * E1000_TX_BUFFER_SIZE)
                                            : paging_virt_to_phys((uint64_t)(uintptr_t)buf);
        tx_ring[i].buffer_addr = buf_phys;
        tx_ring[i].length = 0;
        tx_ring[i].cso = 0;
        tx_ring[i].cmd = 0;
        tx_ring[i].status = E1000_TXD_STAT_DD;
        tx_ring[i].css = 0;
        tx_ring[i].special = 0;
        memset(buf, 0, E1000_TX_BUFFER_SIZE);
    }
    for (int i = 0; i < E1000_RX_RING_SIZE; i++) {
        uint8_t* buf = rx_buffers + (size_t)i * E1000_RX_BUFFER_SIZE;
        uint64_t buf_phys = rx_buffers_phys ? (rx_buffers_phys + (uint64_t)i * E1000_RX_BUFFER_SIZE)
                                            : paging_virt_to_phys((uint64_t)(uintptr_t)buf);
        rx_ring[i].buffer_addr = buf_phys;
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;
        memset(buf, 0, E1000_RX_BUFFER_SIZE);
    }
}

void e1000_init(void) {
    kprintf("Initializing E1000 network driver...\n");
    
    int device_count = pci_get_device_count();
    pci_device_t* devices = pci_get_devices();
    
    for (int i = 0; i < device_count; i++) {
        pci_device_t* dev = &devices[i];
        
        if (dev->vendor_id == 0x8086 && (dev->device_id == 0x100E || dev->device_id == 0x100F)) {
            kprintf("Found E1000 at %d:%d:%d\n", dev->bus, dev->device, dev->function);
            
            uint32_t bar0 = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x10);
            if ((bar0 & 1) == 0) {
                e1000_mmio = (volatile uint32_t*)(bar0 & ~0xF);
                
                uint32_t cmd = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x04);
                cmd |= (1 << 2) | (1 << 1);
                pci_config_write_dword(dev->bus, dev->device, dev->function, 0x04, cmd);
                
                kprintf("MMIO base: 0x%08x\n", (uint32_t)e1000_mmio);
                
                e1000_reset();
                
                uint32_t ral = e1000_read(E1000_RAL);
                uint32_t rah = e1000_read(E1000_RAH);
                net_mac_address[0] = (ral >> 0) & 0xFF;
                net_mac_address[1] = (ral >> 8) & 0xFF;
                net_mac_address[2] = (ral >> 16) & 0xFF;
                net_mac_address[3] = (ral >> 24) & 0xFF;
                net_mac_address[4] = (rah >> 0) & 0xFF;
                net_mac_address[5] = (rah >> 8) & 0xFF;

                e1000_write(E1000_RAL, ral);
                e1000_write(E1000_RAH, (rah & 0xFFFF) | E1000_RAH_AV);
                
                uint32_t mdicfg = e1000_read(E1000_MDICNFG);
                mdicfg |= E1000_MDICNFG_FS | E1000_MDICNFG_FP;
                e1000_write(E1000_MDICNFG, mdicfg);

                if (!tx_ring) {
                    tx_ring = (struct e1000_tx_desc*)dma_alloc(sizeof(struct e1000_tx_desc) * E1000_TX_RING_SIZE, 16, &tx_ring_phys);
                    if (!tx_ring || !tx_ring_phys) {
                        kprintf("Failed to allocate TX ring DMA memory\n");
                        return;
                    }
                }
                if (!rx_ring) {
                    rx_ring = (struct e1000_rx_desc*)dma_alloc(sizeof(struct e1000_rx_desc) * E1000_RX_RING_SIZE, 16, &rx_ring_phys);
                    if (!rx_ring || !rx_ring_phys) {
                        kprintf("Failed to allocate RX ring DMA memory\n");
                        return;
                    }
                }
                if (!tx_buffers) {
                    tx_buffers = (uint8_t*)dma_alloc(E1000_TX_RING_SIZE * E1000_TX_BUFFER_SIZE, 16, &tx_buffers_phys);
                    if (!tx_buffers || !tx_buffers_phys) {
                        kprintf("Failed to allocate TX buffers DMA memory\n");
                        return;
                    }
                }
                if (!rx_buffers) {
                    rx_buffers = (uint8_t*)dma_alloc(E1000_RX_RING_SIZE * E1000_RX_BUFFER_SIZE, 16, &rx_buffers_phys);
                    if (!rx_buffers || !rx_buffers_phys) {
                        kprintf("Failed to allocate RX buffers DMA memory\n");
                        return;
                    }
                }
 
                qemu_debug_printf("TX ring virt=%p phys=0x%llx RX ring virt=%p phys=0x%llx RX buf virt=%p phys=0x%llx\n",
                                   tx_ring, (unsigned long long)tx_ring_phys,
                                   rx_ring, (unsigned long long)rx_ring_phys,
                                   rx_buffers, (unsigned long long)rx_buffers_phys);
                kprintf("TX ring virt=%p phys=0x%llx RX ring virt=%p phys=0x%llx RX buf virt=%p phys=0x%llx\n",
                        tx_ring, (unsigned long long)tx_ring_phys,
                        rx_ring, (unsigned long long)rx_ring_phys,
                        rx_buffers, (unsigned long long)rx_buffers_phys);
                
                e1000_init_tx();
                e1000_init_rx();
                e1000_prepare_descriptors();
                e1000_configure_flow_control();
                e1000_clear_mta();
                e1000_configure_tx();
                e1000_configure_rx();
 
                arp_init();
                ip_init();
                udp_init();
                tcp_init();
                icmp_init();
                if (g_net_config.ip == 0) {
                    g_net_config.ip = parse_ip("10.0.2.15");
                    g_net_config.subnet_mask = parse_ip("255.255.255.0");
                    g_net_config.gateway = parse_ip("10.0.2.2");
                    g_net_config.dns = parse_ip("10.0.2.3");
                }
                apply_net_config_defaults();
                tls_init();
                
                kprintf("E1000 ready - MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       net_mac_address[0], net_mac_address[1], net_mac_address[2],
                       net_mac_address[3], net_mac_address[4], net_mac_address[5]);

                g_e1000_link = e1000_link_up();
                arp_request(g_net_config.gateway ? g_net_config.gateway : g_net_config.ip);

                return;
            }
        }
    }
    
    qemu_debug_printf("E1000 not found or MMIO BAR invalid\n");
    kprintf("E1000 not found\n");
}

int e1000_send_packet(const uint8_t* data, uint16_t length) {
    if (!e1000_mmio || length == 0 || length > E1000_TX_BUFFER_SIZE) {
        tx_errors++;
        return 0;
    }
    
    uint32_t tx_tail = e1000_read(E1000_TDT);
    uint32_t next_tail = (tx_tail + 1) % E1000_TX_RING_SIZE;
    
    if (next_tail == e1000_read(E1000_TDH)) {
        tx_errors++;
        return 0;
    }
    
    struct e1000_tx_desc* desc = &tx_ring[tx_tail];
    
    int timeout = 10;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
    }
    
    if (timeout <= 0) {
        tx_errors++;
        return 0;
    }
    
    uint8_t* tx_buf = tx_buffers + (size_t)tx_tail * E1000_TX_BUFFER_SIZE;
    memcpy(tx_buf, data, length);
    
    desc->length = length;
    desc->cso = 0;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;
    
    e1000_write(E1000_TDT, next_tail);
    
    tx_packets++;
    tx_bytes += length;
    
    return length;
}

int e1000_receive_packet(uint8_t* buffer, uint16_t* length) {
    if (!e1000_mmio) return 0;
    
    struct e1000_rx_desc* desc = &rx_ring[rx_cur_index];

    if (!(desc->status & E1000_RXD_STAT_DD)) {
        static uint32_t idle_spins = 0;
        idle_spins++;
        if ((idle_spins & 0xFFFF) == 0) {
            uint32_t hw_head = e1000_read(E1000_RDH);
            uint32_t hw_tail = e1000_read(E1000_RDT);
            kprintf("RX idle idx=%u hw_head=%u hw_tail=%u status=%02x\n",
                   rx_cur_index, hw_head, hw_tail, desc->status);
        }
        return 0;
    }
    
    uint32_t hw_head = e1000_read(E1000_RDH);
    uint32_t hw_tail = e1000_read(E1000_RDT);
    kprintf("RX got idx=%u hw_head=%u hw_tail=%u len=%u\n",
           rx_cur_index, hw_head, hw_tail, desc->length);
    
        *length = desc->length;
        if (*length > E1000_RX_BUFFER_SIZE) {
            *length = E1000_RX_BUFFER_SIZE;
            rx_errors++;
        }
        
    uint8_t* rx_buf = rx_buffers + (size_t)rx_cur_index * E1000_RX_BUFFER_SIZE;
    memcpy(buffer, rx_buf, *length);
        
        desc->status = 0;
        
    uint32_t completed = rx_cur_index;
    rx_cur_index = (rx_cur_index + 1) % E1000_RX_RING_SIZE;
    e1000_write(E1000_RDT, completed);
        
        rx_packets++;
        rx_bytes += *length;
        return 1;
}

void e1000_poll(void) {
    while (ip_poll_once()) {}
}

void e1000_handle_interrupt(void) {
    if (!e1000_mmio) return;
    
    uint32_t icr = e1000_read(E1000_ICR);
    
    if (icr & E1000_ICR_LSC) {
        g_e1000_link = e1000_link_up();
        kprintf("Link status changed: %s\n", g_e1000_link ? "UP" : "DOWN");
        if (g_e1000_link) {
            arp_request(g_net_config.gateway ? g_net_config.gateway : g_net_config.ip);
        }
    }
    
    e1000_poll();
}

int e1000_link_up(void) {
    if (!e1000_mmio) return 0;
    return (e1000_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
}

void net_send_arp(const char* ip_str) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return;
    }
    
    uint32_t target_host = parse_ip(ip_str);
    if (target_host == 0) {
        target_host = dns_resolve(ip_str);
        if (target_host == 0) {
            kprintf("Invalid IP/hostname: %s\n", ip_str);
            return;
        }
    }
    
    uint32_t resolved_host = target_host;
    uint32_t mask_host = g_net_config.subnet_mask ? g_net_config.subnet_mask : default_subnet_mask_host();
    if (g_net_config.gateway) {
        if ((target_host & mask_host) != (g_net_config.ip & mask_host)) {
            resolved_host = g_net_config.gateway;
        }
    }

    arp_request(resolved_host);
    uint32_t print_ip = htonl(resolved_host);
    kprintf("ARP request sent for %d.%d.%d.%d (target %s)\n",
           (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
           (print_ip >> 8) & 0xFF, print_ip & 0xFF, ip_str);
}

void net_ping(const char* hostname) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return;
    }
    if (!e1000_link_up()) {
        kprintf("Network link is down\n");
        return;
    }
    
    uint32_t dest_ip = dns_resolve(hostname);
    if (dest_ip == 0) {
        kprintf("Could not resolve: %s\n", hostname);
        return;
    }
    
    uint32_t print_ip = htonl(dest_ip);
    kprintf("Pinging %s (%d.%d.%d.%d)...\n", hostname,
           (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
           (print_ip >> 8) & 0xFF, print_ip & 0xFF);
    
    const char* payload_str = "AxonOS Ping Test";
    uint16_t payload_len = (uint16_t)strlen(payload_str);
    uint16_t ident = 0x1337;
    static uint16_t sequence = 0;
    uint16_t seq = ++sequence;
    
    if (!icmp_send_echo(dest_ip, ident, seq, (const uint8_t*)payload_str, payload_len)) {
        kprintf("Failed to send ICMP request\n");
        return;
    }
    
    uint8_t reply_buffer[128];
    int received = icmp_wait_echo(dest_ip, ident, seq, reply_buffer, sizeof(reply_buffer), ICMP_WAIT_DEFAULT_ATTEMPTS);
    if (received > 0) {
        char text[129];
        int copy = received < 128 ? received : 128;
        memcpy(text, reply_buffer, copy);
        text[copy] = '\0';
        kprintf("Reply from %d.%d.%d.%d: %d bytes data '%s'\n",
               (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
               (print_ip >> 8) & 0xFF, print_ip & 0xFF,
               received, text);
    } else {
        kprintf("Request timed out\n");
    }
}

void e1000_print_stats(void) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return;
    }
    
    kprintf("Network Statistics:\n");
    kprintf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 
           net_mac_address[0], net_mac_address[1], net_mac_address[2],
           net_mac_address[3], net_mac_address[4], net_mac_address[5]);
    uint32_t ip_print = htonl(net_ip_address);
    kprintf("IP: %d.%d.%d.%d\n",
           (ip_print >> 24) & 0xFF, (ip_print >> 16) & 0xFF,
           (ip_print >> 8) & 0xFF, ip_print & 0xFF);
    uint32_t dns_print = htonl(net_dns_server);
    kprintf("DNS: %d.%d.%d.%d\n",
           (dns_print >> 24) & 0xFF, (dns_print >> 16) & 0xFF,
           (dns_print >> 8) & 0xFF, dns_print & 0xFF);
    kprintf("Link: %s\n", e1000_link_up() ? "UP" : "DOWN");
    kprintf("TX: %d packets, %d bytes, %d errors\n", tx_packets, tx_bytes, tx_errors);
    kprintf("RX: %d packets, %d bytes, %d errors\n", rx_packets, rx_bytes, rx_errors);
    kprintf("Protocols: ARP=%d, ICMP=%d, UDP=%d, DNS=%d\n",
           ip_get_arp_packets(), ip_get_icmp_packets(),
           ip_get_udp_packets(), ip_get_dns_packets());
    kprintf("Ring Status: TX Tail=%d, RX Head=%d\n", e1000_read(E1000_TDT), e1000_read(E1000_RDH));
    kprintf("DNS Cache: %d entries\n", dns_cache_count);
}

void net_test_dns(void) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return;
    }
    
    kprintf("Testing DNS resolution...\n");
    
    const char* hosts[] = {
        "google.com",
        "cloudflare.com", 
        "github.com",
        "192.168.1.1",
        NULL
    };
    
    for (int i = 0; hosts[i] != NULL; i++) {
        uint32_t ip = dns_resolve(hosts[i]);
        if (ip != 0) {
            uint32_t print_ip = htonl(ip);
            kprintf("%s -> %d.%d.%d.%d\n", hosts[i],
                   (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
                   (print_ip >> 8) & 0xFF, print_ip & 0xFF);
        } else {
            kprintf("%s -> resolution failed\n", hosts[i]);
        }
    }
}

int net_send_to_server(const char* host, uint16_t port, const uint8_t* data, uint16_t len) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return 0;
    }
    if (!e1000_link_up()) {
        kprintf("Network link is down\n");
        return 0;
    }

    if (!data || len == 0) return 0;

    uint32_t dest_ip = parse_ip(host);
    if (dest_ip == 0) {
        dest_ip = dns_resolve(host);
        if (dest_ip == 0) {
            kprintf("Could not resolve destination: %s\n", host);
            return 0;
        }
    }

    uint8_t dest_mac[6];
    if (!net_resolve_mac(dest_ip, dest_mac)) {
        kprintf("Failed to resolve MAC for destination\n");
        return 0;
    }
    int sent = udp_send(dest_ip, UDP_DEFAULT_LOCAL_PORT, port, data, len);
    if (sent > 0) {
        uint32_t ip_print = htonl(dest_ip);
        kprintf("Sent %d bytes UDP -> %d.%d.%d.%d:%d\n", sent,
                (ip_print >> 24) & 0xFF, (ip_print >> 16) & 0xFF,
                (ip_print >> 8) & 0xFF, ip_print & 0xFF, port);
    } else {
        kprintf("Failed to send UDP packet to %s\n", host);
    }
    return sent;
}

int net_get_from_server(const char* host, uint16_t port, uint8_t* out, uint16_t max_len) {
    if (!e1000_mmio || !out || max_len == 0) return 0;
    if (!e1000_link_up()) {
        kprintf("Network link is down\n");
        return 0;
    }

    uint32_t expected_ip_host = 0;
    if (host && *host) {
        uint32_t expected_ip = parse_ip(host);
        if (expected_ip == 0) {
            expected_ip = dns_resolve(host);
            if (expected_ip == 0) {
                kprintf("Could not resolve source: %s\n", host);
                return 0;
            }
        }
        expected_ip_host = expected_ip;
    }

    return udp_wait(UDP_DEFAULT_LOCAL_PORT, expected_ip_host, port, out, max_len, UDP_WAIT_DEFAULT_ATTEMPTS);
}

const struct net_config* net_get_config(void) {
    return &g_net_config;
}

void net_set_ip(uint32_t ip_hostorder) {
    net_ip_address = ip_hostorder;
    g_net_config.ip = ip_hostorder;
}

void net_set_dns(uint32_t dns_hostorder) {
    net_dns_server = dns_hostorder;
    g_net_config.dns = dns_hostorder;
}

void net_set_subnet(uint32_t mask_hostorder) {
    g_net_config.subnet_mask = mask_hostorder;
}

void net_set_subnet_str(const char* ip_str) {
    uint32_t be = parse_ip(ip_str);
    if (be != 0) {
        g_net_config.subnet_mask = be;
    }
}

void net_set_ip_str(const char* ip_str) {
    uint32_t be = parse_ip(ip_str);
    if (be != 0) {
        net_ip_address = be;
        g_net_config.ip = be;
    }
}

void net_set_dns_str(const char* ip_str) {
    uint32_t be = parse_ip(ip_str);
    if (be != 0) {
        net_dns_server = be;
        g_net_config.dns = be;
    }
}

void net_set_gateway(uint32_t gateway_hostorder) {
    g_net_config.gateway = gateway_hostorder;
}

void net_set_gateway_str(const char* ip_str) {
    uint32_t be = parse_ip(ip_str);
    if (be != 0) {
        g_net_config.gateway = be;
    }
}

static int tls_append(char* buf, int cap, int pos, const char* str) {
    if (!str || cap <= 0) return pos;
    size_t sl = strlen(str);
    if (pos >= cap - 1) return pos;
    if (pos + (int)sl >= cap) sl = cap - pos - 1;
    if (sl <= 0) return pos;
    memcpy(buf + pos, str, sl);
    pos += (int)sl;
    buf[pos] = '\0';
    return pos;
}

void net_https_get(const char* host, const char* path) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return;
    }
    if (!e1000_link_up()) {
        kprintf("Network link is down\n");
        return;
    }
    if (!host || !*host) {
        kprintf("Usage: https <host> [path]\n");
        return;
    }
    const char* req_path = (path && *path) ? path : "/";

    tls_session_t* session = tls_session_alloc();
    if (!session) {
        kprintf("TLS: no free session\n");
        return;
    }

    kprintf("TLS connect to %s:%d\n", host, 443);
    if (!tls_client_connect(session, host, 443)) {
        kprintf("TLS: connect failed\n");
        tls_session_free(session);
        return;
    }

    int handshake_budget = 500000;
    while (handshake_budget-- > 0) {
        tls_result_t r = tls_session_poll(session);
        if (r == TLS_ERR_OK) {
            break;
        }
        if (r == TLS_ERR_WANT_READ || r == TLS_ERR_WANT_WRITE) {
            continue;
        }
        if (r == TLS_ERR_ALERT) {
            kprintf("TLS alert: level=%u desc=%u\n",
                   session->pending_alert_level, session->pending_alert_desc);
            tls_session_close(session);
            tls_session_free(session);
            return;
        }
        kprintf("TLS handshake error (%d)\n", r);
        tls_session_close(session);
        tls_session_free(session);
        return;
    }

    if (session->state != TLS_STATE_ESTABLISHED) {
        kprintf("TLS handshake timeout\n");
        tls_session_close(session);
        tls_session_free(session);
        return;
    }

    kprintf("TLS handshake complete\n");

    char req[512];
    int pos = 0;
    req[0] = '\0';
    pos = tls_append(req, sizeof(req), pos, "GET ");
    pos = tls_append(req, sizeof(req), pos, req_path);
    pos = tls_append(req, sizeof(req), pos, " HTTP/1.1\r\nHost: ");
    pos = tls_append(req, sizeof(req), pos, host);
    pos = tls_append(req, sizeof(req), pos, "\r\nConnection: close\r\nUser-Agent: AxonTLS\r\nAccept: */*\r\n\r\n");

    if (tls_session_write(session, (const uint8_t*)req, pos) < pos) {
        kprintf("TLS write failed\n");
        tls_session_close(session);
        tls_session_free(session);
        return;
    }

    uint8_t buf[512];
    while (1) {
        int r = tls_session_read(session, buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = '\0';
            kprintf("%s", buf);
        } else if (r == 0) {
            break;
        } else {
            kprintf("TLS read error\n");
            break;
        }
    }

    tls_session_close(session);
    tls_session_free(session);
}

void net_test_tcp(const char* host, uint16_t port) {
    if (!e1000_mmio) {
        kprintf("E1000 not initialized\n");
        return;
    }

    uint32_t dest_ip = parse_ip(host);
    if (dest_ip == 0) {
        dest_ip = dns_resolve(host);
        if (dest_ip == 0) {
            kprintf("Could not resolve: %s\n", host);
            return;
        }
    }

    uint32_t print_ip = htonl(dest_ip);
    kprintf("Opening TCP connection to %s:%d (%d.%d.%d.%d)...\n", host, port,
           (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
           (print_ip >> 8) & 0xFF, print_ip & 0xFF);

    if (!tcp_connect(dest_ip, port)) {
        return 0;
    }

    tcp_close();
}

static uint32_t default_subnet_mask_host(void) {
    uint32_t mask = g_net_config.subnet_mask;
    if (mask == 0) {
        mask = parse_ip("255.255.255.0");
    }
    return mask;
}

int net_resolve_mac(uint32_t dest_hostorder, uint8_t mac_out[6]) {
    uint32_t mask_host = g_net_config.subnet_mask ? g_net_config.subnet_mask : default_subnet_mask_host();
    uint32_t next_hop = dest_hostorder;
    if (g_net_config.gateway) {
        if ((dest_hostorder & mask_host) != (g_net_config.ip & mask_host)) {
            next_hop = g_net_config.gateway;
        }
    }
    kprintf("net_resolve_mac dest=%d.%d.%d.%d next=%d.%d.%d.%d\n",
            (htonl(dest_hostorder) >> 24) & 0xFF, (htonl(dest_hostorder) >> 16) & 0xFF,
            (htonl(dest_hostorder) >> 8) & 0xFF, htonl(dest_hostorder) & 0xFF,
            (htonl(next_hop) >> 24) & 0xFF, (htonl(next_hop) >> 16) & 0xFF,
            (htonl(next_hop) >> 8) & 0xFF, htonl(next_hop) & 0xFF);
    if (ip_resolve_mac(next_hop, mac_out)) {
        return 1;
    }
    if (g_net_config.gateway && next_hop != g_net_config.gateway) {
        return ip_resolve_mac(g_net_config.gateway, mac_out);
    }
    return 0;
}

void net_debug_dump(void) {
    uint64_t tx_ring_addr = (uint64_t)(uintptr_t)tx_ring;
    uint64_t rx_ring_addr = (uint64_t)(uintptr_t)rx_ring;
    uint64_t rx_buf0_addr = (uint64_t)(uintptr_t)rx_buffers;
    uint64_t tx_ring_phys_cur = tx_ring_phys ? tx_ring_phys : paging_virt_to_phys((uint64_t)(uintptr_t)tx_ring);
    uint64_t rx_ring_phys_cur = rx_ring_phys ? rx_ring_phys : paging_virt_to_phys((uint64_t)(uintptr_t)rx_ring);
    uint64_t rx_buf0_phys_cur = rx_buffers_phys ? rx_buffers_phys : paging_virt_to_phys((uint64_t)(uintptr_t)rx_buffers);
    uint32_t rdbal = e1000_read(E1000_RDBAL);
    uint32_t rdbah = e1000_read(E1000_RDBAH);
    uint32_t tdbal = e1000_read(E1000_TDBAL);
    uint32_t tdbah = e1000_read(E1000_TDBAH);
    uint32_t rdh = e1000_read(E1000_RDH);
    uint32_t rdt = e1000_read(E1000_RDT);
    qemu_debug_printf("net_debug_dump: tx_ring=%p rx_ring=%p rx_buf0=%p\n", tx_ring, rx_ring, rx_buffers);
    qemu_debug_printf("  RDBAL=0x%08x RDBAH=0x%08x TDBAL=0x%08x TDBAH=0x%08x\n", rdbal, rdbah, tdbal, tdbah);
    qemu_debug_printf("  RDH=%u RDT=%u\n", rdh, rdt);
    kprintf("net_debug_dump:\n");
    kprintf("  tx_ring virt=%p\n", tx_ring);
    kprintf("  tx_ring phys=0x%llx\n", (unsigned long long)tx_ring_phys_cur);
    kprintf("  rx_ring virt=%p\n", rx_ring);
    kprintf("  rx_ring phys=0x%llx\n", (unsigned long long)rx_ring_phys_cur);
    kprintf("  rx_buf0 virt=%p\n", rx_buffers);
    kprintf("  rx_buf0 phys=0x%llx\n", (unsigned long long)rx_buf0_phys_cur);
    kprintf("  RDBAL=0x%08x RDBAH=0x%08x\n", rdbal, rdbah);
    kprintf("  TDBAL=0x%08x TDBAH=0x%08x\n", tdbal, tdbah);
    kprintf("  RDH=%u RDT=%u\n", rdh, rdt);
    if (rx_ring) {
        kprintf("  rx_ring[0].buffer_addr=0x%016llx status=0x%02x\n",
                (unsigned long long)rx_ring[0].buffer_addr, rx_ring[0].status);
    }
    if (tx_ring) {
        kprintf("  tx_ring[0].buffer_addr=0x%016llx status=0x%02x\n",
                (unsigned long long)tx_ring[0].buffer_addr, tx_ring[0].status);
    }
}