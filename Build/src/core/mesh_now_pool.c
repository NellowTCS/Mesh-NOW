#include "mesh_now_pool.h"
#include <stdlib.h>
#include <string.h>

esp_err_t mesh_now_pool_create(mesh_now_pool_t *out_pool, size_t block_size,
                               size_t block_count)
{
    if (out_pool == NULL || block_size < 4 || block_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes = block_size * block_count;
    uint8_t *blocks = (uint8_t *)malloc(bytes);
    if (blocks == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Zero every block so unallocated slots read as inactive (active=false)
    memset(blocks, 0, bytes);

    // Build an intrusive free list: the first 4 bytes of each free block hold
    // the index of the next free block, chaining every block together with the
    // head at block 0 and the tail at MESH_NOW_POOL_NONE.
    for (size_t i = 0; i < block_count; i++) {
        uint8_t *blk = blocks + i * block_size;
        uint32_t next =
            (i + 1 < block_count) ? (uint32_t)(i + 1) : MESH_NOW_POOL_NONE;
        memcpy(blk, &next, sizeof(next));
    }

    out_pool->blocks = blocks;
    out_pool->free_head = 0;
    out_pool->block_size = block_size;
    out_pool->block_count = block_count;
    out_pool->used_count = 0;
    return ESP_OK;
}

void mesh_now_pool_destroy(mesh_now_pool_t *pool)
{
    if (pool == NULL || pool->blocks == NULL) {
        return;
    }
    free(pool->blocks);
    pool->blocks = NULL;
    pool->free_head = MESH_NOW_POOL_NONE;
    pool->used_count = 0;
}

void *mesh_now_pool_alloc(mesh_now_pool_t *pool)
{
    if (pool == NULL || pool->free_head == MESH_NOW_POOL_NONE) {
        return NULL;
    }
    uint32_t idx = pool->free_head;
    uint8_t *blk = pool->blocks + idx * pool->block_size;
    uint32_t next;
    memcpy(&next, blk, sizeof(next));
    pool->free_head = next;
    pool->used_count++;
    return blk;
}

void mesh_now_pool_free(mesh_now_pool_t *pool, void *ptr)
{
    if (pool == NULL || ptr == NULL || pool->blocks == NULL) {
        return;
    }
    uint8_t *blk = (uint8_t *)ptr;
    ptrdiff_t off = blk - pool->blocks;
    if (off < 0 || (size_t)off >= pool->block_count * pool->block_size ||
        (off % (ptrdiff_t)pool->block_size) != 0) {
        return; // pointer does not belong to this pool
    }
    uint32_t idx = (uint32_t)(off / pool->block_size);

    // Double-free guard
    uint32_t walk = pool->free_head;
    while (walk != MESH_NOW_POOL_NONE) {
        if (walk == idx) {
            return;
        }
        uint8_t *wblk = pool->blocks + walk * pool->block_size;
        uint32_t next;
        memcpy(&next, wblk, sizeof(next));
        walk = next;
    }

    memcpy(blk, &pool->free_head, sizeof(pool->free_head));
    pool->free_head = idx;
    pool->used_count--;
}

size_t mesh_now_pool_used(const mesh_now_pool_t *pool)
{
    return pool ? pool->used_count : 0;
}

size_t mesh_now_pool_available(const mesh_now_pool_t *pool)
{
    return pool ? (pool->block_count - pool->used_count) : 0;
}
