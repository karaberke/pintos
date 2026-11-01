#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

// struct for arguments
struct proc_args
{
  int argc;
  char **argv;
};

// helper function
// Lorena driving

bool validate_string (void *pointer)
{
  char *charptr = (char *) (pointer);
  // validate for however many bytes we're accessing?

  while (true)
    {
      if (charptr == NULL || !is_user_vaddr (charptr) ||
          !pagedir_get_page (thread_current ()->pagedir, charptr))
        {
          return false;
        }
      if (*charptr == '\0')
        {
          return true;
        }
      charptr++;
    }
  return true;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute (const char *file_name)
{
  char *file_copy;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  file_copy = palloc_get_page (0);
  if (file_copy == NULL)
    return TID_ERROR;
  strlcpy (file_copy, file_name, PGSIZE);

  char *tmp_cpy;

  tmp_cpy = palloc_get_page (0);
  if (tmp_cpy == NULL)
    return TID_ERROR;
  strlcpy (tmp_cpy, file_name, PGSIZE);
  // get counts of the str
  tid_t tid;

  // Gary driving here
  char *save_ptr;
  char *new_file_name = strtok_r (tmp_cpy, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (new_file_name, PRI_DEFAULT, start_process, file_copy);
  if (tid == TID_ERROR)
    {
      palloc_free_page (file_copy);
      palloc_free_page (tmp_cpy);
    }
  // Gary went into load after this

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
    {
      sema_up (&thread_current ()->parent->child_running_sema);
      thread_exit ();
    }

  thread_current ()->parent->child_running = true;
  sema_up (&thread_current ()->parent->child_running_sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait (tid_t child_tid)
{
  // driving Lorena
  // gary drove
  // krishna driving

  struct thread *cur_proc = thread_current ();
  int status;
  bool zomb_lock_held = lock_held_by_current_thread (&cur_proc->zomblock);
  bool child_lock_held = lock_held_by_current_thread (&cur_proc->childlock);

  // if TID found, return status
  //  acquired lock for zombie list
  if (!zomb_lock_held)
    lock_acquire (&cur_proc->zomblock);
  // search through zombie list;
  struct list_elem *zombe;

  for (zombe = list_begin (&cur_proc->zombies);
       zombe != list_end (&cur_proc->zombies); zombe = list_next (zombe))
    {
      struct zombie *curr = list_entry (zombe, struct zombie, zombelem);
      // checking for if the tid = one we are looking for
      if (child_tid == curr->tid)
        {
          // bro is dead, return status
          status = curr->status;
          list_remove (zombe);
          free (curr);

          if (!child_lock_held)
            lock_release (&cur_proc->zomblock);
          return status;
        }
    }

  struct thread *child_proc = NULL;
  if (!child_lock_held)
    lock_acquire (&cur_proc->childlock);

  // find child with given tid in children
  struct list_elem *childe;

  for (childe = list_begin (&cur_proc->children);
       childe != list_end (&cur_proc->children) && !child_proc;
       childe = list_next (childe))
    {
      struct thread *curr = list_entry (childe, struct thread, childelem);
      // checking for if the tid = one we are looking for
      if (child_tid == curr->tid)
        {
          child_proc = curr;
        }
    }

  // check if child process is null or already being waited on
  if (!child_proc)
    {
      if (!child_lock_held)
        lock_release (&cur_proc->childlock);

      if (!zomb_lock_held)
        lock_release (&cur_proc->zomblock);

      return -1;
    }

  lock_acquire (&child_proc->shared_lock);

  if (child_proc->waited_on)
    {
      lock_release (&child_proc->shared_lock);
      if (!child_lock_held)
        lock_release (&cur_proc->childlock);

      if (!zomb_lock_held)
        lock_release (&cur_proc->zomblock);

      return -1;
    }

  child_proc->waited_on = true;
  child_proc->waiting_sema = &cur_proc->wait_sema;

  lock_release (&child_proc->shared_lock);

  if (!child_lock_held)
    lock_release (&cur_proc->childlock);

  if (!zomb_lock_held)
    lock_release (&cur_proc->zomblock);

  // wait for child process to finish; general synchro
  sema_down (&cur_proc->wait_sema);

  // once thread/process exits add to zombie list
  //
  if (!zomb_lock_held)
    lock_acquire (&cur_proc->zomblock);

  // search through zombie list;
  // give it one last chance
  for (zombe = list_begin (&cur_proc->zombies);
       zombe != list_end (&cur_proc->zombies); zombe = list_next (zombe))
    {
      struct zombie *curr = list_entry (zombe, struct zombie, zombelem);
      // checking for if the tid = one we are looking for
      if (child_tid == curr->tid)
        {
          // bro is dead, return status
          status = curr->status;
          list_remove (zombe);
          free (curr);

          if (!zomb_lock_held)
            lock_release (&cur_proc->zomblock);
          return status;
        }
    }

  // ultimate failure
  if (!zomb_lock_held)
    lock_release (&cur_proc->zomblock);

  return -1;
}

/* Free the current process's resources. */
void process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  if (cur->running)
    {
      file_close (cur->running);
      cur->running = NULL;
    }

  if (cur->command_name)
    {
      printf ("%s: exit(%d)\n", cur->command_name, cur->proc_status);
      palloc_free_page (cur->command_name);
    }

  lock_acquire (&cur->childlock);

  struct list_elem *childe;
  while (!list_empty (&cur->children))
    {
      childe = list_pop_front (&cur->children);
      struct thread *child = list_entry (childe, struct thread, childelem);
      process_wait (child->tid);
    }

  lock_release (&cur->childlock);

  // reap any remaining children (Gary implemented based on
  // above child loop)
  lock_acquire (&cur->zomblock);

  struct list_elem *zombe;
  while (!list_empty (&cur->zombies))
    {
      zombe = list_pop_front (&cur->zombies);
      struct zombie *zomb = list_entry (zombe, struct zombie, zombelem);
      process_wait (zomb->tid);
    }

  lock_release (&cur->zomblock);

  // Gary drove here (iirc)
  if (cur->parent)
    {
      lock_acquire (&cur->parent->childlock);
      list_remove (&cur->childelem);
      lock_release (&cur->parent->childlock);

      struct zombie *z = malloc (sizeof (struct zombie));

      if (z)
        {
          z->tid = cur->tid;
          z->status = cur->proc_status;

          lock_acquire (&cur->parent->zomblock);
          list_push_front (&cur->parent->zombies, &z->zombelem);
          lock_release (&cur->parent->zomblock);
        }
    }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp, int argc, char **argv, char *tmp_pg);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  int argc = 0;
  char **argv = palloc_get_page (0); // idk guys

  char *tmp_pg = palloc_get_page (0);
  strlcpy (tmp_pg, file_name, PGSIZE);

  char *saveptr;
  char *token;
  for (token = strtok_r (tmp_pg, " ", &saveptr); token;
       token = strtok_r (NULL, " ", &saveptr))
    {
      // if (!validate_string(token)){
      //   palloc_free_page (tmp_pg);
      //   palloc_free_page (argv);
      //   return success;
      // }
      argc++;
      argv[argc - 1] = token;
    }

  t->command_name = palloc_get_page (0);
  strlcpy (t->command_name, argv[0], PGSIZE);

  if (token)
    {
      palloc_free_page (tmp_pg);
      palloc_free_page (argv);
      return success;
    }

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (argv[0]);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", argv[0]);
      goto done;
    }
  // Deny write to file after opening
  file_deny_write (file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr ||
      memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 ||
      ehdr.e_machine != 3 || ehdr.e_version != 1 ||
      ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", argv[0]);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
          case PT_NULL:
          case PT_NOTE:
          case PT_PHDR:
          case PT_STACK:
          default:
            /* Ignore this segment. */
            break;
          case PT_DYNAMIC:
          case PT_INTERP:
          case PT_SHLIB:
            goto done;
          case PT_LOAD:
            if (validate_segment (&phdr, file))
              {
                bool writable = (phdr.p_flags & PF_W) != 0;
                uint32_t file_page = phdr.p_offset & ~PGMASK;
                uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
                uint32_t page_offset = phdr.p_vaddr & PGMASK;
                uint32_t read_bytes, zero_bytes;
                if (phdr.p_filesz > 0)
                  {
                    /* Normal segment.
                       Read initial part from disk and zero the rest. */
                    read_bytes = page_offset + phdr.p_filesz;
                    zero_bytes =
                        (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE) -
                         read_bytes);
                  }
                else
                  {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                  }
                if (!load_segment (file, file_page, (void *) mem_page,
                                   read_bytes, zero_bytes, writable))
                  goto done;
              }
            else
              goto done;
            break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, argc, argv, tmp_pg))
    goto done;

  // Gary driving here (based on Berke-written code)

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  thread_current ()->running = file;
done:
  /* We arrive here whether the load is successful or not. */
  if (!success)
    {
      file_close (file);
    }

  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack (void **esp, int argc, char **argv, char *tmp_pg)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (!success)
        {
          palloc_free_page (kpage);
          return false;
        }
      *esp = PHYS_BASE;

      uintptr_t stack_ptr = (uintptr_t) *esp;

      int curr_pg_size = PGSIZE;

      for (int arg = argc - 1; arg >= 0; --arg)
        {

          // Gary, krishna, and Lorena Driving
          int slen = strlen (argv[arg]);

          // move ptr down by # of bytes written (token size)
          stack_ptr -= slen + 1;

          slen = strlcpy ((void *) stack_ptr, argv[arg], curr_pg_size);

          curr_pg_size -= slen + 1;
          // bounds check
          if (curr_pg_size <= 0)
            {
              palloc_free_page (tmp_pg);
              palloc_free_page (argv);
              return false;
            }
        }

      int null = 0;

      // aligning the ptr to a mult of 4 for efficiency
      curr_pg_size -= ((stack_ptr % sizeof (char *)) + sizeof (char *));
      stack_ptr -= ((stack_ptr % sizeof (char *)) + sizeof (char *));

      memcpy ((void *) stack_ptr, &null,
              (stack_ptr % sizeof (char *)) + sizeof (char *));

      if (curr_pg_size <= 0)
        {
          palloc_free_page (tmp_pg);
          palloc_free_page (argv);
          return false;
        }
      size_t base = (size_t) PHYS_BASE;

      for (int i = argc - 1; i >= 0; --i)
        {
          // Gary stop; carry on my legacy!

          // Lorena driving
          int slen = strlen (argv[i]);
          base -= slen + 1;

          stack_ptr -= sizeof (char *);

          // copied address of the string
          memcpy ((void *) stack_ptr, (void *) &base, sizeof (char *));

          curr_pg_size -= sizeof (char *);
          if (curr_pg_size <= 0)
            {
              palloc_free_page (tmp_pg);
              palloc_free_page (argv);
              return false;
            }
        }

      palloc_free_page (tmp_pg);
      palloc_free_page (argv);

      uintptr_t stack_ptr_cpy = stack_ptr;
      stack_ptr -= sizeof (char *);
      // change esp, put address where argv is
      memcpy ((void *) stack_ptr, (void *) &stack_ptr_cpy, sizeof (char *));

      // update pagesize
      curr_pg_size -= sizeof (char *);

      if (curr_pg_size <= 0)
        {
          return false;
        }

      stack_ptr -= sizeof (char *);
      // change argc
      memcpy ((void *) stack_ptr, (void *) &argc, sizeof (char *));
      // update pagesize
      curr_pg_size -= sizeof (char *);

      if (curr_pg_size <= 0)
        {
          return false;
        }
      // Lorena: bye guys
      // Gary: Hello
      stack_ptr -= sizeof (char *);
      *esp = (void *) stack_ptr;
    }

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL &&
          pagedir_set_page (t->pagedir, upage, kpage, writable));
}
