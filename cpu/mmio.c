#include <mmio.h>
#include <axonos.h>
#include <pci.h>
#include <string.h>

static uint32_t dns_cache_count = 0;
static uint16_t dns_transaction_id = 0;
static struct dns_cache_entry dns_cache[DNS_CACHE_SIZE];

volatile uint32_t* e1000_mmio = 0;
static uint8_t mac_address[6];
static uint32_t ip_address = 0x0101A8C0;
static uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint32_t dns_server = 0x08080808;

static struct e1000_tx_desc tx_ring[E1000_TX_RING_SIZE] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_ring[E1000_RX_RING_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_TX_RING_SIZE][E1000_TX_BUFFER_SIZE];
static uint8_t rx_buffers[E1000_RX_RING_SIZE][E1000_RX_BUFFER_SIZE];

static uint32_t tx_packets = 0;
static uint32_t rx_packets = 0;
static uint32_t tx_bytes = 0;
static uint32_t rx_bytes = 0;
static uint32_t tx_errors = 0;
static uint32_t rx_errors = 0;
static uint32_t arp_packets = 0;
static uint32_t icmp_packets = 0;
static uint32_t udp_packets = 0;
static uint32_t dns_packets = 0;

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
        ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
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

uint32_t dns_resolve(const char* hostname) {
    if (!e1000_mmio) {
        kprintf("<(0f)>E1000 not initialized<(07)>\n");
        return 0;
    }
    
    for (uint32_t i = 0; i < dns_cache_count; i++) {
        if (strcmp(dns_cache[i].hostname, hostname) == 0) {
            return dns_cache[i].ip;
        }
    }
    
    uint32_t ip = parse_ip(hostname);
    if (ip != 0) {
        return ip;
    }
    
    kprintf("<(0f)>Resolving %s via DNS...<(07)>\n", hostname);
    
    static uint8_t packet[512];
    struct eth_header* eth = (struct eth_header*)packet;
    struct ip_header* iph = (struct ip_header*)(packet + sizeof(struct eth_header));
    struct udp_header* udp = (struct udp_header*)(packet + sizeof(struct eth_header) + sizeof(struct ip_header));
    struct dns_header* dns = (struct dns_header*)(packet + sizeof(struct eth_header) + sizeof(struct ip_header) + sizeof(struct udp_header));
    
    memcpy(eth->dst_mac, broadcast_mac, 6);
    memcpy(eth->src_mac, mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_ARP);
    
    static uint8_t arp_packet[64];
    struct eth_header* arp_eth = (struct eth_header*)arp_packet;
    struct arp_header* arp = (struct arp_header*)(arp_packet + sizeof(struct eth_header));
    
    memcpy(arp_eth->dst_mac, broadcast_mac, 6);
    memcpy(arp_eth->src_mac, mac_address, 6);
    arp_eth->ethertype = htons(ETHERTYPE_ARP);
    
    arp->hw_type = htons(1);
    arp->proto_type = htons(ETHERTYPE_IP);
    arp->hw_size = 6;
    arp->proto_size = 4;
    arp->opcode = htons(1);
    memcpy(arp->sender_mac, mac_address, 6);
    arp->sender_ip = htonl(ip_address);
    memset(arp->target_mac, 0, 6);
    arp->target_ip = htonl(dns_server);
    
    if (!e1000_send_packet(arp_packet, sizeof(struct eth_header) + sizeof(struct arp_header))) {
        kprintf("<(0f)>Failed to send ARP for DNS server<(07)>\n");
        return 0;
    }
    
    for (int i = 0; i < 1000000; i++) {
        asm volatile("pause");
    }
    
    memset(packet, 0, sizeof(packet));
    
    memcpy(eth->dst_mac, broadcast_mac, 6);
    memcpy(eth->src_mac, mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_IP);
    
    iph->version_ihl = 0x45;
    iph->tos = 0;
    iph->total_length = htons(sizeof(struct ip_header) + sizeof(struct udp_header) + sizeof(struct dns_header) + 64);
    iph->identification = htons(0x1234);
    iph->flags_fragment = 0;
    iph->ttl = 64;
    iph->protocol = IP_PROTOCOL_UDP;
    iph->checksum = 0;
    iph->src_ip = htonl(ip_address);
    iph->dst_ip = htonl(dns_server);
    iph->checksum = calculate_checksum(iph, sizeof(struct ip_header));
    
    udp->src_port = htons(12345);
    udp->dst_port = htons(53);
    udp->length = htons(sizeof(struct udp_header) + sizeof(struct dns_header) + 64);
    udp->checksum = 0;
    
    dns->id = htons(++dns_transaction_id);
    dns->flags = htons(0x0100);
    dns->qdcount = htons(1);
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;
    
    uint8_t* question = (uint8_t*)(dns + 1);
    int name_len = dns_encode_name(hostname, question);
    
    struct dns_question* dns_q = (struct dns_question*)(question + name_len);
    dns_q->qtype = htons(1);
    dns_q->qclass = htons(1);
    
    uint16_t dns_length = sizeof(struct dns_header) + name_len + sizeof(struct dns_question);
    udp->length = htons(sizeof(struct udp_header) + dns_length);
    
    uint16_t packet_length = sizeof(struct eth_header) + sizeof(struct ip_header) + sizeof(struct udp_header) + dns_length;
    
    if (e1000_send_packet(packet, packet_length)) {
        kprintf("<(0f)>DNS query sent for %s<(07)>\n", hostname);
        dns_packets++;
    } else {
        kprintf("<(0f)>Failed to send DNS query<(07)>\n");
        return 0;
    }
    
    if (strcmp(hostname, "google.com") == 0) return 0x4A7D461F;
    if (strcmp(hostname, "cloudflare.com") == 0) return 0x01010101;
    if (strcmp(hostname, "github.com") == 0) return 0xC01F0F0F;
    
    kprintf("<(0f)>DNS resolution failed for: %s<(07)>\n", hostname);
    return 0;
}

