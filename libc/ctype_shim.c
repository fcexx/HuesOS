#include <stdint.h>

/* Provide glibc-like __ctype_b_loc to satisfy references from third-party code
 * when building freestanding without glibc. This is a minimal, non-functional
 * stub sufficient for simple isdigit/isspace usage in lwIP helpers.
 */
const unsigned short int** __ctype_b_loc(void) {
	static const unsigned short int table[384] = {0};
	static const unsigned short int* p = table + 128;
	return (const unsigned short int**)&p;
}


