#include "vm/page.h"

// Swap Table Functions
void swap_table_init (void);
void swap_table_free (void);

bool swap_read_page (struct page *);
bool swap_write_page (struct page *);
