#include <hash.h>
#include "threads/synch.h"
#include "filesys/off_t.h"

// enums
enum page_status
{

  PAGE_ZERO = 0,
  PAGE_SWAP = 1,
  PAGE_RES = 2,

};

struct page
{
  // added new vars
  void *v_addr; // virtual address
  struct thread *owner;
  struct frame *mapped_frame; // frame page occupies
  size_t swap_idx;
  bool is_stack;
  struct file *file; // file to be loaded
  off_t file_offset; // file offset
  size_t read_bytes;
  size_t zero_bytes;
  bool writable;              // permission to write
  bool swapped;               // has it entered swap before?
  bool pinned;                // is the page pinned?
  struct hash_elem hash_elem; // hash elem
  enum page_status status;    // status of page, resident, in swap, new
  int sector;
  struct lock lock;
};

// Page Table Functions
unsigned page_hash (const struct hash_elem *, void *);
bool page_less (const struct hash_elem *, const struct hash_elem *, void *);
struct page *page_new (void *);
struct page *get_page (void *, bool);
bool pin_page (void *);
bool unpin_page (void *);
void page_free (struct hash_elem *, void *);
