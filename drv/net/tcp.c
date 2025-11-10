                                                                                                                                                                                                                                                                        #include <string.h>
#include <mmio.h>
#include <net/tcp.h>
#include <net/ip.h>
#include <net/arp.h>
#include <vga.h>

#define TCP_WINDOW_DEFAULT      0x4000
#define TCP_WAIT_ATTEMPTS       500000
#define TCP_LOCAL_PORT_BASE     40000
#define TCP_MAX_SEGMENT_SIZE    536
#define TCP_RECV_BUFFER         4096
#define TCP_RESEND_INTERVAL     20000

struct tcp_connection {
    int state;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t remote_ip;
    uint32_t snd_una;         // first unacknowledged sequence
    uint32_t snd_nxt;         // next sequence to send
    uint32_t rcv_nxt;         // next sequence expected to receive
    uint16_t window;          // our advertised window
    uint16_t remote_window;   // peer window

    uint8_t  recv_buf[TCP_RECV_BUFFER];
    uint16_t recv_len;

    uint8_t  last_payload[TCP_MAX_SEGMENT_SIZE];
    uint16_t last_len;
    uint32_t last_seq;
    uint8_t  last_flags;

    int remote_closed;
};

static uint8_t g_tx_buffer[sizeof(struct eth_header) + sizeof(struct ip_header) + sizeof(struct tcp_header) + TCP_MAX_SEGMENT_SIZE];
static struct tcp_connection g_conn;
static uint16_t g_next_local_port = TCP_LOCAL_PORT_BASE;
static uint32_t g_next_iss = 0x10000000;

