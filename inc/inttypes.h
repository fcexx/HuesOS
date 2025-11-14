#pragma once

#include <stdint.h>

typedef long long intmax_t;
typedef unsigned long long uintmax_t;

typedef struct {
	intmax_t quot;
	intmax_t rem;
} imaxdiv_t;

/* Minimal format macros often used by embedded libs; lwIP uses its own U32_F macros. */
#ifndef PRId32
#define PRId32 "d"
#endif
#ifndef PRIu32
#define PRIu32 "u"
#endif
#ifndef PRIx32
#define PRIx32 "x"
#endif


