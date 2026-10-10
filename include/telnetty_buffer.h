/**
 * @file telnetty_buffer.h
 * @brief High-performance buffer management for TELNET protocol
 * 
 * This header provides efficient buffer management with zero-copy operations,
 * buffer pooling, and optimized memory allocation patterns for high-throughput
 * TELNET applications.
 * 
 * Features:
 * - Zero-copy buffer chains
 * - Buffer pooling for reduced allocation overhead
 * - Dynamic buffer resizing
 * - Memory-efficient operations
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.2.0
 * 
 */

#ifndef TELNETTY_BUFFER_H
#define TELNETTY_BUFFER_H

#include "telnetty_core.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mark functions as potentially unused (header-only library) */
#ifndef TELNETTY_UNUSED
#ifdef __GNUC__
#define TELNETTY_UNUSED __attribute__((unused))
#else
#define TELNETTY_UNUSED
#endif
#endif


/* ============================================================================
 * Configuration Options
 * ============================================================================ */

#ifndef TELNETTY_BUFFER_POOL_SIZE
#define TELNETTY_BUFFER_POOL_SIZE 64    /* Number of buffers in pool */
#endif

#ifndef TELNETTY_BUFFER_POOL_BUFFER_SIZE
#define TELNETTY_BUFFER_POOL_BUFFER_SIZE 4096  /* Size of pooled buffers */
#endif

#ifndef TELNETTY_BUFFER_CHAIN_MAX
#define TELNETTY_BUFFER_CHAIN_MAX 16    /* Maximum buffers in a chain */
#endif

#ifndef TELNETTY_BUFFER_ALIGN
#define TELNETTY_BUFFER_ALIGN 64        /* Memory alignment for buffers */
#endif

/* Enable zero-copy operations by default */
#ifndef TELNETTY_BUFFER_DISABLE_ZERO_COPY
#define TELNETTY_BUFFER_ENABLE_ZERO_COPY 1
#endif

/* ============================================================================
 * Buffer Types and Structures
 * ============================================================================ */

/* Buffer allocation strategies */
typedef enum {
    TELNETTY_BUFFER_ALLOC_STATIC = 0,     /**< Static buffer, no allocation */
    TELNETTY_BUFFER_ALLOC_DYNAMIC = 1,    /**< Dynamic allocation */
    TELNETTY_BUFFER_ALLOC_POOLED = 2,     /**< Pooled allocation */
    TELNETTY_BUFFER_ALLOC_ALIGNED = 3     /**< Aligned allocation */
} telnetty_buffer_alloc_t;

/* Buffer chain structure */
typedef struct telnetty_buffer_chain {
    telnetty_buffer_t* head;              /**< First buffer in chain */
    telnetty_buffer_t* tail;              /**< Last buffer in chain */
    size_t total_length;                /**< Total data length */
    size_t buffer_count;                /**< Number of buffers */
    int flags;                          /**< Chain flags */
} telnetty_buffer_chain_t;

/* Buffer chain flags */
#define TELNETTY_CHAIN_FLAG_READONLY  0x01    /**< Chain is read-only */
#define TELNETTY_CHAIN_FLAG_MUTABLE   0x02    /**< Chain can be modified */

/* Buffer pool structure */
typedef struct telnetty_buffer_pool {
    telnetty_buffer_t** buffers;          /**< Array of buffer pointers */
    size_t capacity;                    /**< Pool capacity */
    size_t count;                       /**< Current available count */
    size_t max_size;                    /**< Maximum buffer size */
    int flags;                          /**< Pool flags */
    void* user_data;                    /**< User data for callbacks */
} telnetty_buffer_pool_t;

/* Pool flags */
#define TELNETTY_POOL_FLAG_THREAD_SAFE 0x01    /**< [DEPRECATED, NO EFFECT]
                                                * One telnetty_context_t per
                                                * thread — the library does
                                                * not lock on this flag.
                                                * Retained for source
                                                * compatibility. */
#define TELNETTY_POOL_FLAG_ZERO_MEMORY 0x02    /**< Zero memory on allocation */

/* Buffer statistics */
typedef struct {
    size_t total_allocated;             /**< Total memory allocated */
    size_t total_used;                  /**< Total memory currently used */
    size_t pool_hits;                   /**< Successful pool allocations */
    size_t pool_misses;                 /**< Failed pool allocations */
    size_t allocations;                 /**< Total allocations */
    size_t deallocations;               /**< Total deallocations */
    size_t chains_created;              /**< Buffer chains created */
    size_t chains_destroyed;            /**< Buffer chains destroyed */
} telnetty_buffer_stats_t;

/* ============================================================================
 * Buffer Pool API
 * ============================================================================ */

