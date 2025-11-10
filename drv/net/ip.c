#include <string.h>
#include <mmio.h>
#include <net/ip.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>

#define IP_RESOLVE_RETRIES 3
#define IP_WAIT_ATTEMPTS 500000

static uint8_t g_rx_buffer[2048];
static uint32_t g_arp_packets = 0;
static uint32_t g_icmp_packets = 0;

void ip_init(void) {
    g_arp_packets = 0;
    g_icmp_packets = 0;
}

uint32_t ip_get_arp_packets(void) { return g_arp_packets; }
uint32_t ip_get_icmp_packets(void) { return g_icmp_packets; }
uint32_t ip_get_udp_packets(void) { return udp_get_packet_count(); }
uint32_t ip_get_dns_packets(void) { return udp_get_dns_packet_count(); }

void ip_handle_frame(uint8_t* frame, uint16_t length) {
    if (length < sizeof(struct eth_header) + sizeof(struct ip_header)) return;
    struct ip_header* iph = (struct ip_header*)(frame + sizeof(struct eth_header));
    uint16_t total_len = ntohs(iph->total_length);
    uint16_t available = length - sizeof(struct eth_header);
    if (total_len > available) total_len = available;

    switch (iph->protocol) {
        case IP_PROTOCOL_ICMP:
            g_icmp_packets++;
            icmp_handle_ipv4(iph, (uint8_t*)iph + sizeof(struct ip_header),
                              total_len > sizeof(struct ip_header)
                                  ? (uint16_t)(total_len - sizeof(struct ip_header))
                                  : 0);
            break;
        case IP_PROTOCOL_UDP:
            udp_handle_ipv4(iph, (uint8_t*)iph + sizeof(struct ip_header),
                            total_len > sizeof(struct ip_header)
                                ? (uint16_t)(total_len - sizeof(struct ip_header))
                                : 0);
            break;
        case IP_PROTOCOL_TCP:
            tcp_handle_ipv4(iph, (uint8_t*)iph + sizeof(struct ip_header),
                            total_len > sizeof(struct ip_header)
                                ? (uint16_t)(total_len - sizeof(struct ip_header))
                                : 0);
            break;
        default:
            break;
    }
}

int ip_poll_once(void) {
    uint16_t length;
    if (!e1000_receive_packet(g_rx_buffer, &length)) return 0;
    if (length < sizeof(struct eth_header)) return 1;

    struct eth_header* eth = (struct eth_header*)g_rx_buffer;
    int is_for_us = 1;
    for (int i = 0; i < 6; i++) {
        if (eth->dst_mac[i] != net_mac_address[i] && eth->dst_mac[i] != 0xFF) {
            is_for_us = 0;
            break;
        }
    }
    if (!is_for_us) {
        kprintf("RX drop dst=%02x:%02x:%02x:%02x:%02x:%02x our=%02x:%02x:%02x:%02x:%02x:%02x\n",
               eth->dst_mac[0], eth->dst_mac[1], eth->dst_mac[2], eth->dst_mac[3], eth->dst_mac[4], eth->dst_mac[5],
               net_mac_address[0], net_mac_address[1], net_mac_address[2], net_mac_address[3], net_mac_address[4], net_mac_address[5]);
        return 1;
    }

    uint16_t ethertype = ntohs(eth->ethertype);
    if (ethertype == ETHERTYPE_ARP) {
        g_arp_packets++;
        arp_handle_packet(g_rx_buffer, length);
    } else if (ethertype == ETHERTYPE_IP) {
        ip_handle_frame(g_rx_buffer, length);
    }
    return 1;
}

int ip_resolve_mac(uint32_t ip_hostorder, uint8_t mac_out[6]) {
    if (arp_try_get_mac(ip_hostorder, mac_out)) return 1;

    for (int attempt = 0; attempt < IP_RESOLVE_RETRIES; attempt++) {
        arp_request(ip_hostorder);
        for (int wait = 0; wait < IP_WAIT_ATTEMPTS; wait++) {
            if (arp_try_get_mac(ip_hostorder, mac_out)) return 1;
            if (!ip_poll_once()) asm volatile("pause");
        }
    }
    return arp_try_get_mac(ip_hostorder, mac_out);
}
