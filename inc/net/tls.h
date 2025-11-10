#ifndef NET_TLS_H
#define NET_TLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS_CONTENT_ALERT       21
#define TLS_CONTENT_HANDSHAKE   22
#define TLS_CONTENT_APPLICATION 23

typedef enum {
    TLS_STATE_CLOSED = 0,
    TLS_STATE_CLIENT_HELLO,
    TLS_STATE_WAIT_SERVER_HELLO,
    TLS_STATE_WAIT_ENCRYPTED_EXTENSIONS,
    TLS_STATE_WAIT_CERTIFICATE,
    TLS_STATE_WAIT_CERT_VERIFY,
    TLS_STATE_WAIT_FINISHED,
    TLS_STATE_SEND_FINISHED,
    TLS_STATE_ESTABLISHED,
    TLS_STATE_ALERT,
} tls_state_t;

typedef enum {
    TLS_ERR_OK = 0,
    TLS_ERR_WANT_READ,
    TLS_ERR_WANT_WRITE,
    TLS_ERR_ALERT,
    TLS_ERR_HANDSHAKE,
    TLS_ERR_INTERNAL,
} tls_result_t;

#define TLS_MAX_RECORD_SIZE    16384
#define TLS_MAX_PLAINTEXT      TLS_MAX_RECORD_SIZE
#define TLS_MAX_CIPHERTEXT     (TLS_MAX_RECORD_SIZE + 256)
#define TLS_AES_GCM_TAG_LEN    16
#define TLS_HANDSHAKE_HASH_LEN 32

struct tcp_connection;

typedef struct tls_hash_ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} tls_hash_ctx_t;

typedef struct tls_session {
    tls_state_t state;
    struct tcp_connection* tcp;

    tls_hash_ctx_t transcript;
    uint8_t handshake_hash[TLS_HANDSHAKE_HASH_LEN];
    uint8_t client_random[32];
    uint8_t server_random[32];

    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t peer_key[32];

    uint8_t handshake_secret[32];
    uint8_t master_secret[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];

    uint8_t client_hs_key[16];
    uint8_t client_hs_iv[12];
    uint8_t server_hs_key[16];
    uint8_t server_hs_iv[12];

    uint8_t client_app_key[16];
    uint8_t client_app_iv[12];
    uint8_t server_app_key[16];
    uint8_t server_app_iv[12];

    uint8_t client_finished_key[32];
    uint8_t server_finished_key[32];

    uint64_t client_seq;
    uint64_t server_seq;

    uint8_t recv_buf[TLS_MAX_CIPHERTEXT];
    uint16_t recv_len;
    uint8_t record_type;

    uint8_t pending_alert_level;
    uint8_t pending_alert_desc;

    char sni_buf[64];
    const char* sni;
    uint32_t server_ip;
    uint16_t server_port;

    int have_handshake_keys;
    int have_app_keys;
    int remote_closed;
} tls_session_t;

void tls_init(void);
tls_session_t* tls_session_alloc(void);
void tls_session_free(tls_session_t* session);

void tls_random_bytes(uint8_t* out, uint32_t len);
int tls_sha256(const uint8_t* data, uint32_t len, uint8_t out[32]);
void tls_hash_init(tls_hash_ctx_t* ctx);
void tls_hash_update(tls_hash_ctx_t* ctx, const uint8_t* data, uint32_t len);
void tls_hash_copy(const tls_hash_ctx_t* src, tls_hash_ctx_t* dst);
void tls_hash_final(const tls_hash_ctx_t* ctx, uint8_t out[32]);
int tls_hmac_sha256(const uint8_t* key, uint32_t key_len,
                    const uint8_t* data, uint32_t data_len,
                    uint8_t out[32]);
int tls_hkdf_extract(const uint8_t* salt, uint32_t salt_len,
                     const uint8_t* ikm, uint32_t ikm_len,
                     uint8_t out[32]);
int tls_hkdf_expand(const uint8_t* prk, const uint8_t* info, uint32_t info_len,
                    uint8_t out_len, uint8_t* out);
int tls_hkdf_expand_label(const uint8_t* secret, const char* label,
                          const uint8_t* context, uint32_t context_len,
                          uint8_t out_len, uint8_t* out);
int tls_label_secret(const uint8_t* secret, const char* label,
                     const uint8_t* context, uint32_t context_len,
                     uint8_t out[32]);
int tls_label_key_iv(const uint8_t* secret, uint8_t key[16], uint8_t iv[12]);
int tls_finished_key(const uint8_t* secret, uint8_t out[32]);

int tls_aes_gcm_encrypt(const uint8_t* key, const uint8_t* iv,
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* plaintext, uint32_t plaintext_len,
                        uint8_t* ciphertext, uint8_t tag[16]);
int tls_aes_gcm_decrypt(const uint8_t* key, const uint8_t* iv,
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* ciphertext, uint32_t ciphertext_len,
                        const uint8_t tag[16], uint8_t* plaintext);

int tls_x25519_keypair(uint8_t priv[32], uint8_t pub[32]);
int tls_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]);

int tls_client_connect(tls_session_t* session, const char* host, uint16_t port);
tls_result_t tls_session_poll(tls_session_t* session);
int tls_session_read(tls_session_t* session, uint8_t* out, int max_len);
int tls_session_write(tls_session_t* session, const uint8_t* data, int len);
void tls_session_close(tls_session_t* session);

int tls_record_send_handshake(tls_session_t* session, const uint8_t* data, uint16_t len);
int tls_record_send_application(tls_session_t* session, const uint8_t* data, uint16_t len);
int tls_record_read(tls_session_t* session, uint8_t* type, const uint8_t** data, uint16_t* len);
void tls_record_consume(tls_session_t* session);

#ifdef __cplusplus
}
#endif

#endif // NET_TLS_H
