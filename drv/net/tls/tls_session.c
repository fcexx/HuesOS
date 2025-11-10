#include <stdint.h>
#include <string.h>
#include <mmio.h>
#include <net/tls.h>
#include <net/tcp.h>

#define TLS_MAX_SESSIONS 2

extern tls_result_t tls_handshake_step(tls_session_t* session);

tls_session_t g_tls_sessions[TLS_MAX_SESSIONS];
uint8_t g_tls_session_used[TLS_MAX_SESSIONS];

void tls_init(void) {
    memset(g_tls_sessions, 0, sizeof(g_tls_sessions));
    memset(g_tls_session_used, 0, sizeof(g_tls_session_used));
}

tls_session_t* tls_session_alloc(void) {
    for (int i = 0; i < TLS_MAX_SESSIONS; i++) {
        if (!g_tls_session_used[i]) {
            g_tls_session_used[i] = 1;
            tls_session_t* s = &g_tls_sessions[i];
            memset(s, 0, sizeof(tls_session_t));
            s->state = TLS_STATE_CLOSED;
            s->sni = s->sni_buf;
            s->sni_buf[0] = '\0';
            tls_hash_init(&s->transcript);
            return s;
        }
    }
    return NULL;
}

void tls_session_free(tls_session_t* session) {
    if (!session) return;
    tls_session_close(session);
    for (int i = 0; i < TLS_MAX_SESSIONS; i++) {
        if (&g_tls_sessions[i] == session) {
            g_tls_session_used[i] = 0;
            memset(session, 0, sizeof(tls_session_t));
            break;
        }
    }
}

static uint32_t tls_resolve_host(const char* host) {
    if (!host || !*host) return 0;
    uint32_t ip = parse_ip(host);
    if (ip == 0) {
        ip = dns_resolve(host);
    }
    return ip;
}

int tls_client_connect(tls_session_t* session, const char* host, uint16_t port) {
    if (!session || !host) return 0;
    if (session->state != TLS_STATE_CLOSED) return 0;

    uint32_t ip = tls_resolve_host(host);
    if (ip == 0) {
        return 0;
    }

    if (!tcp_connect(ip, port)) {
        return 0;
    }

    strncpy(session->sni_buf, host, sizeof(session->sni_buf) - 1);
    session->sni_buf[sizeof(session->sni_buf) - 1] = '\0';
    session->sni = session->sni_buf;

    session->server_ip = ip;
    session->server_port = port;
    session->tcp = NULL;

    tls_random_bytes(session->client_random, sizeof(session->client_random));
    if (tls_x25519_keypair(session->private_key, session->public_key) != 0) {
        tcp_close();
        session->state = TLS_STATE_CLOSED;
        return 0;
    }
    tls_hash_init(&session->transcript);

    session->have_handshake_keys = 0;
    session->have_app_keys = 0;
    session->remote_closed = 0;
    session->client_seq = 0;
    session->server_seq = 0;
    session->recv_len = 0;
    session->record_type = 0;
    session->pending_alert_level = 0;
    session->pending_alert_desc = 0;

    session->state = TLS_STATE_CLIENT_HELLO;
    return 1;
}

tls_result_t tls_session_poll(tls_session_t* session) {
    if (!session) return TLS_ERR_INTERNAL;
    if (session->state == TLS_STATE_ESTABLISHED) return TLS_ERR_OK;
    if (session->state == TLS_STATE_ALERT) return TLS_ERR_ALERT;
    return tls_handshake_step(session);
}

int tls_session_read(tls_session_t* session, uint8_t* out, int max_len) {
    if (!session || !out || max_len <= 0) return -1;
    if (session->state != TLS_STATE_ESTABLISHED) return -1;

    uint8_t type;
    const uint8_t* data;
    uint16_t len;

    if (session->recv_len == 0) {
        if (tls_record_read(session, &type, &data, &len) != 0) {
            return -1;
        }
    } else {
        type = session->record_type;
        data = session->recv_buf;
        len = session->recv_len;
    }

    if (type == 21) {
        if (len >= 2) {
            session->pending_alert_level = data[0];
            session->pending_alert_desc = data[1];
        }
        session->state = TLS_STATE_ALERT;
        tls_record_consume(session);
        return 0;
    }

    if (type != 23) {
        tls_record_consume(session);
        return -1;
    }

    int copy = len < (uint16_t)max_len ? (int)len : max_len;
    memcpy(out, data, copy);
    session->recv_len -= copy;
    if (session->recv_len) {
        memmove(session->recv_buf, session->recv_buf + copy, session->recv_len);
    } else {
        tls_record_consume(session);
    }
    return copy;
}

int tls_session_write(tls_session_t* session, const uint8_t* data, int len) {
    if (!session || !data || len <= 0) return -1;
    if (session->state != TLS_STATE_ESTABLISHED) return -1;

    int total = 0;
    while (total < len) {
        uint16_t chunk = (uint16_t)((len - total) > TLS_MAX_PLAINTEXT ? TLS_MAX_PLAINTEXT : (len - total));
        if (tls_record_send_application(session, data + total, chunk) != 0) {
            return -1;
        }
        total += chunk;
    }
    return total;
}

void tls_session_close(tls_session_t* session) {
    if (!session) return;
    tcp_close();
    session->state = TLS_STATE_CLOSED;
    session->have_handshake_keys = 0;
    session->have_app_keys = 0;
    session->remote_closed = 0;
    session->recv_len = 0;
}
