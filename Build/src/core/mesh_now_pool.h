#ifndef MESH_NOW_POOL_H
#define MESH_NOW_POOL_H

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mesh_now_pool mesh_now_pool_t;

/**
 * Fixed-block memory pool
 */
struct mesh_now_pool {
    uint8_t *blocks;        // backing array: block_count * block_size bytes
    uint32_t free_head;     // index of first free block, MESH_NOW_POOL_NONE if none
    size_t block_size;      // bytes per block (>= 4)
    size_t block_count;     // total blocks
    size_t used_count;      // blocks currently handed out
};

/**
 * Create a pool that allocates backing memory from the heap (once)
 *
 * @param out_pool   pool metadata
 * @param block_size bytes per block, must be >= 4
 * @param block_count number of blocks
 * @return ESP_OK, or ESP_ERR_NO_MEM if the backing allocation fails
 */
esp_err_t mesh_now_pool_create(mesh_now_pool_t *out_pool,
                               size_t block_size,
                               size_t block_count);

/**
 * Free the backing memory and reset the pool.
 */
void mesh_now_pool_destroy(mesh_now_pool_t *pool);

/**
 * Hand out one block. Returns NULL when the pool is exhausted.
 */
void *mesh_now_pool_alloc(mesh_now_pool_t *pool);

/**
 * Return a block to the pool. Must be a pointer previously handed out by
 * mesh_now_pool_alloc() on this same pool otherwise... undefined behavior
 */
void mesh_now_pool_free(mesh_now_pool_t *pool, void *ptr);

/**
 * Blocks currently in use
 */
size_t mesh_now_pool_used(const mesh_now_pool_t *pool);

/**
 * Blocks still free
 */
size_t mesh_now_pool_available(const mesh_now_pool_t *pool);

#define MESH_NOW_POOL_NONE UINT32_MAX

#endif // MESH_NOW_POOL_H
