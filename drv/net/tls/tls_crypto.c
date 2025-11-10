#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <net/tls.h>

// ================= SHA-256 ==================
#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x) (ROTR32((x),2) ^ ROTR32((x),13) ^ ROTR32((x),22))
#define SIGMA1(x) (ROTR32((x),6) ^ ROTR32((x),11) ^ ROTR32((x),25))
#define sigma0(x) (ROTR32((x),7) ^ ROTR32((x),18) ^ ((x) >> 3))
#define sigma1(x) (ROTR32((x),17) ^ ROTR32((x),19) ^ ((x) >> 10))

static const uint32_t k_sha256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void tls_hash_init(tls_hash_ctx_t* ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->datalen = 0;
}

static void tls_sha256_transform(tls_hash_ctx_t* ctx, const uint8_t block[64]) {
    uint32_t m[64];
    for (int i = 0; i < 16; i++) {
        m[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        m[i] = sigma1(m[i-2]) + m[i-7] + sigma0(m[i-15]) + m[i-16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t temp1 = h + SIGMA1(e) + CH(e,f,g) + k_sha256[i] + m[i];
        uint32_t temp2 = SIGMA0(a) + MAJ(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void tls_hash_update(tls_hash_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            tls_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void tls_hash_copy(const tls_hash_ctx_t* src, tls_hash_ctx_t* dst) {
    memcpy(dst, src, sizeof(tls_hash_ctx_t));
}

void tls_hash_final(const tls_hash_ctx_t* ctx_in, uint8_t out[32]) {
    tls_hash_ctx_t ctx;
    memcpy(&ctx, ctx_in, sizeof(tls_hash_ctx_t));

    uint32_t i = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56) ctx.data[i++] = 0x00;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64) ctx.data[i++] = 0x00;
        tls_sha256_transform(&ctx, ctx.data);
        memset(ctx.data, 0, 56);
    }

    ctx.bitlen += ctx.datalen * 8;
    ctx.data[63] = (uint8_t)(ctx.bitlen);
    ctx.data[62] = (uint8_t)(ctx.bitlen >> 8);
    ctx.data[61] = (uint8_t)(ctx.bitlen >> 16);
    ctx.data[60] = (uint8_t)(ctx.bitlen >> 24);
    ctx.data[59] = (uint8_t)(ctx.bitlen >> 32);
    ctx.data[58] = (uint8_t)(ctx.bitlen >> 40);
    ctx.data[57] = (uint8_t)(ctx.bitlen >> 48);
    ctx.data[56] = (uint8_t)(ctx.bitlen >> 56);
    tls_sha256_transform(&ctx, ctx.data);

    for (i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(ctx.state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx.state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx.state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx.state[i]);
    }
}

int tls_sha256(const uint8_t* data, uint32_t len, uint8_t out[32]) {
    if (!out) return -1;
    tls_hash_ctx_t ctx;
    tls_hash_init(&ctx);
    if (data && len) tls_hash_update(&ctx, data, len);
    tls_hash_final(&ctx, out);
    return 0;
}

// ============== Random generator =================
static uint64_t g_tls_rng_state;

static uint64_t tls_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void tls_random_bytes(uint8_t* out, uint32_t len) {
    if (!out) return;
    uint64_t state = g_tls_rng_state;
    if (state == 0) {
        state = tls_rdtsc() ^ ((uint64_t)(uintptr_t)out << 32);
        if (state == 0) state = 0x9e3779b97f4a7c15ULL;
    }
    for (uint32_t i = 0; i < len; i++) {
        state ^= state << 7;
        state ^= state >> 9;
        state ^= state << 8;
        out[i] = (uint8_t)state;
    }
    g_tls_rng_state = state ^ tls_rdtsc();
}

// ============== HMAC / HKDF =================

int tls_hmac_sha256(const uint8_t* key, uint32_t key_len,
                    const uint8_t* data, uint32_t data_len,
                    uint8_t out[32]) {
    if (!out) return -1;
    uint8_t key_block[64];
    memset(key_block, 0, sizeof(key_block));
    if (key && key_len) {
        if (key_len > 64) {
            tls_sha256(key, key_len, key_block);
        } else {
            memcpy(key_block, key, key_len);
        }
    }
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }
    uint8_t tmp[32];
    tls_hash_ctx_t ctx;
    tls_hash_init(&ctx);
    tls_hash_update(&ctx, ipad, 64);
    if (data && data_len) tls_hash_update(&ctx, data, data_len);
    tls_hash_final(&ctx, tmp);

    tls_hash_init(&ctx);
    tls_hash_update(&ctx, opad, 64);
    tls_hash_update(&ctx, tmp, 32);
    tls_hash_final(&ctx, out);
    return 0;
}

int tls_hkdf_extract(const uint8_t* salt, uint32_t salt_len,
                     const uint8_t* ikm, uint32_t ikm_len,
                     uint8_t out[32]) {
    if (!out) return -1;
    return tls_hmac_sha256(salt, salt_len, ikm, ikm_len, out);
}

int tls_hkdf_expand(const uint8_t* prk, const uint8_t* info, uint32_t info_len,
                    uint8_t out_len, uint8_t* out) {
    if (!prk || !out || out_len == 0) return -1;
    uint8_t t[32];
    uint8_t t_len = 0;
    uint8_t counter = 1;
    uint32_t pos = 0;
    while (pos < out_len) {
        uint8_t buffer[32 + info_len + 1];
        uint8_t* ptr = buffer;
        if (t_len) {
            memcpy(ptr, t, t_len);
            ptr += t_len;
        }
        if (info && info_len) {
            memcpy(ptr, info, info_len);
            ptr += info_len;
        }
        *ptr = counter;
        tls_hmac_sha256(prk, 32, buffer, (uint32_t)(t_len + info_len + 1), t);
        t_len = 32;
        uint8_t copy = (out_len - pos) < 32 ? (out_len - pos) : 32;
        memcpy(out + pos, t, copy);
        pos += copy;
        counter++;
    }
    return 0;
}

int tls_hkdf_expand_label(const uint8_t* secret, const char* label,
                          const uint8_t* context, uint32_t context_len,
                          uint8_t out_len, uint8_t* out) {
    if (!secret || !label || !out) return -1;
    const char prefix[] = "tls13 ";
    uint8_t label_len = (uint8_t)strlen(label);
    uint8_t full_label_len = (uint8_t)(sizeof(prefix) - 1 + label_len);
    uint8_t info_len = 2 + 1 + full_label_len + 1 + (uint8_t)context_len;
    if (info_len > sizeof(prefix) + 255) return -1;
    uint8_t info[2 + 1 + 255];
    info[0] = (uint8_t)(out_len >> 8);
    info[1] = (uint8_t)(out_len & 0xFF);
    info[2] = full_label_len;
    memcpy(&info[3], prefix, sizeof(prefix) - 1);
    memcpy(&info[3 + sizeof(prefix) - 1], label, label_len);
    info[3 + full_label_len] = (uint8_t)context_len;
    if (context && context_len) {
        memcpy(&info[4 + full_label_len], context, context_len);
    }
    return tls_hkdf_expand(secret, info, info_len, out_len, out);
}

int tls_label_secret(const uint8_t* secret, const char* label,
                     const uint8_t* context, uint32_t context_len,
                     uint8_t out[32]) {
    return tls_hkdf_expand_label(secret, label, context, context_len, 32, out);
}

int tls_label_key_iv(const uint8_t* secret, uint8_t key[16], uint8_t iv[12]) {
    if (tls_hkdf_expand_label(secret, "key", NULL, 0, 16, key) != 0) return -1;
    if (tls_hkdf_expand_label(secret, "iv", NULL, 0, 12, iv) != 0) return -1;
    return 0;
}

int tls_finished_key(const uint8_t* secret, uint8_t out[32]) {
    return tls_hkdf_expand_label(secret, "finished", NULL, 0, 32, out);
}

// ============== AES-128 / GCM =================

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static void aes_key_expand(const uint8_t key[16], uint32_t round_keys[44]) {
    static const uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36};
    for (int i = 0; i < 4; i++) {
        round_keys[i] = ((uint32_t)key[4*i] << 24) |
                        ((uint32_t)key[4*i+1] << 16) |
                        ((uint32_t)key[4*i+2] << 8) |
                        (uint32_t)key[4*i+3];
    }
    for (int i = 4; i < 44; i++) {
        uint32_t temp = round_keys[i-1];
        if (i % 4 == 0) {
            temp = (temp << 8) | (temp >> 24);
            temp = ((uint32_t)aes_sbox[(temp >> 24) & 0xFF] << 24) |
                   ((uint32_t)aes_sbox[(temp >> 16) & 0xFF] << 16) |
                   ((uint32_t)aes_sbox[(temp >> 8) & 0xFF] << 8) |
                   (uint32_t)aes_sbox[temp & 0xFF];
            temp ^= ((uint32_t)rcon[i/4 - 1] << 24);
        }
        round_keys[i] = round_keys[i-4] ^ temp;
    }
}

static void aes_add_round_key(uint8_t state[16], const uint32_t round_keys[44], int round) {
    for (int i = 0; i < 4; i++) {
        uint32_t rk = round_keys[round * 4 + i];
        state[4*i + 0] ^= (uint8_t)(rk >> 24);
        state[4*i + 1] ^= (uint8_t)(rk >> 16);
        state[4*i + 2] ^= (uint8_t)(rk >> 8);
        state[4*i + 3] ^= (uint8_t)rk;
    }
}

static void aes_sub_bytes(uint8_t state[16]) {
    for (int i = 0; i < 16; i++) state[i] = aes_sbox[state[i]];
}

static void aes_shift_rows(uint8_t state[16]) {
    uint8_t temp;
    temp = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = temp;
    temp = state[2]; uint8_t temp2 = state[6]; state[2] = state[10]; state[6] = state[14]; state[10] = temp; state[14] = temp2;
    temp = state[3]; state[3] = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = temp;
}

static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static void aes_mix_columns(uint8_t state[16]) {
    for (int i = 0; i < 4; i++) {
        uint8_t* col = &state[4*i];
        uint8_t a0 = col[0];
        uint8_t a1 = col[1];
        uint8_t a2 = col[2];
        uint8_t a3 = col[3];
        uint8_t r0 = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
        uint8_t r1 = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
        uint8_t r2 = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
        uint8_t r3 = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
        col[0] = r0;
        col[1] = r1;
        col[2] = r2;
        col[3] = r3;
    }
}

static void aes_encrypt_block(const uint32_t round_keys[44], const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[16];
    memcpy(state, in, 16);
    aes_add_round_key(state, round_keys, 0);
    for (int round = 1; round <= 9; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, round_keys, round);
    }
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, round_keys, 10);
    memcpy(out, state, 16);
}

