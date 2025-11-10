#include <stdint.h>
#include <string.h>
#include <net/tls.h>

typedef int32_t fe[10];

static int64_t load_3(const uint8_t *in) {
    return (int64_t)in[0] | ((int64_t)in[1] << 8) | ((int64_t)in[2] << 16);
}

static int64_t load_4(const uint8_t *in) {
    return (int64_t)in[0] | ((int64_t)in[1] << 8) | ((int64_t)in[2] << 16) | ((int64_t)in[3] << 24);
}

static void fe_0(fe h) { memset(h, 0, sizeof(fe)); }
static void fe_1(fe h) { fe_0(h); h[0] = 1; }

static void fe_add(fe h, const fe f, const fe g) {
    for (int i = 0; i < 10; i++) h[i] = f[i] + g[i];
}

static void fe_sub(fe h, const fe f, const fe g) {
    for (int i = 0; i < 10; i++) h[i] = f[i] - g[i];
}

static void fe_copy(fe h, const fe f) {
    for (int i = 0; i < 10; i++) h[i] = f[i];
}

static void fe_apcarry(fe h) {
    int32_t carry;
    carry = (h[0] + (1 << 25)) >> 26; h[1] += carry; h[0] -= carry << 26;
    carry = (h[1] + (1 << 24)) >> 25; h[2] += carry; h[1] -= carry << 25;
    carry = (h[2] + (1 << 25)) >> 26; h[3] += carry; h[2] -= carry << 26;
    carry = (h[3] + (1 << 24)) >> 25; h[4] += carry; h[3] -= carry << 25;
    carry = (h[4] + (1 << 25)) >> 26; h[5] += carry; h[4] -= carry << 26;
    carry = (h[5] + (1 << 24)) >> 25; h[6] += carry; h[5] -= carry << 25;
    carry = (h[6] + (1 << 25)) >> 26; h[7] += carry; h[6] -= carry << 26;
    carry = (h[7] + (1 << 24)) >> 25; h[8] += carry; h[7] -= carry << 25;
    carry = (h[8] + (1 << 25)) >> 26; h[9] += carry; h[8] -= carry << 26;
    carry = (h[9] + (1 << 24)) >> 25; h[9] -= carry << 25; h[0] += carry * 19;
    carry = (h[0] + (1 << 25)) >> 26; h[1] += carry; h[0] -= carry << 26;
}

