/**
 * @file telnetty_mud.h
 * @brief MUD-specific protocol implementations
 * 
 * This header provides implementations for MUD-specific protocols that extend
 * the base TELNET protocol with features commonly used in Multi-User Dungeons.
 * 
 * Supported Protocols:
 * - MSDP (MUD Server Data Protocol)
 * - GMCP (Generic MUD Communication Protocol)
 * - MTTS (MUD Terminal Type Standard)
 * - MSSP (MUD Server Status Protocol)
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.0.0
 * 
 */

#ifndef TELNETTY_MUD_H
#define TELNETTY_MUD_H

#include "telnetty_core.h"
#include "telnetty_events.h"
#include <stdint.h>
#include <stdbool.h>

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
 * MSDP (MUD Server Data Protocol)
 * ============================================================================ */

/* MSDP option code */
#define TELNETTY_TELOPT_MSDP 69

/* MSDP data types */
typedef enum {
    TELNETTY_MSDP_VAR = 1,                /**< Variable */
    TELNETTY_MSDP_VAL = 2,                /**< Value */
    TELNETTY_MSDP_TABLE_OPEN = 3,         /**< Table open */
    TELNETTY_MSDP_TABLE_CLOSE = 4,        /**< Table close */
    TELNETTY_MSDP_ARRAY_OPEN = 5,         /**< Array open */
    TELNETTY_MSDP_ARRAY_CLOSE = 6         /**< Array close */
} telnetty_msdp_type_t;

/* MSDP variable flags */
typedef enum {
    TELNETTY_MSDP_FLAG_COMMAND = 1 << 0,      /**< Command variable */
    TELNETTY_MSDP_FLAG_LIST = 1 << 1,         /**< List variable */
    TELNETTY_MSDP_FLAG_SENDABLE = 1 << 2,     /**< Sendable variable */
    TELNETTY_MSDP_FLAG_REPORTABLE = 1 << 3,   /**< Reportable variable */
    TELNETTY_MSDP_FLAG_CONFIGURABLE = 1 << 4, /**< Configurable variable */
    TELNETTY_MSDP_FLAG_REPORTED = 1 << 5,     /**< Reported variable */
    TELNETTY_MSDP_FLAG_UPDATED = 1 << 6       /**< Updated variable */
} telnetty_msdp_flag_t;

/* MSDP variable structure */
typedef struct {
    char* name;                         /**< Variable name */
    char* value;                        /**< Variable value */
    uint32_t flags;                     /**< Variable flags */
    time_t last_updated;                /**< Last update time */
} telnetty_msdp_variable_t;

/* MSDP handler function type */
typedef void (*telnetty_msdp_handler_t)(
    telnetty_context_t* ctx,
    const char* var,
    const char* val,
    void* user_data
);

/* Recursive MSDP event type — surfaced to telnetty_msdp_recursive_callback_t
 * so nested TABLE / ARRAY payloads keep their structure instead of collapsing
 * to a flat "var/val" pair. */
typedef enum {
    TELNETTY_MSDP_EV_VAR = 1,           /**< A variable name (before its value) */
    TELNETTY_MSDP_EV_VAL,               /**< A scalar value */
    TELNETTY_MSDP_EV_TABLE_OPEN,        /**< Nested table begins */
    TELNETTY_MSDP_EV_TABLE_CLOSE,       /**< Nested table ends */
    TELNETTY_MSDP_EV_ARRAY_OPEN,        /**< Array begins */
    TELNETTY_MSDP_EV_ARRAY_CLOSE        /**< Array ends */
} telnetty_msdp_event_t;

/* Recursive MSDP handler function type.
 *  - `depth`: 0 at the outermost variable, incremented per open table/array.
 *  - `path`:  dot-joined ancestor variable names, or "" at depth 0.
 *  - `value`: non-NULL for EV_VAL, otherwise NULL (containers/vars carry no
 *             payload here — the VAR event delivers the name).
 */
typedef void (*telnetty_msdp_recursive_callback_t)(
    telnetty_context_t* ctx,
    int depth,
    telnetty_msdp_event_t type,
    const char* path,
    const char* value,
    void* user_data
);

#ifndef TELNETTY_MSDP_MAX_DEPTH
#define TELNETTY_MSDP_MAX_DEPTH 16
#endif

/* MSDP data structure */
typedef struct {
    telnetty_msdp_variable_t* variables;  /**< MSDP variables */
    size_t variable_count;              /**< Number of variables */
    size_t capacity;                    /**< Variable array capacity */
    telnetty_msdp_handler_t handler;      /**< MSDP handler callback (flat) */
    void* user_data;                    /**< User data for handler */
    telnetty_msdp_recursive_callback_t rec_handler; /**< Structured handler */
    void* rec_user_data;                /**< User data for structured handler */
    bool negotiation_complete;          /**< Negotiation is complete */
} telnetty_msdp_data_t;

/**
 * MSDP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_MSDP)
 * @param command Command received
 * @param user_data User data (should be telnetty_msdp_data_t*)
 */
static void telnetty_msdp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable MSDP protocol
 * 
 * @param ctx TELNET context
 * @param handler MSDP handler callback (optional)
 * @param user_data User data for handler
 * @return 0 on success, -1 on failure
 */
static int telnetty_enable_msdp(
    telnetty_context_t* ctx,
    telnetty_msdp_handler_t handler,
    void* user_data
);

/**
 * Install a structured MSDP callback that preserves TABLE / ARRAY nesting.
 *
 * The recursive callback fires alongside the flat `handler` from
 * telnetty_enable_msdp — flat consumers keep working; new consumers get
 * depth-tracked EV_TABLE_OPEN/CLOSE and EV_ARRAY_OPEN/CLOSE events plus
 * the ancestor-joined `path` for each EV_VAR / EV_VAL. Depth is capped
 * at TELNETTY_MSDP_MAX_DEPTH (default 16); frames that would exceed the
 * cap fire EVENT_ERROR with TELNETTY_ERR_PROTOCOL and are abandoned.
 *
 * `cb` may be NULL to remove the handler. Must be called after
 * telnetty_enable_msdp.
 */
static TELNETTY_UNUSED int telnetty_msdp_set_recursive_callback(
    telnetty_context_t* ctx,
    telnetty_msdp_recursive_callback_t cb,
    void* user_data
);

/**
 * Send MSDP variable update
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @param val Variable value
 * @return 0 on success, -1 on failure
 */
static int telnetty_msdp_send(
    telnetty_context_t* ctx,
    const char* var,
    const char* val
);

/**
 * Send MSDP variable update with formatting
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @param fmt Format string
 * @param ... Format arguments
 * @return 0 on success, -1 on failure
 */
