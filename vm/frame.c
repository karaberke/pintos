#include "vm/frame.h"
#include <stddef.h>
#include <stdint.h>
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "user/syscall.h"
#include "vm/swap.h"
#include <bitmap.h>
#include <round.h>
#include "userprog/pagedir.h"
#include "threads/thread.h"

// Frame Table Array
struct frame *frame_table = NULL;
size_t user_pages;
uint8_t *user_pool_base = NULL;
unsigned clock_ptr;

static uintptr_t frame_no (void *);

void frame_table_init (size_t user_page_limit)
{

  // recalculate number of user pages
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;
  // user page
  user_pool_base = free_start + kernel_pages * PGSIZE;

  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (user_pages), PGSIZE);
  user_pages -= bm_pages;

  user_pool_base += bm_pages * PGSIZE;

  // allocate mem for frame table
  frame_table = (struct frame *) calloc (sizeof (struct frame), user_pages);
  if (!frame_table)
    {
      PANIC ("could not allocate frame table oh no");
    }

  for (size_t i = 0; i < user_pages; i++)
    {
      lock_init (&frame_table[i].lock);
      lock_release (&frame_new (PAL_ZERO)->lock);
    }
  clock_ptr = 0;
}

void frame_table_free (void)
{
  for (size_t i = 0; i < user_pages; i++)
    {
      if (frame_table[i].p_addr)
        {
          palloc_free_page (frame_table[i].p_addr);
        }
    }
  free (frame_table);
}

bool frame_is_free (struct frame *frame) { return !frame->mapped_page; }

// looks thru frame table and maps page to open frame upon success,
// return success/fail
bool map_page_to_frame (void *v_addr, void *p_addr, bool writable)
{
  struct page *page = get_page (v_addr, true);

  if (!page)
    {
      return false;
    }

  size_t frame_num = frame_no (p_addr);
  struct frame *frame_to_map = &frame_table[frame_num];

  if (!lock_held_by_current_thread (&frame_to_map->lock))
    {
      return false;
    }

  // Frame updated page it points to
  frame_to_map->mapped_page = page;
  // Page updated frame it points to
  page->mapped_frame = frame_to_map;
  page->writable = writable;
  page->status = PAGE_RES;

  return true;
}

static uintptr_t frame_no (void *k_addr)
{
  return ((uintptr_t) (k_addr) - (uintptr_t) (user_pool_base)) >> PGBITS;
}

// ! Note : a lock for the frame is acquired in this function below
// ! ( so it must water bucket, RELEASE! by the recieving function)
struct frame *get_frame (void)
{
  for (size_t i = 0; i < user_pages; i++)
    {
      if (frame_is_free (&frame_table[i]) &&
          !lock_held_by_current_thread (&frame_table[i].lock) &&
          lock_try_acquire (&frame_table[i].lock))
        {
          return &frame_table[i];
        }
    }
  return clock_algo ();
}

struct frame *frame_new (int flags)
{
  void *new_frame_addr = palloc_get_page (PAL_USER | flags);
  if (!new_frame_addr)
    {
      return NULL;
    }
  uintptr_t frame_number = frame_no (new_frame_addr);
  ASSERT (!frame_table[frame_number].p_addr);
  lock_acquire (&frame_table[frame_number].lock);
  frame_table[frame_number].p_addr = new_frame_addr;

  return &frame_table[frame_number];
}

// in case we need to water bucket, RELEASE! early
void release_frame_lock (void *p_addr)
{
  size_t frame_num = frame_no (p_addr);
  struct frame *frame = &frame_table[frame_num];
  if (lock_held_by_current_thread (&frame->lock))
    {
      lock_release (&frame->lock);
    }
}

struct frame *evict_page_from_frame (struct frame *frame)
{
  struct page *victim_page = frame->mapped_page;
  lock_acquire (&victim_page->lock);

  // if page is dirty, write data back to disk?
  bool pgdir_dirty =
      pagedir_is_dirty (thread_current ()->pagedir, victim_page->v_addr);
  pagedir_clear_page (thread_current ()->pagedir, victim_page->v_addr);
  if (victim_page->swapped || pgdir_dirty)
    {
      if (!swap_write_page (victim_page))
        {
          return NULL;
        }
    }

  victim_page->mapped_frame = NULL;
  frame->mapped_page = NULL;

  lock_release (&victim_page->lock);

  return frame;
}

struct frame *clock_algo (void)
{
  bool page_found = false;
  struct frame *curr;
  while (!page_found)
    {
      curr = &frame_table[clock_ptr];
      if (pagedir_is_accessed (thread_current ()->pagedir,
                               curr->mapped_page->v_addr))
        {
          pagedir_set_accessed (thread_current ()->pagedir,
                                curr->mapped_page->v_addr, 0);
        }
      else if (lock_try_acquire (&curr->lock) && !curr->mapped_page->pinned)
        {
          page_found = true;
          evict_page_from_frame (curr);
        }
      clock_ptr = (clock_ptr + 1) % user_pages;
    }

  return curr;
}