static void xor_block(uint8_t* dst, const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

static void ghash_mul(uint8_t* y, const uint8_t* h) {
    uint8_t z[16] = {0};
    uint8_t v[16];
    memcpy(v, h, 16);
    for (int i = 0; i < 128; i++) {
        int bit = (y[i/8] >> (7 - (i % 8))) & 1;
        if (bit) {
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        }
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | ((v[j-1] & 1) << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(y, z, 16);
}

static void ghash(const uint8_t* h, const uint8_t* aad, uint32_t aad_len,
                  const uint8_t* ciphertext, uint32_t ciphertext_len,
                  uint8_t out[16]) {
    uint8_t y[16] = {0};
    uint8_t block[16];
    uint32_t aad_blocks = (aad_len + 15) / 16;
    for (uint32_t i = 0; i < aad_blocks; i++) {
        memset(block, 0, 16);
        uint32_t copy = (aad_len - i*16) < 16 ? (aad_len - i*16) : 16;
        if (copy) memcpy(block, aad + i*16, copy);
        xor_block(y, y, block);
        ghash_mul(y, h);
    }
    uint32_t ct_blocks = (ciphertext_len + 15) / 16;
    for (uint32_t i = 0; i < ct_blocks; i++) {
        memset(block, 0, 16);
        uint32_t copy = (ciphertext_len - i*16) < 16 ? (ciphertext_len - i*16) : 16;
        if (copy) memcpy(block, ciphertext + i*16, copy);
        xor_block(y, y, block);
        ghash_mul(y, h);
    }
    uint64_t aad_bits = (uint64_t)aad_len * 8;
    uint64_t ct_bits = (uint64_t)ciphertext_len * 8;
    uint8_t len_block[16];
    for (int i = 0; i < 8; i++) {
        len_block[i] = (uint8_t)(aad_bits >> (56 - i*8));
        len_block[8 + i] = (uint8_t)(ct_bits >> (56 - i*8));
    }
    xor_block(y, y, len_block);
    ghash_mul(y, h);
    memcpy(out, y, 16);
}

static void increment_iv(uint8_t block[16]) {
    for (int i = 15; i >= 12; i--) {
        if (++block[i]) break;
    }
}

int tls_aes_gcm_encrypt(const uint8_t* key, const uint8_t* iv,
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* plaintext, uint32_t plaintext_len,
                        uint8_t* ciphertext, uint8_t tag[16]) {
    if (!key || !iv || (!plaintext && plaintext_len) || !ciphertext || !tag) return -1;
    uint32_t round_keys[44];
    aes_key_expand(key, round_keys);

    uint8_t H[16] = {0};
    aes_encrypt_block(round_keys, H, H);

    uint8_t J0[16];
    memset(J0, 0, 16);
    memcpy(J0, iv, 12);
    J0[15] = 1;

    uint8_t ctr[16];
    memcpy(ctr, J0, 16);
    uint32_t offset = 0;
    while (offset < plaintext_len) {
        increment_iv(ctr);
        uint8_t keystream[16];
        aes_encrypt_block(round_keys, ctr, keystream);
        uint32_t chunk = plaintext_len - offset;
        if (chunk > 16) chunk = 16;
        for (uint32_t i = 0; i < chunk; i++) {
            ciphertext[offset + i] = plaintext[offset + i] ^ keystream[i];
        }
        offset += chunk;
    }

    uint8_t S[16];
    ghash(H, aad, aad_len, ciphertext, plaintext_len, S);

    uint8_t auth_block[16];
    aes_encrypt_block(round_keys, J0, auth_block);
    xor_block(tag, auth_block, S);
    return 0;
}

int tls_aes_gcm_decrypt(const uint8_t* key, const uint8_t* iv,
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* ciphertext, uint32_t ciphertext_len,
                        const uint8_t tag[16], uint8_t* plaintext) {
    if (!key || !iv || (!ciphertext && ciphertext_len) || !plaintext || !tag) return -1;
    uint32_t round_keys[44];
    aes_key_expand(key, round_keys);

    uint8_t H[16] = {0};
    aes_encrypt_block(round_keys, H, H);

    uint8_t S[16];
    ghash(H, aad, aad_len, ciphertext, ciphertext_len, S);

    uint8_t J0[16];
    memset(J0, 0, 16);
    memcpy(J0, iv, 12);
    J0[15] = 1;

    uint8_t auth_block[16];
    aes_encrypt_block(round_keys, J0, auth_block);

    uint8_t calc_tag[16];
    xor_block(calc_tag, auth_block, S);
    if (memcmp(calc_tag, tag, 16) != 0) {
        return -1;
    }

    uint8_t ctr[16];
    memcpy(ctr, J0, 16);
    uint32_t offset = 0;
    while (offset < ciphertext_len) {
        increment_iv(ctr);
        uint8_t keystream[16];
        aes_encrypt_block(round_keys, ctr, keystream);
        uint32_t chunk = ciphertext_len - offset;
        if (chunk > 16) chunk = 16;
        for (uint32_t i = 0; i < chunk; i++) {
            plaintext[offset + i] = ciphertext[offset + i] ^ keystream[i];
        }
        offset += chunk;
    }
    return 0;
}
