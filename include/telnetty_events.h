/**
 * @file telnetty_events.h
 * @brief Event-driven architecture for TELNET protocol handling
 * 
 * This header extends the core TELNET implementation with a rich event system
 * that allows for clean separation of concerns and easy protocol extension.
 * 
 * Features:
 * - Extensible event system
 * - Event queuing and prioritization
 * - Custom event support
 * - Thread-safe event handling (optional)
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.2.0
 * 
 */

#ifndef TELNETTY_EVENTS_H
#define TELNETTY_EVENTS_H

#include "telnetty_core.h"
#include <stdarg.h>
#include <time.h>

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
 * Extended Event Types
 * ============================================================================ */

/* Extended event types for advanced protocol handling */
typedef enum {
    /* Core events (defined in telnetty_core.h) */
    TELNETTY_EVENT_EXTENDED_START = 50,   /**< Start of extended events */
    
    /* Connection events */
    TELNETTY_EVENT_CONNECT,               /**< Connection established */
    TELNETTY_EVENT_DISCONNECT,            /**< Connection terminated */
    TELNETTY_EVENT_KEEPALIVE,             /**< Keepalive timeout */
    TELNETTY_EVENT_FLOW_CONTROL,          /**< Flow control event */
    
    /* Protocol negotiation events */
    TELNETTY_EVENT_NEGOTIATION_COMPLETE,  /**< Option negotiation complete */
    TELNETTY_EVENT_NEGOTIATION_FAILED,    /**< Option negotiation failed */
    TELNETTY_EVENT_PROTOCOL_VIOLATION,    /**< Protocol violation detected */
    
    /* Buffer events */
    TELNETTY_EVENT_BUFFER_HIGH,           /**< Buffer high water mark */
    TELNETTY_EVENT_BUFFER_LOW,            /**< Buffer low water mark */
    TELNETTY_EVENT_BUFFER_OVERFLOW        /**< Buffer overflow */
} telnetty_extended_event_type_t;

/* ============================================================================
 * Event Data Structures
 * ============================================================================ */

/* Connection event data */
typedef struct {
    const char* address;                /**< Remote address */
    int port;                           /**< Remote port */
    time_t timestamp;                   /**< Connection time */
} telnetty_event_connect_t;

typedef struct {
    int reason;                         /**< Disconnect reason */
    const char* message;                /**< Optional message */
} telnetty_event_disconnect_t;

/* Negotiation event data */
typedef struct {
    uint8_t option;                     /**< Option being negotiated */
    int attempts;                       /**< Number of attempts */
    time_t start_time;                  /**< Negotiation start time */
} telnetty_event_negotiation_t;

/* Buffer event data (stored in extended area) */
typedef struct {
    size_t current_size;                /**< Current buffer size */
    size_t max_size;                    /**< Maximum buffer size */
    size_t watermark;                   /**< Water mark threshold */
} telnetty_event_buffer_t;

/* ============================================================================
 * Event Queue System
 * ============================================================================ */

/* Event priority levels */
typedef enum {
    TELNETTY_PRIORITY_LOW = 0,            /**< Low priority events */
    TELNETTY_PRIORITY_NORMAL = 1,         /**< Normal priority events */
    TELNETTY_PRIORITY_HIGH = 2,           /**< High priority events */
    TELNETTY_PRIORITY_CRITICAL = 3        /**< Critical priority events */
} telnetty_event_priority_t;

/* Event queue entry */
typedef struct telnetty_event_entry {
    telnetty_event_type_t type;           /**< Event type */
    telnetty_event_priority_t priority;   /**< Event priority */
    time_t timestamp;                   /**< Event timestamp */
    telnetty_event_data_union_t data;     /**< Event data */
    struct telnetty_event_entry* next;    /**< Next event in queue */
} telnetty_event_entry_t;

/* Event queue structure */
typedef struct {
    telnetty_event_entry_t* head;         /**< Queue head */
    telnetty_event_entry_t* tail;         /**< Queue tail */
    size_t count;                       /**< Number of queued events */
    size_t max_size;                    /**< Maximum queue size */
    int flags;                          /**< Queue flags */
} telnetty_event_queue_t;