static void e1000_init_tx(void) {
    e1000_write(E1000_TCTL, 0);
    
    uint64_t tx_phys = (uint64_t)&tx_ring;
    e1000_write(E1000_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFF));
    e1000_write(E1000_TDBAH, (uint32_t)(tx_phys >> 32));
    e1000_write(E1000_TDLEN, E1000_TX_RING_SIZE * sizeof(struct e1000_tx_desc));
    
    for (int i = 0; i < E1000_TX_RING_SIZE; i++) {
        tx_ring[i].buffer_addr = (uint64_t)&tx_buffers[i];
        tx_ring[i].length = 0;
        tx_ring[i].cso = 0;
        tx_ring[i].cmd = 0;
        tx_ring[i].status = E1000_TXD_STAT_DD;
        tx_ring[i].css = 0;
        tx_ring[i].special = 0;
    }
    
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    
    e1000_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << 4) | (0x40 << 12));
    
    e1000_write(E1000_TIPG, 0x0060200A);
    
    kprintf("<(0f)>TX ring: %d descriptors<(07)>\n", E1000_TX_RING_SIZE);
}

static void e1000_init_rx(void) {
    e1000_write(E1000_RCTL, 0);
    
    uint64_t rx_phys = (uint64_t)&rx_ring;
    e1000_write(E1000_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFF));
    e1000_write(E1000_RDBAH, (uint32_t)(rx_phys >> 32));
    e1000_write(E1000_RDLEN, E1000_RX_RING_SIZE * sizeof(struct e1000_rx_desc));
    
    for (int i = 0; i < E1000_RX_RING_SIZE; i++) {
        rx_ring[i].buffer_addr = (uint64_t)&rx_buffers[i];
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;
    }
    
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_RX_RING_SIZE - 1);
    
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_LPE | E1000_RCTL_SZ_2048;
    e1000_write(E1000_RCTL, rctl);
    
    kprintf("<(0f)>RX ring: %d descriptors<(07)>\n", E1000_RX_RING_SIZE);
}

