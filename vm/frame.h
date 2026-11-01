#include <stdbool.h>
#include "threads/synch.h"

struct frame
{
  void *p_addr;
  // added new vars
  struct page *mapped_page;
  struct lock lock;
};

// Frame Table Functions
void frame_table_init (size_t);
void frame_table_free (void);
// Frame Functions
bool frame_is_free (struct frame *);
bool map_page_to_frame (void *v_addr, void *p_addr, bool writable);

struct frame *get_frame (void);
struct frame *frame_new (int flags);
void release_frame_lock (void *);
struct frame *clock_algo (void);
struct frame *evict_page_from_frame (struct frame *);