static int telnetty_msdp_sendf(
    telnetty_context_t* ctx,
    const char* var,
    const char* fmt,
    ...
);

/**
 * Request MSDP variable from peer
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @return 0 on success, -1 on failure
 */
static int telnetty_msdp_request(
    telnetty_context_t* ctx,
    const char* var
);

/**
 * Set MSDP variable flags
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @param flags Flags to set
 * @return 0 on success, -1 on failure
 */
static int telnetty_msdp_set_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
);

/**
 * Clear MSDP variable flags
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @param flags Flags to clear
 * @return 0 on success, -1 on failure
 */
static int telnetty_msdp_clear_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
);

/**
 * Get MSDP variable value
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @return Variable value or NULL if not found
 */
static const char* telnetty_msdp_get(
    telnetty_context_t* ctx,
    const char* var
);

/**
 * Remove MSDP variable
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @return 0 on success, -1 on failure
 */
static int telnetty_msdp_remove(
    telnetty_context_t* ctx,
    const char* var
);

/* ============================================================================
 * GMCP (Generic MUD Communication Protocol)
 * ============================================================================ */

/* GMCP option code */
#define TELNETTY_TELOPT_GMCP 201

/* GMCP message structure */
typedef struct {
    char* module;                       /**< GMCP module */
    char* message;                      /**< GMCP message */
    char* data;                         /**< GMCP data (JSON format) */
    time_t timestamp;                   /**< Message timestamp */
} telnetty_gmcp_message_t;

/* GMCP handler function type */
typedef void (*telnetty_gmcp_handler_t)(
    telnetty_context_t* ctx,
    const char* module,
    const char* message,
    const char* data,
    void* user_data
);

/* GMCP data structure */
typedef struct {
    telnetty_gmcp_handler_t handler;      /**< GMCP handler callback */
    void* user_data;                    /**< User data for handler */
    char** supported_modules;           /**< Supported GMCP modules */
    size_t module_count;                /**< Number of supported modules */
    size_t capacity;                    /**< Capacity of supported_modules array */
    bool negotiation_complete;          /**< Negotiation is complete */
} telnetty_gmcp_data_t;

/**
 * GMCP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_GMCP)
 * @param command Command received
 * @param user_data User data (should be telnetty_gmcp_data_t*)
 */
static void telnetty_gmcp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable GMCP protocol
 * 
 * @param ctx TELNET context
 * @param handler GMCP handler callback (optional)
 * @param user_data User data for handler
 * @return 0 on success, -1 on failure
 */
static int telnetty_enable_gmcp(
    telnetty_context_t* ctx,
    telnetty_gmcp_handler_t handler,
    void* user_data
);

/**
 * Send GMCP message
 * 
 * @param ctx TELNET context
 * @param module GMCP module
 * @param message GMCP message
 * @param data GMCP data (JSON format, optional)
 * @return 0 on success, -1 on failure
 */
static int telnetty_gmcp_send(
    telnetty_context_t* ctx,
    const char* module,
    const char* message,
    const char* data
);

/**
 * Send GMCP message with formatting
 * 
 * @param ctx TELNET context
 * @param module GMCP module
 * @param message GMCP message
 * @param fmt Format string for JSON data
 * @param ... Format arguments
 * @return 0 on success, -1 on failure
 */
static int telnetty_gmcp_sendf(
    telnetty_context_t* ctx,
    const char* module,
    const char* message,
    const char* fmt,
    ...
);

/**
 * Register GMCP module support
 * 
 * @param ctx TELNET context
 * @param module Module name
 * @return 0 on success, -1 on failure
 */
static int telnetty_gmcp_register_module(
    telnetty_context_t* ctx,
    const char* module
);

/**
 * Unregister GMCP module support
 * 
 * @param ctx TELNET context
 * @param module Module name
 * @return 0 on success, -1 on failure
 */
static int telnetty_gmcp_unregister_module(
    telnetty_context_t* ctx,
    const char* module
);

/**
 * Send GMCP Core.Hello message
 * 
 * @param ctx TELNET context
 * @param client Client name
 * @param version Client version
 * @return 0 on success, -1 on failure
 */
static int telnetty_gmcp_send_hello(
    telnetty_context_t* ctx,
    const char* client,
    const char* version
);

/**
 * Send GMCP Core.Supports.Set message
 * 
 * @param ctx TELNET context
 * @param modules Array of supported modules
 * @param count Number of modules
 * @return 0 on success, -1 on failure
 */
static int telnetty_gmcp_send_supports(
    telnetty_context_t* ctx,
    const char* const* modules,
    size_t count
);

/* ============================================================================
 * MTTS (MUD Terminal Type Standard)
 * ============================================================================ */

/* MTTS option code */
#define TELNETTY_TELOPT_MTTS 24  /* Uses same code as TTYPE */

/* MTTS flags */
typedef enum {
    TELNETTY_MTTS_FLAG_ANSI = 1 << 0,           /**< ANSI color support */
    TELNETTY_MTTS_FLAG_VT100 = 1 << 1,          /**< VT100 emulation */
    TELNETTY_MTTS_FLAG_UTF8 = 1 << 2,           /**< UTF-8 support */
    TELNETTY_MTTS_FLAG_256COLORS = 1 << 3,      /**< 256-color support */
    TELNETTY_MTTS_FLAG_MOUSETRACKING = 1 << 4,  /**< Mouse tracking */
    TELNETTY_MTTS_FLAG_COLORPALETTE = 1 << 5,   /**< Color palette */
    TELNETTY_MTTS_FLAG_SCREENREADER = 1 << 6,   /**< Screen reader */
    TELNETTY_MTTS_FLAG_PROXY = 1 << 7,          /**< Proxy connection */
    TELNETTY_MTTS_FLAG_TRUECOLOR = 1 << 8       /**< True color support */
} telnetty_mtts_flag_t;

/* MTTS data structure. Trailing fields (cycle, local_flags, terminal_type)
 * were added in 1.1 to drive the three-cycle TTYPE handshake; older callers
 * that only set the first four fields continue to work — cycle starts at 0
 * and terminal_type NULL means the runtime falls back to a default. */
typedef struct {
    uint64_t flags;                     /**< MTTS flags reported by peer (server-side view) */
    char* client_name;                  /**< Client name from cycle 1 */
    char* client_version;               /**< Client version (unused in bare MTTS) */
    bool negotiation_complete;          /**< Three-cycle exchange has finished */
    uint32_t cycle;                     /**< Next cycle to send/expect (0..3) */
    uint64_t local_flags;               /**< Flags we advertise as a client */
    char* terminal_type;                /**< Terminal type we advertise (cycle 2) */
} telnetty_mtts_data_t;