/* Queue flags */
#define TELNETTY_QUEUE_FLAG_THREAD_SAFE   0x01    /**< [DEPRECATED, NO EFFECT]
                                                   * One telnetty_context_t
                                                   * per thread — the library
                                                   * does not lock on this
                                                   * flag. Retained for
                                                   * source compatibility. */
#define TELNETTY_QUEUE_FLAG_DROP_OLDEST   0x02    /**< Drop oldest on overflow */
#define TELNETTY_QUEUE_FLAG_DROP_NEWEST   0x04    /**< Drop newest on overflow */

/* ============================================================================
 * Event Handler System
 * ============================================================================ */

/* Extended event handler signature */
typedef void (*telnetty_extended_event_callback_t)(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data,
    void* user_data,
    telnetty_event_priority_t priority
);

/* Handler registration entry */
typedef struct telnetty_event_handler {
    telnetty_event_type_t event_type;     /**< Event type to handle */
    telnetty_extended_event_callback_t cb;/**< Handler callback */
    void* user_data;                    /**< User data */
    telnetty_event_priority_t priority;   /**< Handler priority */
    struct telnetty_event_handler* next;  /**< Next handler */
} telnetty_event_handler_t;

/* ============================================================================
 * Event System API
 * ============================================================================ */

#ifdef TELNETTY_EVENTS_IMPLEMENTATION
/**
 * Create an event queue
 * 
 * @param max_size Maximum number of events to queue
 * @param flags Queue configuration flags
 * @return New event queue or NULL on failure
 */
static TELNETTY_UNUSED telnetty_event_queue_t* telnetty_event_queue_create(
    size_t max_size,
    int flags
);

/**
 * Destroy an event queue
 * 
 * @param queue Event queue to destroy
 */
static TELNETTY_UNUSED void telnetty_event_queue_destroy(telnetty_event_queue_t* queue);

/**
 * Queue an event
 * 
 * @param queue Event queue
 * @param type Event type
 * @param priority Event priority
 * @param data Event data (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_event_queue_push(
    telnetty_event_queue_t* queue,
    telnetty_event_type_t type,
    telnetty_event_priority_t priority,
    const telnetty_event_data_union_t* data
);

/**
 * Get next event from queue
 * 
 * @param queue Event queue
 * @param event Output event structure
 * @return 1 if event retrieved, 0 if queue empty, -1 on error
 */
static TELNETTY_UNUSED int telnetty_event_queue_pop(
    telnetty_event_queue_t* queue,
    telnetty_event_entry_t* event
);

/**
 * Peek at next event without removing it
 * 
 * @param queue Event queue
 * @param event Output event structure
 * @return 1 if event available, 0 if queue empty, -1 on error
 */
static TELNETTY_UNUSED int telnetty_event_queue_peek(
    telnetty_event_queue_t* queue,
    telnetty_event_entry_t* event
);

/**
 * Clear all events from queue
 * 
 * @param queue Event queue
 */
static TELNETTY_UNUSED void telnetty_event_queue_clear(telnetty_event_queue_t* queue);

/**
 * Get number of queued events
 * 
 * @param queue Event queue
 * @return Number of events in queue
 */
static TELNETTY_UNUSED size_t telnetty_event_queue_count(telnetty_event_queue_t* queue);

/**
 * Register an event handler
 * 
 * @param ctx TELNET context
 * @param event_type Event type to handle
 * @param cb Handler callback
 * @param user_data User data for handler
 * @param priority Handler priority
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_register_event_handler(
    telnetty_context_t* ctx,
    telnetty_event_type_t event_type,
    telnetty_extended_event_callback_t cb,
    void* user_data,
    telnetty_event_priority_t priority
);

/**
 * Unregister an event handler
 * 
 * @param ctx TELNET context
 * @param event_type Event type
 * @param cb Handler callback to remove
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_unregister_event_handler(
    telnetty_context_t* ctx,
    telnetty_event_type_t event_type,
    telnetty_extended_event_callback_t cb
);

/**
 * Fire an event with priority
 * 
 * @param ctx TELNET context
 * @param type Event type
 * @param priority Event priority
 * @param data Event data
 */
