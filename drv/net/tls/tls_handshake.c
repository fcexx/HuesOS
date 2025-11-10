#include <stdint.h>
#include <string.h>
#include <mmio.h>
#include <net/tls.h>
#include <net/tcp.h>

static tls_result_t tls_send_client_hello(tls_session_t* session);
static tls_result_t tls_handle_server_hello(tls_session_t* session);
static tls_result_t tls_handle_encrypted_extensions(tls_session_t* session);
static tls_result_t tls_handle_certificate(tls_session_t* session);
static tls_result_t tls_handle_cert_verify(tls_session_t* session);
static tls_result_t tls_handle_finished(tls_session_t* session);
static tls_result_t tls_send_client_finished(tls_session_t* session);

static void tls_update_transcript(tls_session_t* session, const uint8_t* data, uint32_t len) {
    tls_hash_update(&session->transcript, data, len);
}

static int tls_parse_length(const uint8_t** p, const uint8_t* end, uint32_t* out_len) {
    if (*p >= end) return -1;
    uint8_t first = *(*p)++;
    if (first < 0x80) {
        *out_len = first;
        return (*p + *out_len <= end) ? 0 : -1;
    }
    uint8_t bytes = first & 0x7F;
    if (bytes == 0 || bytes > 4) return -1;
    if (*p + bytes > end) return -1;
    uint32_t len = 0;
    for (uint8_t i = 0; i < bytes; i++) {
        len = (len << 8) | *(*p)++;
    }
    *out_len = len;
    return (*p + len <= end) ? 0 : -1;
}

static char tls_to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static void tls_lowercase(char* s) {
    for (; *s; ++s) *s = tls_to_lower(*s);
}

static int tls_hostname_match(const char* host, const char* pattern) {
    if (!host || !pattern) return 0;
    char host_buf[128];
    char pattern_buf[128];
    strncpy(host_buf, host, sizeof(host_buf) - 1);
    strncpy(pattern_buf, pattern, sizeof(pattern_buf) - 1);
    host_buf[sizeof(host_buf) - 1] = '\0';
    pattern_buf[sizeof(pattern_buf) - 1] = '\0';
    tls_lowercase(host_buf);
    tls_lowercase(pattern_buf);

    if (pattern_buf[0] == '*' && pattern_buf[1] == '.') {
        const char* suffix = strchr(host_buf, '.');
        if (!suffix) return 0;
        return strcmp(suffix + 1, pattern_buf + 2) == 0;
    }
    return strcmp(host_buf, pattern_buf) == 0;
}