static void tcp_reset_connection(void) {
    memset(&g_conn, 0, sizeof(g_conn));
    g_conn.window = TCP_WINDOW_DEFAULT;
    g_conn.remote_window = TCP_WINDOW_DEFAULT;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const uint8_t* data, uint16_t len) {
    uint32_t sum = 0;
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += IP_PROTOCOL_TCP;
    sum += len;

    const uint16_t* words = (const uint16_t*)data;
    uint16_t length = len;
    while (length > 1) {
        sum += *words++;
        length -= 2;
    }
    if (length == 1) {
        sum += (*(const uint8_t*)words) << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static int tcp_output(uint32_t seq, uint8_t flags, const uint8_t* payload, uint16_t len, int remember) {
    if (!e1000_mmio) return 0;

    uint16_t tcp_len = sizeof(struct tcp_header) + len;
    uint32_t frame_len = sizeof(struct eth_header) + sizeof(struct ip_header) + tcp_len;
    if (frame_len > sizeof(g_tx_buffer)) return 0;

    memset(g_tx_buffer, 0, frame_len);

    struct eth_header* eth = (struct eth_header*)g_tx_buffer;
    struct ip_header* iph = (struct ip_header*)(g_tx_buffer + sizeof(struct eth_header));
    struct tcp_header* tcp = (struct tcp_header*)(g_tx_buffer + sizeof(struct eth_header) + sizeof(struct ip_header));
    uint8_t* tcp_payload = (uint8_t*)tcp + sizeof(struct tcp_header);

    uint8_t dest_mac[6];
    if (!net_resolve_mac(g_conn.remote_ip, dest_mac)) {
        kprintf("Failed to resolve MAC for TCP destination\n");
        return 0;
    }

    memcpy(eth->dst_mac, dest_mac, 6);
    memcpy(eth->src_mac, net_mac_address, 6);
    eth->ethertype = htons(ETHERTYPE_IP);

    iph->version_ihl = 0x45;
    iph->tos = 0;
    iph->total_length = htons(sizeof(struct ip_header) + tcp_len);
    iph->identification = htons((uint16_t)(seq & 0xFFFF));
    iph->flags_fragment = 0;
    iph->ttl = 64;
    iph->protocol = IP_PROTOCOL_TCP;
    iph->checksum = 0;
    iph->src_ip = htonl(net_ip_address);
    iph->dst_ip = htonl(g_conn.remote_ip);
    iph->checksum = calculate_checksum(iph, sizeof(struct ip_header));

    uint8_t header_flags = flags;
    uint32_t ack_val = 0;
    if (!(g_conn.state == TCP_STATE_SYN_SENT && !(flags & TCP_FLAG_ACK))) {
        ack_val = g_conn.rcv_nxt;
    }

    tcp->src_port = htons(g_conn.local_port);
    tcp->dst_port = htons(g_conn.remote_port);
    tcp->seq = htonl(seq);
    if (ack_val) {
        header_flags |= TCP_FLAG_ACK;
        tcp->ack = htonl(ack_val);
    } else {
        tcp->ack = 0;
    }
    tcp->offset_reserved = (uint8_t)(5 << 4);
    tcp->flags = header_flags;
    tcp->window = htons(g_conn.window);
    tcp->checksum = 0;
    tcp->urgent = 0;

    if (payload && len) {
        memcpy(tcp_payload, payload, len);
    }

    tcp->checksum = tcp_checksum(net_ip_address, g_conn.remote_ip, (uint8_t*)tcp, tcp_len);

    if (!e1000_send_packet(g_tx_buffer, (uint16_t)frame_len)) {
        return 0;
    }

    if (remember) {
        if (len && len <= TCP_MAX_SEGMENT_SIZE) {
            memcpy(g_conn.last_payload, payload, len);
            g_conn.last_len = len;
        } else {
            g_conn.last_len = 0;
        }
        g_conn.last_seq = seq;
        g_conn.last_flags = header_flags;
    }

    return 1;
}

static void tcp_resend_last(void) {
    if (!g_conn.last_seq && g_conn.last_len == 0 && !(g_conn.last_flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))) return;
    tcp_output(g_conn.last_seq, g_conn.last_flags, g_conn.last_len ? g_conn.last_payload : NULL, g_conn.last_len, 0);
}

void tcp_init(void) {
    tcp_reset_connection();
    g_next_local_port = TCP_LOCAL_PORT_BASE;
    g_next_iss += 0x1000;
}

int tcp_get_state(void) {
    return g_conn.state;
}

int tcp_remote_closed(void) {
    return g_conn.remote_closed;
}

void tcp_close(void) {
    if (g_conn.state == TCP_STATE_ESTABLISHED) {
        uint32_t seq_before = g_conn.snd_nxt;
        if (tcp_output(seq_before, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0, 0)) {
            g_conn.snd_nxt = seq_before + 1;
            uint32_t wait_ack = TCP_WAIT_ATTEMPTS;
            while (g_conn.snd_una < g_conn.snd_nxt && wait_ack--) {
                if (!ip_poll_once()) {
                    asm volatile("pause");
                }
            }
        }
    }
    tcp_reset_connection();
}

int tcp_connect(uint32_t dest_ip_hostorder, uint16_t dest_port) {
    if (!e1000_mmio) return 0;
    if (g_conn.state != TCP_STATE_CLOSED) {
        kprintf("TCP connection already in use\n");
        return 0;
    }

    tcp_reset_connection();

    g_conn.remote_ip = dest_ip_hostorder;
    g_conn.remote_port = dest_port;
    g_conn.local_port = g_next_local_port++;
    if (g_next_local_port < TCP_LOCAL_PORT_BASE) {
        g_next_local_port = TCP_LOCAL_PORT_BASE;
    }

    uint32_t iss = g_next_iss += 0x1000;
    g_conn.snd_una = iss;
    g_conn.snd_nxt = iss;
    g_conn.rcv_nxt = 0;
    g_conn.remote_closed = 0;
    g_conn.recv_len = 0;
    g_conn.state = TCP_STATE_SYN_SENT;

    if (!tcp_output(g_conn.snd_nxt, TCP_FLAG_SYN, NULL, 0, 1)) {
        kprintf("Failed to send TCP SYN\n");
        tcp_reset_connection();
        return 0;
    }
    g_conn.snd_nxt += 1;

    for (uint32_t i = 0; i < TCP_WAIT_ATTEMPTS; i++) {
        if (g_conn.state == TCP_STATE_ESTABLISHED) {
            return 1;
        }
        if (!ip_poll_once()) {
            asm volatile("pause");
        }
    }

    kprintf("TCP connect timeout\n");
    tcp_reset_connection();
    return 0;
}

int tcp_send(const uint8_t* data, uint16_t len, uint32_t attempts) {
    if (!data || len == 0) return 0;
    if (g_conn.state != TCP_STATE_ESTABLISHED) return 0;

    uint32_t resend_budget = attempts ? attempts : TCP_WAIT_ATTEMPTS;
    uint16_t offset = 0;

    while (offset < len) {
        uint16_t chunk = len - offset;
        if (chunk > TCP_MAX_SEGMENT_SIZE) chunk = TCP_MAX_SEGMENT_SIZE;

        uint32_t seq_before = g_conn.snd_nxt;
        if (!tcp_output(seq_before, TCP_FLAG_PSH | TCP_FLAG_ACK, data + offset, chunk, 1)) {
            return 0;
        }
        g_conn.snd_nxt = seq_before + chunk;

        uint32_t advance = g_conn.snd_nxt;
        uint32_t wait_budget = TCP_RESEND_INTERVAL;
        uint32_t retries_left = resend_budget;

        while (g_conn.snd_una < advance) {
            if (!ip_poll_once()) {
                asm volatile("pause");
                if (wait_budget-- == 0) {
                    if (retries_left-- == 0) {
                        return 0;
                    }
                    wait_budget = TCP_RESEND_INTERVAL;
                    g_conn.snd_nxt = seq_before;
                    tcp_resend_last();
                    g_conn.snd_nxt = seq_before + chunk;
                }
            } else {
                wait_budget = TCP_RESEND_INTERVAL;
            }

            if (g_conn.remote_closed) {
                return 0;
            }
        }

        offset += chunk;
    }

    return len;
}

int tcp_recv(uint8_t* out, uint16_t max_len, uint32_t attempts) {
    if (!out || max_len == 0) return 0;
    uint32_t wait_budget = attempts ? attempts : TCP_WAIT_ATTEMPTS;

    while (1) {
        if (g_conn.recv_len > 0) {
            uint16_t take = g_conn.recv_len;
            if (take > max_len) take = max_len;
            memcpy(out, g_conn.recv_buf, take);
            g_conn.recv_len -= take;
            if (g_conn.recv_len) {
                memmove(g_conn.recv_buf, g_conn.recv_buf + take, g_conn.recv_len);
            }
            return take;
        }

        if (g_conn.remote_closed) {
            return 0;
        }

        if (wait_budget == 0) {
            return -1;
        }

        if (!ip_poll_once()) {
            asm volatile("pause");
            wait_budget--;
        }
    }
}

void tcp_handle_ipv4(const struct ip_header* iph, const uint8_t* payload, uint16_t payload_len) {
    if (!payload || payload_len < sizeof(struct tcp_header)) return;

    const struct tcp_header* tcp = (const struct tcp_header*)payload;
    uint16_t header_len = (uint16_t)((tcp->offset_reserved >> 4) * 4);
    if (header_len < sizeof(struct tcp_header) || header_len > payload_len) return;

    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;
    uint16_t data_len = (uint16_t)(payload_len - header_len);
    const uint8_t* data = payload + header_len;

    if (src_port != g_conn.remote_port || dst_port != g_conn.local_port) {
        return;
    }

    if (flags & TCP_FLAG_ACK) {
        if (ack > g_conn.snd_una && ack <= g_conn.snd_nxt) {
            g_conn.snd_una = ack;
        }
        g_conn.remote_window = ntohs(tcp->window);
    }

    if (g_conn.state == TCP_STATE_SYN_SENT) {
        if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK) && ack == g_conn.snd_nxt) {
            g_conn.rcv_nxt = seq + 1;
            g_conn.remote_window = ntohs(tcp->window);
            g_conn.state = TCP_STATE_ESTABLISHED;
            tcp_output(g_conn.snd_nxt, TCP_FLAG_ACK, NULL, 0, 0);
        } else if (flags & TCP_FLAG_RST) {
            tcp_reset_connection();
        }
        return;
    }

    if (g_conn.state != TCP_STATE_ESTABLISHED) {
        return;
    }

    if (flags & TCP_FLAG_RST) {
        tcp_reset_connection();
        return;
    }

    if (data_len) {
        if (seq == g_conn.rcv_nxt) {
            uint16_t space = TCP_RECV_BUFFER - g_conn.recv_len;
            uint16_t copy_len = data_len;
            if (copy_len > space) copy_len = space;
            if (copy_len) {
                memcpy(g_conn.recv_buf + g_conn.recv_len, data, copy_len);
                g_conn.recv_len += copy_len;
            }
            g_conn.rcv_nxt += data_len;
        }
        tcp_output(g_conn.snd_nxt, TCP_FLAG_ACK, NULL, 0, 0);
    }

    if (flags & TCP_FLAG_FIN) {
        g_conn.rcv_nxt += 1;
        g_conn.remote_closed = 1;
        tcp_output(g_conn.snd_nxt, TCP_FLAG_ACK, NULL, 0, 0);
    }
}