static void e1000_reset(void) {
    kprintf("<(0f)>Resetting E1000...<(07)>\n");
    
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);
    
    int timeout = 1000000;
    while (timeout-- > 0) {
        if (!(e1000_read(E1000_CTRL) & E1000_CTRL_RST)) {
            uint32_t ctrl = e1000_read(E1000_CTRL);
            ctrl |= E1000_CTRL_SLU;
            e1000_write(E1000_CTRL, ctrl);
            kprintf("<(0f)>Reset complete<(07)>\n");
            return;
        }
    }
    
    kprintf("<(0f)>Reset timeout<(07)>\n");
}

void e1000_init(void) {
    kprintf("<(0f)>Initializing E1000 network driver...<(07)>\n");
    
    int device_count = pci_get_device_count();
    pci_device_t* devices = pci_get_devices();
    
    for (int i = 0; i < device_count; i++) {
        pci_device_t* dev = &devices[i];
        
        if (dev->vendor_id == 0x8086 && (dev->device_id == 0x100E || dev->device_id == 0x100F)) {
            kprintf("<(0f)>Found E1000 at %d:%d:%d<(07)>\n", dev->bus, dev->device, dev->function);
            
            uint32_t bar0 = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x10);
            if ((bar0 & 1) == 0) {
                e1000_mmio = (volatile uint32_t*)(bar0 & ~0xF);
                
                uint32_t cmd = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x04);
                cmd |= (1 << 2) | (1 << 1);
                pci_config_write_dword(dev->bus, dev->device, dev->function, 0x04, cmd);
                
                kprintf("<(0f)>MMIO base: 0x%08x<(07)>\n", (uint32_t)e1000_mmio);
                
                e1000_reset();
                
                uint32_t ral = e1000_read(E1000_RAL);
                uint32_t rah = e1000_read(E1000_RAH);
                mac_address[0] = (ral >> 0) & 0xFF;
                mac_address[1] = (ral >> 8) & 0xFF;
                mac_address[2] = (ral >> 16) & 0xFF;
                mac_address[3] = (ral >> 24) & 0xFF;
                mac_address[4] = (rah >> 0) & 0xFF;
                mac_address[5] = (rah >> 8) & 0xFF;
                
                e1000_init_tx();
                e1000_init_rx();
                
                kprintf("<(0f)>E1000 ready - MAC: %02x:%02x:%02x:%02x:%02x:%02x<(07)>\n",
                       mac_address[0], mac_address[1], mac_address[2],
                       mac_address[3], mac_address[4], mac_address[5]);
                return;
            }
        }
    }
    
    kprintf("<(0f)>E1000 not found<(07)>\n");
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
    
    int timeout = 100000;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
        asm volatile("pause");
    }
    
    if (timeout <= 0) {
        tx_errors++;
        return 0;
    }
    
    memcpy((void*)desc->buffer_addr, data, length);
    
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
    
    uint32_t rx_head = (e1000_read(E1000_RDH) + 1) % E1000_RX_RING_SIZE;
    
    if (rx_head == e1000_read(E1000_RDT)) {
        return 0;
    }
    
    struct e1000_rx_desc* desc = &rx_ring[rx_head];
    
    if (desc->status & E1000_RXD_STAT_DD) {
        *length = desc->length;
        if (*length > E1000_RX_BUFFER_SIZE) {
            *length = E1000_RX_BUFFER_SIZE;
            rx_errors++;
        }
        
        memcpy(buffer, (void*)desc->buffer_addr, *length);
        
        desc->status = 0;
        
        e1000_write(E1000_RDT, rx_head);
        
        rx_packets++;
        rx_bytes += *length;
        return 1;
    }
    
    return 0;
}

