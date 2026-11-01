#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/off_t.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define POINTER_SIZE sizeof (uintptr_t)
#define DIRECT_BLOCKS 124
#define INDIRECT_BLOCKS 1
#define DIRECT_BYTES (DIRECT_BLOCKS * BLOCK_SECTOR_SIZE)
#define POINTERS_PER_BLOCK (BLOCK_SECTOR_SIZE / POINTER_SIZE)
#define BYTES_PER_NTH_INDEXED_BLOCK (POINTERS_PER_BLOCK * BLOCK_SECTOR_SIZE)

static size_t zeros[512] = {0};

static size_t available_blocks;
static struct lock available_blocks_lock;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  block_index_t blocks[DIRECT_BLOCKS + INDIRECT_BLOCKS]; /* Pointers to DIRECT
  BLOCKS blocks and 1 doubly indirect block */
  off_t length;   /* File size in bytes. */
  unsigned magic; /* Magic number. */
  bool is_file;
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
  struct list_elem elem;   /* Element in inode list. */
  block_sector_t sector;   /* Sector number ofl disk location. */
  off_t pending_length;    /* length after we extend */
  struct lock extend_lock; /* lock for extending the inode */
  int open_cnt;            /* Number of openers. */
  bool removed;            /* True if deleted, false otherwise. */
  int deny_write_cnt;      /* 0: writes ok, >0: deny writes. */
  struct inode_disk data;  /* Inode content. */
};

static bool block_allocate (block_index_t *);
static void block_release (block_index_t);
static block_index_t indirect_block_access (struct inode *, block_index_t,
                                            bool);
static void indirect_block_release (block_index_t);

static size_t reserve_blocks (struct inode *, block_index_t, block_index_t);
static void give_blocks (size_t);