static TELNETTY_UNUSED void telnetty_fire_event_ex(
    telnetty_context_t* ctx,
    telnetty_event_type_t type,
    telnetty_event_priority_t priority,
    const telnetty_event_data_union_t* data
);

/**
 * Compatibility no-op. Events are always dispatched synchronously from
 * telnetty_fire_event / telnetty_process: DATA and SB payloads borrow the
 * caller's buffers, so they cannot safely be queued for later. The
 * standalone telnetty_event_queue_* API is available for application
 * events that own their data.
 * 
 * @param ctx TELNET context
 * @return 0, or -1 if ctx is NULL
 */
static TELNETTY_UNUSED int telnetty_process_events(telnetty_context_t* ctx);

/**
 * Fire a custom event
 * 
 * @param ctx TELNET context
 * @param event_id Custom event ID (>= TELNETTY_EVENT_CUSTOM_START)
 * @param data Custom event data
 * @param priority Event priority
 */
static TELNETTY_UNUSED void telnetty_fire_custom_event(
    telnetty_context_t* ctx,
    int event_id,
    void* data,
    telnetty_event_priority_t priority
);

/**
 * Create a connection event. The address is carried in `data.data`
 * (pointer and length) for compatibility with existing handlers; the port
 * is not represented, so put it in the address string ("host:port") if a
 * handler needs it.
 * 
 * @param address Remote address (borrowed; must outlive the event)
 * @param port Ignored
 * @return Event data union with connection info
 */
static TELNETTY_UNUSED telnetty_event_data_union_t telnetty_event_create_connect(
    const char* address,
    int port
);

/**
 * Create a disconnection event
 * 
 * @param reason Disconnect reason
 * @param message Optional message
 * @return Event data union with disconnect info
 */
static TELNETTY_UNUSED telnetty_event_data_union_t telnetty_event_create_disconnect(
    int reason,
    const char* message
);

/**
 * Create a buffer event
 * 
 * @param current_size Current buffer size
 * @param max_size Maximum buffer size
 * @param watermark Water mark threshold
 * @return Event data union with buffer info
 */
static TELNETTY_UNUSED telnetty_event_data_union_t telnetty_event_create_buffer(
    size_t current_size,
    size_t max_size,
    size_t watermark
);
#endif /* TELNETTY_EVENTS_IMPLEMENTATION */

/* ============================================================================
 * Inline Helper Functions
 * ============================================================================ */

/**
 * Check if event queue is empty
 */
static inline TELNETTY_UNUSED bool telnetty_event_queue_is_empty(telnetty_event_queue_t* queue) {
    return queue ? queue->count == 0 : true;
}

/**
 * Check if event queue is full
 */
static inline TELNETTY_UNUSED bool telnetty_event_queue_is_full(telnetty_event_queue_t* queue) {
    return queue ? queue->count >= queue->max_size : true;
}

/**
 * Get queue capacity
 */
static inline TELNETTY_UNUSED size_t telnetty_event_queue_capacity(telnetty_event_queue_t* queue) {
    return queue ? queue->max_size : 0;
}

/* ============================================================================
 * Implementation
 * ============================================================================ */

#ifdef TELNETTY_EVENTS_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Memory allocation wrappers */
#ifndef TELNETTY_EVENTS_MALLOC
#define TELNETTY_EVENTS_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_EVENTS_FREE
#define TELNETTY_EVENTS_FREE(ptr) free(ptr)
#endif

/* ============================================================================
 * Event Queue Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_event_queue_t* telnetty_event_queue_create(size_t max_size, int flags) {
    telnetty_event_queue_t* queue = (telnetty_event_queue_t*)TELNETTY_EVENTS_MALLOC(sizeof(telnetty_event_queue_t));
    if (!queue) return NULL;
    
    memset(queue, 0, sizeof(telnetty_event_queue_t));
    queue->max_size = max_size > 0 ? max_size : 1000; /* Default max size */
    queue->flags = flags;
    
    return queue;
}

static TELNETTY_UNUSED void telnetty_event_queue_destroy(telnetty_event_queue_t* queue) {
    if (!queue) return;
    
    /* Free all queued events */
    telnetty_event_entry_t* entry = queue->head;
    while (entry) {
        telnetty_event_entry_t* next = entry->next;
        TELNETTY_EVENTS_FREE(entry);
        entry = next;
    }
    
    TELNETTY_EVENTS_FREE(queue);
}