static void fe_mul(fe h, const fe f, const fe g) {
    int64_t f0 = f[0]; int64_t f1 = f[1]; int64_t f2 = f[2]; int64_t f3 = f[3]; int64_t f4 = f[4];
    int64_t f5 = f[5]; int64_t f6 = f[6]; int64_t f7 = f[7]; int64_t f8 = f[8]; int64_t f9 = f[9];
    int64_t g0 = g[0]; int64_t g1 = g[1]; int64_t g2 = g[2]; int64_t g3 = g[3]; int64_t g4 = g[4];
    int64_t g5 = g[5]; int64_t g6 = g[6]; int64_t g7 = g[7]; int64_t g8 = g[8]; int64_t g9 = g[9];
    int64_t g1_19 = 19 * g1;
    int64_t g2_19 = 19 * g2;
    int64_t g3_19 = 19 * g3;
    int64_t g4_19 = 19 * g4;
    int64_t g5_19 = 19 * g5;
    int64_t g6_19 = 19 * g6;
    int64_t g7_19 = 19 * g7;
    int64_t g8_19 = 19 * g8;
    int64_t g9_19 = 19 * g9;
    int64_t f1_2 = 2 * f1;
    int64_t f3_2 = 2 * f3;
    int64_t f5_2 = 2 * f5;
    int64_t f7_2 = 2 * f7;
    int64_t f9_2 = 2 * f9;

    int64_t h0 = f0*g0 + f1_2*g9_19 + f2*g8_19 + f3_2*g7_19 + f4*g6_19 + f5_2*g5_19 + f6*g4_19 + f7_2*g3_19 + f8*g2_19 + f9_2*g1_19;
    int64_t h1 = f0*g1 + f1*g0 + f2*g9_19 + f3*g8_19 + f4*g7_19 + f5*g6_19 + f6*g5_19 + f7*g4_19 + f8*g3_19 + f9*g2_19;
    int64_t h2 = f0*g2 + f1_2*g1 + f2*g0 + f3_2*g9_19 + f4*g8_19 + f5_2*g7_19 + f6*g6_19 + f7_2*g5_19 + f8*g4_19 + f9_2*g3_19;
    int64_t h3 = f0*g3 + f1*g2 + f2*g1 + f3*g0 + f4*g9_19 + f5*g8_19 + f6*g7_19 + f7*g6_19 + f8*g5_19 + f9*g4_19;
    int64_t h4 = f0*g4 + f1_2*g3 + f2*g2 + f3_2*g1 + f4*g0 + f5_2*g9_19 + f6*g8_19 + f7_2*g7_19 + f8*g6_19 + f9_2*g5_19;
    int64_t h5 = f0*g5 + f1*g4 + f2*g3 + f3*g2 + f4*g1 + f5*g0 + f6*g9_19 + f7*g8_19 + f8*g7_19 + f9*g6_19;
    int64_t h6 = f0*g6 + f1_2*g5 + f2*g4 + f3_2*g3 + f4*g2 + f5_2*g1 + f6*g0 + f7_2*g9_19 + f8*g8_19 + f9_2*g7_19;
    int64_t h7 = f0*g7 + f1*g6 + f2*g5 + f3*g4 + f4*g3 + f5*g2 + f6*g1 + f7*g0 + f8*g9_19 + f9*g8_19;
    int64_t h8 = f0*g8 + f1_2*g7 + f2*g6 + f3_2*g5 + f4*g4 + f5_2*g3 + f6*g2 + f7_2*g1 + f8*g0 + f9_2*g9_19;
    int64_t h9 = f0*g9 + f1*g8 + f2*g7 + f3*g6 + f4*g5 + f5*g4 + f6*g3 + f7*g2 + f8*g1 + f9*g0;

    fe result;
    result[0] = (int32_t)h0; result[1] = (int32_t)h1; result[2] = (int32_t)h2; result[3] = (int32_t)h3; result[4] = (int32_t)h4;
    result[5] = (int32_t)h5; result[6] = (int32_t)h6; result[7] = (int32_t)h7; result[8] = (int32_t)h8; result[9] = (int32_t)h9;
    fe_apcarry(result);
    fe_copy(h, result);
}

static void fe_sq(fe h, const fe f) {
    fe_mul(h, f, f);
}

static void fe_sq2(fe h, const fe f) {
    fe temp;
    fe_mul(temp, f, f);
    for (int i = 0; i < 10; i++) h[i] = 2 * temp[i];
    fe_apcarry(h);
}

static void fe_mul121666(fe h, const fe f) {
    for (int i = 0; i < 10; i++) h[i] = f[i] * 121666;
    fe_apcarry(h);
}

static void fe_cswap(fe f, fe g, int b) {
    int32_t mask = -b;
    for (int i = 0; i < 10; i++) {
        int32_t x = mask & (f[i] ^ g[i]);
        f[i] ^= x;
        g[i] ^= x;
    }
}