#ifdef TELNETTY_BUFFER_IMPLEMENTATION
/**
 * Create a buffer pool
 * 
 * @param capacity Number of buffers in pool
 * @param buffer_size Size of each buffer
 * @param flags Pool configuration flags
 * @return New buffer pool or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_pool_t* telnetty_buffer_pool_create(
    size_t capacity,
    size_t buffer_size,
    int flags
);

/**
 * Destroy a buffer pool and free all resources
 * 
 * @param pool Buffer pool to destroy
 */
static TELNETTY_UNUSED void telnetty_buffer_pool_destroy(telnetty_buffer_pool_t* pool);

/**
 * Get a buffer from the pool
 * 
 * @param pool Buffer pool
 * @return Buffer from pool or NULL if pool empty
 */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_pool_get(telnetty_buffer_pool_t* pool);

/**
 * Return a buffer to the pool
 * 
 * @param pool Buffer pool
 * @param buffer Buffer to return
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_pool_put(telnetty_buffer_pool_t* pool, telnetty_buffer_t* buffer);

/**
 * Get buffer pool statistics
 * 
 * @param pool Buffer pool
 * @param stats Output statistics
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_pool_get_stats(
    telnetty_buffer_pool_t* pool,
    telnetty_buffer_stats_t* stats
);

/* ============================================================================
 * Buffer Chain API
 * ============================================================================ */

/**
 * Create a buffer chain
 * 
 * @return New buffer chain or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_chain_t* telnetty_buffer_chain_create(void);

/**
 * Destroy a buffer chain and all contained buffers
 * 
 * @param chain Buffer chain to destroy
 */
static TELNETTY_UNUSED void telnetty_buffer_chain_destroy(telnetty_buffer_chain_t* chain);

/**
 * Append a buffer to a chain
 * 
 * @param chain Buffer chain
 * @param buffer Buffer to append
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_chain_append(
    telnetty_buffer_chain_t* chain,
    telnetty_buffer_t* buffer
);

/**
 * Prepend a buffer to a chain
 * 
 * @param chain Buffer chain
 * @param buffer Buffer to prepend
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_chain_prepend(
    telnetty_buffer_chain_t* chain,
    telnetty_buffer_t* buffer
);

/**
 * Get total length of all data in chain
 * 
 * @param chain Buffer chain
 * @return Total data length
 */
static TELNETTY_UNUSED size_t telnetty_buffer_chain_length(telnetty_buffer_chain_t* chain);

/**
 * Flatten buffer chain into single buffer
 * 
 * @param chain Buffer chain
 * @param pool Optional buffer pool for allocation
 * @return Flattened buffer or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_chain_flatten(
    telnetty_buffer_chain_t* chain,
    telnetty_buffer_pool_t* pool
);

/**
 * Create a view of buffer chain (zero-copy)
 * 
 * @param chain Buffer chain
 * @param offset Starting offset
 * @param length Length of view (0 for all)
 * @return Buffer chain view or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_chain_t* telnetty_buffer_chain_view(
    telnetty_buffer_chain_t* chain,
    size_t offset,
    size_t length
);

/**
 * Split buffer chain at specified position
 * 
 * @param chain Buffer chain to split
 * @param position Position to split at
 * @return New chain with remaining data or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_chain_t* telnetty_buffer_chain_split(
    telnetty_buffer_chain_t* chain,
    size_t position
);

/* ============================================================================
 * Advanced Buffer Operations
 * ============================================================================ */

/**
 * Create a buffer with specific allocation strategy
 * 
 * @param size Buffer size
 * @param alloc Allocation strategy
 * @param pool Optional buffer pool
 * @return New buffer or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_create_ex(
    size_t size,
    telnetty_buffer_alloc_t alloc,
    telnetty_buffer_pool_t* pool
);

/**
 * Create a buffer that references existing memory (zero-copy)
 * 
 * @param data Existing data buffer
 * @param size Buffer size
 * @param flags Buffer flags
 * @return New buffer or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_create_reference(
    uint8_t* data,
    size_t size,
    int flags
);

/**
 * Ensure buffer has at least specified capacity
 * 
 * @param buffer Buffer to resize
 * @param minimum_size Minimum required size
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_ensure_capacity(
    telnetty_buffer_t* buffer,
    size_t minimum_size
);

/**
 * Compact buffer by removing consumed data
 * 
 * @param buffer Buffer to compact
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_compact(telnetty_buffer_t* buffer);

/**
 * Copy data from buffer to destination
 * 
 * @param buffer Source buffer
 * @param offset Starting offset
 * @param dest Destination buffer
 * @param length Length to copy
 * @return Number of bytes copied or -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_copy(
    const telnetty_buffer_t* buffer,
    size_t offset,
    uint8_t* dest,
    size_t length
);

/**
 * Search for data in buffer
 * 
 * @param buffer Buffer to search
 * @param data Data to find
 * @param data_length Length of search data
 * @param start_offset Starting offset for search
 * @return Position of data or -1 if not found
 */
static TELNETTY_UNUSED int telnetty_buffer_find(
    const telnetty_buffer_t* buffer,
    const uint8_t* data,
    size_t data_length,
    size_t start_offset
);