/**
 * MTTS option handler
 * 
 * @param ctx TELNET context
 * @param option Option code
 * @param command Command received
 * @param user_data User data (should be telnetty_mtts_data_t*)
 */
static void telnetty_mtts_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable MTTS protocol
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_mtts(telnetty_context_t* ctx);

/**
 * Get MTTS flags
 * 
 * @param ctx TELNET context
 * @return MTTS flags or 0 if not negotiated
 */
static TELNETTY_UNUSED uint64_t telnetty_mtts_get_flags(telnetty_context_t* ctx);

/**
 * Check if MTTS flag is set
 * 
 * @param ctx TELNET context
 * @param flag Flag to check
 * @return true if flag is set, false otherwise
 */
static bool telnetty_mtts_has_flag(
    telnetty_context_t* ctx,
    telnetty_mtts_flag_t flag
);

/**
 * Get MTTS client information
 * 
 * @param ctx TELNET context
 * @param name Output client name (optional)
 * @param version Output client version (optional)
 * @return 0 if info available, -1 otherwise
 */
static int telnetty_mtts_get_client_info(
    telnetty_context_t* ctx,
    const char** name,
    const char** version
);

/**
 * Send MTTS response
 * 
 * @param ctx TELNET context
 * @param flags MTTS flags
 * @param client_name Client name
 * @param client_version Client version
 * @return 0 on success, -1 on failure
 */
static int telnetty_mtts_send_response(
    telnetty_context_t* ctx,
    uint64_t flags,
    const char* client_name,
    const char* client_version
);

/* ============================================================================
 * MSSP (MUD Server Status Protocol)
 * ============================================================================ */

/* MSSP option code */
#define TELNETTY_TELOPT_MSSP 70

/* MSSP variable types */
typedef enum {
    TELNETTY_MSSP_VAR = 1,                /**< MSSP variable */
    TELNETTY_MSSP_VAL = 2                 /**< MSSP value */
} telnetty_mssp_type_t;

/* MSSP variable structure */
typedef struct {
    char* name;                         /**< Variable name */
    char* value;                        /**< Variable value */
} telnetty_mssp_variable_t;

/* MSSP data structure */
typedef struct {
    telnetty_mssp_variable_t* variables;  /**< MSSP variables */
    size_t variable_count;              /**< Number of variables */
    size_t capacity;                    /**< Array capacity */
    bool auto_send;                     /**< Auto-send on connection */
} telnetty_mssp_data_t;

/**
 * MSSP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_MSSP)
 * @param command Command received
 * @param user_data User data (should be telnetty_mssp_data_t*)
 */
static void telnetty_mssp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable MSSP protocol
 * 
 * @param ctx TELNET context
 * @param auto_send Auto-send MSSP data on connection
 * @return 0 on success, -1 on failure
 */
static int telnetty_enable_mssp(
    telnetty_context_t* ctx,
    bool auto_send
);

/**
 * Add MSSP variable
 * 
 * @param ctx TELNET context
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on failure
 */
static int telnetty_mssp_add(
    telnetty_context_t* ctx,
    const char* name,
    const char* value
);

/**
 * Send MSSP data to client
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mssp_send(telnetty_context_t* ctx);

/**
 * Clear all MSSP variables
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mssp_clear(telnetty_context_t* ctx);

/**
 * Set standard MSSP variables
 * 
 * @param ctx TELNET context
 * @param server_name Server name
 * @param server_type Server type
 * @param server_url Server URL
 * @param server_port Server port
 * @param player_count Current player count
 * @param max_players Maximum player count
 * @return 0 on success, -1 on failure
 */
static int telnetty_mssp_set_standard_vars(
    telnetty_context_t* ctx,
    const char* server_name,
    const char* server_type,
    const char* server_url,
    int server_port,
    int player_count,
    int max_players
);

/* ============================================================================
 * Implementation
 * ============================================================================ */

#ifdef TELNETTY_MUD_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

/* Memory allocation wrappers */
#ifndef TELNETTY_MUD_MALLOC
#define TELNETTY_MUD_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_MUD_FREE
#define TELNETTY_MUD_FREE(ptr) free(ptr)
#endif

#ifndef TELNETTY_MUD_REALLOC
#define TELNETTY_MUD_REALLOC(ptr, size) realloc(ptr, size)
#endif

/* Forward declarations for internal functions */
static TELNETTY_UNUSED telnetty_msdp_data_t* telnetty_msdp_data_create(void);
static TELNETTY_UNUSED void telnetty_msdp_data_destroy(telnetty_msdp_data_t* data);
static int telnetty_msdp_send_variable(
    telnetty_context_t* ctx,
    telnetty_msdp_variable_t* var
);
static TELNETTY_UNUSED telnetty_gmcp_data_t* telnetty_gmcp_data_create(void);
static TELNETTY_UNUSED void telnetty_gmcp_data_destroy(telnetty_gmcp_data_t* data);
static TELNETTY_UNUSED telnetty_mtts_data_t* telnetty_mtts_data_create(void);
static TELNETTY_UNUSED void telnetty_mtts_data_destroy(telnetty_mtts_data_t* data);
static TELNETTY_UNUSED telnetty_mssp_data_t* telnetty_mssp_data_create(void);
static TELNETTY_UNUSED void telnetty_mssp_data_destroy(telnetty_mssp_data_t* data);

/* ============================================================================
 * MSDP Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_msdp_data_t* telnetty_msdp_data_create(void) {
    telnetty_msdp_data_t* data = (telnetty_msdp_data_t*)TELNETTY_MUD_MALLOC(sizeof(telnetty_msdp_data_t));
    if (!data) return NULL;
    
    memset(data, 0, sizeof(telnetty_msdp_data_t));
    data->capacity = 16; /* Initial capacity */
    data->variables = (telnetty_msdp_variable_t*)TELNETTY_MUD_MALLOC(
        data->capacity * sizeof(telnetty_msdp_variable_t)
    );
    
    if (!data->variables) {
        TELNETTY_MUD_FREE(data);
        return NULL;
    }
    
    memset(data->variables, 0, data->capacity * sizeof(telnetty_msdp_variable_t));
    
    return data;
}

static TELNETTY_UNUSED void telnetty_msdp_data_destroy(telnetty_msdp_data_t* data) {
    if (!data) return;
    
    /* Free all variables */
    for (size_t i = 0; i < data->variable_count; i++) {
        if (data->variables[i].name) {
            TELNETTY_MUD_FREE(data->variables[i].name);
        }
        if (data->variables[i].value) {
            TELNETTY_MUD_FREE(data->variables[i].value);
        }
    }
    
    TELNETTY_MUD_FREE(data->variables);
    TELNETTY_MUD_FREE(data);
}

