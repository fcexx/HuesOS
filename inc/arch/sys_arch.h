#pragma once

#include <stdint.h>

typedef uint32_t sys_prot_t;

static inline sys_prot_t sys_arch_protect(void) { return 0; }
static inline void sys_arch_unprotect(sys_prot_t p) { (void)p; }