/**
 * Compare two buffers
 * 
 * @param buffer1 First buffer
 * @param buffer2 Second buffer
 * @return 0 if equal, non-zero otherwise
 */
static TELNETTY_UNUSED int telnetty_buffer_compare(
    const telnetty_buffer_t* buffer1,
    const telnetty_buffer_t* buffer2
);

/**
 * Clone a buffer
 * 
 * @param buffer Buffer to clone
 * @param pool Optional buffer pool for allocation
 * @return Cloned buffer or NULL on failure
 */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_clone(
    const telnetty_buffer_t* buffer,
    telnetty_buffer_pool_t* pool
);

/**
 * Append data from another buffer
 * 
 * @param dest Destination buffer
 * @param src Source buffer
 * @param offset Starting offset in source
 * @param length Length to append (0 for all)
 * @return Number of bytes appended or -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_append_buffer(
    telnetty_buffer_t* dest,
    const telnetty_buffer_t* src,
    size_t offset,
    size_t length
);

/* ============================================================================
 * Statistics and Debugging
 * ============================================================================ */

/**
 * Get global buffer statistics
 * 
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_buffer_get_global_stats(telnetty_buffer_stats_t* stats);

/**
 * Reset global buffer statistics
 */
static TELNETTY_UNUSED void telnetty_buffer_reset_global_stats(void);

/**
 * Print buffer statistics for debugging
 * 
 * @param stats Statistics to print
 * @param prefix Optional prefix for output
 */
static TELNETTY_UNUSED void telnetty_buffer_print_stats(
    const telnetty_buffer_stats_t* stats,
    const char* prefix
);

/**
 * Validate buffer integrity
 * 
 * @param buffer Buffer to validate
 * @return true if buffer is valid, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_buffer_validate(const telnetty_buffer_t* buffer);

/**
 * Validate buffer chain integrity
 * 
 * @param chain Buffer chain to validate
 * @return true if chain is valid, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_buffer_chain_validate(const telnetty_buffer_chain_t* chain);
#endif /* TELNETTY_BUFFER_IMPLEMENTATION */

/* ============================================================================
 * Inline Helper Functions
 * ============================================================================ */

/**
 * Get available space in buffer
 */
static inline TELNETTY_UNUSED size_t telnetty_buffer_available(const telnetty_buffer_t* buffer) {
    return buffer ? buffer->size - buffer->length : 0;
}

/**
 * Check if buffer is empty
 */
static inline TELNETTY_UNUSED bool telnetty_buffer_is_empty(const telnetty_buffer_t* buffer) {
    return buffer ? buffer->length == 0 : true;
}

/**
 * Check if buffer is full
 */
static inline TELNETTY_UNUSED bool telnetty_buffer_is_full(const telnetty_buffer_t* buffer) {
    return buffer ? buffer->length >= buffer->size : true;
}

/**
 * Get buffer utilization percentage
 */
static inline TELNETTY_UNUSED double telnetty_buffer_utilization(const telnetty_buffer_t* buffer) {
    return buffer ? (double)buffer->length / buffer->size * 100.0 : 0.0;
}

/**
 * Reset buffer without freeing memory
 */
static inline TELNETTY_UNUSED void telnetty_buffer_reset_ex(telnetty_buffer_t* buffer) {
    if (buffer) {
        buffer->length = 0;
        buffer->offset = 0;
    }
}

/**
 * Advance buffer offset
 */
static inline TELNETTY_UNUSED int telnetty_buffer_advance(telnetty_buffer_t* buffer, size_t count) {
    if (!buffer || buffer->offset + count > buffer->length) return -1;
    buffer->offset += count;
    return 0;
}

/**
 * Rewind buffer offset
 */
static inline TELNETTY_UNUSED int telnetty_buffer_rewind(telnetty_buffer_t* buffer, size_t count) {
    if (!buffer || buffer->offset < count) return -1;
    buffer->offset -= count;
    return 0;
}

/**
 * Get remaining data in buffer
 */
static inline TELNETTY_UNUSED size_t telnetty_buffer_remaining(const telnetty_buffer_t* buffer) {
    return buffer ? buffer->length - buffer->offset : 0;
}

/**
 * Get current read position
 */
static inline TELNETTY_UNUSED const uint8_t* telnetty_buffer_position(const telnetty_buffer_t* buffer) {
    return buffer && buffer->data ? buffer->data + buffer->offset : NULL;
}

/**
 * Check if buffer chain is empty
 */
static inline TELNETTY_UNUSED bool telnetty_buffer_chain_is_empty(const telnetty_buffer_chain_t* chain) {
    return chain ? chain->total_length == 0 : true;
}

/**
 * Get buffer chain buffer count
 */
static inline TELNETTY_UNUSED size_t telnetty_buffer_chain_count(const telnetty_buffer_chain_t* chain) {
    return chain ? chain->buffer_count : 0;
}