static void telnetty_msdp_data_free(void* p) {
    telnetty_msdp_data_destroy((telnetty_msdp_data_t*)p);
}

/* Q-method already answered. This only records the outcome. */
static void telnetty_msdp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)ctx;
    if (option != TELNETTY_TELOPT_MSDP) return;
    telnetty_msdp_data_t* msdp_data = (telnetty_msdp_data_t*)user_data;
    if (!msdp_data) return;
    msdp_data->negotiation_complete =
        (command == TELNETTY_DO || command == TELNETTY_WILL);
}

/* Recursive walker: emits structured events into rec_handler. `path_stack`
 * holds ancestor VAR names dot-joined lazily on each fire. */
static void telnetty__msdp_walk_recursive(
    telnetty_context_t* ctx,
    telnetty_msdp_data_t* msdp,
    const uint8_t* data,
    size_t length
) {
    if (!msdp->rec_handler) return;

    const char* stack[TELNETTY_MSDP_MAX_DEPTH];
    int depth = 0;
    memset(stack, 0, sizeof(stack));
    /* Ephemeral per-VAR name storage — one slot per depth level. Names are
     * copied here because the payload may go out of scope mid-walk. */
    char names[TELNETTY_MSDP_MAX_DEPTH][128];

    char path_buf[1024];
    #define BUILD_PATH() do { \
        size_t off = 0; path_buf[0] = '\0'; \
        for (int _d = 0; _d < depth; _d++) { \
            if (!stack[_d]) continue; \
            size_t nl = strlen(stack[_d]); \
            if (off + nl + 2 >= sizeof(path_buf)) break; \
            if (off) path_buf[off++] = '.'; \
            memcpy(path_buf + off, stack[_d], nl); \
            off += nl; path_buf[off] = '\0'; \
        } \
    } while (0)

    size_t i = 0;
    while (i < length) {
        uint8_t tag = data[i];
        if (tag == TELNETTY_MSDP_VAR) {
            i++;
            size_t vs = i;
            while (i < length && data[i] > TELNETTY_MSDP_ARRAY_CLOSE) i++;
            size_t vn = i - vs;
            if (depth < 0 || depth >= TELNETTY_MSDP_MAX_DEPTH) {
                telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL,
                                      "MSDP nesting exceeds max depth");
                return;
            }
            if (vn >= sizeof(names[0])) vn = sizeof(names[0]) - 1;
            memcpy(names[depth], data + vs, vn);
            names[depth][vn] = '\0';
            stack[depth] = names[depth];
            BUILD_PATH();
            msdp->rec_handler(ctx, depth, TELNETTY_MSDP_EV_VAR,
                              path_buf, NULL, msdp->rec_user_data);
        } else if (tag == TELNETTY_MSDP_VAL) {
            i++;
            size_t vs = i;
            while (i < length && data[i] > TELNETTY_MSDP_ARRAY_CLOSE) i++;
            size_t vn = i - vs;
            char val[512];
            if (vn >= sizeof(val)) vn = sizeof(val) - 1;
            memcpy(val, data + vs, vn);
            val[vn] = '\0';
            BUILD_PATH();
            msdp->rec_handler(ctx, depth, TELNETTY_MSDP_EV_VAL,
                              path_buf, val, msdp->rec_user_data);
        } else if (tag == TELNETTY_MSDP_TABLE_OPEN || tag == TELNETTY_MSDP_ARRAY_OPEN) {
            BUILD_PATH();
            msdp->rec_handler(ctx, depth,
                tag == TELNETTY_MSDP_TABLE_OPEN
                    ? TELNETTY_MSDP_EV_TABLE_OPEN
                    : TELNETTY_MSDP_EV_ARRAY_OPEN,
                path_buf, NULL, msdp->rec_user_data);
            i++;
            if (depth + 1 >= TELNETTY_MSDP_MAX_DEPTH) {
                telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL,
                                      "MSDP nesting exceeds max depth");
                return;
            }
            depth++;
        } else if (tag == TELNETTY_MSDP_TABLE_CLOSE || tag == TELNETTY_MSDP_ARRAY_CLOSE) {
            if (depth > 0) { depth--; stack[depth] = NULL; }
            BUILD_PATH();
            msdp->rec_handler(ctx, depth,
                tag == TELNETTY_MSDP_TABLE_CLOSE
                    ? TELNETTY_MSDP_EV_TABLE_CLOSE
                    : TELNETTY_MSDP_EV_ARRAY_CLOSE,
                path_buf, NULL, msdp->rec_user_data);
            i++;
        } else {
            i++;
        }
    }
    #undef BUILD_PATH
}

static void telnetty_msdp_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_msdp_data_t* msdp = (telnetty_msdp_data_t*)user_data;
    if (!msdp || !data) return;

    /* Recursive pass (structured) — runs when a recursive handler is set. */
    telnetty__msdp_walk_recursive(ctx, msdp, data, length);

    /* Flat pass (legacy compatibility) — always runs when a flat handler
     * is set, so existing callers see the same var/val stream they always
     * did even when a recursive handler is also registered. */
    if (!msdp->handler) return;
    size_t i = 0;
    while (i < length) {
        if (data[i] != TELNETTY_MSDP_VAR) { i++; continue; }
        i++;
        size_t vs = i;
        while (i < length && data[i] > TELNETTY_MSDP_ARRAY_CLOSE) i++;
        size_t vn = i - vs;
        char var[128];
        if (vn >= sizeof(var)) vn = sizeof(var) - 1;
        memcpy(var, data + vs, vn);
        var[vn] = '\0';
        char val[512];
        val[0] = '\0';
        if (i < length && data[i] == TELNETTY_MSDP_VAL) {
            i++;
            size_t vls = i;
            while (i < length && data[i] > TELNETTY_MSDP_ARRAY_CLOSE) i++;
            size_t vln = i - vls;
            if (vln >= sizeof(val)) vln = sizeof(val) - 1;
            memcpy(val, data + vls, vln);
            val[vln] = '\0';
        }
        msdp->handler(ctx, var, val, msdp->user_data);
    }
}

static TELNETTY_UNUSED int telnetty_msdp_set_recursive_callback(
    telnetty_context_t* ctx,
    telnetty_msdp_recursive_callback_t cb,
    void* user_data
) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MSDP];
    if (!opt || !opt->user_data) return -1;
    telnetty_msdp_data_t* d = (telnetty_msdp_data_t*)opt->user_data;
    d->rec_handler = cb;
    d->rec_user_data = user_data;
    return 0;
}

