#ifndef PS2_H
#define PS2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// initialize PS/2 keyboard
void ps2_keyboard_init();

// get symbol (blocking function, like in Unix)
char kgetc();

// check if there are available symbols (non-blocking)
int kgetc_available();

// get string with support for arrows and editing
char* kgets(char* buffer, int max_length);

#ifdef __cplusplus
}
#endif

#endif // PS2_H