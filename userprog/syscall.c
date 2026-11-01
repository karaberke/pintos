#include "userprog/syscall.h"
#include <list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "syscall.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"

#define array_buffer 512

static void syscall_handler (struct intr_frame *);
struct file *get_file (int fd);

void syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool validate_stack_pointer (void *esp)
{
  uint8_t *intptr = (uint8_t *) (esp);
  // validate for however many bytes we're accessing?
  for (unsigned i = 0; i < sizeof (int); i++)
    {
      struct page *curr = get_page (intptr + i, false);
      if (intptr + i == NULL || !is_user_vaddr (intptr + i) || !curr)
        {
          return false;
        }
    }
  return true;
}

// Lorena drove
static bool validate_pointer (void *pointer, int ptr_size, bool pin,
                              const void *esp)
{

  uint8_t *intptr = (uint8_t *) (pointer);
  // validate for however many bytes we're accessing?
  for (int i = 0; i < ptr_size; i++)
    {
      bool stack_addr =
          is_user_vaddr (intptr + i) &&
          (void *) (intptr + i) >= (void *) ((uint8_t *) esp - 32);
      bool stack_lim_exceeded =
          (((uintptr_t) PHYS_BASE) - ((uintptr_t) esp) - PGSIZE) > STACK_LIMIT;
      struct page *curr =
          get_page (intptr + i, stack_addr && !stack_lim_exceeded);
      if (!curr)
        {
          return false;
        }
      lock_acquire (&curr->lock);
      curr->is_stack = stack_addr;

      if (pin)
        {
          if (curr->status != PAGE_RES)
            {
              if (!page_lodge (curr))
                {
                  lock_release (&curr->lock);
                  return false;
                }
            }
          pin_page (curr->v_addr);
        }
      lock_release (&curr->lock);
    }
  return true;
}

// validate for however many bytes we're accessing?
static bool validate_string (void *pointer, bool pin, const void *esp)
{
  char *charptr = (char *) (pointer);
  while (true)
    {
      bool stack_addr = is_user_vaddr (charptr) &&
                        (void *) (charptr) >= (void *) ((uint8_t *) esp - 32);
      bool stack_lim_exceeded =
          (((uintptr_t) PHYS_BASE) - ((uintptr_t) esp) - PGSIZE) > STACK_LIMIT;
      struct page *curr = get_page (charptr, stack_addr && !stack_lim_exceeded);

      if (!curr)
        {
          return false;
        }
      lock_acquire (&curr->lock);
      curr->is_stack = stack_addr;

      if (pin)
        {
          if (curr->status != PAGE_RES)
            {
              if (!page_lodge (curr))
                {
                  return false;
                }
            }
          pin_page (curr->v_addr);
        }
      lock_release (&curr->lock);
      if (*charptr == '\0')
        {
          return true;
        }
      charptr++;
    }
  return true;
}

static bool unpin_pointer (void *pointer, int ptr_size)
{
  uint8_t *intptr = (uint8_t *) (pointer);
  // validate for however many bytes we're accessing?
  for (int i = 0; i < ptr_size; i++)
    {
      struct page *curr = get_page (intptr + i, false);
      lock_acquire (&curr->lock);
      unpin_page (curr->v_addr);
      lock_release (&curr->lock);
    }
  return true;
}

// validate for however many bytes we're accessing?
static bool unpin_string (void *pointer)
{
  char *charptr = (char *) (pointer);
  while (true)
    {
      struct page *curr = get_page (charptr, false);
      lock_acquire (&curr->lock);
      unpin_page (curr->v_addr);
      lock_release (&curr->lock);
      if (*charptr == '\0')
        {
          return true;
        }
      charptr++;
    }
  return true;
}

