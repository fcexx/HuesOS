#ifndef AXONOS_OSH_LINE_H
#define AXONOS_OSH_LINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Строковый редактор для osh: история, редактирование, подсказки (Tab)
// Возвращает длину введённой строки (>=0) или -1 при ошибке/ESC.
int osh_line_read(const char* prompt, const char* cwd, char* out, int out_size);
int osh_line_was_ctrlc(void);

void osh_history_init(void);
void osh_history_add(const char* line);

// Из axosh экспортируется список builtin-команд
int osh_get_builtin_names(const char*** out_names);

#ifdef __cplusplus
}
#endif

#endif


