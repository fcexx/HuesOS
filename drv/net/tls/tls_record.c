#include <stdint.h>
#include <string.h>
#include <net/tls.h>
#include <net/tcp.h>

static int tcp_read_exact(uint8_t* out, uint16_t len) {
    uint16_t got = 0;
    while (got < len) {
        int r = tcp_recv(out + got, len - got, 0);
        if (r <= 0) return -1;
        got += (uint16_t)r;
    }
    return 0;
}

static void tls_make_nonce(const uint8_t base[12], uint64_t seq, uint8_t nonce[12]) {
    memcpy(nonce, base, 12);
    for (int i = 0; i < 8; i++) {
        nonce[11 - i] ^= (uint8_t)(seq >> (i * 8));
    }
}

static int tls_send_plain(tls_session_t* session, uint8_t content_type, const uint8_t* data, uint16_t len) {
    if (len > TLS_MAX_RECORD_SIZE) return -1;
    uint8_t header[5];
    header[0] = content_type;
    header[1] = 0x03;
    header[2] = 0x03;
    header[3] = (uint8_t)(len >> 8);
    header[4] = (uint8_t)(len & 0xFF);
    if (tcp_send(header, sizeof(header), 0) != sizeof(header)) return -1;
    if (len && tcp_send(data, len, 0) != len) return -1;
    return 0;
}

static int tls_send_encrypted(tls_session_t* session, const uint8_t* key, const uint8_t* iv,
                              uint64_t* seq_ptr, uint8_t inner_type,
                              const uint8_t* data, uint16_t len) {
    if (len > TLS_MAX_RECORD_SIZE) return -1;
    uint8_t nonce[12];
    tls_make_nonce(iv, *seq_ptr, nonce);

    uint16_t pt_len = len + 1;
    uint8_t plaintext[TLS_MAX_PLAINTEXT + 1];
    memcpy(plaintext, data, len);
    plaintext[len] = inner_type;

    uint8_t ciphertext[TLS_MAX_PLAINTEXT + TLS_AES_GCM_TAG_LEN + 1];
    uint8_t tag[16];

    uint8_t header[5];
    uint16_t ct_len = pt_len + TLS_AES_GCM_TAG_LEN;
    header[0] = TLS_CONTENT_APPLICATION;
    header[1] = 0x03;
    header[2] = 0x03;
    header[3] = (uint8_t)(ct_len >> 8);
    header[4] = (uint8_t)(ct_len & 0xFF);

    if (tls_aes_gcm_encrypt(key, nonce, header, sizeof(header), plaintext, pt_len, ciphertext, tag) != 0) {
        return -1;
    }

    if (tcp_send(header, sizeof(header), 0) != sizeof(header)) return -1;
    if (tcp_send(ciphertext, pt_len, 0) != pt_len) return -1;
    if (tcp_send(tag, TLS_AES_GCM_TAG_LEN, 0) != TLS_AES_GCM_TAG_LEN) return -1;

    (*seq_ptr)++;
    return 0;
}

int tls_record_send_handshake(tls_session_t* session, const uint8_t* data, uint16_t len) {
    if (!session || !data) return -1;
    if (!session->have_handshake_keys) {
        return tls_send_plain(session, TLS_CONTENT_HANDSHAKE, data, len);
    }
    const uint8_t* key = session->client_hs_key;
    const uint8_t* iv = session->client_hs_iv;
    uint64_t* seq = &session->client_seq;
    return tls_send_encrypted(session, key, iv, seq, TLS_CONTENT_HANDSHAKE, data, len);
}

int tls_record_send_application(tls_session_t* session, const uint8_t* data, uint16_t len) {
    if (!session || !data) return -1;
    if (!session->have_app_keys) {
        return -1;
    }
    const uint8_t* key = session->client_app_key;
    const uint8_t* iv = session->client_app_iv;
    uint64_t* seq = &session->client_seq;
    return tls_send_encrypted(session, key, iv, seq, TLS_CONTENT_APPLICATION, data, len);
}

static int tls_decrypt_record(tls_session_t* session, uint8_t outer_type, uint8_t* buffer, uint16_t* len) {
    const uint8_t* key;
    const uint8_t* iv;
    uint64_t* seq_ptr;
    if (session->have_app_keys) {
        key = session->server_app_key;
        iv = session->server_app_iv;
        seq_ptr = &session->server_seq;
    } else if (session->have_handshake_keys) {
        key = session->server_hs_key;
        iv = session->server_hs_iv;
        seq_ptr = &session->server_seq;
    } else {
        return -1;
    }

    if (*len < TLS_AES_GCM_TAG_LEN) return -1;
    uint16_t ciphertext_len = (uint16_t)(*len - TLS_AES_GCM_TAG_LEN);
    uint8_t* ciphertext = buffer;
    uint8_t* tag = buffer + ciphertext_len;

    uint8_t header[5];
    header[0] = outer_type;
    header[1] = 0x03;
    header[2] = 0x03;
    header[3] = (uint8_t)(*len >> 8);
    header[4] = (uint8_t)(*len & 0xFF);

    uint8_t nonce[12];
    tls_make_nonce(iv, *seq_ptr, nonce);

    uint8_t plaintext[TLS_MAX_CIPHERTEXT];
    if (tls_aes_gcm_decrypt(key, nonce, header, sizeof(header), ciphertext, ciphertext_len, tag, plaintext) != 0) {
        return -1;
    }

    (*seq_ptr)++;

    int idx = ciphertext_len - 1;
    while (idx >= 0 && plaintext[idx] == 0x00) idx--;
    if (idx < 0) return -1;
    uint8_t inner_type = plaintext[idx];
    uint16_t pt_len = (uint16_t)idx;
    memcpy(buffer, plaintext, pt_len);
    *len = pt_len;
    session->record_type = inner_type;
    return 0;
}

int tls_record_read(tls_session_t* session, uint8_t* type, const uint8_t** data, uint16_t* len) {
    if (!session) return -1;
    if (session->recv_len != 0) {
        if (type) *type = session->record_type;
        if (data) *data = session->recv_buf;
        if (len) *len = session->recv_len;
        return 0;
    }

    uint8_t header[5];
    if (tcp_read_exact(header, sizeof(header)) != 0) return -1;
    uint16_t body_len = ((uint16_t)header[3] << 8) | header[4];
    if (body_len > TLS_MAX_CIPHERTEXT) return -1;
    if (tcp_read_exact(session->recv_buf, body_len) != 0) return -1;
    session->recv_len = body_len;

    uint8_t outer_type = header[0];
    if (outer_type == TLS_CONTENT_APPLICATION && (session->have_handshake_keys || session->have_app_keys)) {
        uint16_t len_mut = session->recv_len;
        if (tls_decrypt_record(session, outer_type, session->recv_buf, &len_mut) != 0) {
            return -1;
        }
        session->recv_len = len_mut;
    } else {
        session->record_type = outer_type;
    }

    if (type) *type = session->record_type;
    if (data) *data = session->recv_buf;
    if (len) *len = session->recv_len;
    return 0;
}

void tls_record_consume(tls_session_t* session) {
    if (!session) return;
    session->recv_len = 0;
    session->record_type = 0;
}
