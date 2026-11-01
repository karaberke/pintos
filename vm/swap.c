#include "vm/swap.h"
#include <bitmap.h>
#include <stdint.h>
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "vm/frame.h"
#include <stdio.h>

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct block *swap_device;

static struct bitmap *swap_table;

struct lock swap_table_lock;

void swap_table_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  size_t swap_slots = (block_size (swap_device) * BLOCK_SECTOR_SIZE) / PGSIZE;
  swap_table = bitmap_create (swap_slots);
  lock_init (&swap_table_lock);
}

void swap_table_free (void)
{
  lock_acquire (&swap_table_lock);
  bitmap_destroy (swap_table);
  lock_release (&swap_table_lock);
}

bool swap_write_page (struct page *page)
{
  ASSERT (page != NULL);
  ASSERT (page->mapped_frame != NULL);
  lock_acquire (&swap_table_lock);
  size_t slot = bitmap_scan_and_flip (swap_table, 0, 1, false);
  if (slot == BITMAP_ERROR)
    {
      PANIC ("They told us to panic because swap is empty and so we shall");
    }
  page->swap_idx = slot;
  uint8_t *frame_addr = page->mapped_frame->p_addr;
  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
      block_write (swap_device, slot * SECTORS_PER_PAGE + i,
                   frame_addr + i * BLOCK_SECTOR_SIZE);
    }

  page->status = PAGE_SWAP;
  page->swapped = true;
  page->mapped_frame = NULL;

  lock_release (&swap_table_lock);
  return true;
}

bool swap_read_page (struct page *page)
{
  ASSERT (page != NULL);
  ASSERT (page->status == PAGE_SWAP);
  ASSERT (page->mapped_frame != NULL);
  uint8_t *frame_addr = (uint8_t *) page->mapped_frame->p_addr;
  size_t slot = page->swap_idx;

  lock_acquire (&swap_table_lock);
  bool bm_test_success = bitmap_test (swap_table, slot);
  if (!bm_test_success)
    {
      return false;
    }

  for (size_t i = 0; i < SECTORS_PER_PAGE; i++)
    {
      block_read (swap_device, slot * SECTORS_PER_PAGE + i,
                  frame_addr + i * BLOCK_SECTOR_SIZE);
    }

  bitmap_reset (swap_table, slot);

  page->status = PAGE_RES;
  page->swap_idx = SIZE_MAX;
  lock_release (&swap_table_lock);
  return true;
}
