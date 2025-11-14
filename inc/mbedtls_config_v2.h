/* Minimal mbedTLS v2.x config for TLS1.2 client in freestanding kernel. */
#include <stddef.h>
#ifndef AXONOS_MBEDTLS_CONFIG_V2_H
#define AXONOS_MBEDTLS_CONFIG_V2_H

/* Platform */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
/* Redirect std allocators to our kernel heap */
void *axon_calloc(size_t n, size_t sz);
void axon_free(void *p);
#define MBEDTLS_PLATFORM_STD_CALLOC axon_calloc
#define MBEDTLS_PLATFORM_STD_FREE   axon_free
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_SSL_MAX_CONTENT_LEN            16384

/* Core and crypto */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C

#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_MD_C

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_CIPHER_MODE_CFB
#define MBEDTLS_CIPHER_MODE_OFB
#define MBEDTLS_CIPHER_MODE_XTS
#define MBEDTLS_CMAC_C

#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_POLY1305_C

#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_NO_UDBL_DIVISION
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

/* X509 */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* TLS */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
/* client-only; do not compile server code */
/* #define MBEDTLS_SSL_SRV_C */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#undef MBEDTLS_SSL_PROTO_TLS1
#undef MBEDTLS_SSL_PROTO_SSL3

/* Optional */
/* enable lightweight debug to locate handshake failure */
#define MBEDTLS_DEBUG_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#undef MBEDTLS_TIMING_C
#define MBEDTLS_VERSION_C
/* #define MBEDTLS_TIMING_C */

/* v2.x uses include/mbedtls/check_config.h */
#include "mbedtls/check_config.h"
#endif


