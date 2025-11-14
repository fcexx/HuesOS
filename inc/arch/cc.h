#pragma once

#include <stdint.h>
#include <stddef.h>

#ifndef BYTE_ORDER
#define BYTE_ORDER                     LITTLE_ENDIAN
#endif

#define LWIP_PLATFORM_ASSERT(x)        do { (void)(x); } while (0)
#define LWIP_PLATFORM_DIAG(x)          do { (void)0; } while (0)

#define PACK_STRUCT_FIELD(x)           x
#define PACK_STRUCT_STRUCT             __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define U16_F                          "u"
#define S16_F                          "d"
#define X16_F                          "x"
#define U32_F                          "u"
#define S32_F                          "d"
#define X32_F                          "x"


