#pragma once

#include <stdint.h>
#include <stddef.h>

// E1000 Register Definitions
#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_RCTL     0x00100
#define E1000_TCTL     0x00400
#define E1000_TIPG     0x00410
#define E1000_IMS      0x000D0
#define E1000_ICR      0x000C0

// Transmit Registers
#define E1000_TDBAL    0x03800
#define E1000_TDBAH    0x03804
#define E1000_TDLEN    0x03808
#define E1000_TDH      0x03810
#define E1000_TDT      0x03818

// Receive Registers
#define E1000_RDBAL    0x02800
#define E1000_RDBAH    0x02804
#define E1000_RDLEN    0x02808
#define E1000_RDH      0x02810
#define E1000_RDT      0x02818

// MAC Address Registers
#define E1000_RAL      0x05400
#define E1000_RAH      0x05404

// Control Bits
#define E1000_CTRL_RST     (1 << 26)
#define E1000_CTRL_SLU     (1 << 6)
#define E1000_STATUS_LU    (1 << 1)
#define E1000_TCTL_EN      (1 << 1)
#define E1000_TCTL_PSP     (1 << 3)
#define E1000_RCTL_EN      (1 << 1)
#define E1000_RCTL_BAM     (1 << 15)
#define E1000_RCTL_SECRC   (1 << 26)
#define E1000_RCTL_LPE     (1 << 5)
#define E1000_RCTL_SZ_2048 (3 << 16)
#define E1000_TXD_STAT_DD  (1 << 0)
#define E1000_RXD_STAT_DD  (1 << 0)
#define E1000_TXD_CMD_EOP  (1 << 0)
#define E1000_TXD_CMD_IFCS (1 << 1)
#define E1000_TXD_CMD_RS   (1 << 3)
#define E1000_ICR_LSC      (1 << 2)

// Buffer sizes
#define E1000_RX_BUFFER_SIZE 2048
#define E1000_TX_BUFFER_SIZE 2048
#define E1000_RX_RING_SIZE 64
#define E1000_TX_RING_SIZE 64

// Network protocols
#define ETHERTYPE_IP    0x0800
#define ETHERTYPE_ARP   0x0806
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP  6
#define IP_PROTOCOL_UDP  17

// Transmit Descriptor
struct e1000_tx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

// Receive Descriptor
struct e1000_rx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

// Network packet structures
struct eth_header {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} __attribute__((packed));

struct ip_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t data[48]; // Fixed size array instead of flex array
} __attribute__((packed));

struct arp_header {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_size;
    uint8_t proto_size;
    uint16_t opcode;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

// Additional E1000 register definitions
// Note: ring sizes are defined above. Avoid duplicate defines here.

// Additional control bits
#define E1000_RCTL_SZ_2048_ALT (0 << 16) // Alternative definition

// Driver functions
void e1000_init(void);
int e1000_send_packet(const uint8_t* data, uint16_t length);
int e1000_receive_packet(uint8_t* buffer, uint16_t* length);
void e1000_print_stats(void);
int e1000_link_up(void);
void e1000_poll(void);
void e1000_handle_interrupt(void);

// Network functions
void net_ping(const char* ip_str);
void net_send_arp(const char* ip_str);

// Configure network interface
void net_set_ip(uint32_t ip_hostorder_le);
void net_set_dns(uint32_t dns_hostorder_le);
void net_set_ip_str(const char* ip_str);
void net_set_dns_str(const char* ip_str);

// Simple packet send API: send raw payload to a server (UDP)
// Returns number of bytes sent on success, 0 on failure.
int net_send_to_server(const char* host, uint16_t port, const uint8_t* data, uint16_t len);

// DNS functions
uint32_t dns_resolve(const char* hostname);
void net_test_dns(void);

// Byte order helpers
uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);

// Checksum calculation
uint16_t calculate_checksum(const void* data, uint16_t length);

// Utility functions
uint32_t parse_ip(const char* ip_str);

// Global variable
extern volatile uint32_t* e1000_mmio;

// DNS structures
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

struct dns_question {
    uint16_t qtype;
    uint16_t qclass;
} __attribute__((packed));

struct dns_answer {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint32_t rdata;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

// DNS cache
#define DNS_CACHE_SIZE 10
struct dns_cache_entry {
    char hostname[64];
    uint32_t ip;
    uint32_t timestamp;
};

// DNS name encoding
int dns_encode_name(const char* name, uint8_t* buffer);