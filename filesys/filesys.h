#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block *fs_device;
struct dir;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (char *name, off_t initial_size);
void *filesys_open (char *name);
bool filesys_remove (char *name);

struct inode *parse_path (char *, bool, bool, off_t, bool *, struct dir **);

#endif /* filesys/filesys.h */