static int tls_extract_host_from_cert(const uint8_t* cert, uint32_t len, const char* host) {
    if (!host || !*host) return 1;
    const uint8_t* p = cert;
    const uint8_t* end = cert + len;
    if (p >= end || *p++ != 0x30) return 0;
    uint32_t seq_len;
    if (tls_parse_length(&p, end, &seq_len) != 0 || p + seq_len != end) return 0;

    for (uint32_t i = 0; i + 1 < len; i++) {
        if (cert[i] == 0x82) { // dNSName
            uint32_t name_len = cert[i + 1];
            if (i + 2 + name_len <= len && name_len < 120) {
                char name[128];
                memcpy(name, &cert[i + 2], name_len);
                name[name_len] = '\0';
                if (tls_hostname_match(host, name)) {
                    return 1;
                }
            }
        }
    }

    for (uint32_t i = 0; i + 5 < len; i++) {
        if (cert[i] == 0x06 && cert[i + 1] == 0x03 && cert[i + 2] == 0x55 && cert[i + 3] == 0x04 && cert[i + 4] == 0x03) {
            const uint8_t* q = cert + i + 5;
            if (q >= end) break;
            uint8_t tag = *q++;
            uint32_t name_len;
            if (tls_parse_length(&q, end, &name_len) != 0) break;
            if (q + name_len > end) break;
            if (name_len < 120 && (tag == 0x0c || tag == 0x13)) {
                char name[128];
                memcpy(name, q, name_len);
                name[name_len] = '\0';
                if (tls_hostname_match(host, name)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int tls_verify_certificate(const uint8_t* cert, uint32_t len, const char* host) {
    if (!host || !*host) return 1;
    return tls_extract_host_from_cert(cert, len, host);
}

tls_result_t tls_handshake_step(tls_session_t* session) {
    switch (session->state) {
    case TLS_STATE_CLIENT_HELLO:
        return tls_send_client_hello(session);
    case TLS_STATE_WAIT_SERVER_HELLO:
        return tls_handle_server_hello(session);
    case TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS:
        return tls_handle_encrypted_extensions(session);
    case TLS_STATE_WAIT_CERTIFICATE:
        return tls_handle_certificate(session);
    case TLS_STATE_WAIT_CERT_VERIFY:
        return tls_handle_cert_verify(session);
    case TLS_STATE_WAIT_FINISHED:
        return tls_handle_finished(session);
    case TLS_STATE_SEND_FINISHED:
        return tls_send_client_finished(session);
    case TLS_STATE_ALERT:
        return TLS_ERR_ALERT;
    case TLS_STATE_ESTABLISHED:
        return TLS_ERR_OK;
    default:
        return TLS_ERR_INTERNAL;
    }
}

static tls_result_t tls_send_client_hello(tls_session_t* session) {
    uint8_t buf[512];
    uint8_t* p = buf + 4;

    *p++ = 0x03; *p++ = 0x03; // legacy_version
    memcpy(p, session->client_random, 32); p += 32;

    *p++ = 0; // session id length

    // Cipher suites
    *p++ = 0x00; *p++ = 0x02; // length
    *p++ = 0x13; *p++ = 0x01; // TLS_AES_128_GCM_SHA256

    // Compression methods
    *p++ = 0x01; *p++ = 0x00;

    uint8_t* ext_len_ptr = p; p += 2;

    uint16_t host_len = (uint16_t)strlen(session->sni ? session->sni : "");
    if (host_len) {
        *p++ = 0x00; *p++ = 0x00; // SNI
        uint8_t* sni_len_ptr = p; p += 2;
        uint16_t server_name_list_len = (uint16_t)(host_len + 3);
        *p++ = (uint8_t)(server_name_list_len >> 8);
        *p++ = (uint8_t)(server_name_list_len & 0xFF);
        *p++ = 0x00; // host_name
        *p++ = (uint8_t)(host_len >> 8);
        *p++ = (uint8_t)(host_len & 0xFF);
        memcpy(p, session->sni, host_len); p += host_len;
        uint16_t sni_len = (uint16_t)(p - (sni_len_ptr + 2));
        sni_len_ptr[0] = (uint8_t)(sni_len >> 8);
        sni_len_ptr[1] = (uint8_t)(sni_len & 0xFF);
    }

    // Supported groups
    *p++ = 0x00; *p++ = 0x0a;
    *p++ = 0x00; *p++ = 0x06;
    *p++ = 0x00; *p++ = 0x04;
    *p++ = 0x00; *p++ = 0x1d; // x25519
    *p++ = 0x00; *p++ = 0x17; // secp256r1

    // Signature algorithms
    *p++ = 0x00; *p++ = 0x0d;
    *p++ = 0x00; *p++ = 0x06;
    *p++ = 0x00; *p++ = 0x04;
    *p++ = 0x08; *p++ = 0x04; // rsa_pss_rsae_sha256
    *p++ = 0x04; *p++ = 0x03; // ecdsa_secp256r1_sha256

    // Supported versions
    *p++ = 0x00; *p++ = 0x2b;
    *p++ = 0x00; *p++ = 0x03;
    *p++ = 0x02;
    *p++ = 0x03; *p++ = 0x04;

    // PSK key exchange modes
    *p++ = 0x00; *p++ = 0x2d;
    *p++ = 0x00; *p++ = 0x02;
    *p++ = 0x01; // length
    *p++ = 0x01; // psk_dhe_ke

    // Key share
    *p++ = 0x00; *p++ = 0x33;
    *p++ = 0x00; *p++ = 0x26; // 38 bytes
    *p++ = 0x00; *p++ = 0x24; // client shares length
    *p++ = 0x00; *p++ = 0x1d;
    *p++ = 0x00; *p++ = 0x20;
    memcpy(p, session->public_key, 32); p += 32;

    uint16_t ext_len = (uint16_t)(p - (ext_len_ptr + 2));
    ext_len_ptr[0] = (uint8_t)(ext_len >> 8);
    ext_len_ptr[1] = (uint8_t)(ext_len & 0xFF);

    uint32_t body_len = (uint32_t)(p - (buf + 4));
    buf[0] = 0x01;
    buf[1] = (uint8_t)(body_len >> 16);
    buf[2] = (uint8_t)(body_len >> 8);
    buf[3] = (uint8_t)(body_len & 0xFF);

    tls_update_transcript(session, buf, body_len + 4);
    if (tls_record_send_handshake(session, buf, (uint16_t)(body_len + 4)) != 0) {
        return TLS_ERR_INTERNAL;
    }
    session->state = TLS_STATE_WAIT_SERVER_HELLO;
    return TLS_ERR_WANT_READ;
}

static int tls_parse_server_hello(tls_session_t* session, const uint8_t* data, uint32_t len) {
    if (len < 38) return -1;
    const uint8_t* p = data;
    uint8_t handshake_type = *p++;
    uint32_t msg_len = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    p += 3;
    if (msg_len + 4 != len) return -1;

    const uint8_t* end = p + msg_len;
    if (end - p < 2 + 32 + 1) return -1;
    if (p[0] != 0x03 || p[1] != 0x03) return -1;
    p += 2;
    memcpy(session->server_random, p, 32); p += 32;

    uint8_t session_id_len = *p++;
    if (end - p < session_id_len + 2 + 1) return -1;
    p += session_id_len;

    uint16_t cipher_suite = ((uint16_t)p[0] << 8) | p[1]; p += 2;
    if (cipher_suite != 0x1301) return -1;

    uint8_t comp_len = *p++;
    if (comp_len != 1 || *p++ != 0x00) return -1;

    if (end - p < 2) return -1;
    uint16_t ext_len = ((uint16_t)p[0] << 8) | p[1]; p += 2;
    if (end - p != ext_len) return -1;

    int have_version = 0;
    int have_key_share = 0;
    while (p < end) {
        if (end - p < 4) return -1;
        uint16_t ext_type = ((uint16_t)p[0] << 8) | p[1];
        uint16_t ext_size = ((uint16_t)p[2] << 8) | p[3];
        p += 4;
        if (end - p < ext_size) return -1;
        switch (ext_type) {
        case 0x002b: // supported_versions
            if (ext_size != 2 || !(p[0] == 0x03 && p[1] == 0x04)) return -1;
            have_version = 1;
            break;
        case 0x0033: // key_share
            if (ext_size < 4) return -1;
            uint16_t group = ((uint16_t)p[0] << 8) | p[1];
            uint16_t klen = ((uint16_t)p[2] << 8) | p[3];
            if (group != 0x001d || klen != 32 || ext_size < 4 + 32) return -1;
            memcpy(session->peer_key, p + 4, 32);
            have_key_share = 1;
            break;
        default:
            break;
        }
        p += ext_size;
    }

    if (!have_version || !have_key_share) return -1;
    if (handshake_type != 0x02) return -1;
    return 0;
}

static tls_result_t tls_handle_server_hello(tls_session_t* session) {
    uint8_t type;
    const uint8_t* data;
    uint16_t len;
    if (tls_record_read(session, &type, &data, &len) != 0) {
        return TLS_ERR_WANT_READ;
    }

    if (type == TLS_CONTENT_ALERT) {
        if (len >= 2) {
            session->pending_alert_level = data[0];
            session->pending_alert_desc = data[1];
        }
        session->state = TLS_STATE_ALERT;
        return TLS_ERR_ALERT;
    }

    if (type != TLS_CONTENT_HANDSHAKE) {
        return TLS_ERR_HANDSHAKE;
    }

    if (tls_parse_server_hello(session, data, len) != 0) {
        return TLS_ERR_HANDSHAKE;
    }

    tls_update_transcript(session, data, len);

    uint8_t shared[32];
    if (tls_x25519_shared(session->private_key, session->peer_key, shared) != 0) {
        return TLS_ERR_INTERNAL;
    }

    uint8_t zero_salt[32] = {0};
    uint8_t early_secret[32];
    tls_hkdf_extract(zero_salt, 32, NULL, 0, early_secret);

    uint8_t empty_hash[32];
    tls_sha256(NULL, 0, empty_hash);

    uint8_t derived[32];
    tls_hkdf_expand_label(early_secret, "derived", empty_hash, 32, 32, derived);
    tls_hkdf_extract(derived, 32, shared, 32, session->handshake_secret);

    tls_hash_ctx_t tmp;
    tls_hash_copy(&session->transcript, &tmp);
    uint8_t handshake_hash[32];
    tls_hash_final(&tmp, handshake_hash);

    uint8_t client_hs_secret[32];
    uint8_t server_hs_secret[32];
    tls_hkdf_expand_label(session->handshake_secret, "c hs traffic", handshake_hash, 32, 32, client_hs_secret);
    tls_hkdf_expand_label(session->handshake_secret, "s hs traffic", handshake_hash, 32, 32, server_hs_secret);

    tls_label_key_iv(client_hs_secret, session->client_hs_key, session->client_hs_iv);
    tls_label_key_iv(server_hs_secret, session->server_hs_key, session->server_hs_iv);
    tls_finished_key(client_hs_secret, session->client_finished_key);
    tls_finished_key(server_hs_secret, session->server_finished_key);

    session->have_handshake_keys = 1;
    session->have_app_keys = 0;
    session->client_seq = 0;
    session->server_seq = 0;

    tls_record_consume(session);
    session->state = TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS;
    return TLS_ERR_WANT_READ;
}

static tls_result_t tls_handle_encrypted_extensions(tls_session_t* session) {
    uint8_t type;
    const uint8_t* data;
    uint16_t len;
    if (tls_record_read(session, &type, &data, &len) != 0) {
        return TLS_ERR_WANT_READ;
    }

    if (type == TLS_CONTENT_ALERT) {
        if (len >= 2) {
            session->pending_alert_level = data[0];
            session->pending_alert_desc = data[1];
        }
        session->state = TLS_STATE_ALERT;
        return TLS_ERR_ALERT;
    }

    if (type != TLS_CONTENT_HANDSHAKE || len < 4 || data[0] != 8) {
        return TLS_ERR_HANDSHAKE;
    }

    uint32_t msg_len = ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    if (msg_len + 4 != len) return TLS_ERR_HANDSHAKE;

    tls_update_transcript(session, data, len);
    tls_record_consume(session);
    session->state = TLS_STATE_WAIT_CERTIFICATE;
    return TLS_ERR_WANT_READ;
}

static tls_result_t tls_handle_certificate(tls_session_t* session) {
    uint8_t type;
    const uint8_t* data;
    uint16_t len;
    if (tls_record_read(session, &type, &data, &len) != 0) {
        return TLS_ERR_WANT_READ;
    }

    if (type == TLS_CONTENT_ALERT) {
        if (len >= 2) {
            session->pending_alert_level = data[0];
            session->pending_alert_desc = data[1];
        }
    }

    if (type != TLS_CONTENT_HANDSHAKE || len < 7 || data[0] != 11) {
        return TLS_ERR_HANDSHAKE;
    }

    const uint8_t* p = data + 4;
    const uint8_t* end = data + len;
    if (p >= end) return TLS_ERR_HANDSHAKE;
    uint8_t ctx_len = *p++;
    if (p + ctx_len + 3 > end) return TLS_ERR_HANDSHAKE;
    p += ctx_len;
    uint32_t list_len = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    p += 3;
    if (p + list_len != end) return TLS_ERR_HANDSHAKE;

    const uint8_t* list_end = p + list_len;
    int cert_index = 0;
    while (p < list_end) {
        if (p + 3 > list_end) return TLS_ERR_HANDSHAKE;
        uint32_t cert_len = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        p += 3;
        if (p + cert_len + 2 > list_end) return TLS_ERR_HANDSHAKE;
        if (cert_index == 0) {
            if (!tls_verify_certificate(p, cert_len, session->sni)) {
                return TLS_ERR_HANDSHAKE;
            }
        }
        p += cert_len;
        uint16_t ext_len = ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        if (p + ext_len > list_end) return TLS_ERR_HANDSHAKE;
        p += ext_len;
        cert_index++;
    }

    tls_update_transcript(session, data, len);
    tls_record_consume(session);
    session->state = TLS_STATE_WAIT_CERT_VERIFY;
    return TLS_ERR_WANT_READ;
}

static tls_result_t tls_handle_cert_verify(tls_session_t* session) {
    uint8_t type;
    const uint8_t* data;
    uint16_t len;
    if (tls_record_read(session, &type, &data, &len) != 0) {
        return TLS_ERR_WANT_READ;
    }

    if (type == TLS_CONTENT_ALERT) {
        if (len >= 2) {
            session->pending_alert_level = data[0];
            session->pending_alert_desc = data[1];
        }
        session->state = TLS_STATE_ALERT;
        return TLS_ERR_ALERT;
    }

    if (type != TLS_CONTENT_HANDSHAKE || len < 6 || data[0] != 15) {
        return TLS_ERR_HANDSHAKE;
    }

    const uint8_t* p = data + 4;
    const uint8_t* end = data + len;
    if (p + 2 > end) return TLS_ERR_HANDSHAKE;
    p += 2; // signature scheme
    if (p + 2 > end) return TLS_ERR_HANDSHAKE;
    uint16_t sig_len = ((uint16_t)p[0] << 8) | p[1];
    p += 2;
    if (p + sig_len != end) return TLS_ERR_HANDSHAKE;

    // TODO: Verify signature using certificate chain

    tls_update_transcript(session, data, len);
    tls_record_consume(session);
    session->state = TLS_STATE_WAIT_FINISHED;
    return TLS_ERR_WANT_READ;
}

static tls_result_t tls_handle_finished(tls_session_t* session) {
    uint8_t type;
    const uint8_t* data;
    uint16_t len;
    if (tls_record_read(session, &type, &data, &len) != 0) {
        return TLS_ERR_WANT_READ;
    }

    if (type == TLS_CONTENT_ALERT) {
        if (len >= 2) {
            session->pending_alert_level = data[0];
            session->pending_alert_desc = data[1];
        }
        session->state = TLS_STATE_ALERT;
        return TLS_ERR_ALERT;
    }

    if (type != TLS_CONTENT_HANDSHAKE || len < 4 || data[0] != 20) {
        return TLS_ERR_HANDSHAKE;
    }

    uint32_t msg_len = ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    if (msg_len + 4 != len || msg_len != 32) return TLS_ERR_HANDSHAKE;

    tls_hash_ctx_t tmp;
    tls_hash_copy(&session->transcript, &tmp);
    uint8_t handshake_hash[32];
    tls_hash_final(&tmp, handshake_hash);

    uint8_t expected[32];
    tls_hmac_sha256(session->server_finished_key, 32, handshake_hash, 32, expected);
    if (memcmp(expected, data + 4, 32) != 0) {
        session->state = TLS_STATE_ALERT;
        session->pending_alert_level = 2;
        session->pending_alert_desc = 40; // handshake_failure
        return TLS_ERR_ALERT;
    }

    tls_update_transcript(session, data, len);

    uint8_t empty_hash[32];
    tls_sha256(NULL, 0, empty_hash);

    uint8_t derived[32];
    tls_hkdf_expand_label(session->handshake_secret, "derived", empty_hash, 32, 32, derived);
    uint8_t zero[32] = {0};
    tls_hkdf_extract(derived, 32, zero, 0, session->master_secret);

    tls_hash_copy(&session->transcript, &tmp);
    tls_hash_final(&tmp, handshake_hash);

    tls_hkdf_expand_label(session->master_secret, "c ap traffic", handshake_hash, 32, 32, session->client_app_secret);
    tls_hkdf_expand_label(session->master_secret, "s ap traffic", handshake_hash, 32, 32, session->server_app_secret);
    tls_label_key_iv(session->client_app_secret, session->client_app_key, session->client_app_iv);
    tls_label_key_iv(session->server_app_secret, session->server_app_key, session->server_app_iv);

    session->have_app_keys = 1;
    session->client_seq = 0;
    session->server_seq = 0;

    tls_record_consume(session);
    session->state = TLS_STATE_SEND_FINISHED;
    return TLS_ERR_WANT_WRITE;
}

static tls_result_t tls_send_client_finished(tls_session_t* session) {
    uint8_t handshake_hash[32];
    tls_hash_ctx_t tmp;
    tls_hash_copy(&session->transcript, &tmp);
    tls_hash_final(&tmp, handshake_hash);

    uint8_t verify_data[32];
    tls_hmac_sha256(session->client_finished_key, 32, handshake_hash, 32, verify_data);

    uint8_t msg[4 + 32];
    msg[0] = 20;
    msg[1] = 0;
    msg[2] = 0;
    msg[3] = 32;
    memcpy(msg + 4, verify_data, 32);

    tls_update_transcript(session, msg, sizeof(msg));

    if (tls_record_send_handshake(session, msg, sizeof(msg)) != 0) {
        return TLS_ERR_INTERNAL;
    }

    session->state = TLS_STATE_ESTABLISHED;
    return TLS_ERR_OK;
}
