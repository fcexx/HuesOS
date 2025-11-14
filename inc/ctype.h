#pragma once

static inline int isdigit(int c) { return (c >= '0' && c <= '9'); }
static inline int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); }
static inline int islower(int c) { return (c >= 'a' && c <= 'z'); }
static inline int isupper(int c) { return (c >= 'A' && c <= 'Z'); }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }


