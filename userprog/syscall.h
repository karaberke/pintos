#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

void halt (void) NO_RETURN;
// exits
void exit (int status) NO_RETURN;
tid_t exec (const char *file);
int wait (tid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char name[READDIR_MAX_LEN + 1]);
bool isdir (int fd);
int inumber (int fd);

#endif /* userprog/syscall.h */