static void fe_frombytes(fe h, const uint8_t *s) {
    int64_t h0 = load_4(s);
    int64_t h1 = load_3(s + 4) << 6;
    int64_t h2 = load_3(s + 7) << 5;
    int64_t h3 = load_3(s + 10) << 3;
    int64_t h4 = load_3(s + 13) << 2;
    int64_t h5 = load_4(s + 16);
    int64_t h6 = load_3(s + 20) << 7;
    int64_t h7 = load_3(s + 23) << 5;
    int64_t h8 = load_3(s + 26) << 4;
    int64_t h9 = (load_3(s + 29) & 0x7fffff) << 2;

    int64_t carry0 = (h0 + (1LL << 25)) >> 26; h1 += carry0; h0 -= carry0 << 26;
    int64_t carry1 = (h1 + (1LL << 24)) >> 25; h2 += carry1; h1 -= carry1 << 25;
    int64_t carry2 = (h2 + (1LL << 25)) >> 26; h3 += carry2; h2 -= carry2 << 26;
    int64_t carry3 = (h3 + (1LL << 24)) >> 25; h4 += carry3; h3 -= carry3 << 25;
    int64_t carry4 = (h4 + (1LL << 25)) >> 26; h5 += carry4; h4 -= carry4 << 26;
    int64_t carry5 = (h5 + (1LL << 24)) >> 25; h6 += carry5; h5 -= carry5 << 25;
    int64_t carry6 = (h6 + (1LL << 25)) >> 26; h7 += carry6; h6 -= carry6 << 26;
    int64_t carry7 = (h7 + (1LL << 24)) >> 25; h8 += carry7; h7 -= carry7 << 25;
    int64_t carry8 = (h8 + (1LL << 25)) >> 26; h9 += carry8; h8 -= carry8 << 26;
    int64_t carry9 = (h9 + (1LL << 24)) >> 25; h9 -= carry9 << 25; h0 += carry9 * 19;
    carry0 = (h0 + (1LL << 25)) >> 26; h1 += carry0; h0 -= carry0 << 26;

    h[0] = (int32_t)h0; h[1] = (int32_t)h1; h[2] = (int32_t)h2; h[3] = (int32_t)h3; h[4] = (int32_t)h4;
    h[5] = (int32_t)h5; h[6] = (int32_t)h6; h[7] = (int32_t)h7; h[8] = (int32_t)h8; h[9] = (int32_t)h9;
}

static void fe_tobytes(uint8_t *s, const fe h) {
    fe t;
    fe_copy(t, h);
    fe_apcarry(t);
    fe_apcarry(t);
    int32_t q = (19 * t[9] + (1 << 24)) >> 25;
    q = (t[0] + q) >> 26;
    q = (t[1] + q) >> 25;
    q = (t[2] + q) >> 26;
    q = (t[3] + q) >> 25;
    q = (t[4] + q) >> 26;
    q = (t[5] + q) >> 25;
    q = (t[6] + q) >> 26;
    q = (t[7] + q) >> 25;
    q = (t[8] + q) >> 26;
    q = (t[9] + q) >> 25;

    t[0] += 19 * q;
    fe_apcarry(t);
    fe_apcarry(t);

    int32_t h0 = t[0];
    int32_t h1 = t[1];
    int32_t h2 = t[2];
    int32_t h3 = t[3];
    int32_t h4 = t[4];
    int32_t h5 = t[5];
    int32_t h6 = t[6];
    int32_t h7 = t[7];
    int32_t h8 = t[8];
    int32_t h9 = t[9];

    s[0] = (uint8_t)(h0 >> 0);
    s[1] = (uint8_t)(h0 >> 8);
    s[2] = (uint8_t)(h0 >> 16);
    s[3] = (uint8_t)((h0 >> 24) | (h1 << 2));
    s[4] = (uint8_t)(h1 >> 6);
    s[5] = (uint8_t)(h1 >> 14);
    s[6] = (uint8_t)((h1 >> 22) | (h2 << 3));
    s[7] = (uint8_t)(h2 >> 5);
    s[8] = (uint8_t)(h2 >> 13);
    s[9] = (uint8_t)((h2 >> 21) | (h3 << 5));
    s[10] = (uint8_t)(h3 >> 3);
    s[11] = (uint8_t)(h3 >> 11);
    s[12] = (uint8_t)((h3 >> 19) | (h4 << 6));
    s[13] = (uint8_t)(h4 >> 2);
    s[14] = (uint8_t)(h4 >> 10);
    s[15] = (uint8_t)(h4 >> 18);
    s[16] = (uint8_t)(h5 >> 0);
    s[17] = (uint8_t)(h5 >> 8);
    s[18] = (uint8_t)(h5 >> 16);
    s[19] = (uint8_t)((h5 >> 24) | (h6 << 1));
    s[20] = (uint8_t)(h6 >> 7);
    s[21] = (uint8_t)(h6 >> 15);
    s[22] = (uint8_t)((h6 >> 23) | (h7 << 3));
    s[23] = (uint8_t)(h7 >> 5);
    s[24] = (uint8_t)(h7 >> 13);
    s[25] = (uint8_t)((h7 >> 21) | (h8 << 4));
    s[26] = (uint8_t)(h8 >> 4);
    s[27] = (uint8_t)(h8 >> 12);
    s[28] = (uint8_t)((h8 >> 20) | (h9 << 6));
    s[29] = (uint8_t)(h9 >> 2);
    s[30] = (uint8_t)(h9 >> 10);
    s[31] = (uint8_t)(h9 >> 18);
}