static block_index_t byte_to_block (off_t pos)
{
  return pos / BLOCK_SECTOR_SIZE;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  block_index_t block_idx = byte_to_block (pos);
  if (block_idx >= DIRECT_BLOCKS)
    {
      // indexing into the sector within a given block
      return indirect_block_access (inode, block_idx, false);
    }
  return inode->data.blocks[block_idx];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init (void)
{
  list_init (&open_inodes);
  available_blocks = block_size (fs_device);
  lock_init (&available_blocks_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create (block_sector_t sector, off_t length, bool isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_file = !isdir;
      struct inode inode = {.data = *disk_inode};
      // end driving > . <
      // redo with all existing blocks?
      off_t allocated_blocks =
          reserve_blocks (&inode, 0, byte_to_block (length));
      if (allocated_blocks > 0)
        {
          // TODO: idk about this one chat
          memcpy (disk_inode, &inode.data, BLOCK_SECTOR_SIZE);
          if (BLOCK_SECTOR_SIZE * allocated_blocks < length)
            {
              disk_inode->length = BLOCK_SECTOR_SIZE * allocated_blocks;
            }
          block_write (fs_device, sector, disk_inode);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->extend_lock);
  block_read (fs_device, inode->sector, &inode->data);
  // TODO: synch worries
  inode->pending_length = inode->data.length;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  block_write (fs_device, inode->sector, &inode->data);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          block_index_t last_block = byte_to_block (inode->data.length);
          // for loop for direct freeing
          for (block_index_t i = 0; i < last_block; i++)
            {
              if (i == DIRECT_BLOCKS)
                {
                  indirect_block_release (inode->data.blocks[i]);
                  // i += BYTES_PER_NTH_INDEXED_BLOCK;
                  break; // TODO: find how many bytes are in one doubly indexed
                         // block
                }
              else
                {
                  block_release (inode->data.blocks[i]);
                }
            }
          give_blocks (byte_to_block (inode->data.length));
          // TODO: figure out synchronization
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size,
                     off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  if (size + offset > inode->data.length)
    {
      return 0;
    }

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      if (!sector_idx)
        {
          memcpy (buffer + bytes_read, zeros, chunk_size - sector_ofs);
        }
      else if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */

          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                      off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  off_t allocated_blocks = 0;
  if (size + offset > inode->data.length)
    {
      lock_acquire (&inode->extend_lock);
      if (size + offset > inode->data.length)
        {
          inode->pending_length = size + offset;
          block_index_t start_ext = byte_to_block (inode->data.length) + 1;
          block_index_t end_ext = byte_to_block (inode->pending_length);
          if (end_ext >= start_ext)
            {
              // TODO: we want to write what we have been allowed to get
              // even if we don't get all of it
              allocated_blocks = reserve_blocks (inode, start_ext, end_ext);

              if (!allocated_blocks)
                {
                  return 0;
                }

              if (inode->pending_length > allocated_blocks * BLOCK_SECTOR_SIZE)
                {
                  inode->pending_length = BLOCK_SECTOR_SIZE * allocated_blocks;
                }
              inode->data.length = inode->pending_length;
            }
        }
      lock_release (&inode->extend_lock);
    }

  // TODO: handle failure to allocate blocks
  size_t blocks_added = 0;
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (!sector_idx)
        {
          break;
        }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      if (offset > inode->data.length)
        {
          blocks_added++;
        }
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  // if (lock_held_by_current_thread (&inode->extend_lock))
  //   {
  //     if (blocks_added < allocated_blocks)
  //       {
  //         give_blocks (allocated_blocks - blocks_added);
  //         inode->pending_length = inode->data.length + bytes_written;
  //       }
  //     inode->data.length = inode->pending_length;
  //     lock_release (&inode->extend_lock);
  //   }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length (const struct inode *inode) { return inode->data.length; }

bool inode_isdir (const struct inode *inode) { return !inode->data.is_file; }

static bool block_allocate (block_index_t *block_addr)
{
  if (!free_map_allocate (1, block_addr))
    {
      return false;
    }
  block_write (fs_device, *block_addr, zeros);
  return true;
}

static void block_release (block_index_t block) { free_map_release (block, 1); }

static block_index_t indirect_block_access (struct inode *inode,
                                            block_index_t block_idx,
                                            bool create)
{
  // uhh why are you here in this function ahh assertion
  ASSERT (block_idx >= DIRECT_BLOCKS)

  if (!inode->data.blocks[DIRECT_BLOCKS])
    {
      if (!block_allocate (&inode->data.blocks[DIRECT_BLOCKS]))
        {
          return 0;
          // TODO : figure out a better way to handle error case
        }
    }
  block_index_t indirect_block_idx = block_idx - DIRECT_BLOCKS;
  block_index_t first_lvl_block_idx =
      indirect_block_idx / POINTERS_PER_BLOCK;
  block_index_t second_lvl_block_idx = indirect_block_idx%POINTERS_PER_BLOCK;

  block_index_t buff[POINTERS_PER_BLOCK];
  block_read (fs_device, inode->data.blocks[DIRECT_BLOCKS], buff);
  // TODO : if block_to_read is zero, return zero ( it wasn't allocated yet) or
  // make a new allocated block
  block_index_t block_to_read = buff[first_lvl_block_idx];
  if (!block_to_read)
    {
      if (!create)
        {
          return 0;
        }
      if (!block_allocate (&buff[first_lvl_block_idx]))
        {
          return 0;
          // TODO : figure out a better way to handle error case
        }
      block_to_read = buff[first_lvl_block_idx];
      block_write (fs_device, inode->data.blocks[DIRECT_BLOCKS], buff);
    }
  block_read (fs_device, block_to_read, buff);
  block_index_t block_to_return = buff[second_lvl_block_idx];
  if (!block_to_return)
    {
      if (!create)
        {
          return 0;
        }
      if (!block_allocate (&buff[second_lvl_block_idx]))
        {
          return 0;
          // TODO : figure out a better way to handle error case
        }
      block_to_return = buff[second_lvl_block_idx];
      block_write (fs_device, block_to_read, buff);
    }
  return block_to_return;
}

static void indirect_block_release (block_index_t indirect_blk)
{
  if (indirect_blk)
    {
      block_index_t first_lvl_buf[POINTERS_PER_BLOCK];
      block_read (fs_device, indirect_blk, first_lvl_buf);
      for (block_index_t i = 0; i < POINTERS_PER_BLOCK; ++i)
        {
          if (first_lvl_buf[i])
            {
              block_index_t second_lvl_buf[POINTERS_PER_BLOCK];
              block_read (fs_device, first_lvl_buf[i], second_lvl_buf);
              for (block_index_t i = 0; i < POINTERS_PER_BLOCK; ++i)
                {
                  if (second_lvl_buf[i])
                    {
                      block_release (second_lvl_buf[i]);
                    }
                }
              block_release (first_lvl_buf[i]);
            }
        }
      block_release (indirect_blk);
    }
}

static block_index_t reserve_blocks (struct inode *inode, block_index_t start,
                                     block_index_t end)
{
  block_index_t allocated_blocks = 0;

  for (block_index_t i = start; i <= end; i++)
    {
      if (i >= DIRECT_BLOCKS)
        {
          // indexing into the sector within a given block
          if (!indirect_block_access (inode, i, true))
            {
              break;
            }
        }
      else if (!block_allocate (&inode->data.blocks[i]))
        {
          break;
        }
      allocated_blocks++;
    }
  lock_acquire (&available_blocks_lock);
  available_blocks -= allocated_blocks;
  lock_release (&available_blocks_lock);
  return allocated_blocks;
}

static void give_blocks (size_t blocks_to_free)
{
  // TODO: try releasing the blocks probably
  lock_acquire (&available_blocks_lock);
  if (available_blocks + blocks_to_free > block_size (fs_device))
    {
      lock_release (&available_blocks_lock);
      ASSERT (available_blocks + blocks_to_free <= block_size (fs_device))
    }
  available_blocks += blocks_to_free;
  lock_release (&available_blocks_lock);
}