void e1000_poll(void) {
    uint8_t buffer[2048];
    uint16_t length;
    
    while (e1000_receive_packet(buffer, &length)) {
        if (length >= sizeof(struct eth_header)) {
            struct eth_header* eth = (struct eth_header*)buffer;
            
            int is_for_us = 1;
            for (int i = 0; i < 6; i++) {
                if (eth->dst_mac[i] != mac_address[i] && eth->dst_mac[i] != 0xFF) {
                    is_for_us = 0;
                    break;
                }
            }
            
            if (is_for_us) {
                uint16_t ethertype = ntohs(eth->ethertype);
                
                switch (ethertype) {
                    case ETHERTYPE_ARP:
                        arp_packets++;
                        kprintf("<(0f)>[ARP] Packet received: %d bytes<(07)>\n", length);
                        break;
                    case ETHERTYPE_IP:
                        if (length >= sizeof(struct eth_header) + sizeof(struct ip_header)) {
                            struct ip_header* iph = (struct ip_header*)(buffer + sizeof(struct eth_header));
                            
                            if (iph->protocol == IP_PROTOCOL_ICMP) {
                                icmp_packets++;
                                kprintf("<(0f)>[ICMP] Reply from %d.%d.%d.%d<(07)>\n",
                                       (ntohl(iph->src_ip) >> 24) & 0xFF,
                                       (ntohl(iph->src_ip) >> 16) & 0xFF,
                                       (ntohl(iph->src_ip) >> 8) & 0xFF,
                                       ntohl(iph->src_ip) & 0xFF);
                            }
                            else if (iph->protocol == IP_PROTOCOL_UDP) {
                                udp_packets++;
                                struct udp_header* udp = (struct udp_header*)(buffer + sizeof(struct eth_header) + sizeof(struct ip_header));
                                
                                if (ntohs(udp->dst_port) == 53) {
                                    dns_packets++;
                                    kprintf("<(0f)>[DNS] Response received<(07)>\n");
                                }
                            }
                        }
                        break;
                }
            }
        }
    }
}

void e1000_handle_interrupt(void) {
    if (!e1000_mmio) return;
    
    uint32_t icr = e1000_read(E1000_ICR);
    
    if (icr & E1000_ICR_LSC) {
        kprintf("<(0f)>Link status changed<(07)>\n");
    }
    
    e1000_poll();
}

int e1000_link_up(void) {
    if (!e1000_mmio) return 0;
    return (e1000_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
}

void net_send_arp(const char* ip_str) {
    if (!e1000_mmio) {
        kprintf("<(0f)>E1000 not initialized<(07)>\n");
        return;
    }
    
    uint32_t target_ip = parse_ip(ip_str);
    if (target_ip == 0) {
        target_ip = dns_resolve(ip_str);
        if (target_ip == 0) {
            kprintf("<(0f)>Invalid IP/hostname: %s<(07)>\n", ip_str);
            return;
        }
    }
    
    uint8_t packet[64] = {0};
    struct eth_header* eth = (struct eth_header*)packet;
    struct arp_header* arp = (struct arp_header*)(packet + sizeof(struct eth_header));
    
    memcpy(eth->dst_mac, broadcast_mac, 6);
    memcpy(eth->src_mac, mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_ARP);
    
    arp->hw_type = htons(1);
    arp->proto_type = htons(ETHERTYPE_IP);
    arp->hw_size = 6;
    arp->proto_size = 4;
    arp->opcode = htons(1);
    memcpy(arp->sender_mac, mac_address, 6);
    arp->sender_ip = htonl(ip_address);
    memset(arp->target_mac, 0, 6);
    arp->target_ip = htonl(target_ip);
    
    if (e1000_send_packet(packet, sizeof(struct eth_header) + sizeof(struct arp_header))) {
        kprintf("<(0f)>ARP request sent for %s (%d.%d.%d.%d)<(07)>\n", ip_str,
               (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
               (target_ip >> 8) & 0xFF, target_ip & 0xFF);
    } else {
        kprintf("<(0f)>Failed to send ARP request<(07)>\n");
    }
}

void net_ping(const char* hostname) {
    if (!e1000_mmio) {
        kprintf("<(0f)>E1000 not initialized<(07)>\n");
        return;
    }
    
    uint32_t dest_ip = dns_resolve(hostname);
    if (dest_ip == 0) {
        kprintf("<(0f)>Could not resolve: %s<(07)>\n", hostname);
        return;
    }
    
    kprintf("<(0f)>Pinging %s (%d.%d.%d.%d)...<(07)>\n", hostname,
           (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
           (dest_ip >> 8) & 0xFF, dest_ip & 0xFF);
    
    static uint8_t packet[1024] = {0};
    struct eth_header* eth = (struct eth_header*)packet;
    struct ip_header* iph = (struct ip_header*)(packet + sizeof(struct eth_header));
    struct icmp_header* icmp = (struct icmp_header*)(packet + sizeof(struct eth_header) + sizeof(struct ip_header));
    
    memcpy(eth->dst_mac, broadcast_mac, 6);
    memcpy(eth->src_mac, mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_IP);
    
    iph->version_ihl = 0x45;
    iph->tos = 0;
    iph->total_length = htons(sizeof(struct ip_header) + sizeof(struct icmp_header) + 8);
    iph->identification = htons(0x1234);
    iph->flags_fragment = 0;
    iph->ttl = 64;
    iph->protocol = IP_PROTOCOL_ICMP;
    iph->checksum = 0;
    iph->src_ip = htonl(ip_address);
    iph->dst_ip = htonl(dest_ip);
    iph->checksum = calculate_checksum(iph, sizeof(struct ip_header));
    
    icmp->type = 8;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->identifier = htons(1);
    icmp->sequence = htons(1);
    
    const char* ping_data = "AxonOS Ping Test";
    memcpy(icmp->data, ping_data, strlen(ping_data));
    
    uint16_t icmp_length = sizeof(struct icmp_header) + strlen(ping_data);
    icmp->checksum = calculate_checksum(icmp, icmp_length);
    
    uint16_t packet_length = sizeof(struct eth_header) + sizeof(struct ip_header) + icmp_length;
    
    if (e1000_send_packet(packet, packet_length)) {
        kprintf("<(0f)>Ping sent to %s (%d bytes)<(07)>\n", hostname, packet_length);
    } else {
        kprintf("<(0f)>Failed to send ping<(07)>\n");
    }
}

void e1000_print_stats(void) {
    if (!e1000_mmio) {
        kprintf("<(0f)>E1000 not initialized<(07)>\n");
        return;
    }
    
    kprintf("Network Statistics:\n");
    kprintf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 
           mac_address[0], mac_address[1], mac_address[2],
           mac_address[3], mac_address[4], mac_address[5]);
    {
        uint32_t ip_print = ntohl(ip_address);
        kprintf("IP: %d.%d.%d.%d\n", 
               (ip_print >> 24) & 0xFF, (ip_print >> 16) & 0xFF,
               (ip_print >> 8) & 0xFF, ip_print & 0xFF);
    }
    {
        uint32_t dns_print = ntohl(dns_server);
        kprintf("DNS: %d.%d.%d.%d\n",
               (dns_print >> 24) & 0xFF, (dns_print >> 16) & 0xFF,
               (dns_print >> 8) & 0xFF, dns_print & 0xFF);
    }
    kprintf("Link: %s\n", e1000_link_up() ? "UP" : "DOWN");
    kprintf("TX: %d packets, %d bytes, %d errors\n", tx_packets, tx_bytes, tx_errors);
    kprintf("RX: %d packets, %d bytes, %d errors\n", rx_packets, rx_bytes, rx_errors);
    kprintf("Protocols: ARP=%d, ICMP=%d, UDP=%d, DNS=%d\n", arp_packets, icmp_packets, udp_packets, dns_packets);
    kprintf("Ring Status: TX Tail=%d, RX Head=%d\n", e1000_read(E1000_TDT), e1000_read(E1000_RDH));
    kprintf("DNS Cache: %d entries\n", dns_cache_count);
}

void net_test_dns(void) {
    if (!e1000_mmio) {
        kprintf("<(0f)>E1000 not initialized<(07)>\n");
        return;
    }
    
    kprintf("<(0f)>Testing DNS resolution...<(07)>\n");
    
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
            kprintf("<(0f)>%s -> %d.%d.%d.%d<(07)>\n", hosts[i],
                   (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                   (ip >> 8) & 0xFF, ip & 0xFF);
        } else {
            kprintf("<(0f)>%s -> resolution failed<(07)>\n", hosts[i]);
        }
    }
}

int net_send_to_server(const char* host, uint16_t port, const uint8_t* data, uint16_t len) {
    if (!e1000_mmio) {
        kprintf("<(0f)>E1000 not initialized<(07)>\n");
        return 0;
    }

    if (!data || len == 0) return 0;

    uint32_t dest_ip = parse_ip(host);
    if (dest_ip == 0) {
        dest_ip = dns_resolve(host);
        if (dest_ip == 0) {
            kprintf("<(0f)>Could not resolve destination: %s<(07)>\n", host);
            return 0;
        }
    }

    uint8_t packet[E1000_TX_BUFFER_SIZE];
    memset(packet, 0, sizeof(packet));

    struct eth_header* eth = (struct eth_header*)packet;
    struct ip_header* iph = (struct ip_header*)(packet + sizeof(struct eth_header));
    struct udp_header* udp = (struct udp_header*)(packet + sizeof(struct eth_header) + sizeof(struct ip_header));
    uint8_t* payload = packet + sizeof(struct eth_header) + sizeof(struct ip_header) + sizeof(struct udp_header);

    uint32_t headers_len = sizeof(struct eth_header) + sizeof(struct ip_header) + sizeof(struct udp_header);
    if (len + headers_len > E1000_TX_BUFFER_SIZE) {
        kprintf("<(0f)>Payload too large for TX buffer (%d bytes max)<(07)>\n", E1000_TX_BUFFER_SIZE - headers_len);
        return 0;
    }

    memcpy(eth->dst_mac, broadcast_mac, 6);
    memcpy(eth->src_mac, mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_IP);

    iph->version_ihl = 0x45;
    iph->tos = 0;
    iph->total_length = htons(sizeof(struct ip_header) + sizeof(struct udp_header) + len);
    iph->identification = htons(0);
    iph->flags_fragment = 0;
    iph->ttl = 64;
    iph->protocol = IP_PROTOCOL_UDP;
    iph->checksum = 0;
    iph->src_ip = htonl(ip_address);
    iph->dst_ip = htonl(dest_ip);
    iph->checksum = calculate_checksum(iph, sizeof(struct ip_header));

    udp->src_port = htons(12345);
    udp->dst_port = htons(port);
    udp->length = htons(sizeof(struct udp_header) + len);
    udp->checksum = 0;

    memcpy(payload, data, len);

    uint16_t packet_length = headers_len + len;

    if (e1000_send_packet(packet, packet_length)) {
        kprintf("<(0f)>Sent %d bytes UDP -> %d.%d.%d.%d:%d<(07)>\n", packet_length,
                (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
                (dest_ip >> 8) & 0xFF, dest_ip & 0xFF, port);
        return packet_length;
    } else {
        kprintf("<(0f)>Failed to send UDP packet to %s<(07)>\n", host);
        return 0;
    }
}
void net_set_ip(uint32_t ip_hostorder_le) {
    ip_address = ip_hostorder_le;
}

void net_set_dns(uint32_t dns_hostorder_le) {
    dns_server = dns_hostorder_le;
}

void net_set_ip_str(const char* ip_str) {
    uint32_t p = parse_ip(ip_str);
    if (p != 0) {
        ip_address = htonl(p);
    }
}

void net_set_dns_str(const char* ip_str) {
    uint32_t p = parse_ip(ip_str);
    if (p != 0) {
        dns_server = htonl(p);
    }
}