/* ============================================================================
 * Implementation
 * ============================================================================ */

#ifdef TELNETTY_BUFFER_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Memory allocation wrappers */
#ifndef TELNETTY_BUFFER_MALLOC
#define TELNETTY_BUFFER_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_BUFFER_FREE
#define TELNETTY_BUFFER_FREE(ptr) free(ptr)
#endif

#ifndef TELNETTY_BUFFER_REALLOC
#define TELNETTY_BUFFER_REALLOC(ptr, size) realloc(ptr, size)
#endif

/* Global statistics */
static TELNETTY_UNUSED telnetty_buffer_stats_t g_buffer_stats; /* zero-initialized (static storage) */

/* ============================================================================
 * Buffer Pool Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_buffer_pool_t* telnetty_buffer_pool_create(
    size_t capacity,
    size_t buffer_size,
    int flags
) {
    telnetty_buffer_pool_t* pool = (telnetty_buffer_pool_t*)TELNETTY_BUFFER_MALLOC(sizeof(telnetty_buffer_pool_t));
    if (!pool) return NULL;
    
    memset(pool, 0, sizeof(telnetty_buffer_pool_t));
    
    pool->capacity = capacity;
    pool->count = 0;
    pool->max_size = buffer_size;
    pool->flags = flags;
    
    /* Allocate buffer array */
    pool->buffers = (telnetty_buffer_t**)TELNETTY_BUFFER_MALLOC(capacity * sizeof(telnetty_buffer_t*));
    if (!pool->buffers) {
        TELNETTY_BUFFER_FREE(pool);
        return NULL;
    }
    
    /* Pre-allocate buffers — pack densely so pool_get's LIFO index is valid.
     * On any allocation failure, tear down cleanly rather than leaving NULL
     * gaps that would later cause a NULL-deref from pool_get. */
    for (size_t i = 0; i < capacity; i++) {
        telnetty_buffer_t* buf = telnetty_buffer_create_ex(buffer_size, TELNETTY_BUFFER_ALLOC_DYNAMIC, NULL);
        if (!buf) {
            for (size_t j = 0; j < pool->count; j++) {
                telnetty_buffer_destroy(pool->buffers[j]);
            }
            TELNETTY_BUFFER_FREE(pool->buffers);
            TELNETTY_BUFFER_FREE(pool);
            return NULL;
        }
        pool->buffers[pool->count++] = buf;
    }

    /* Update statistics */
    g_buffer_stats.total_allocated += pool->count * buffer_size;
    g_buffer_stats.allocations += pool->count;

    return pool;
}

static TELNETTY_UNUSED void telnetty_buffer_pool_destroy(telnetty_buffer_pool_t* pool) {
    if (!pool) return;
    
    /* Free all buffers in pool */
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->buffers[i]) {
            telnetty_buffer_destroy(pool->buffers[i]);
        }
    }
    
    TELNETTY_BUFFER_FREE(pool->buffers);
    TELNETTY_BUFFER_FREE(pool);
}

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_pool_get(telnetty_buffer_pool_t* pool) {
    if (!pool) return NULL;
    if (pool->count == 0) {
        /* Pool empty — fall back to a fresh allocation of the pool's own
         * buffer size. Callers can still `pool_put` the result later, up to
         * `capacity`. Miss counted so users can spot under-sized pools. */
        g_buffer_stats.pool_misses++;
        return telnetty_buffer_create_ex(pool->max_size, TELNETTY_BUFFER_ALLOC_DYNAMIC, NULL);
    }

    telnetty_buffer_t* buffer = pool->buffers[--pool->count];
    g_buffer_stats.pool_hits++;

    telnetty_buffer_reset_ex(buffer);
    return buffer;
}

static TELNETTY_UNUSED int telnetty_buffer_pool_put(telnetty_buffer_pool_t* pool, telnetty_buffer_t* buffer) {
    if (!pool || !buffer || pool->count >= pool->capacity) {
        return -1;
    }
    
    /* Reset buffer before returning to pool */
    telnetty_buffer_reset_ex(buffer);
    
    /* Return to pool */
    pool->buffers[pool->count++] = buffer;
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_buffer_pool_get_stats(
    telnetty_buffer_pool_t* pool,
    telnetty_buffer_stats_t* stats
) {
    if (!pool || !stats) return -1;
    
    /* Calculate pool-specific statistics */
    stats->total_allocated = pool->capacity * pool->max_size;
    stats->total_used = (pool->capacity - pool->count) * pool->max_size;
    stats->pool_hits = g_buffer_stats.pool_hits;
    stats->pool_misses = g_buffer_stats.pool_misses;
    
    return 0;
}

/* ============================================================================
 * Buffer Chain Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_buffer_chain_t* telnetty_buffer_chain_create(void) {
    telnetty_buffer_chain_t* chain = (telnetty_buffer_chain_t*)TELNETTY_BUFFER_MALLOC(sizeof(telnetty_buffer_chain_t));
    if (!chain) return NULL;
    
    memset(chain, 0, sizeof(telnetty_buffer_chain_t));
    chain->flags = TELNETTY_CHAIN_FLAG_MUTABLE;
    
    g_buffer_stats.chains_created++;
    
    return chain;
}

static TELNETTY_UNUSED void telnetty_buffer_chain_destroy(telnetty_buffer_chain_t* chain) {
    if (!chain) return;
    
    /* Destroy all buffers in chain */
    telnetty_buffer_t* buffer = chain->head;
    while (buffer) {
        telnetty_buffer_t* next = buffer->next;
        telnetty_buffer_destroy(buffer);
        buffer = next;
    }
    
    TELNETTY_BUFFER_FREE(chain);
    
    g_buffer_stats.chains_destroyed++;
}

