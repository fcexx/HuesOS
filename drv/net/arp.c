#include <string.h>
#include <mmio.h>
#include <stdint.h>
#include <net/arp.h>

#define ARP_TABLE_SIZE 16

struct arp_entry {
    uint32_t ip;       // host order
    uint8_t mac[6];
    int valid;
};

static struct arp_entry g_arp_table[ARP_TABLE_SIZE];

static void arp_table_insert(uint32_t ip, const uint8_t mac[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip) {
            memcpy(g_arp_table[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!g_arp_table[i].valid) {
            g_arp_table[i].valid = 1;
            g_arp_table[i].ip = ip;
            memcpy(g_arp_table[i].mac, mac, 6);
            return;
        }
    }
    // overwrite oldest (simple strategy)
    g_arp_table[0].valid = 1;
    g_arp_table[0].ip = ip;
    memcpy(g_arp_table[0].mac, mac, 6);
}

void arp_init(void) {
    memset(g_arp_table, 0, sizeof(g_arp_table));
}

int arp_try_get_mac(uint32_t ip_hostorder, uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip_hostorder) {
            memcpy(mac_out, g_arp_table[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

void arp_request(uint32_t ip_hostorder) {
    uint8_t packet[sizeof(struct eth_header) + sizeof(struct arp_header)];
    struct eth_header* eth = (struct eth_header*)packet;
    struct arp_header* arp = (struct arp_header*)(packet + sizeof(struct eth_header));

    memcpy(eth->dst_mac, net_broadcast_mac, 6);
    memcpy(eth->src_mac, net_mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_ARP);

    arp->hw_type = htons(1);
    arp->proto_type = htons(ETHERTYPE_IP);
    arp->hw_size = 6;
    arp->proto_size = 4;
    arp->opcode = htons(1);
    memcpy(arp->sender_mac, net_mac_address, 6);
    arp->sender_ip = htonl(net_ip_address);
    memset(arp->target_mac, 0, 6);
    arp->target_ip = htonl(ip_hostorder);

    uint32_t print_ip = htonl(ip_hostorder);
    kprintf("ARP request for %d.%d.%d.%d\n",
           (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
           (print_ip >> 8) & 0xFF, print_ip & 0xFF);

    e1000_send_packet(packet, sizeof(packet));
}

static void arp_send_reply(const struct arp_header* request) {
    uint8_t packet[sizeof(struct eth_header) + sizeof(struct arp_header)];
    struct eth_header* eth = (struct eth_header*)packet;
    struct arp_header* arp = (struct arp_header*)(packet + sizeof(struct eth_header));

    memcpy(eth->dst_mac, request->sender_mac, 6);
    memcpy(eth->src_mac, net_mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_ARP);

    arp->hw_type = htons(1);
    arp->proto_type = htons(ETHERTYPE_IP);
    arp->hw_size = 6;
    arp->proto_size = 4;
    arp->opcode = htons(2);
    memcpy(arp->sender_mac, net_mac_address, 6);
    arp->sender_ip = htonl(net_ip_address);
    memcpy(arp->target_mac, request->sender_mac, 6);
    arp->target_ip = request->sender_ip;

    e1000_send_packet(packet, sizeof(packet));
}

void arp_handle_packet(const uint8_t* frame, uint16_t length) {
    if (length < sizeof(struct eth_header) + sizeof(struct arp_header)) return;

    const struct arp_header* arp = (const struct arp_header*)(frame + sizeof(struct eth_header));
    uint16_t opcode = ntohs(arp->opcode);
    uint32_t sender_ip = ntohl(arp->sender_ip);

    arp_table_insert(sender_ip, arp->sender_mac);

    uint32_t print_ip = htonl(sender_ip);
    kprintf("ARP %s from %d.%d.%d.%d [%02x:%02x:%02x:%02x:%02x:%02x]\n",
            opcode == 1 ? "request" : "reply",
            (print_ip >> 24) & 0xFF, (print_ip >> 16) & 0xFF,
            (print_ip >> 8) & 0xFF, print_ip & 0xFF,
            arp->sender_mac[0], arp->sender_mac[1], arp->sender_mac[2],
            arp->sender_mac[3], arp->sender_mac[4], arp->sender_mac[5]);

    if (opcode == 1) { // request
        uint32_t target_ip = ntohl(arp->target_ip);
        if (target_ip == net_ip_address) {
            arp_send_reply(arp);
        }
    }
}