static int telnetty_enable_msdp(
    telnetty_context_t* ctx,
    telnetty_msdp_handler_t handler,
    void* user_data
) {
    if (!ctx) return -1;
    telnetty_msdp_data_t* msdp_data = telnetty_msdp_data_create();
    if (!msdp_data) return -1;
    msdp_data->handler = handler;
    msdp_data->user_data = user_data;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_MSDP, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_MSDP, telnetty_msdp_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_MSDP, telnetty_msdp_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_MSDP, msdp_data, telnetty_msdp_data_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MSDP);
}

static int telnetty_msdp_send(
    telnetty_context_t* ctx,
    const char* var,
    const char* val
) {
    if (!ctx || !var) return -1;
    
    /* Calculate required buffer size */
    size_t var_len = strlen(var);
    size_t val_len = val ? strlen(val) : 0;
    size_t total_size = 2 + var_len + (val ? (1 + val_len) : 0);
    
    /* Allocate temporary buffer */
    uint8_t* buffer = (uint8_t*)TELNETTY_MUD_MALLOC(total_size);
    if (!buffer) return -1;
    
    /* Build MSDP data */
    size_t offset = 0;
    buffer[offset++] = TELNETTY_MSDP_VAR;
    memcpy(buffer + offset, var, var_len);
    offset += var_len;
    
    if (val) {
        buffer[offset++] = TELNETTY_MSDP_VAL;
        memcpy(buffer + offset, val, val_len);
        offset += val_len;
    }
    
    /* Send subnegotiation */
    int result = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_MSDP, buffer, offset);
    
    TELNETTY_MUD_FREE(buffer);
    
    return result;
}

static int telnetty_msdp_sendf(
    telnetty_context_t* ctx,
    const char* var,
    const char* fmt,
    ...
) {
    if (!ctx || !var || !fmt) return -1;
    
    /* Format the value */
    va_list args;
    va_start(args, fmt);
    
    /* Calculate required size */
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (size < 0) {
        va_end(args);
        return -1;
    }
    
    /* Allocate buffer and format */
    char* buffer = (char*)TELNETTY_MUD_MALLOC(size + 1);
    if (!buffer) {
        va_end(args);
        return -1;
    }
    
    vsnprintf(buffer, size + 1, fmt, args);
    va_end(args);
    
    /* Send formatted value */
    int result = telnetty_msdp_send(ctx, var, buffer);
    
    TELNETTY_MUD_FREE(buffer);
    
    return result;
}

static int telnetty_msdp_request(
    telnetty_context_t* ctx,
    const char* var
) {
    if (!ctx || !var) return -1;
    
    /* Send request for variable */
    size_t var_len = strlen(var);
    size_t total_size = 2 + var_len;
    
    uint8_t* buffer = (uint8_t*)TELNETTY_MUD_MALLOC(total_size);
    if (!buffer) return -1;
    
    buffer[0] = TELNETTY_MSDP_VAR;
    strcpy((char*)buffer + 1, var);
    buffer[1 + var_len] = TELNETTY_MSDP_VAL; /* Empty value requests variable */
    
    int result = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_MSDP, buffer, total_size);
    
    TELNETTY_MUD_FREE(buffer);
    
    return result;
}

static int telnetty_msdp_set_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
) {
    (void)ctx;
    (void)var;
    (void)flags;
    /* This would need integration with MSDP data structure */
    /* For now, return success */
    return 0;
}

static int telnetty_msdp_clear_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
) {
    (void)ctx;
    (void)var;
    (void)flags;
    /* This would need integration with MSDP data structure */
    /* For now, return success */
    return 0;
}

static const char* telnetty_msdp_get(
    telnetty_context_t* ctx,
    const char* var
) {
    (void)ctx;
    (void)var;
    /* This would need integration with MSDP data structure */
    /* For now, return NULL */
    return NULL;
}

static int telnetty_msdp_remove(
    telnetty_context_t* ctx,
    const char* var
) {
    (void)ctx;
    (void)var;
    /* This would need integration with MSDP data structure */
    /* For now, return success */
    return 0;
}

/* ============================================================================
 * GMCP Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_gmcp_data_t* telnetty_gmcp_data_create(void) {
    telnetty_gmcp_data_t* data = (telnetty_gmcp_data_t*)TELNETTY_MUD_MALLOC(sizeof(telnetty_gmcp_data_t));
    if (!data) return NULL;
    
    memset(data, 0, sizeof(telnetty_gmcp_data_t));
    data->capacity = 8; /* Initial capacity */
    data->supported_modules = (char**)TELNETTY_MUD_MALLOC(
        data->capacity * sizeof(char*)
    );
    
    if (!data->supported_modules) {
        TELNETTY_MUD_FREE(data);
        return NULL;
    }
    
    memset(data->supported_modules, 0, data->capacity * sizeof(char*));
    
    return data;
}

static TELNETTY_UNUSED void telnetty_gmcp_data_destroy(telnetty_gmcp_data_t* data) {
    if (!data) return;
    
    /* Free supported modules */
    for (size_t i = 0; i < data->module_count; i++) {
        if (data->supported_modules[i]) {
            TELNETTY_MUD_FREE(data->supported_modules[i]);
        }
    }
    
    TELNETTY_MUD_FREE(data->supported_modules);
    TELNETTY_MUD_FREE(data);
}

static void telnetty_gmcp_data_free(void* p) {
    telnetty_gmcp_data_destroy((telnetty_gmcp_data_t*)p);
}

static void telnetty_gmcp_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_gmcp_data_t* gd = (telnetty_gmcp_data_t*)user_data;
    if (!gd || !gd->handler || !data || length == 0) return;
    const char* sp = memchr(data, ' ', length);
    char package[128];
    char message[128];
    const char* json = "";
    size_t head = sp ? (size_t)(sp - (const char*)data) : length;
    if (head >= sizeof(package)) head = sizeof(package) - 1;
    memcpy(package, data, head);
    package[head] = '\0';
    char* dot = strrchr(package, '.');
    if (dot && dot != package) {
        size_t ml = strlen(dot + 1);
        if (ml >= sizeof(message)) ml = sizeof(message) - 1;
        memcpy(message, dot + 1, ml);
        message[ml] = '\0';
        *dot = '\0';
    } else {
        message[0] = '\0';
    }
    char jsonbuf[1024];
    if (sp && (size_t)(sp - (const char*)data) + 1 < length) {
        size_t jl = length - (size_t)(sp + 1 - (const char*)data);
        if (jl >= sizeof(jsonbuf)) jl = sizeof(jsonbuf) - 1;
        memcpy(jsonbuf, sp + 1, jl);
        jsonbuf[jl] = '\0';
        json = jsonbuf;
    }
    gd->handler(ctx, package, message, json, gd->user_data);
}