static TELNETTY_UNUSED int telnetty_buffer_chain_append(
    telnetty_buffer_chain_t* chain,
    telnetty_buffer_t* buffer
) {
    if (!chain || !buffer || chain->buffer_count >= TELNETTY_BUFFER_CHAIN_MAX) {
        return -1;
    }
    
    /* Link buffer to chain */
    buffer->next = NULL;
    
    if (!chain->head) {
        /* Empty chain */
        chain->head = chain->tail = buffer;
    } else {
        /* Append to end */
        chain->tail->next = buffer;
        chain->tail = buffer;
    }
    
    /* Update chain statistics */
    chain->total_length += telnetty_buffer_remaining(buffer);
    chain->buffer_count++;
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_buffer_chain_prepend(
    telnetty_buffer_chain_t* chain,
    telnetty_buffer_t* buffer
) {
    if (!chain || !buffer || chain->buffer_count >= TELNETTY_BUFFER_CHAIN_MAX) {
        return -1;
    }
    
    /* Link buffer to chain */
    buffer->next = chain->head;
    chain->head = buffer;
    
    if (!chain->tail) {
        chain->tail = buffer;
    }
    
    /* Update chain statistics */
    chain->total_length += telnetty_buffer_remaining(buffer);
    chain->buffer_count++;
    
    return 0;
}

static TELNETTY_UNUSED size_t telnetty_buffer_chain_length(telnetty_buffer_chain_t* chain) {
    return chain ? chain->total_length : 0;
}

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_chain_flatten(
    telnetty_buffer_chain_t* chain,
    telnetty_buffer_pool_t* pool
) {
    if (!chain || !chain->head) return NULL;
    
    /* Create single buffer for all data */
    telnetty_buffer_t* flat_buffer = pool ? 
        telnetty_buffer_pool_get(pool) : 
        telnetty_buffer_create_ex(chain->total_length, TELNETTY_BUFFER_ALLOC_DYNAMIC, NULL);
    
    if (!flat_buffer) return NULL;
    
    /* Copy all data from chain */
    telnetty_buffer_t* current = chain->head;
    size_t offset = 0;
    
    while (current) {
        size_t remaining = telnetty_buffer_remaining(current);
        if (remaining > 0) {
            memcpy(flat_buffer->data + offset, 
                   telnetty_buffer_position(current), 
                   remaining);
            offset += remaining;
        }
        current = current->next;
    }
    
    flat_buffer->length = offset;
    
    return flat_buffer;
}

static TELNETTY_UNUSED telnetty_buffer_chain_t* telnetty_buffer_chain_view(
    telnetty_buffer_chain_t* chain,
    size_t offset,
    size_t length
) {
    if (!chain || offset >= chain->total_length) return NULL;
    
    /* Create view chain */
    telnetty_buffer_chain_t* view = telnetty_buffer_chain_create();
    if (!view) return NULL;
    
    view->flags |= TELNETTY_CHAIN_FLAG_READONLY;
    
    /* Find starting position */
    telnetty_buffer_t* current = chain->head;
    size_t current_offset = 0;
    
    while (current && current_offset + telnetty_buffer_remaining(current) <= offset) {
        current_offset += telnetty_buffer_remaining(current);
        current = current->next;
    }
    
    if (!current) {
        telnetty_buffer_chain_destroy(view);
        return NULL;
    }
    
    /* Create buffer references for the view */
    size_t remaining_length = length > 0 ? length : (chain->total_length - offset);
    
    while (current && remaining_length > 0) {
        size_t buffer_remaining = telnetty_buffer_remaining(current);
        size_t skip = (current_offset < offset) ? (offset - current_offset) : 0;
        
        if (skip < buffer_remaining) {
            /* Create reference buffer for the view */
            telnetty_buffer_t* ref = telnetty_buffer_create_reference(
                (uint8_t*)telnetty_buffer_position(current) + skip,
                buffer_remaining - skip,
                TELNETTY_BUFFER_FLAG_STATIC
            );
            
            if (ref) {
                /* Ensure we don't exceed requested length */
                if (remaining_length < ref->length) {
                    ref->length = remaining_length;
                }
                
                telnetty_buffer_chain_append(view, ref);
                remaining_length -= ref->length;
            }
        }
        
        current_offset += buffer_remaining;
        current = current->next;
    }
    
    return view;
}

static TELNETTY_UNUSED telnetty_buffer_chain_t* telnetty_buffer_chain_split(
    telnetty_buffer_chain_t* chain,
    size_t position
) {
    if (!chain || position >= chain->total_length) return NULL;

    /* Create new chain for the split */
    telnetty_buffer_chain_t* new_chain = telnetty_buffer_chain_create();
    if (!new_chain) return NULL;

    /* Snapshot the original tail before we mutate chain->tail below.
     * Prior versions read chain->tail after reassigning it, which pointed
     * new_chain->tail at the wrong end of the split. */
    telnetty_buffer_t* orig_tail = chain->tail;

    /* Find split position */
    telnetty_buffer_t* current = chain->head;
    size_t current_pos = 0;
    telnetty_buffer_t* prev = NULL;

    while (current && current_pos + telnetty_buffer_remaining(current) <= position) {
        current_pos += telnetty_buffer_remaining(current);
        prev = current;
        current = current->next;
    }

    if (!current) {
        telnetty_buffer_chain_destroy(new_chain);
        return NULL;
    }

    if (current_pos == position) {
        /* Clean split between whole buffers. */
        if (prev) {
            prev->next = NULL;
            chain->tail = prev;
        } else {
            chain->head = NULL;
            chain->tail = NULL;
        }

        new_chain->head = current;
        new_chain->tail = orig_tail;
    } else {
        /* Split within a buffer: cut `current` at (position - current_pos)
         * bytes past its read cursor. Left half stays on `chain`; right half
         * is a fresh dynamic buffer that leads `new_chain`. */
        size_t cut = position - current_pos;
        size_t rem = telnetty_buffer_remaining(current);
        size_t right_len = rem - cut;

        telnetty_buffer_t* right = telnetty_buffer_create(right_len ? right_len : 1);
        if (!right) {
            telnetty_buffer_chain_destroy(new_chain);
            return NULL;
        }
        if (right_len > 0) {
            memcpy(right->data,
                   (uint8_t*)telnetty_buffer_position(current) + cut,
                   right_len);
            right->length = right_len;
        }

        /* Truncate `current` to hold only its left half. */
        current->length = current->offset + cut;

        right->next = current->next;
        current->next = NULL;
        chain->tail = current;

        new_chain->head = right;
        new_chain->tail = right->next ? orig_tail : right;
    }

    /* Recount buffers on both sides. */
    size_t new_count = 0;
    for (telnetty_buffer_t* t = new_chain->head; t; t = t->next) new_count++;
    size_t old_count = 0;
    for (telnetty_buffer_t* t = chain->head; t; t = t->next) old_count++;
    new_chain->buffer_count = new_count;
    chain->buffer_count = old_count;

    size_t remaining_length = chain->total_length - position;
    new_chain->total_length = remaining_length;
    chain->total_length = position;

    return new_chain;
}

/* ============================================================================
 * Advanced Buffer Operations Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_create_ex(
    size_t size,
    telnetty_buffer_alloc_t alloc,
    telnetty_buffer_pool_t* pool
) {
    telnetty_buffer_t* buffer = NULL;
    
    switch (alloc) {
        case TELNETTY_BUFFER_ALLOC_STATIC:
            /* Create buffer structure only */
            buffer = (telnetty_buffer_t*)TELNETTY_BUFFER_MALLOC(sizeof(telnetty_buffer_t));
            if (!buffer) return NULL;
            memset(buffer, 0, sizeof(telnetty_buffer_t));
            buffer->flags = TELNETTY_BUFFER_FLAG_STATIC;
            break;
            
        case TELNETTY_BUFFER_ALLOC_DYNAMIC:
            /* Use standard allocation */
            buffer = telnetty_buffer_create(size);
            if (buffer) {
                buffer->flags = TELNETTY_BUFFER_FLAG_DYNAMIC;
            }
            break;
            
        case TELNETTY_BUFFER_ALLOC_POOLED:
            /* Try to get from pool first */
            if (pool) {
                buffer = telnetty_buffer_pool_get(pool);
            }
            if (!buffer) {
                /* Fall back to dynamic allocation */
                buffer = telnetty_buffer_create(size);
                if (buffer) {
                    buffer->flags = TELNETTY_BUFFER_FLAG_DYNAMIC;
                }
            } else {
                buffer->flags = TELNETTY_BUFFER_FLAG_DYNAMIC; /* Pool buffers are dynamic */
            }
            break;
            
        case TELNETTY_BUFFER_ALLOC_ALIGNED:
            /* Allocate aligned memory */
            buffer = (telnetty_buffer_t*)TELNETTY_BUFFER_MALLOC(sizeof(telnetty_buffer_t));
            if (!buffer) return NULL;
            
            /* Allocate aligned data buffer */
            /* The data is released with free() like every other dynamic
             * buffer, so only allocators whose memory free() accepts are
             * usable: C11 aligned_alloc (size rounded up to a multiple of
             * the alignment), else plain malloc's natural alignment. This
             * replaces posix_memalign (not C99) and _aligned_malloc, whose
             * memory must not be passed to free(). */
            void* aligned_data = NULL;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(_WIN32)
            {
                size_t rounded = (size + TELNETTY_BUFFER_ALIGN - 1)
                               / TELNETTY_BUFFER_ALIGN * TELNETTY_BUFFER_ALIGN;
                aligned_data = aligned_alloc(TELNETTY_BUFFER_ALIGN, rounded);
            }
#else
            aligned_data = malloc(size);
#endif
            
            if (!aligned_data) {
                TELNETTY_BUFFER_FREE(buffer);
                return NULL;
            }
            
            buffer->data = (uint8_t*)aligned_data;
            buffer->size = size;
            buffer->length = 0;
            buffer->offset = 0;
            buffer->next = NULL;
            buffer->flags = TELNETTY_BUFFER_FLAG_DYNAMIC;
            break;
    }
    
    if (buffer && alloc != TELNETTY_BUFFER_ALLOC_STATIC) {
        g_buffer_stats.allocations++;
        g_buffer_stats.total_allocated += size;
    }
    
    return buffer;
}

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_create_reference(
    uint8_t* data,
    size_t size,
    int flags
) {
    telnetty_buffer_t* buffer = (telnetty_buffer_t*)TELNETTY_BUFFER_MALLOC(sizeof(telnetty_buffer_t));
    if (!buffer) return NULL;
    
    buffer->data = data;
    buffer->size = size;
    buffer->length = size;
    buffer->offset = 0;
    buffer->next = NULL;
    buffer->flags = flags | TELNETTY_BUFFER_FLAG_STATIC;
    
    return buffer;
}

