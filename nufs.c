#define FUSE_USE_VERSION 26
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fuse.h>
#include "blocks.h"
#include "directory.h"
#include "slist.h"
#include "bitmap.h"
#include "inode.h"

/**
 * Extracts directory part from a file path
 */
void get_directory_from_path(const char *filepath, char *out_dirname)
{
  strcpy(out_dirname, filepath);
  char *last_sep = strrchr(out_dirname, '/');
  if (last_sep && filepath != out_dirname)
  {
    *last_sep = '\0';
  }
}

/**
 * implementation for: man 2 access
 * Checks if a file exists.
 */
int nufs_access(const char *path, int mask)
{
  int rv = -1;
  int inum = find_path(path);

  if (inum >= 0)
  {
    rv = 0;
  }
  else
  {
    rv = -ENOENT;
  }
  printf("access(%s, %04o) -> %d\n", path, mask, rv);
  return rv;
}

/**
 * mknod makes a filesystem object like a file or directory
 * called for: man 2 open, man 2 link
 */
int nufs_mknod(const char *path, mode_t mode, dev_t rdev)
{
  int inode_id = alloc_inode();
  inode_t *node = get_inode(inode_id);
  int blk = alloc_block();
  if (inode_id == -1 || blk == -1)
  {
    return -ENOSPC;
  }
  node->block = blk;
  node->refs = 1;
  node->size = 0;
  node->mode = mode;

  char parent_path[512];
  get_directory_from_path(path, parent_path);
  int parent_inode = find_path(parent_path);
  if (parent_inode == -1)
  {
    return -ENOENT;
  }
  inode_t *parent_node = get_inode(parent_inode);
  const char *new_name = strrchr(path, '/') + 1;
  int status = directory_put(parent_node, new_name, inode_id);
  printf("mknod(%s, %04o) -> inum=%d block=%d\n", path, mode, inode_id, blk);
  return status;
}

/**
 * Gets an object's attributes (type, permissions, size, etc).
 * Implementation for: man 2 stat
 * This is a crucial function.
 */
int nufs_getattr(const char *path, struct stat *st)
{
  int ret = 0;
  int inode_index = find_path(path);
  if (inode_index == -1)
  {
    return -ENOENT;
  }

  inode_t *inode = get_inode(inode_index);
  st->st_mode = inode->mode;
  st->st_size = inode->size;
  st->st_uid = getuid();
  st->st_nlink = inode->refs;

  printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, ret, st->st_mode, st->st_size);
  return ret;
}

/**
 * Creates a new directory
 */
