#include "userprog/syscall.h"
#include <list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "syscall.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"

#define array_buffer 512

static void syscall_handler (struct intr_frame *);
static void *get_file (int fd);

struct lock fs_lock;
void syscall_init (void)
{
  lock_init (&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// Lorena drove
static bool validate_pointer (void *pointer, int ptr_size)
{
  uint8_t *intptr = (uint8_t *) (pointer);
  // validate for however many bytes we're accessing?
  for (int i = 0; i < ptr_size; i++)
    {
      if (intptr + i == NULL || !is_user_vaddr (intptr + i) ||
          !pagedir_get_page (thread_current ()->pagedir, intptr + i))
        {
          return false;
        }
    }
  return true;
}

// Everybody drove here
static void syscall_handler (struct intr_frame *f)
{
  if (!validate_pointer (f->esp, sizeof (int)))
    {
      thread_exit ();
    }

  int *espint = (int *) (f->esp);
  int user_syscall = *(espint);

  if (user_syscall == SYS_HALT)
    {
      halt ();
    }

  espint++;

  int args[3];

  int argc = 1;
  if (user_syscall == SYS_CREATE || user_syscall == SYS_SEEK)
    {
      argc++; // 2 arguments
    }
  else if (user_syscall == SYS_READ || user_syscall == SYS_WRITE)
    {
      argc += 2;
    }

  // putting arguments in array
  for (int i = 0; i < argc; i++, espint++)
    {
      if (!validate_pointer (espint, sizeof (int)))
        {
          // for now
          thread_exit ();
        }
      args[i] = *(espint);
    }

  // Gary driving here
  int ret_val = -1;
  // now use args in syscalls (switch statements)
  switch (user_syscall)
    {
      case SYS_EXIT:
        exit (args[0]);
        NOT_REACHED ();
      case SYS_EXEC:
        ret_val = exec ((char *) args[0]);
        break;
      case SYS_WAIT:
        ret_val = wait (args[0]);
        break;
      case SYS_CREATE:
        ret_val = create ((char *) args[0], args[1]);
        break;
      case SYS_REMOVE:
        ret_val = remove ((char *) args[0]);
        break;
      case SYS_OPEN:
        ret_val = open ((char *) args[0]);
        break;
      case SYS_FILESIZE:
        ret_val = filesize (args[0]);
        break;
      case SYS_READ:
        ret_val = read (args[0], (void *) args[1], args[2]);
        break;
      case SYS_WRITE:
        ret_val = write (args[0], (void *) args[1], args[2]);
        break;
      case SYS_SEEK:
        seek (args[0], args[1]);
        break;
      case SYS_TELL:
        ret_val = tell (args[0]);
        break;
      case SYS_CLOSE:
        close (args[0]);
        break;
      case SYS_CHDIR:
        ret_val = chdir ((char *) args[0]);
        break;
      case SYS_MKDIR:
        ret_val = mkdir ((char *) args[0]);
        break;
      case SYS_READDIR:
        ret_val = readdir (args[0], (char *) args[1]);
        break;
      case SYS_ISDIR:
        ret_val = isdir (args[0]);
        break;
      case SYS_INUMBER:
        ret_val = inumber (args[0]);
        break;
      default:
        break;
    }

  if (user_syscall != SYS_SEEK && user_syscall != SYS_CLOSE)
    {
      f->eax = ret_val;
    }
  // Gary portion over
}
// Berke drove here
void halt (void) { shutdown_power_off (); }
// Berke finished here lmao

// exits
void exit (int status)
{
  struct thread *t = thread_current ();
  t->proc_status = status;
  // Gary drove here

  // Rethink clean up functionality apparently ):<

  // need close syscall ig
  for (int i = 0; i < MAX_FILES; i++)
    {
      close (i + 2);
    }

  // Gary portion over

  // trying to wait on all the children before moving on with life
  // Gary portion over (iirc)

  // general synch sema upped here to account for process terminations
  // due to exceptions, hopefully preventing deadlock (hold and wait)
  thread_exit ();
}

tid_t exec (const char *file)
{
  if (!validate_string ((void *) file))
    {
      thread_exit ();
    }

  tid_t childID = process_execute (file);
  sema_down (&thread_current ()->child_running_sema);
  if (!thread_current ()->child_running)
    {
      childID = -1;
    }
  return childID;
}

// syscall wait
int wait (tid_t pid)
{
  // return status pid passed using exit
  return process_wait (pid);
}

// Berke drove
bool create (const char *file, unsigned initial_size)
{
  if (!validate_string ((void *) file))
    {
      thread_exit ();
    }
  return filesys_create (file, initial_size);
}
// Berke finished

// Krishna is driving here
bool remove (const char *file)
{
  if (!validate_string ((void *) file))
    {
      thread_exit ();
    }
  return filesys_remove (file);
}

// Krishna driving here
int open (const char *file)
{
  if (!validate_string ((void *) file))
    {
      thread_exit ();
    }

  struct thread *t = thread_current ();
  int new_fd = -1;

  for (int i = 0; i < MAX_FILES && new_fd < 0; i++)
    {
      if (!t->file_list[t->fd_ptr].ref.file)
        {
          t->file_list[t->fd_ptr].ref.file = filesys_open (file);
          if (!t->file_list[t->fd_ptr].ref.file)
            {
              return -1;
            }
          t->file_list[t->fd_ptr].fd = t->fd_ptr + 2;
          new_fd = t->fd_ptr + 2;
        }
      t->fd_ptr = (t->fd_ptr + 1) % MAX_FILES;
    }

  return new_fd;
}

// Krishna drove here
int filesize (int fd)
{
  void *file_ptr = get_file (fd);
  struct file *file = (struct file *) file_ptr;
  return (file) ? file_length (file) : -1;
}

// Krishna drove here
int read (int fd, void *buffer, unsigned size)
{
  if (!validate_pointer (buffer, size))
    {
      thread_exit ();
    }
  // case where you read from the keyboard..?
  if (fd == 0)
    {
      uint8_t key = input_getc ();
      // need to ret i after writing contents of key to the buffer
      return key;
    }

  // Lorena Drove
  void *file_ptr = get_file (fd);
  struct file *file = (struct file *) file_ptr;
  return (file) ? file_read (file, buffer, size) : -1;
}

// Lorena Drove here (I took the wheel from Berke)
int write (int fd, const void *buffer, unsigned length)
{
  if (!validate_pointer ((void *) buffer, length))
    {
      thread_exit ();
    }
  // case where you write to the output
  if (fd == 1)
    {
      putbuf (buffer, length);
      return length;
    }

  // Lorena Drove
  void *file_ptr = get_file (fd);
  struct file *file = (struct file *) file_ptr;
  return (file) ? file_write (file, buffer, length) : -1;
}

// Lorena drove again
void seek (int fd, unsigned position)
{
  void *file_ptr = get_file (fd);

  if (file_ptr)
    {
      struct file *file = (struct file *) file_ptr;
      file_seek (file, position);
    }
}

// krishna drove here
unsigned tell (int fd)
{
  void *file_ptr = get_file (fd);

  if (!file_ptr)
    {
      return -1;
    }

  struct file *file = (struct file *) file_ptr;

  return file_tell (file);
}

// Berke drove
void close (int fd)
{
  // idk truthfully
  void *file_ptr = get_file (fd);

  if (file_ptr)
    {
      struct file *file = (struct file *) file_ptr;
      thread_current ()->file_list[fd - 2].ref.file = NULL;
      file_close (file);
    }
}

bool chdir (const char *dir)
{
  if (!validate_string ((void *) dir))
    {
      thread_exit ();
    }

  struct inode *inode = parse_path (dir, false, true, 0, NULL, NULL);

  if (inode == NULL)
    {
      return false;
    }

  struct thread *t = thread_current ();

  if (t->cwd)
    {
      dir_close (t->cwd);
    }

  t->cwd = dir_open (inode);

  return false;
}
bool mkdir (const char *dir)
{

  if (!validate_string ((void *) dir))
    {
      thread_exit ();
    }

  bool created = false;
  struct dir *parent = NULL;
  struct inode *inode = parse_path (dir, true, false, 0, &created, &parent);

  if (inode == NULL || !created || !inode_isdir (inode))
    {
      return false;
    }

  struct dir *new_dir = dir_open (inode);
  bool success =
      dir_add (new_dir, ".", inode_get_inumber (inode)) &&
      dir_add (new_dir, "..", inode_get_inumber (dir_get_inode (parent)));

  return success;
}
bool readdir (int fd, char name[READDIR_MAX_LEN + 1])
{

  if (!validate_string ((void *) name))
    {
      thread_exit ();
    }
  void *file_ptr = get_file (fd);
  struct dir *dir = dir_open (((struct file *) file_ptr)->inode);

  // TODO : fix this return later
  return dir_readdir (dir, name);
}
bool isdir (int fd)
{
  void *file_ptr = get_file (fd);

  return inode_isdir (((struct file *) file_ptr)->inode);
}
int inumber (int fd)
{
  void *file_ptr = get_file (fd);
  return inode_get_inumber (((struct file *) file_ptr)->inode);
}

static void *get_file (int fd)
{
  if (fd < 2 || fd >= MAX_FILES)
    {
      return NULL;
    }
  return thread_current ()->file_list[fd - 2].ref.file;
}