static TELNETTY_UNUSED int telnetty_event_queue_push(
    telnetty_event_queue_t* queue,
    telnetty_event_type_t type,
    telnetty_event_priority_t priority,
    const telnetty_event_data_union_t* data
) {
    if (!queue) return -1;
    
    /* Check if queue is full */
    if (queue->count >= queue->max_size) {
        if (queue->flags & TELNETTY_QUEUE_FLAG_DROP_NEWEST) {
            return -1; /* Drop newest event */
        } else if (queue->flags & TELNETTY_QUEUE_FLAG_DROP_OLDEST) {
            /* Remove oldest event */
            telnetty_event_entry_t* old_head = queue->head;
            if (old_head) {
                queue->head = old_head->next;
                if (!queue->head) queue->tail = NULL;
                TELNETTY_EVENTS_FREE(old_head);
                queue->count--;
            }
        } else {
            return -1; /* Queue overflow */
        }
    }
    
    /* Create new event entry */
    telnetty_event_entry_t* entry = (telnetty_event_entry_t*)TELNETTY_EVENTS_MALLOC(sizeof(telnetty_event_entry_t));
    if (!entry) return -1;
    
    entry->type = type;
    entry->priority = priority;
    entry->timestamp = time(NULL);
    entry->next = NULL;
    
    if (data) {
        entry->data = *data;
    } else {
        memset(&entry->data, 0, sizeof(telnetty_event_data_union_t));
    }
    
    /* Insert into queue based on priority */
    if (!queue->head) {
        /* Empty queue */
        queue->head = queue->tail = entry;
    } else if (priority >= queue->tail->priority) {
        /* Add to end */
        queue->tail->next = entry;
        queue->tail = entry;
    } else {
        /* Find insertion point */
        telnetty_event_entry_t** ptr = &queue->head;
        while (*ptr && (*ptr)->priority > priority) {
            ptr = &(*ptr)->next;
        }
        entry->next = *ptr;
        *ptr = entry;
        if (!entry->next) queue->tail = entry;
    }
    
    queue->count++;
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_event_queue_pop(
    telnetty_event_queue_t* queue,
    telnetty_event_entry_t* event
) {
    if (!queue || !event) return -1;
    
    if (!queue->head) return 0; /* Queue empty */
    
    /* Copy event data */
    telnetty_event_entry_t* entry = queue->head;
    *event = *entry;
    
    /* Remove from queue */
    queue->head = entry->next;
    if (!queue->head) queue->tail = NULL;
    
    TELNETTY_EVENTS_FREE(entry);
    queue->count--;
    
    return 1;
}

static TELNETTY_UNUSED int telnetty_event_queue_peek(
    telnetty_event_queue_t* queue,
    telnetty_event_entry_t* event
) {
    if (!queue || !event) return -1;
    
    if (!queue->head) return 0; /* Queue empty */
    
    *event = *queue->head;
    return 1;
}

static TELNETTY_UNUSED void telnetty_event_queue_clear(telnetty_event_queue_t* queue) {
    if (!queue) return;
    
    telnetty_event_entry_t* entry = queue->head;
    while (entry) {
        telnetty_event_entry_t* next = entry->next;
        TELNETTY_EVENTS_FREE(entry);
        entry = next;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

static TELNETTY_UNUSED size_t telnetty_event_queue_count(telnetty_event_queue_t* queue) {
    return queue ? queue->count : 0;
}

/* ============================================================================
 * Event Handler Implementation
 * ============================================================================ */

/* Free the whole handler chain rooted at `p`. Installed onto
 * telnetty__event_handlers_free_hook so telnetty_destroy can release the
 * list without depending on this header. */
static TELNETTY_UNUSED void telnetty__events_free_chain(void* p) {
    telnetty_event_handler_t* h = (telnetty_event_handler_t*)p;
    while (h) {
        telnetty_event_handler_t* next = h->next;
        TELNETTY_EVENTS_FREE(h);
        h = next;
    }
}

/* Walk the ctx->event_handlers chain, invoking every handler whose
 * event_type matches (or that registered as a wildcard via
 * TELNETTY_EVENT_CUSTOM_START). Installed onto
 * telnetty__extended_dispatch_hook so core telnetty_fire_event routes
 * through us. */
static TELNETTY_UNUSED void telnetty__events_dispatch(
    telnetty_context_t* ctx,
    telnetty_event_type_t type,
    const telnetty_event_data_union_t* data
) {
    if (!ctx || !ctx->event_handlers) return;
    telnetty_event_handler_t* h = (telnetty_event_handler_t*)ctx->event_handlers;
    while (h) {
        telnetty_event_handler_t* next = h->next;
        if (h->event_type == type || h->event_type == TELNETTY_EVENT_CUSTOM_START) {
            h->cb(ctx, type, data, h->user_data, h->priority);
        }
        h = next;
    }
}

/* One-time hook install so telnetty_destroy can walk our list and the core
 * fire_event routes through us. Runs on the first register call — hooks are
 * plain function pointers with no init-order dependency. */
static inline void telnetty__events_ensure_hook(void) {
    if (!telnetty__event_handlers_free_hook)
        telnetty__event_handlers_free_hook = telnetty__events_free_chain;
    if (!telnetty__extended_dispatch_hook)
        telnetty__extended_dispatch_hook = telnetty__events_dispatch;
}

static TELNETTY_UNUSED int telnetty_register_event_handler(
    telnetty_context_t* ctx,
    telnetty_event_type_t event_type,
    telnetty_extended_event_callback_t cb,
    void* user_data,
    telnetty_event_priority_t priority
) {
    if (!ctx || !cb) return -1;

    telnetty_event_handler_t* handler = (telnetty_event_handler_t*)TELNETTY_EVENTS_MALLOC(sizeof(telnetty_event_handler_t));
    if (!handler) return -1;

    handler->event_type = event_type;
    handler->cb = cb;
    handler->user_data = user_data;
    handler->priority = priority;
    handler->next = NULL;

    telnetty__events_ensure_hook();

    /* Priority-ordered insert: CRITICAL > HIGH > NORMAL > LOW; FIFO within
     * the same priority. */
    telnetty_event_handler_t** slot = (telnetty_event_handler_t**)&ctx->event_handlers;
    while (*slot && (*slot)->priority >= priority) {
        slot = &(*slot)->next;
    }
    handler->next = *slot;
    *slot = handler;
    return 0;
}

static TELNETTY_UNUSED int telnetty_unregister_event_handler(
    telnetty_context_t* ctx,
    telnetty_event_type_t event_type,
    telnetty_extended_event_callback_t cb
) {
    if (!ctx || !cb) return -1;
    telnetty_event_handler_t** slot = (telnetty_event_handler_t**)&ctx->event_handlers;
    int removed = 0;
    while (*slot) {
        telnetty_event_handler_t* h = *slot;
        if (h->cb == cb && h->event_type == event_type) {
            *slot = h->next;
            TELNETTY_EVENTS_FREE(h);
            removed++;
            continue;
        }
        slot = &h->next;
    }
    return removed > 0 ? 0 : -1;
}

static TELNETTY_UNUSED void telnetty_fire_event_ex(
    telnetty_context_t* ctx,
    telnetty_event_type_t type,
    telnetty_event_priority_t priority,
    const telnetty_event_data_union_t* data
) {
    (void)priority;
    if (!ctx) return;
    /* telnetty_fire_event runs the basic callback and, via the installed
     * dispatch hook, the extended handler chain. Priority is used at
     * registration time (ordering); at dispatch we honor that order. */
    telnetty_fire_event(ctx, type, data);
}

static TELNETTY_UNUSED int telnetty_process_events(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    return 0;
}

/* Binary-safe custom event fire. Prefer this — the legacy variant below
 * runs strlen() on the caller's pointer, which is UB for non-string data. */
static TELNETTY_UNUSED void telnetty_fire_custom_event_bin(
    telnetty_context_t* ctx,
    int event_id,
    const void* data,
    size_t length,
    telnetty_event_priority_t priority
) {
    if (!ctx || event_id < TELNETTY_EVENT_CUSTOM_START) return;

    telnetty_event_data_union_t event_data;
    event_data.data.data = (const uint8_t*)data;
    event_data.data.length = length;

    telnetty_fire_event_ex(ctx, (telnetty_event_type_t)event_id, priority, &event_data);
}

static TELNETTY_UNUSED void telnetty_fire_custom_event(
    telnetty_context_t* ctx,
    int event_id,
    void* data,
    telnetty_event_priority_t priority
) {
    /* Legacy signal-only path. Callers must NOT rely on the payload length
     * — pass NULL for data, or use telnetty_fire_custom_event_bin() for
     * binary/known-length payloads. */
    telnetty_fire_custom_event_bin(ctx, event_id, data, 0, priority);
}

/* ============================================================================
 * Event Creation Helpers
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_event_data_union_t telnetty_event_create_connect(
    const char* address,
    int port
) {
    (void)port;
    telnetty_event_data_union_t data;
    /* Address in data.data; see the declaration for why port is dropped. */
    data.data.data = (const uint8_t*)address;
    data.data.length = address ? strlen(address) : 0;
    return data;
}

static TELNETTY_UNUSED telnetty_event_data_union_t telnetty_event_create_disconnect(
    int reason,
    const char* message
) {
    telnetty_event_data_union_t data;
    data.error.code = reason;
    data.error.message = message;
    return data;
}

static TELNETTY_UNUSED telnetty_event_data_union_t telnetty_event_create_buffer(
    size_t current_size,
    size_t max_size,
    size_t watermark
) {
    telnetty_event_data_union_t data;
    telnetty_event_buffer_t* buf = (telnetty_event_buffer_t*)&data.ext.data;
    buf->current_size = current_size;
    buf->max_size = max_size;
    buf->watermark = watermark;
    return data;
}

/* ============================================================================
 * Convenience Wrappers
 *
 * These call into the implementation above, so they only exist when
 * TELNETTY_EVENTS_IMPLEMENTATION is defined. Declaring them unconditionally
 * referenced static functions that have no definition in other TUs
 * (-Wundefined-internal).
 * ============================================================================ */

/**
 * Fire a high priority event
 */
static inline void telnetty_fire_event_high(
    telnetty_context_t* ctx,
    telnetty_event_type_t type,
    const telnetty_event_data_union_t* data
) {
    telnetty_fire_event_ex(ctx, type, TELNETTY_PRIORITY_HIGH, data);
}

/**
 * Fire a low priority event
 */
static inline void telnetty_fire_event_low(
    telnetty_context_t* ctx,
    telnetty_event_type_t type,
    const telnetty_event_data_union_t* data
) {
    telnetty_fire_event_ex(ctx, type, TELNETTY_PRIORITY_LOW, data);
}

/**
 * Fire a connection event
 */
static inline void telnetty_fire_connect_event(
    telnetty_context_t* ctx,
    const char* address,
    int port
) {
    telnetty_event_data_union_t data = telnetty_event_create_connect(address, port);
    telnetty_fire_event_ex(ctx, (telnetty_event_type_t)TELNETTY_EVENT_CONNECT, TELNETTY_PRIORITY_NORMAL, &data);
}

/**
 * Fire a disconnection event
 */
static inline void telnetty_fire_disconnect_event(
    telnetty_context_t* ctx,
    int reason,
    const char* message
) {
    telnetty_event_data_union_t data = telnetty_event_create_disconnect(reason, message);
    telnetty_fire_event_ex(ctx, (telnetty_event_type_t)TELNETTY_EVENT_DISCONNECT, TELNETTY_PRIORITY_HIGH, &data);
}

/**
 * Fire a buffer event
 */
static inline void telnetty_fire_buffer_event(
    telnetty_context_t* ctx,
    telnetty_event_type_t type,
    size_t current_size,
    size_t max_size,
    size_t watermark
) {
    telnetty_event_data_union_t data = telnetty_event_create_buffer(current_size, max_size, watermark);
    telnetty_fire_event_ex(ctx, type, TELNETTY_PRIORITY_NORMAL, &data);
}

#endif /* TELNETTY_EVENTS_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_EVENTS_H */