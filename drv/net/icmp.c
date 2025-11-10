#include <string.h>
#include <mmio.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/arp.h>
#include <vga.h>

#define ICMP_HEADER_SIZE 8
#define ICMP_MAX_PAYLOAD 128

struct icmp_echo_waiter {
    int state; // 0 idle, 1 waiting, 2 received
    uint32_t expect_ip;
    uint16_t ident;
    uint16_t sequence;
    uint8_t* out;
    uint16_t max_len;
    uint16_t received;
};

static uint8_t g_tx_buffer[E1000_TX_BUFFER_SIZE];
static struct icmp_echo_waiter g_icmp_waiter;
static uint16_t g_sequence_counter = 1;

static void icmp_notify_reply(uint32_t src_ip, uint16_t ident, uint16_t sequence,
                              const uint8_t* payload, uint16_t payload_len) {
    if (g_icmp_waiter.state != 1) return;
    if (g_icmp_waiter.expect_ip && g_icmp_waiter.expect_ip != src_ip) return;
    if (g_icmp_waiter.ident != ident) return;
    if (g_icmp_waiter.sequence != sequence) return;

    if (payload_len > g_icmp_waiter.max_len) payload_len = g_icmp_waiter.max_len;
    if (g_icmp_waiter.out && payload_len) {
        memcpy(g_icmp_waiter.out, payload, payload_len);
    }
    g_icmp_waiter.received = payload_len;
    g_icmp_waiter.state = 2;
}

void icmp_init(void) {
    memset(&g_icmp_waiter, 0, sizeof(g_icmp_waiter));
    g_sequence_counter = 1;
}

int icmp_send_echo(uint32_t dest_ip_hostorder, uint16_t ident, uint16_t sequence,
                   const uint8_t* payload, uint16_t payload_len) {
    if (!e1000_mmio) return 0;

    if (payload_len > ICMP_MAX_PAYLOAD) payload_len = ICMP_MAX_PAYLOAD;

    uint8_t dest_mac[6];
    if (!ip_resolve_mac(dest_ip_hostorder, dest_mac)) {
        kprintf("<(0f)>Failed to resolve MAC for ICMP target<(07)>\n");
        return 0;
    }

    uint32_t headers_len = sizeof(struct eth_header) + sizeof(struct ip_header) + ICMP_HEADER_SIZE;
    if (headers_len + payload_len > E1000_TX_BUFFER_SIZE) {
        kprintf("<(0f)>ICMP payload too large<(07)>\n");
        return 0;
    }

    memset(g_tx_buffer, 0, headers_len + payload_len);

    struct eth_header* eth = (struct eth_header*)g_tx_buffer;
    struct ip_header* iph = (struct ip_header*)(g_tx_buffer + sizeof(struct eth_header));
    uint8_t* icmp = g_tx_buffer + sizeof(struct eth_header) + sizeof(struct ip_header);

    memcpy(eth->dst_mac, dest_mac, 6);
    memcpy(eth->src_mac, net_mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_IP);

    iph->version_ihl = 0x45;
    iph->tos = 0;
    iph->total_length = htons(sizeof(struct ip_header) + ICMP_HEADER_SIZE + payload_len);
    iph->identification = htons(g_sequence_counter++);
    iph->flags_fragment = 0;
    iph->ttl = 64;
    iph->protocol = IP_PROTOCOL_ICMP;
    iph->checksum = 0;
    iph->src_ip = htonl(net_ip_address);
    iph->dst_ip = htonl(dest_ip_hostorder);
    iph->checksum = calculate_checksum(iph, sizeof(struct ip_header));

    icmp[0] = 8; // Echo request
    icmp[1] = 0;
    icmp[2] = 0;
    icmp[3] = 0;
    *(uint16_t*)(icmp + 4) = htons(ident);
    *(uint16_t*)(icmp + 6) = htons(sequence);

    if (payload_len && payload) {
        memcpy(icmp + ICMP_HEADER_SIZE, payload, payload_len);
    }

    uint16_t icmp_len = ICMP_HEADER_SIZE + payload_len;
    uint16_t checksum = calculate_checksum(icmp, icmp_len);
    *(uint16_t*)(icmp + 2) = checksum;

    return e1000_send_packet(g_tx_buffer, headers_len + payload_len);
}

int icmp_wait_echo(uint32_t expect_ip_hostorder, uint16_t ident, uint16_t sequence,
                   uint8_t* out, uint16_t max_len, uint32_t attempts) {
    memset(&g_icmp_waiter, 0, sizeof(g_icmp_waiter));
    g_icmp_waiter.state = 1;
    g_icmp_waiter.expect_ip = expect_ip_hostorder;
    g_icmp_waiter.ident = ident;
    g_icmp_waiter.sequence = sequence;
    g_icmp_waiter.out = out;
    g_icmp_waiter.max_len = max_len;

    if (attempts == 0) attempts = ICMP_WAIT_DEFAULT_ATTEMPTS;

    for (uint32_t i = 0; i < attempts && g_icmp_waiter.state == 1; i++) {
        if (!ip_poll_once()) {
            asm volatile("pause");
        }
    }

    int result = (g_icmp_waiter.state == 2) ? g_icmp_waiter.received : 0;
    g_icmp_waiter.state = 0;
    return result;
}

void icmp_handle_ipv4(const struct ip_header* iph, const uint8_t* payload, uint16_t payload_len) {
    if (!payload || payload_len < ICMP_HEADER_SIZE) return;

    uint8_t type = payload[0];
    uint8_t code = payload[1];
    uint16_t ident = ntohs(*(const uint16_t*)(payload + 4));
    uint16_t sequence = ntohs(*(const uint16_t*)(payload + 6));
    const uint8_t* data = payload + ICMP_HEADER_SIZE;
    uint16_t data_len = (payload_len > ICMP_HEADER_SIZE) ? (payload_len - ICMP_HEADER_SIZE) : 0;

    if (type == 0 && code == 0) {
        icmp_notify_reply(ntohl(iph->src_ip), ident, sequence, data, data_len);
    }
}