static void telnetty_gmcp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (option != TELNETTY_TELOPT_GMCP) return;
    telnetty_gmcp_data_t* gmcp_data = (telnetty_gmcp_data_t*)user_data;
    if (!gmcp_data) return;
    gmcp_data->negotiation_complete =
        (command == TELNETTY_DO || command == TELNETTY_WILL);
    if (command == TELNETTY_DO)
        telnetty_gmcp_send_hello(ctx, "Telnetty", "1.0.0");
}

static int telnetty_enable_gmcp(
    telnetty_context_t* ctx,
    telnetty_gmcp_handler_t handler,
    void* user_data
) {
    if (!ctx) return -1;
    telnetty_gmcp_data_t* gmcp_data = telnetty_gmcp_data_create();
    if (!gmcp_data) return -1;
    gmcp_data->handler = handler;
    gmcp_data->user_data = user_data;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_GMCP, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_GMCP, telnetty_gmcp_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_GMCP, telnetty_gmcp_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_GMCP, gmcp_data, telnetty_gmcp_data_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_GMCP);
}

static int telnetty_gmcp_send(
    telnetty_context_t* ctx,
    const char* module,
    const char* message,
    const char* data
) {
    if (!ctx || !module || !message) return -1;

    /* Wire format is "Package.Message" or "Package.Message <json>". */
    size_t module_len = strlen(module);
    size_t message_len = strlen(message);
    size_t data_len = (data && data[0]) ? strlen(data) : 0;
    int dotted = message_len > 0;
    int spaced = data_len > 0;
    size_t total_size = module_len
        + (dotted ? 1 + message_len : 0)
        + (spaced ? 1 + data_len : 0);
    if (total_size == 0) return -1;

    uint8_t* buffer = (uint8_t*)TELNETTY_MUD_MALLOC(total_size);
    if (!buffer) return -1;

    size_t offset = 0;
    memcpy(buffer + offset, module, module_len);
    offset += module_len;
    if (dotted) {
        buffer[offset++] = '.';
        memcpy(buffer + offset, message, message_len);
        offset += message_len;
    }
    if (spaced) {
        buffer[offset++] = ' ';
        memcpy(buffer + offset, data, data_len);
        offset += data_len;
    }

    int result = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_GMCP, buffer, offset);

    TELNETTY_MUD_FREE(buffer);

    return result;
}

static int telnetty_gmcp_sendf(
    telnetty_context_t* ctx,
    const char* module,
    const char* message,
    const char* fmt,
    ...
) {
    if (!ctx || !module || !message) return -1;
    
    /* Format the data if provided */
    char* data = NULL;
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        
        /* Calculate required size */
        va_list args_copy;
        va_copy(args_copy, args);
        int size = vsnprintf(NULL, 0, fmt, args_copy);
        va_end(args_copy);
        
        if (size > 0) {
            data = (char*)TELNETTY_MUD_MALLOC(size + 1);
            if (data) {
                vsnprintf(data, size + 1, fmt, args);
            }
        }
        
        va_end(args);
    }
    
    /* Send GMCP message */
    int result = telnetty_gmcp_send(ctx, module, message, data);
    
    if (data) {
        TELNETTY_MUD_FREE(data);
    }
    
    return result;
}

static int telnetty_gmcp_register_module(
    telnetty_context_t* ctx,
    const char* module
) {
    (void)ctx;
    (void)module;
    /* This would need integration with GMCP data structure */
    /* For now, return success */
    return 0;
}

static int telnetty_gmcp_unregister_module(
    telnetty_context_t* ctx,
    const char* module
) {
    (void)ctx;
    (void)module;
    /* This would need integration with GMCP data structure */
    /* For now, return success */
    return 0;
}

static int telnetty_gmcp_send_hello(
    telnetty_context_t* ctx,
    const char* client,
    const char* version
) {
    if (!ctx) return -1;
    
    /* Send Core.Hello message */
    return telnetty_gmcp_sendf(ctx, "Core", "Hello", 
        "{\"client\": \"%s\", \"version\": \"%s\"}",
        client ? client : "Unknown",
        version ? version : "1.0.0");
}

static int telnetty_gmcp_send_supports(
    telnetty_context_t* ctx,
    const char* const* modules,
    size_t count
) {
    if (!ctx) return -1;
    
    /* Build JSON array of modules */
    size_t json_size = 2; /* [] */
    for (size_t i = 0; i < count; i++) {
        json_size += 3 + strlen(modules[i]); /* "module" */
        if (i < count - 1) json_size += 2; /* , */
    }
    
    char* json_data = (char*)TELNETTY_MUD_MALLOC(json_size + 1);
    if (!json_data) return -1;
    
    size_t offset = 0;
    json_data[offset++] = '[';
    
    for (size_t i = 0; i < count; i++) {
        json_data[offset++] = '"';
        strcpy(json_data + offset, modules[i]);
        offset += strlen(modules[i]);
        json_data[offset++] = '"';
        
        if (i < count - 1) {
            json_data[offset++] = ',';
            json_data[offset++] = ' ';
        }
    }
    
    json_data[offset++] = ']';
    json_data[offset] = '\0';
    
    /* Send Core.Supports.Set message */
    int result = telnetty_gmcp_send(ctx, "Core.Supports", "Set", json_data);
    
    TELNETTY_MUD_FREE(json_data);
    
    return result;
}

/* ============================================================================
 * MTTS Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_mtts_data_t* telnetty_mtts_data_create(void) {
    telnetty_mtts_data_t* data = (telnetty_mtts_data_t*)TELNETTY_MUD_MALLOC(sizeof(telnetty_mtts_data_t));
    if (!data) return NULL;
    
    memset(data, 0, sizeof(telnetty_mtts_data_t));
    
    return data;
}

static TELNETTY_UNUSED void telnetty_mtts_data_destroy(telnetty_mtts_data_t* data) {
    if (!data) return;
    if (data->client_name)    TELNETTY_MUD_FREE(data->client_name);
    if (data->client_version) TELNETTY_MUD_FREE(data->client_version);
    if (data->terminal_type)  TELNETTY_MUD_FREE(data->terminal_type);
    TELNETTY_MUD_FREE(data);
}

static void telnetty_mtts_data_free(void* p) {
    telnetty_mtts_data_destroy((telnetty_mtts_data_t*)p);
}

static telnetty_mtts_data_t* telnetty_mtts_of(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_TTYPE];
    return (opt && opt->user_data) ? (telnetty_mtts_data_t*)opt->user_data : NULL;
}

/* MTTS TTYPE subnegotiation codes. Same as RFC 1091. */
#define TELNETTY_TTYPE_IS   0
#define TELNETTY_TTYPE_SEND 1