// Everybody drove here
static void syscall_handler (struct intr_frame *f)
{
  if (!validate_stack_pointer (f->esp))
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
      if (!validate_pointer (espint, sizeof (int), false, f->esp))
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
        if (!validate_string ((void *) args[0], true, f->esp))
          {
            thread_exit ();
          }
        ret_val = exec ((char *) args[0]);
        break;
      case SYS_WAIT:
        ret_val = wait (args[0]);
        break;
      case SYS_CREATE:
        if (!validate_string ((void *) args[0], true, f->esp))
          {
            thread_exit ();
          }
        ret_val = create ((char *) args[0], args[1]);
        break;
      case SYS_REMOVE:
        if (!validate_string ((void *) args[0], true, f->esp))
          {
            thread_exit ();
          }
        ret_val = remove ((char *) args[0]);
        break;
      case SYS_OPEN:
        if (!validate_string ((void *) args[0], true, f->esp))
          {
            thread_exit ();
          }
        ret_val = open ((char *) args[0]);
        break;
      case SYS_FILESIZE:
        ret_val = filesize (args[0]);
        break;
      case SYS_READ:
        if (!validate_pointer ((void *) args[1], args[2], true, f->esp))
          {
            thread_exit ();
          }
        ret_val = read (args[0], (void *) args[1], args[2]);
        break;
      case SYS_WRITE:
        if (!validate_pointer ((void *) args[1], args[2], true, f->esp))
          {
            thread_exit ();
          }
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
  struct thread *current = thread_current ();
  current->proc_status = status;
  // Gary drove here

  // Rethink clean up functionality apparently ):<

  // need close syscall ig
  struct list_elem *fileel;

  while (!list_empty (&current->file_list))
    {
      fileel = list_pop_front (&current->file_list);
      struct file *fl = list_entry (fileel, struct file, elem);
      close (fl->fd);
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

  tid_t childID = process_execute (file);
  sema_down (&thread_current ()->child_running_sema);
  if (!thread_current ()->child_running)
    {
      childID = -1;
    }
  unpin_string ((void *) file);
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
  lock_acquire (&fs_lock);
  bool created = filesys_create (file, initial_size);
  lock_release (&fs_lock);
  unpin_string ((void *) file);
  return created;
}
// Berke finished

// Krishna is driving here
bool remove (const char *file)
{
  lock_acquire (&fs_lock);
  bool removed = filesys_remove (file);
  lock_release (&fs_lock);
  unpin_string ((void *) file);
  return removed;
}

// Krishna driving here
int open (const char *file)
{

  struct thread *t = thread_current ();
  lock_acquire (&fs_lock);
  struct file *curr_file = filesys_open (file);

  if (!curr_file)
    {
      lock_release (&fs_lock);
      return -1;
    }

  list_push_back (&t->file_list, &curr_file->elem);
  curr_file->fd = ++t->fd_counter;

  unpin_string ((void *) file);
  lock_release (&fs_lock);
  return curr_file->fd;
}

// Krishna drove here
int filesize (int fd)
{
  lock_acquire (&fs_lock);
  struct file *file = get_file (fd);

  if (!file)
    {
      // failure case
      lock_release (&fs_lock);
      return -1;
    }

  int file_len = file_length (file);
  lock_release (&fs_lock);
  return file_len;
}

// Krishna drove here
int read (int fd, void *buffer, unsigned size)
{
  // case where you read from the keyboard..?
  if (fd == 0)
    {
      uint8_t key = input_getc ();
      // need to ret i after writing contents of key to the buffer
      unpin_pointer (buffer, size);
      return key;
    }

  // Lorena Drove
  lock_acquire (&fs_lock);
  struct file *file = get_file (fd);
  if (!file)
    {
      lock_release (&fs_lock);
      unpin_pointer (buffer, size);
      return -1;
    }
  int bytesread = file_read (file, buffer, size);
  lock_release (&fs_lock);
  unpin_pointer (buffer, size);
  return bytesread;
}

// Lorena Drove here (I took the wheel from Berke)
int write (int fd, const void *buffer, unsigned length)
{
  // case where you write to the output
  if (fd == 1)
    {
      putbuf (buffer, length);
      unpin_pointer ((void *) buffer, length);
      return length;
    }

  // Lorena Drove
  lock_acquire (&fs_lock);
  struct file *file = get_file (fd);
  if (!file)
    {
      lock_release (&fs_lock);
      unpin_pointer ((void *) buffer, length);
      return -1;
    }
  int byteswritten = file_write (file, buffer, length);
  lock_release (&fs_lock);
  unpin_pointer ((void *) buffer, length);
  return byteswritten;
}

// Lorena drove again
void seek (int fd, unsigned position)
{
  lock_acquire (&fs_lock);
  struct file *file = get_file (fd);

  if (!file)
    {
      lock_release (&fs_lock);
      return;
    }

  file_seek (file, position);
  lock_release (&fs_lock);
}

// krishna drove here
unsigned tell (int fd)
{
  lock_acquire (&fs_lock);
  struct file *file = get_file (fd);

  if (!file)
    {
      lock_release (&fs_lock);
      return -1;
    }

  int told = file_tell (file);
  lock_release (&fs_lock);
  return told;
}

// Berke drove
void close (int fd)
{
  // idk truthfully
  lock_acquire (&fs_lock);
  struct file *file = get_file (fd);

  if (!file)
    {
      lock_release (&fs_lock);
      return;
    }

  list_remove (&file->elem);
  file_close (file);
  lock_release (&fs_lock);
}

// Helper functions

// Looks for a given open file within a particular thread/process
struct file *get_file (int fd)
{
  struct thread *t = thread_current ();
  struct list_elem *cur_file_fd;
  for (cur_file_fd = list_begin (&t->file_list);
       cur_file_fd != list_end (&t->file_list);
       cur_file_fd = list_next (cur_file_fd))
    {
      struct file *curr_f = list_entry (cur_file_fd, struct file, elem);
      if (curr_f->fd == fd)
        {
          // great, found the file
          return curr_f;
        }
    }

  // failure case
  return NULL;
}
