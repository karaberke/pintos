#include "vm/page.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include <stddef.h>
#include <hash.h>
#include <stdint.h>
#include "userprog/pagedir.h"

struct page *page_new (void *page)
{
  struct page *new_page = malloc (sizeof (struct page));
  if (new_page == NULL)
    {
      return NULL;
    }

  new_page->v_addr = pg_round_down (page);
  new_page->owner = thread_current ();
  new_page->mapped_frame = NULL;
  new_page->status = PAGE_ZERO;
  new_page->swap_idx = SIZE_MAX;

  new_page->pinned = false;
  new_page->swapped = false;

  // add to the thread's spt ig
  hash_insert (&thread_current ()->spt, &new_page->hash_elem);

  lock_init (&new_page->lock);

  return new_page;
}

/* yanked from pintos section A */
/* Returns a hash value for page p. */
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->v_addr, sizeof p->v_addr);
}

/* Returns true if page a precedes page b. */
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->v_addr < b->v_addr;
}
/* end yank from pintos section A */

struct page *get_page (void *v_addr, bool create)
{
  struct page p;
  struct hash_elem *e;
  p.v_addr = pg_round_down (v_addr);
  e = hash_find (&thread_current ()->spt, &p.hash_elem);

  if (!e)
    {
      return create ? page_new (v_addr) : NULL;
    }
  return hash_entry (e, struct page, hash_elem);
}

bool pin_page (void *v_addr)
{
  // pin page to address
  struct page *page = get_page (v_addr, 0);
  if (!page)
    {
      return false;
    }
  page->pinned = true;
  return true;
}

bool unpin_page (void *v_addr)
{
  // unpin page from address
  struct page *page = get_page (v_addr, 0);
  if (!page)
    {
      return false;
    }
  page->pinned = false;
  return true;
}

void page_free (struct hash_elem *element, void *aux UNUSED)
{
  // unmap frame from this page
  struct page *page = hash_entry (element, struct page, hash_elem);
  if (!lock_held_by_current_thread (&page->lock))
    {
      lock_acquire (&page->lock);
    }
  if (page->mapped_frame)
    {
      if (lock_held_by_current_thread (&page->mapped_frame->lock) ||
          lock_try_acquire (&page->mapped_frame->lock))
        {
          page->mapped_frame->mapped_page = NULL;
          lock_release (&page->mapped_frame->lock);
        }
      page->mapped_frame = NULL;
      // clear the page reference to real mem
      pagedir_clear_page (thread_current ()->pagedir, page->v_addr);
    }
  // free allocated metadata
  free (page);
}