int nufs_mkdir(const char *path, mode_t mode)
{
  int rv = nufs_mknod(path, mode | 040000, 0);
  printf("mkdir(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_link(const char *from, const char *to)
{
  int rv = -1;
  printf("link(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

/**
 * Removes a directory
 */
int nufs_rmdir(const char *path)
{
  int inum = find_path(path);
  if (inum == -1)
  {
    return -ENOENT;
  }

  inode_t *root_Inode = get_inode(0);
  const char *dir_name = path + 1;
  directory_delete(root_Inode, dir_name);
  free_inode(inum);
  int rv = 0;
  printf("rmdir(%s) -> %d\n", path, rv);
  return rv;
}

/**
 * implements: man 2 rename
 * called to move a file within the same filesystem
 */
int nufs_rename(const char *src, const char *dst)
{
  int inum = find_path(src);
  if (inum == -1)
  {
    return -ENOENT;
  }

  char src_dir[512], dst_dir[512];
  get_directory_from_path(src, src_dir);
  get_directory_from_path(dst, dst_dir);

  const char *old_name = strrchr(src, '/') + 1;
  const char *new_name = strrchr(dst, '/') + 1;

  int src_parent_id = find_path(src_dir);
  int dst_parent_id = find_path(dst_dir);

  inode_t *src_parent = get_inode(src_parent_id);
  inode_t *dst_parent = get_inode(dst_parent_id);

  int exists = find_path(dst);
  if (exists >= 0)
  {
    // Directory exists
    directory_delete(dst_parent, new_name);
  }

  directory_put(dst_parent, new_name, inum);
  directory_delete(src_parent, old_name);

  printf("rename(%s => %s) -> 0\n", src, dst);
  return 0;
}

/**
 * Deletes a file
 */
int nufs_unlink(const char *path)
{
  int inum = find_path(path);
  if (inum == -1)
  {
    return -ENOENT;
  }
  else
  {
    inode_t *target_node = get_inode(inum);
    free_block(target_node->block);
    free_inode(inum);
    inode_t *root_node = get_inode(0);
    const char *filename = path + 1;
    directory_delete(root_node, filename);
    int result = 0;
    printf("unlink(%s) -> %d\n", path, result);
    return result;
  }
}

int nufs_chmod(const char *path, mode_t mode)
{
  int rv = -1;
  printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

/**
 * Changes file size
 */
int nufs_truncate(const char *path, off_t size)
{
  int inum = find_path(path);
  if (inum == -1)
  {
    return -ENOENT;
  }

  inode_t *node = get_inode(inum);
  if (size > node->size)
  {
    int grow_status = grow_inode(node, size);
    if (grow_status == -1)
      return grow_status;
  }
  else if (size < node->size)
  {
    shrink_inode(node, size);
  }

  node->size = size;
  printf("truncate(%s, %ld bytes) -> 0\n", path, size);
  return 0;
}

/**
 * implementation for: man 2 readdir
 * lists the contents of a directory
 */
int nufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler_func,
                 off_t offset, struct fuse_file_info *fi)
{
  struct stat metadata;
  int result = nufs_getattr(path, &metadata);
  assert(result == 0);
  filler_func(buffer, ".", &metadata, 0);
  slist_t *node = directory_list(path);

  while (node)
  {
    const char *entry_name = node->data;
    char constructed_path[128];

    // Build the full path
    if (strcmp(path, "/") == 0)
    {
      snprintf(constructed_path, sizeof(constructed_path), "/%s", entry_name);
    }
    else
    {
      snprintf(constructed_path, sizeof(constructed_path), "%s/%s", path, entry_name);
    }

    nufs_getattr(constructed_path, &metadata);
    filler_func(buffer, entry_name, &metadata, 0);
    node = node->next;
  }

  slist_free(node);
  printf("readdir(%s) -> %d\n", path, result);
  return 0;
}

int nufs_open(const char *path, struct fuse_file_info *fi)
{
  int rv = 0;
  printf("open(%s) -> %d\n", path, rv);
  return rv;
}

/**
 * Reads data from a file
 */
int nufs_read(const char *path, char *buffer, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
  int inum = find_path(path);
  if (inum == -1)
  {
    return -ENOENT;
  }

  inode_t *inode = get_inode(inum);

  // If offset is beyond EOF, return 0 (nothing to read)
  if ((size_t)offset >= inode->size)
  {
    return 0;
  }

  // Clamp size to not read beyond EOF
  size_t readable = inode->size - offset;
  if (size > readable)
  {
    size = readable;
  }

  size_t total_read = 0;
  size_t remaining = size;
  size_t current_offset = offset;

  while (remaining > 0)
  {
    int blk_idx = current_offset / BLOCK_SIZE;
    int blk_offset = current_offset % BLOCK_SIZE;

    int blk_num = inode_get_bnum(inode, blk_idx);
    if (blk_num == -1)
    {
      break;
    }

    void *blk = blocks_get_block(blk_num);
    size_t blk_space = BLOCK_SIZE - blk_offset;
    size_t read_amount = remaining < blk_space ? remaining : blk_space;
    memcpy(buffer + total_read, (char *)blk + blk_offset, read_amount);
    current_offset += read_amount;
    total_read += read_amount;
    remaining -= read_amount;
  }

  printf("read(%s, %ld bytes, @+%ld) -> %ld\n", path, size, offset, total_read);
  return total_read;
}

/**
 * Writes data to file
 */
int nufs_write(const char *path, const char *data, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
  int inum = find_path(path);
  if (inum == -1)
  {
    return -ENOENT;
  }

  inode_t *inode = get_inode(inum);
  size_t final_size = offset + size;

  // Grow file if the write extends beyond the current file size
  if (final_size > inode->size)
  {
    int res = nufs_truncate(path, final_size);
    if (res < 0)
    {
      return res;
    }
  }

  size_t bytes_written = 0;
  size_t remaining = size;
  size_t current_offset = offset;

  while (remaining > 0)
  {
    int blk_idx = current_offset / BLOCK_SIZE;
    int blk_offset = current_offset % BLOCK_SIZE;

    int blk_num = inode_get_bnum(inode, blk_idx);
    if (blk_num == -1)
    {
      break;
    }

    void *blk = blocks_get_block(blk_num);
    size_t blk_space = BLOCK_SIZE - blk_offset;
    size_t write_amount = remaining < blk_space ? remaining : blk_space;
    memcpy((char *)blk + blk_offset, data + bytes_written, write_amount);
    current_offset += write_amount;
    bytes_written += write_amount;
    remaining -= write_amount;
  }

  printf("write(%s, %ld bytes, @+%ld) -> %ld\n", path, size, offset, bytes_written);
  return bytes_written;
}

/**
 * Update the timestamps on a file or directory.
 */
int nufs_utimens(const char *path, const struct timespec ts[2])
{
  int rv = -1;
  printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n", path, ts[0].tv_sec,
         ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
  return rv;
}

int nufs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi,
               unsigned int flags, void *data)
{
  int rv = -1;
  printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
  return rv;
}

void nufs_init_ops(struct fuse_operations *ops)
{
  memset(ops, 0, sizeof(struct fuse_operations));
  ops->access = nufs_access;
  ops->getattr = nufs_getattr;
  ops->readdir = nufs_readdir;
  ops->mknod = nufs_mknod;
  ops->mkdir = nufs_mkdir;
  ops->link = nufs_link;
  ops->unlink = nufs_unlink;
  ops->rmdir = nufs_rmdir;
  ops->rename = nufs_rename;
  ops->chmod = nufs_chmod;
  ops->truncate = nufs_truncate;
  ops->open = nufs_open;
  ops->read = nufs_read;
  ops->write = nufs_write;
  ops->utimens = nufs_utimens;
  ops->ioctl = nufs_ioctl;
}

struct fuse_operations nufs_ops;

int main(int argc, char *argv[])
{
  assert(argc > 2 && argc < 6);
  const char *image_path = argv[--argc];
  blocks_init(image_path);
  directory_init();
  nufs_init_ops(&nufs_ops);
  return fuse_main(argc, argv, &nufs_ops, NULL);
}