/* Send one cycle of the three-cycle TTYPE IS response as a client. */
static void telnetty_mtts_send_cycle(telnetty_context_t* ctx, telnetty_mtts_data_t* d) {
    uint8_t buf[128];
    size_t off = 0;
    buf[off++] = TELNETTY_TTYPE_IS;

    const char* s;
    char flags_buf[32];
    if (d->cycle == 0) {
        s = d->client_name ? d->client_name : "TELNETTY";
    } else if (d->cycle == 1) {
        s = d->terminal_type ? d->terminal_type : "XTERM-256COLOR";
    } else {
        /* MTTS bitflags — the standard prefixes them with "MTTS ". */
        int n = snprintf(flags_buf, sizeof(flags_buf), "MTTS %llu",
                         (unsigned long long)d->local_flags);
        if (n < 0 || (size_t)n >= sizeof(flags_buf)) { flags_buf[0] = '\0'; }
        s = flags_buf;
    }

    size_t slen = strlen(s);
    if (off + slen > sizeof(buf)) slen = sizeof(buf) - off;
    memcpy(buf + off, s, slen);
    off += slen;
    telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_TTYPE, buf, off);
}

/* SB handler: peer (server) sent IAC SB TTYPE ... IAC SE. When the payload
 * is `SEND`, respond with the next cycle. When it's `IS <name>`, capture
 * the name into `client_name` for later inspection. */
static void telnetty_mtts_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    if (option != TELNETTY_TELOPT_TTYPE || length < 1) return;
    telnetty_mtts_data_t* d = (telnetty_mtts_data_t*)user_data;
    if (!d) return;

    if (data[0] == TELNETTY_TTYPE_SEND) {
        /* Client-side: respond to server's SEND with the next cycle. */
        telnetty_mtts_send_cycle(ctx, d);
        if (d->cycle < 2) {
            d->cycle++;
        } else {
            d->negotiation_complete = true;
        }
    } else if (data[0] == TELNETTY_TTYPE_IS && length > 1) {
        /* Server-side: capture the peer's cycle payload. Cycle 0 -> name,
         * cycle 2 -> "MTTS <flags>" — parse the trailing digits into flags. */
        size_t n = length - 1;
        char* copy = (char*)TELNETTY_MUD_MALLOC(n + 1);
        if (!copy) return;
        memcpy(copy, data + 1, n);
        copy[n] = '\0';

        if (d->cycle == 0) {
            if (d->client_name) TELNETTY_MUD_FREE(d->client_name);
            d->client_name = copy;
        } else if (d->cycle == 2) {
            if (n > 5 && memcmp(copy, "MTTS ", 5) == 0) {
                unsigned long long f = 0;
                for (const char* p = copy + 5; *p; p++) {
                    if (*p < '0' || *p > '9') break;
                    f = f * 10 + (unsigned)(*p - '0');
                }
                d->flags = f;
            }
            TELNETTY_MUD_FREE(copy);
            d->negotiation_complete = true;
        } else {
            /* Cycle 1 payload (terminal_type) — retained for inspection. */
            if (d->terminal_type) TELNETTY_MUD_FREE(d->terminal_type);
            d->terminal_type = copy;
        }

        if (d->cycle < 2) d->cycle++;
    }
}

/* Option handler: called when TTYPE negotiation settles. As a client we
 * accept DO by leaving state in place; server pushes SEND via subneg. */
static void telnetty_mtts_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)ctx; (void)command;
    if (option != TELNETTY_TELOPT_TTYPE) return;
    telnetty_mtts_data_t* d = (telnetty_mtts_data_t*)user_data;
    if (!d) return;
    /* Nothing to do here: SB handler drives the state machine. */
}

static TELNETTY_UNUSED int telnetty_enable_mtts(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_mtts_data_t* mtts_data = telnetty_mtts_data_create();
    if (!mtts_data) return -1;

    /* Default advertisement: ANSI + 256 color + UTF-8. Callers can override
     * via the returned pointer through get_client_info + direct edit, or by
     * calling telnetty_mtts_send_response with a specific flag set. */
    mtts_data->local_flags = TELNETTY_MTTS_FLAG_ANSI
                           | TELNETTY_MTTS_FLAG_UTF8
                           | TELNETTY_MTTS_FLAG_256COLORS;

    telnetty_set_option_support(ctx, TELNETTY_TELOPT_TTYPE, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_TTYPE, telnetty_mtts_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_TTYPE, telnetty_mtts_sb_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_TTYPE, mtts_data, telnetty_mtts_data_free);

    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_TTYPE);
}

static TELNETTY_UNUSED uint64_t telnetty_mtts_get_flags(telnetty_context_t* ctx) {
    telnetty_mtts_data_t* d = telnetty_mtts_of(ctx);
    return d ? d->flags : 0;
}

static bool telnetty_mtts_has_flag(
    telnetty_context_t* ctx,
    telnetty_mtts_flag_t flag
) {
    uint64_t flags = telnetty_mtts_get_flags(ctx);
    return (flags & (uint64_t)flag) != 0;
}

static int telnetty_mtts_get_client_info(
    telnetty_context_t* ctx,
    const char** name,
    const char** version
) {
    telnetty_mtts_data_t* d = telnetty_mtts_of(ctx);
    if (!d) return -1;
    if (name)    *name    = d->client_name;
    if (version) *version = d->client_version;
    return 0;
}

/* Configure the client-side MTTS response before negotiation, or override
 * after enable_mtts. Ownership: strings are duplicated so the caller may
 * free their originals immediately. */
static int telnetty_mtts_send_response(
    telnetty_context_t* ctx,
    uint64_t flags,
    const char* client_name,
    const char* client_version
) {
    telnetty_mtts_data_t* d = telnetty_mtts_of(ctx);
    if (!d) return -1;

    d->local_flags = flags;
    if (client_name) {
        char* dup = (char*)TELNETTY_MUD_MALLOC(strlen(client_name) + 1);
        if (dup) {
            memcpy(dup, client_name, strlen(client_name) + 1);
            if (d->client_name) TELNETTY_MUD_FREE(d->client_name);
            d->client_name = dup;
        }
    }
    if (client_version) {
        char* dup = (char*)TELNETTY_MUD_MALLOC(strlen(client_version) + 1);
        if (dup) {
            memcpy(dup, client_version, strlen(client_version) + 1);
            if (d->client_version) TELNETTY_MUD_FREE(d->client_version);
            d->client_version = dup;
        }
    }
    return 0;
}

