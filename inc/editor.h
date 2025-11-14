#ifndef AXONOS_EDITOR_H
#define AXONOS_EDITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Запускает полноэкранный текстовый редактор.
 * Если path != NULL, попытается открыть файл, иначе создаст пустой буфер.
 */
void editor_run(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* AXONOS_EDITOR_H */


