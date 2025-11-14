/* Minimal mbedTLS config for client TLS over lwIP in freestanding kernel.
 * Focus on TLS 1.2 client with X.509, ECDHE-ECDSA/RSA and AES-GCM/CHACHA20-POLY1305.
 * NOTE: Crypto provider must be available (PSA or built-in). This file assumes default build.
 */
#ifndef AXONOS_MBEDTLS_CONFIG_H
#define AXONOS_MBEDTLS_CONFIG_H

/* System support */
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* SSL/TLS */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_HANDSHAKE_WITH_CERT_ENABLED
/* GitHub и другие серверы могут присылать Certificate ~14–15 КБ в одном record.
 * Установим стандартные 16 КБ, иначе возможен затык/ошибка при рукопожатии. */
#define MBEDTLS_SSL_IN_CONTENT_LEN            16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN           16384

/* X.509 */
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

/* RNG */
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C

/* Debug (optional) */
/* #define MBEDTLS_DEBUG_C */

/* Optimize footprint */
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE     0

/* Use our own calloc/free via platform glue */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

#include "third_party/mbedtls/library/mbedtls_check_config.h"

#endif


