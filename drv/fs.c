#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../inc/fs.h"

#define MAX_FS_DRIVERS 8

static struct fs_driver *g_drivers[MAX_FS_DRIVERS];
static int g_drivers_count = 0;

int fs_register_driver(struct fs_driver *drv) {
    if (!drv || !drv->ops) return -1;
    if (g_drivers_count >= MAX_FS_DRIVERS) return -1;
    g_drivers[g_drivers_count++] = drv;
    return 0;
}

int fs_unregister_driver(struct fs_driver *drv) {
    for (int i = 0; i < g_drivers_count; i++) {
        if (g_drivers[i] == drv) {
            for (int j = i; j + 1 < g_drivers_count; j++) g_drivers[j] = g_drivers[j+1];
            g_drivers[--g_drivers_count] = NULL;
            return 0;
        }
    }
    return -1;
}

/* Try drivers in registration order. Drivers should return -1 if they do not handle the path. */
struct fs_file *fs_create_file(const char *path) {
    if (!path) return NULL;
    for (int i = 0; i < g_drivers_count; i++) {
        struct fs_driver *drv = g_drivers[i];
        if (!drv || !drv->ops || !drv->ops->create) continue;
        struct fs_file *file = NULL;
        int r = drv->ops->create(path, &file);
        if (r == 0 && file) {
            /* driver should set file->fs_private to drv->driver_data if needed */
            return file;
        }
        if (r < 0 && r != -1) {
            /* real error, stop */
            return NULL;
        }
        /* r == -1 -> not handled, try next */
    }
    return NULL;
}

struct fs_file *fs_open(const char *path) {
    if (!path) return NULL;
    for (int i = 0; i < g_drivers_count; i++) {
        struct fs_driver *drv = g_drivers[i];
        if (!drv || !drv->ops || !drv->ops->open) continue;
        struct fs_file *file = NULL;
        int r = drv->ops->open(path, &file);
        if (r == 0 && file) return file;
        if (r < 0 && r != -1) return NULL; /* real error */
    }
    return NULL;
}

ssize_t fs_read(struct fs_file *file, void *buf, size_t size, size_t offset) {
    if (!file || !file->path) return -1;
    for (int i = 0; i < g_drivers_count; i++) {
        struct fs_driver *drv = g_drivers[i];
        if (!drv || !drv->ops || drv->driver_data != file->fs_private) continue;
        if (!drv->ops->read) return -1;
        return drv->ops->read(file, buf, size, offset);
    }
    return -1;
}

ssize_t fs_write(struct fs_file *file, const void *buf, size_t size, size_t offset) {
    if (!file || !file->path) return -1;
    for (int i = 0; i < g_drivers_count; i++) {
        struct fs_driver *drv = g_drivers[i];
        if (!drv || !drv->ops || drv->driver_data != file->fs_private) continue;
        if (!drv->ops->write) return -1;
        return drv->ops->write(file, buf, size, offset);
    }
    return -1;
}

void fs_file_free(struct fs_file *file) {
    if (!file) return;
    /* let driver release internal resources if provided */
    for (int i = 0; i < g_drivers_count; i++) {
        struct fs_driver *drv = g_drivers[i];
        if (!drv || !drv->ops) continue;
        if (file->fs_private == drv->driver_data) {
            if (drv->ops->release) drv->ops->release(file);
            break;
        }
    }
}