static TELNETTY_UNUSED int telnetty_buffer_ensure_capacity(
    telnetty_buffer_t* buffer,
    size_t minimum_size
) {
    if (!buffer || buffer->size >= minimum_size) return 0;
    
    /* Don't resize static buffers */
    if (buffer->flags & TELNETTY_BUFFER_FLAG_STATIC) return -1;
    
    /* Calculate new size. Clamp before doubling so a zero-size buffer
     * (e.g. from a reference view) does not spin forever at 0 * 2 = 0. */
    size_t new_size = buffer->size;
    if (new_size == 0) new_size = TELNETTY_INITIAL_BUFFER_SIZE;
    while (new_size < minimum_size) {
        new_size *= 2;
    }
    
    /* Reallocate */
    uint8_t* new_data = (uint8_t*)TELNETTY_BUFFER_REALLOC(buffer->data, new_size);
    if (!new_data) return -1;
    
    g_buffer_stats.total_allocated += (new_size - buffer->size);
    
    buffer->data = new_data;
    buffer->size = new_size;
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_buffer_compact(telnetty_buffer_t* buffer) {
    if (!buffer || buffer->offset == 0) return 0;
    
    size_t remaining = telnetty_buffer_remaining(buffer);
    if (remaining > 0) {
        memmove(buffer->data, buffer->data + buffer->offset, remaining);
    }
    
    buffer->length = remaining;
    buffer->offset = 0;
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_buffer_copy(
    const telnetty_buffer_t* buffer,
    size_t offset,
    uint8_t* dest,
    size_t length
) {
    if (!buffer || !dest) return -1;
    
    size_t available = buffer->length - offset;
    size_t to_copy = (length < available) ? length : available;
    
    if (to_copy > 0) {
        memcpy(dest, buffer->data + offset, to_copy);
    }
    
    return (int)to_copy;
}

static TELNETTY_UNUSED int telnetty_buffer_find(
    const telnetty_buffer_t* buffer,
    const uint8_t* data,
    size_t data_length,
    size_t start_offset
) {
    if (!buffer || !data || data_length == 0 || start_offset >= buffer->length) {
        return -1;
    }
    
    const uint8_t* start = buffer->data + start_offset;
    size_t search_length = buffer->length - start_offset;
    
    if (data_length > search_length) return -1;

    /* memchr on the first byte, memcmp on the tail — turns the naive O(nm)
     * scan into O(n) in the common case where the leading byte is rare. */
    const uint8_t first = data[0];
    const uint8_t* cursor = start;
    size_t remaining = search_length - data_length + 1;
    while (remaining > 0) {
        const uint8_t* hit = (const uint8_t*)memchr(cursor, first, remaining);
        if (!hit) return -1;
        if (data_length == 1 || memcmp(hit, data, data_length) == 0) {
            return (int)(start_offset + (hit - start));
        }
        size_t consumed = (size_t)(hit - cursor) + 1;
        if (consumed >= remaining) return -1;
        remaining -= consumed;
        cursor = hit + 1;
    }
    return -1;
}

static TELNETTY_UNUSED int telnetty_buffer_compare(
    const telnetty_buffer_t* buffer1,
    const telnetty_buffer_t* buffer2
) {
    if (!buffer1 && !buffer2) return 0;
    if (!buffer1) return -1;
    if (!buffer2) return 1;
    
    size_t len1 = buffer1->length;
    size_t len2 = buffer2->length;
    
    if (len1 != len2) return (len1 < len2) ? -1 : 1;
    
    return memcmp(buffer1->data, buffer2->data, len1);
}

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_clone(
    const telnetty_buffer_t* buffer,
    telnetty_buffer_pool_t* pool
) {
    if (!buffer) return NULL;
    
    size_t size = telnetty_buffer_remaining(buffer);
    telnetty_buffer_t* clone = pool ? 
        telnetty_buffer_pool_get(pool) : 
        telnetty_buffer_create_ex(size, TELNETTY_BUFFER_ALLOC_DYNAMIC, NULL);
    
    if (!clone) return NULL;
    
    /* Copy data */
    const uint8_t* src = telnetty_buffer_position(buffer);
    memcpy(clone->data, src, size);
    clone->length = size;
    
    return clone;
}

static TELNETTY_UNUSED int telnetty_buffer_append_buffer(
    telnetty_buffer_t* dest,
    const telnetty_buffer_t* src,
    size_t offset,
    size_t length
) {
    if (!dest || !src) return -1;
    
    size_t available = src->length - offset;
    size_t to_append = (length == 0 || length > available) ? available : length;
    
    if (to_append == 0) return 0;
    
    /* Ensure destination has enough capacity */
    if (telnetty_buffer_ensure_capacity(dest, dest->length + to_append) != 0) {
        return -1;
    }
    
    /* Append data */
    memcpy(dest->data + dest->length, src->data + offset, to_append);
    dest->length += to_append;
    
    return (int)to_append;
}

/* ============================================================================
 * Statistics and Debugging Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_buffer_get_global_stats(telnetty_buffer_stats_t* stats) {
    if (!stats) return -1;
    
    memcpy(stats, &g_buffer_stats, sizeof(telnetty_buffer_stats_t));
    
    return 0;
}

static TELNETTY_UNUSED void telnetty_buffer_reset_global_stats(void) {
    memset(&g_buffer_stats, 0, sizeof(telnetty_buffer_stats_t));
}

static TELNETTY_UNUSED void telnetty_buffer_print_stats(
    const telnetty_buffer_stats_t* stats,
    const char* prefix
) {
    if (!stats) return;
    
    const char* pre = prefix ? prefix : "";
    
    printf("%sBuffer Statistics:\n", pre);
    printf("%s  Memory: %zu allocated, %zu used (%.1f%%)\n", 
           pre, stats->total_allocated, stats->total_used,
           stats->total_allocated > 0 ? 
           (double)stats->total_used / stats->total_allocated * 100.0 : 0.0);
    printf("%s  Pool: %zu hits, %zu misses (%.1f%% hit rate)\n",
           pre, stats->pool_hits, stats->pool_misses,
           stats->pool_hits + stats->pool_misses > 0 ?
           (double)stats->pool_hits / (stats->pool_hits + stats->pool_misses) * 100.0 : 0.0);
    printf("%s  Operations: %zu allocations, %zu deallocations\n",
           pre, stats->allocations, stats->deallocations);
    printf("%s  Chains: %zu created, %zu destroyed\n",
           pre, stats->chains_created, stats->chains_destroyed);
}

static TELNETTY_UNUSED bool telnetty_buffer_validate(const telnetty_buffer_t* buffer) {
    if (!buffer) return false;
    
    /* Basic validation */
    if (buffer->length > buffer->size) return false;
    if (buffer->offset > buffer->length) return false;
    if (!buffer->data && buffer->size > 0) return false;
    
    return true;
}

static TELNETTY_UNUSED bool telnetty_buffer_chain_validate(const telnetty_buffer_chain_t* chain) {
    if (!chain) return false;
    
    /* Validate chain structure */
    if (chain->buffer_count > TELNETTY_BUFFER_CHAIN_MAX) return false;
    
    /* Validate buffer count matches actual count */
    size_t actual_count = 0;
    size_t actual_length = 0;
    telnetty_buffer_t* current = chain->head;
    
    while (current) {
        if (!telnetty_buffer_validate(current)) return false;
        actual_length += telnetty_buffer_remaining(current);
        actual_count++;
        current = current->next;
    }
    
    if (actual_count != chain->buffer_count) return false;
    if (actual_length != chain->total_length) return false;
    
    return true;
}

#endif /* TELNETTY_BUFFER_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_BUFFER_H */