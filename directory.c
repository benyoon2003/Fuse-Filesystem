#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "slist.h"
#include "blocks.h"
#include "inode.h"
#include "bitmap.h"
#include "directory.h"

/**
 * Fetches the directory entries from an inode and sets the count
 */
static dirent_t *get_entries(inode_t *dd, int *out_count)
{
  // Read the block from disk where directory entries are stored
  void *entry_block = blocks_get_block(dd->block);
  int entry_count = dd->size / sizeof(dirent_t);

  if (out_count)
  {
    *out_count = entry_count; // Return number of entries
  }

  return (dirent_t *)entry_block;
}

/**
 * Get inode number from file path
 */
int find_path(const char *path)
{
    if (strcmp(path, "/") == 0)
    {
        return 0; // Root path
    }

    inode_t *current = get_inode(0); // Start from root inode
    int inode_num = 0;

    const char *start = path;

    // Skip leading slash if present
    if (*start == '/')
        start++;

    while (*start != '\0')
    {
        const char *end = strchr(start, '/');
        int len = end ? (end - start) : strlen(start);

        // Copy the segment into a temporary buffer
        char name[256];
        if (len >= sizeof(name))
            return -1; // Segment too long
        memcpy(name, start, len);
        name[len] = '\0';

        inode_num = directory_lookup(current, name);
        if (inode_num == -1)
        {
            return -1; // Segment not found
        }

        current = get_inode(inode_num);

        // Advance to next segment
        if (!end)
            break;
        start = end + 1;
    }

    return inode_num;
}


/**
 * Initializes the root directory if not already initialized
 */
void directory_init()
{
  inode_t *root = get_inode(0);

  if (root->refs == 0)
  {
    root->refs = 1;
    root->mode = 040755;
    root->size = 0;
    root->block = alloc_block();

    void *inode_map = get_inode_bitmap();
    bitmap_put(inode_map, 0, 1); // Mark root inode as used
  }
}

/**
 * Prints the contents of a directory to stdout
 */
void print_directory(inode_t *dd)
{
  int count = 0;
  dirent_t *entries = get_entries(dd, &count);

  for (int i = 0; i < count; i++)
  {
    printf("Entry: %d: %s\n", entries[i].inum, entries[i].name);
  }
}

/**
 * Looks up an entry by name in a directory inode
 */
int directory_lookup(inode_t *dd, const char *name)
{
  int entry_count = 0;
  dirent_t *entries = get_entries(dd, &entry_count);

  for (int i = 0; i < entry_count; i++)
  {
    if (strcmp(entries[i].name, name) == 0)
    {
      return entries[i].inum;
    }
  }
  return -1; // No match found
}

/**
 * Deletes an entry by name from a directory
 */
int directory_delete(inode_t *dd, const char *name)
{
  int entry_count = 0;
  dirent_t *entries = get_entries(dd, &entry_count);

  for (int i = 0; i < entry_count; i++)
  {
    if (strcmp(entries[i].name, name) == 0)
    {
      entries[i] = entries[entry_count - 1]; // Replace with last
      dd->size -= sizeof(dirent_t);
      return 0;
    }
  }
  return -ENOENT; // Not found
}

/**
 * Adds a new entry to the directory
 */
int directory_put(inode_t *dd, const char *name, int inum)
{
  if (directory_lookup(dd, name) >= 0)
  {
    return -EEXIST; // Duplicate entry
  }

  if (strlen(name) >= DIR_NAME_LENGTH)
  {
    return -ENAMETOOLONG; // Name exceeds limit
  }

  int entry_count = 0;
  dirent_t *entries = get_entries(dd, &entry_count);
  dirent_t *new_entry = &entries[entry_count];
  dd->size += sizeof(dirent_t); // Update size
  strcpy(new_entry->name, name);
  new_entry->inum = inum;
  return 0;
}

/**
 * Lists all entries in a directory as a string list
 */
slist_t *directory_list(const char *path)
{
  int inode_num = find_path(path);
  if (inode_num == -1)
  {
    return NULL; // Path invalid
  }

  inode_t *dir_node = get_inode(inode_num);
  int count = 0;
  dirent_t *entries = get_entries(dir_node, &count);
  slist_t *slist = NULL;
  for (int i = 0; i < count; i++)
  {
    slist = slist_cons(entries[i].name, slist);
  }

  return slist;
}