/* ============================================================================
 * MSSP Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_mssp_data_t* telnetty_mssp_data_create(void) {
    telnetty_mssp_data_t* data = (telnetty_mssp_data_t*)TELNETTY_MUD_MALLOC(sizeof(telnetty_mssp_data_t));
    if (!data) return NULL;
    
    memset(data, 0, sizeof(telnetty_mssp_data_t));
    data->capacity = 16; /* Initial capacity */
    data->variables = (telnetty_mssp_variable_t*)TELNETTY_MUD_MALLOC(
        data->capacity * sizeof(telnetty_mssp_variable_t)
    );
    
    if (!data->variables) {
        TELNETTY_MUD_FREE(data);
        return NULL;
    }
    
    memset(data->variables, 0, data->capacity * sizeof(telnetty_mssp_variable_t));
    
    return data;
}

static TELNETTY_UNUSED void telnetty_mssp_data_destroy(telnetty_mssp_data_t* data) {
    if (!data) return;
    
    /* Free all variables */
    for (size_t i = 0; i < data->variable_count; i++) {
        if (data->variables[i].name) {
            TELNETTY_MUD_FREE(data->variables[i].name);
        }
        if (data->variables[i].value) {
            TELNETTY_MUD_FREE(data->variables[i].value);
        }
    }
    
    TELNETTY_MUD_FREE(data->variables);
    TELNETTY_MUD_FREE(data);
}

static void telnetty_mssp_data_free(void* p) {
    telnetty_mssp_data_destroy((telnetty_mssp_data_t*)p);
}

static telnetty_mssp_data_t* telnetty_mssp_of(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MSSP];
    return (opt && opt->user_data) ? (telnetty_mssp_data_t*)opt->user_data : NULL;
}

static void telnetty_mssp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (option != TELNETTY_TELOPT_MSSP) return;
    telnetty_mssp_data_t* mssp_data = (telnetty_mssp_data_t*)user_data;
    if (!mssp_data) return;
    if ((command == TELNETTY_DO || command == TELNETTY_WILL) && mssp_data->auto_send)
        telnetty_mssp_send(ctx);
}

static int telnetty_enable_mssp(
    telnetty_context_t* ctx,
    bool auto_send
) {
    if (!ctx) return -1;
    telnetty_mssp_data_t* mssp_data = telnetty_mssp_data_create();
    if (!mssp_data) return -1;
    mssp_data->auto_send = auto_send;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_MSSP, 1, 0);
    telnetty_register_option(ctx, TELNETTY_TELOPT_MSSP, telnetty_mssp_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_MSSP, mssp_data, telnetty_mssp_data_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MSSP);
}

static int telnetty_mssp_add(
    telnetty_context_t* ctx,
    const char* name,
    const char* value
) {
    telnetty_mssp_data_t* data = telnetty_mssp_of(ctx);
    if (!data || !name) return -1;
    for (size_t i = 0; i < data->variable_count; i++) {
        if (data->variables[i].name && strcmp(data->variables[i].name, name) == 0) {
            TELNETTY_MUD_FREE(data->variables[i].value);
            data->variables[i].value = value ? strdup(value) : strdup("");
            return data->variables[i].value ? 0 : -1;
        }
    }
    if (data->variable_count >= data->capacity) {
        size_t nc = data->capacity * 2;
        telnetty_mssp_variable_t* nv = (telnetty_mssp_variable_t*)TELNETTY_MUD_REALLOC(
            data->variables, nc * sizeof(*nv));
        if (!nv) return -1;
        memset(nv + data->capacity, 0, (nc - data->capacity) * sizeof(*nv));
        data->variables = nv;
        data->capacity = nc;
    }
    telnetty_mssp_variable_t* slot = &data->variables[data->variable_count];
    slot->name = strdup(name);
    slot->value = strdup(value ? value : "");
    if (!slot->name || !slot->value) return -1;
    data->variable_count++;
    return 0;
}

static TELNETTY_UNUSED int telnetty_mssp_send(telnetty_context_t* ctx) {
    telnetty_mssp_data_t* data = telnetty_mssp_of(ctx);
    if (!data) return -1;
    size_t bytes = 0;
    for (size_t i = 0; i < data->variable_count; i++) {
        bytes += 2;
        if (data->variables[i].name) bytes += strlen(data->variables[i].name);
        if (data->variables[i].value) bytes += strlen(data->variables[i].value);
    }
    if (bytes == 0) return 0;
    uint8_t* buf = (uint8_t*)TELNETTY_MUD_MALLOC(bytes);
    if (!buf) return -1;
    size_t o = 0;
    for (size_t i = 0; i < data->variable_count; i++) {
        const char* n = data->variables[i].name ? data->variables[i].name : "";
        const char* v = data->variables[i].value ? data->variables[i].value : "";
        buf[o++] = 1; /* VAR */
        memcpy(buf + o, n, strlen(n));
        o += strlen(n);
        buf[o++] = 2; /* VAL */
        memcpy(buf + o, v, strlen(v));
        o += strlen(v);
    }
    int rc = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_MSSP, buf, o);
    TELNETTY_MUD_FREE(buf);
    return rc;
}

static TELNETTY_UNUSED int telnetty_mssp_clear(telnetty_context_t* ctx) {
    telnetty_mssp_data_t* data = telnetty_mssp_of(ctx);
    if (!data) return -1;
    for (size_t i = 0; i < data->variable_count; i++) {
        TELNETTY_MUD_FREE(data->variables[i].name);
        TELNETTY_MUD_FREE(data->variables[i].value);
        data->variables[i].name = data->variables[i].value = NULL;
    }
    data->variable_count = 0;
    return 0;
}

static int telnetty_mssp_set_standard_vars(
    telnetty_context_t* ctx,
    const char* server_name,
    const char* server_type,
    const char* server_url,
    int server_port,
    int player_count,
    int max_players
) {
    if (!ctx) return -1;
    
    /* Add standard MSSP variables */
    if (server_name) telnetty_mssp_add(ctx, "NAME", server_name);
    if (server_type) telnetty_mssp_add(ctx, "CODEBASE", server_type);
    if (server_url) telnetty_mssp_add(ctx, "HOSTNAME", server_url);
    if (server_port > 0) {
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", server_port);
        telnetty_mssp_add(ctx, "PORT", port_str);
    }
    if (player_count >= 0) {
        char count_str[16];
        snprintf(count_str, sizeof(count_str), "%d", player_count);
        telnetty_mssp_add(ctx, "PLAYERS", count_str);
    }
    if (max_players > 0) {
        char max_str[16];
        snprintf(max_str, sizeof(max_str), "%d", max_players);
        telnetty_mssp_add(ctx, "MAXPLAYERS", max_str);
    }
    
    return 0;
}

#endif /* TELNETTY_MUD_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_MUD_H */