static void fe_pow22523(fe out, const fe z) {
    fe t0, t1, t2;
    fe_sq(t0, z);
    fe_sq(t1, t0);
    fe_sq(t1, t1);
    fe_mul(t1, z, t1);
    fe_mul(t0, t0, t1);
    fe_sq(t2, t0);
    fe_mul(t1, t1, t2);
    fe_sq(t2, t1);
    for (int i = 0; i < 4; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t2, t1);
    for (int i = 0; i < 9; i++) fe_sq(t2, t2);
    fe_mul(t2, t2, t1);
    fe_sq(t2, t2);
    for (int i = 0; i < 19; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t2, t1);
    for (int i = 0; i < 9; i++) fe_sq(t2, t2);
    fe_mul(t0, t2, t0);
    fe_sq(t2, t0);
    fe_sq(t2, t2);
    fe_mul(out, t2, t1);
}

static void fe_invert(fe out, const fe z) {
    fe_pow22523(out, z);
}

static void x25519_scalarmult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t e[32];
    memcpy(e, scalar, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;

    fe x1, x2, z2, x3, z3, tmp0, tmp1;
    fe_frombytes(x1, point);
    fe_1(x2);
    fe_0(z2);
    fe_copy(x3, x1);
    fe_1(z3);

    int swap = 0;
    for (int pos = 254; pos >= 0; pos--) {
        int b = (e[pos / 8] >> (pos & 7)) & 1;
        swap ^= b;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = b;

        fe_sub(tmp0, x3, z3);
        fe_add(tmp1, x2, z2);
        fe_add(x2, x2, z2);
        fe_sub(z2, x3, z3);
        fe_mul(z2, z2, tmp1);
        fe_mul(x2, x2, tmp0);
        fe_sq(tmp0, tmp0);
        fe_sq(tmp1, tmp1);
        fe_add(x3, z2, x2);
        fe_sub(z3, z2, x2);
        fe_sq(x2, tmp1);
        fe_sq2(z2, z3);
        fe_mul121666(tmp1, z2);
        fe_sq(z3, z3);
        fe_add(z2, tmp1, tmp0);
        fe_mul(z2, z2, z3);
        fe_mul(z3, tmp0, tmp1);
        fe_mul(x3, x3, x3);
    }
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
}

int tls_x25519_keypair(uint8_t priv[32], uint8_t pub[32]) {
    if (!priv || !pub) return -1;
    tls_random_bytes(priv, 32);
    priv[0] &= 248;
    priv[31] &= 127;
    priv[31] |= 64;
    static const uint8_t basepoint[32] = {9};
    x25519_scalarmult(pub, priv, basepoint);
    return 0;
}

int tls_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]) {
    if (!priv || !peer_pub || !out) return -1;
    x25519_scalarmult(out, priv, peer_pub);
    return 0;
}
