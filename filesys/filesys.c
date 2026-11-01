#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/inode.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
static bool create_file (struct dir *, off_t offset, const char *name,
                         bool isdir);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done (void) { free_map_close (); }

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create (char *name, off_t initial_size)
{

  bool created = false;
  parse_path (name, true, false, initial_size, &created, NULL);

  return created;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
void *filesys_open (char *name)
{
  struct inode *inode = parse_path (name, false, false, 0, NULL, NULL);

  if (inode == NULL)
    {
      return NULL;
    }

  if (inode_isdir (inode))
    {
      return dir_open (inode);
    }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove (char *name)
{
  struct dir *dir = NULL;
  struct inode *inode = parse_path (name, false, false, 0, NULL, &dir);
  if (inode == NULL)
    {
      return false;
    }
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

static int count_entries (const char *path)
{
  int entries = 0;
  char *buf = malloc (strlen (path) + 1);
  if (!buf)
    {
      return -1;
    }
  char *save_ptr;
  char *token;
  for (token = strtok_r (buf, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      entries++;
    }
  free (buf);
  return entries;
}

struct inode *parse_path (char *path, bool create, bool isdir,
                          off_t initial_size, bool *created,
                          struct dir **parent)
{
  // loop through the path?
  // TODO: finish function logic
  struct dir *curr_dir = NULL;
  struct inode *curr_inode = NULL;
  char *save_ptr;
  char *token;
  int entries = count_entries (path);
  if (entries == -1)
    {
      return NULL;
    }
  bool absolute = (path[0] == '/');
  for (token = strtok_r (path, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      if (curr_dir == NULL)
        {
          if (absolute)
            {
              curr_dir = dir_open_root ();
              if (!curr_dir)
                {
                  return NULL;
                }
            }

          else
            {
              curr_dir = dir_reopen (thread_current ()->cwd);
              if (!curr_dir)
                {
                  return NULL;
                }
            }
        }
      else
        {
          dir_close (curr_dir);
          curr_dir = dir_open (curr_inode);
          if (!curr_dir)
            {
              return NULL;
            }
        }

      if (!dir_lookup (curr_dir, token, &curr_inode))
        {
          if (!create || entries != 1)
            {
              return NULL;
            }
          // make a new file or directory
          if (!create_file (curr_dir, initial_size, token, isdir))
            {
              return NULL;
            }
          dir_lookup (curr_dir, token, &curr_inode);

          // open/find? the resulting inode
          if (curr_inode && created)
            {
              *created = true;
            }

          return curr_inode; // if allocat
        }

      entries--;
    }

  // TODO: flesh out
  if (parent)
    {
      *parent = curr_dir;
    }
  return curr_inode;
}
static bool create_file (struct dir *dir, off_t initial_size, const char *name,
                         bool isdir)
{
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL && free_map_allocate (1, &inode_sector) &&
                  inode_create (inode_sector, initial_size, isdir) &&
                  dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  return success;
}
