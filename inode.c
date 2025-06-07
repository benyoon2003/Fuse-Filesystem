#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bitmap.h"
#include "blocks.h"
#include "inode.h"

/**
 * Retrieves the pointer to the inode structure based on inode number
 */
inode_t *get_inode(int inum)
{
    uint8_t *inode_base = (uint8_t *)get_inode_bitmap() + INODE_BITMAP_SIZE;
    return (inode_t *)(inode_base + (inum * sizeof(inode_t)));
}

/**
 * Allocates a new inode by setting the first available bit in the bitmap
 */
int alloc_inode()
{
    uint8_t *bitmap = (uint8_t *)get_inode_bitmap();

    for (int i = 0; i < INODE_COUNT; i++)
    {
        int byte_idx = i / 8;
        int bit_idx = i % 8;

        if ((bitmap[byte_idx] & (1 << bit_idx)) == 0)
        {
            bitmap_put(bitmap, i, 1);
            return i;
        }
    }
    return -1;
}

/**
 * Frees a given inode by clearing its corresponding bit in the bitmap
 */
void free_inode(int inum)
{
    bitmap_put(get_inode_bitmap(), inum, 0);
}

/**
 * Gets the block number corresponding to a logical block index in an inode
 */
int inode_get_bnum(inode_t *node, int logical_block)
{
    if (node->size <= BLOCK_SIZE)
    {
        return logical_block == 0 ? node->block : -1;
    }
    int *indirect_ptr = (int *)blocks_get_block(node->block);
    return indirect_ptr[logical_block];
}

/**
 * Grows the inode to fit the specified size, allocating blocks as needed
 */
int grow_inode(inode_t *node, int new_size)
{
    int current_blocks = (node->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int target_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Nothing to do if already large enough
    if (target_blocks <= current_blocks)
        return 0;

    // Growing from direct to indirect
    if (node->size <= BLOCK_SIZE && target_blocks > 1)
    {
        int new_indirect_block = alloc_block();
        if (new_indirect_block == -1)
            return -ENOSPC;

        int *indirect_ptrs = (int *)blocks_get_block(new_indirect_block);

        // Migrate existing direct block
        indirect_ptrs[0] = node->block;

        node->block = new_indirect_block;
        current_blocks = 1; // Since indirect_ptrs[0] is now the old direct block
    }

    // Allocate additional blocks in indirect mode
    if (node->size > BLOCK_SIZE || (node->size <= BLOCK_SIZE && target_blocks > 1))
    {
        int *indirect_ptrs = (int *)blocks_get_block(node->block);

        while (current_blocks < target_blocks)
        {
            int block_id = alloc_block();
            if (block_id == -1)
                return -ENOSPC;
            indirect_ptrs[current_blocks++] = block_id;
        }
    }

    return 0;
}


/**
 * Shrinks the inode size, freeing unused blocks as needed
 */
int shrink_inode(inode_t *node, int new_size)
{
    int old_block_count = (node->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int required_blocks = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    if (node->size <= BLOCK_SIZE)
    {
        return 0; // Nothing to shrink
    }

    int *indirect_arr = (int *)blocks_get_block(node->block);
    for (int i = required_blocks; i < old_block_count; i++)
    {
        free_block(indirect_arr[i]);
    }

    if (required_blocks == 1)
    {
        int preserved_block = indirect_arr[0];
        free_block(node->block);
        node->block = preserved_block;
    }

    return 0;
}
