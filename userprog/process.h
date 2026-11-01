#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/off_t.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool load_page (struct file *file, off_t ofs, uint8_t *upage, uint8_t *kpage,
                uint32_t read_bytes, uint32_t zero_bytes, bool writable);

#endif /* userprog/process.h */
