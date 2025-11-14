#pragma once

#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

/* Minimal prototypes to satisfy third-party includes; not used by kernel. */
void* malloc(size_t size);
void  free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t nmemb, size_t size);
int   abs(int x);


