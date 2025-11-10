#include <string.h>
#include <mmio.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/arp.h>
#include <vga.h>

struct udp_waiter {
    int state; // 0 idle, 1 waiting, 2 received
    uint16_t local_port;
    uint32_t expect_ip;
    uint16_t expect_src_port;
    uint8_t* out;
    uint16_t max_len;
    uint16_t received;
};

static uint8_t g_tx_buffer[E1000_TX_BUFFER_SIZE];
static struct udp_waiter g_waiter;
static uint32_t g_packet_count = 0;
static uint32_t g_dns_packets = 0;

void udp_init(void) {
    memset(&g_waiter, 0, sizeof(g_waiter));
    g_packet_count = 0;
    g_dns_packets = 0;
}

uint32_t udp_get_packet_count(void) {
    return g_packet_count;
}

uint32_t udp_get_dns_packet_count(void) {
    return g_dns_packets;
}

static void udp_notify(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                       const uint8_t* data, uint16_t data_len) {
    if (g_waiter.state != 1) return;
    if (g_waiter.local_port && g_waiter.local_port != dst_port) return;
    if (g_waiter.expect_ip && g_waiter.expect_ip != src_ip) return;
    if (g_waiter.expect_src_port && g_waiter.expect_src_port != src_port) return;

    if (data_len > g_waiter.max_len) data_len = g_waiter.max_len;
    if (g_waiter.out && data_len) {
        memcpy(g_waiter.out, data, data_len);
    }
    g_waiter.received = data_len;
    g_waiter.state = 2;
}

int udp_send(uint32_t dest_ip_hostorder, uint16_t src_port, uint16_t dest_port,
             const uint8_t* data, uint16_t len) {
    if (!e1000_mmio || !data || len == 0) return 0;

    uint8_t dest_mac[6];
    if (!net_resolve_mac(dest_ip_hostorder, dest_mac)) {
        kprintf("Failed to resolve MAC for UDP destination\n");
        return 0;
    }

    uint32_t headers_len = sizeof(struct eth_header) + sizeof(struct ip_header) + sizeof(struct udp_header);
    if (headers_len + len > E1000_TX_BUFFER_SIZE) {
        kprintf("UDP payload too large (%d bytes max)\n", E1000_TX_BUFFER_SIZE - headers_len);
        return 0;
    }

    memset(g_tx_buffer, 0, headers_len + len);

    struct eth_header* eth = (struct eth_header*)g_tx_buffer;
    struct ip_header* iph = (struct ip_header*)(g_tx_buffer + sizeof(struct eth_header));
    struct udp_header* udp = (struct udp_header*)(g_tx_buffer + sizeof(struct eth_header) + sizeof(struct ip_header));
    uint8_t* payload = g_tx_buffer + headers_len;

    memcpy(eth->dst_mac, dest_mac, 6);
    memcpy(eth->src_mac, net_mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_IP);

    iph->version_ihl = 0x45;
    iph->tos = 0;
    iph->total_length = htons(sizeof(struct ip_header) + sizeof(struct udp_header) + len);
    iph->identification = htons(0);
    iph->flags_fragment = 0;
    iph->ttl = 64;
    iph->protocol = IP_PROTOCOL_UDP;
    iph->checksum = 0;
    iph->src_ip = htonl(net_ip_address);
    iph->dst_ip = htonl(dest_ip_hostorder);
    iph->checksum = calculate_checksum(iph, sizeof(struct ip_header));

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dest_port);
    udp->length = htons(sizeof(struct udp_header) + len);
    udp->checksum = 0;

    memcpy(payload, data, len);

    return e1000_send_packet(g_tx_buffer, headers_len + len);
}

int udp_wait(uint16_t local_port, uint32_t expect_ip_hostorder, uint16_t expect_src_port,
             uint8_t* out, uint16_t max_len, uint32_t attempts) {
    if (!e1000_mmio || !out || max_len == 0) return 0;

    memset(&g_waiter, 0, sizeof(g_waiter));
    g_waiter.state = 1;
    g_waiter.local_port = local_port;
    g_waiter.expect_ip = expect_ip_hostorder;
    g_waiter.expect_src_port = expect_src_port;
    g_waiter.out = out;
    g_waiter.max_len = max_len;

    if (attempts == 0) attempts = UDP_WAIT_DEFAULT_ATTEMPTS;

    for (uint32_t i = 0; i < attempts && g_waiter.state == 1; i++) {
        if (!ip_poll_once()) asm volatile("pause");
    }

    int result = (g_waiter.state == 2) ? g_waiter.received : 0;
    g_waiter.state = 0;
    return result;
}

void udp_handle_ipv4(const struct ip_header* iph, const uint8_t* payload, uint16_t payload_len) {
    if (!payload || payload_len < sizeof(struct udp_header)) return;

    const struct udp_header* udp = (const struct udp_header*)payload;
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);

    const uint8_t* data = payload + sizeof(struct udp_header);
    uint16_t data_len = payload_len - sizeof(struct udp_header);

    g_packet_count++;
    if (src_port == 53 || dst_port == 53) {
        g_dns_packets++;
    }

    udp_notify(ntohl(iph->src_ip), src_port, dst_port, data, data_len);
}
