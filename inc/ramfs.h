#ifndef INC_RAMFS_H
#define INC_RAMFS_H

#include <stddef.h>

int ramfs_register(void);
int ramfs_unregister(void);
int ramfs_mkdir(const char *path);
int ramfs_remove(const char *path);

#endif /* INC_RAMFS_H */
