/**
 * @file telnetty_mcp.h
 * @brief MCP 2.1 (MUD Client Protocol) implementation
 * 
 * This header provides an implementation of the MCP 2.1 protocol as defined
 * at https://www.moo.mud.org/mcp/mcp2.html. MCP allows for structured
 * communication between MUD clients and servers.
 * 
 * Features:
 * - MCP 2.1 negotiation and authentication
 * - Package management
 * - Message routing
 * - Multi-package support
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.2.0
 * 
 */

#ifndef TELNETTY_MCP_H
#define TELNETTY_MCP_H

#include "telnetty_core.h"
#include "telnetty_events.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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
 * MCP 2.1 Constants
 * ============================================================================ */

/* MCP 2.1 is an IN-BAND protocol keyed on the "#$#" line prefix — it does
 * not have a TELNET option code, contrary to what several C libraries have
 * historically claimed. We still need a `telnetty_option_t*` slot to hang
 * MCP's per-context state on so the standard option-freer path can clean
 * it up on destroy; TELNETTY_MCP_STATE_TELOPT (207, previously unassigned
 * in the MUD range) is a bookkeeping-only code — we never send WILL/DO/
 * WONT/DONT for it on the wire.
 *
 * Historical note: prior 1.1 releases used TELOPT 200 for this bookkeeping,
 * which collided with ATCP (which really is TELOPT 200; see telnetty_mud.h).
 * TELNETTY_TELOPT_MCP is retained as a deprecated alias for source
 * compatibility — it now equals TELNETTY_MCP_STATE_TELOPT, not 200. */
#define TELNETTY_MCP_STATE_TELOPT 207
#define TELNETTY_TELOPT_MCP TELNETTY_MCP_STATE_TELOPT  /* deprecated alias */

/* MCP message types */
typedef enum {
    TELNETTY_MCP_MESSAGE_AUTH = 0,        /**< Authentication message */
    TELNETTY_MCP_MESSAGE_NEGOTIATE = 1,   /**< Negotiation message */
    TELNETTY_MCP_MESSAGE_DATA = 2,        /**< Data message */
    TELNETTY_MCP_MESSAGE_ERROR = 3,       /**< Error message */
    TELNETTY_MCP_MESSAGE_CLOSE = 4        /**< Close message */
} telnetty_mcp_message_type_t;

/* MCP authentication states */
typedef enum {
    TELNETTY_MCP_AUTH_NONE = 0,           /**< No authentication */
    TELNETTY_MCP_AUTH_KEY = 1,            /**< Key-based authentication */
    TELNETTY_MCP_AUTH_USER = 2            /**< User/password authentication */
} telnetty_mcp_auth_type_t;

/* MCP negotiation states */
typedef enum {
    TELNETTY_MCP_STATE_DISABLED = 0,      /**< MCP is disabled */
    TELNETTY_MCP_STATE_NEGOTIATING = 1,   /**< Negotiation in progress */
    TELNETTY_MCP_STATE_ENABLED = 2,       /**< MCP is enabled */
    TELNETTY_MCP_STATE_FAILED = 3         /**< Negotiation failed */
} telnetty_mcp_state_t;

/* ============================================================================
 * MCP Package Management
 * ============================================================================ */

/* Forward declaration for MCP package handler */
struct telnetty_mcp_package;
typedef struct telnetty_mcp_package telnetty_mcp_package_t;

/* MCP package handler function type */
typedef void (*telnetty_mcp_package_handler_t)(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
);

/* MCP package structure */
struct telnetty_mcp_package {
    char* name;                         /**< Package name */
    char* version;                      /**< Package version */
    char* description;                  /**< Package description */
    bool active;                        /**< Package is active */
    telnetty_mcp_package_handler_t handler; /**< Package handler */
    void* user_data;                    /**< User data for handler */
    void* handler_data;                 /**< Handler-specific data */
    bool peer_supports;                 /**< Peer advertised this package (1.1) */
};

/* ============================================================================
 * MCP Data Structures
 * ============================================================================ */

/* MCP authentication data */
typedef struct {
    telnetty_mcp_auth_type_t type;        /**< Authentication type */
    char* key;                          /**< Authentication key */
    char* username;                     /**< Username */
    char* password;                     /**< Password */
    bool authenticated;                 /**< Authentication successful */
} telnetty_mcp_auth_data_t;

/* MCP message structure */
typedef struct {
    char* package;                      /**< Package name */
    char* message;                      /**< Message type */
    char* data;                         /**< Message data (JSON) */
    time_t timestamp;                   /**< Message timestamp */
    int message_id;                     /**< Message ID */
} telnetty_mcp_message_t;

/* Bounded cord (multiline data-tag) tracking entry. */
typedef struct {
    char* tag;
    char* accum;                        /* growing payload */
    size_t accum_len;
    size_t accum_cap;
    time_t deadline;                    /* cord expires past this */
    char* package;                      /* pending dispatch: package or keyword */
    char* message;                      /* pending dispatch: message name */
    char* args;                         /* simple key/value text from the opener */
    int matched;                        /* opener matched a registered package */
} telnetty_mcp_cord_t;

/* MCP main data structure. Fields after `server_key` were added in 1.1 for
 * the real in-band parser; older callers that don't touch them still work
 * because they're zero-initialized. */
typedef struct {
    telnetty_mcp_state_t state;           /**< MCP state */
    telnetty_mcp_auth_data_t auth;        /**< Authentication data */
    telnetty_mcp_package_t** packages;    /**< Registered packages array */
    size_t package_count;               /**< Number of packages */
    size_t package_capacity;            /**< Package array capacity */
    telnetty_mcp_package_handler_t default_handler; /**< Default handler */
    void* default_handler_data;         /**< Default handler data */
    int last_message_id;                /**< Last message ID */
    bool server_mode;                   /**< Server mode */
    char* server_key;                   /**< Server authentication key */

    char* auth_key;                     /**< Auth key we generated (client reply). */
    telnetty_mcp_cord_t cords[32];      /**< Open multiline values (bounded). */
    char* session_key;                  /**< Key required on the wire. May alias auth_key. */
    char* line_acc;                     /**< Partial #$# line being assembled. */
    size_t line_len;
    size_t line_cap;
    int line_mode;                      /**< 0 undecided, 1 skip in-band, 2 collecting. */
    int negotiate_done;                 /**< Peer sent mcp-negotiate-end. */
    int disabled;                       /**< telnetty_disable_mcp() ran on this ctx. */
} telnetty_mcp_data_t;

/* ============================================================================
 * MCP Core Functions
 * ============================================================================ */

#ifdef TELNETTY_MCP_IMPLEMENTATION
/**
 * Enable MCP 2.1 protocol
 * 
 * @param ctx TELNET context
 * @param server_mode true for server mode, false for client mode
 * @param auth_type Authentication type
 * @param key Authentication key (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_mcp(
    telnetty_context_t* ctx,
    bool server_mode,
    telnetty_mcp_auth_type_t auth_type,
    const char* key
);

/**
 * Disable MCP 2.1 protocol
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_mcp(telnetty_context_t* ctx);

/**
 * MCP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_MCP)
 * @param command Command received
 * @param user_data User data (should be telnetty_mcp_data_t*)
 */
static TELNETTY_UNUSED void telnetty_mcp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Register an MCP package
 * 
 * @param ctx TELNET context
 * @param name Package name
 * @param version Package version
 * @param description Package description (optional)
 * @param handler Package handler (optional)
 * @param user_data User data for handler
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_register_package(
    telnetty_context_t* ctx,
    const char* name,
    const char* version,
    const char* description,
    telnetty_mcp_package_handler_t handler,
    void* user_data
);

/**
 * Unregister an MCP package
 * 
 * @param ctx TELNET context
 * @param name Package name
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_unregister_package(
    telnetty_context_t* ctx,
    const char* name
);

/**
 * Send MCP message
 * 
 * @param ctx TELNET context
 * @param package Package name
 * @param message Message type
 * @param data Keyword arguments (`key: value ...`), or a bare value which
 *        is sent as `data: "<value>"`. NULL sends the message with no arguments.
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_send(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data
);

/**
 * Send MCP message with formatting
 * 
 * @param ctx TELNET context
 * @param package Package name
 * @param message Message type
 * @param fmt Format string for JSON data
 * @param ... Format arguments
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_sendf(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* fmt,
    ...
);

/**
 * Send MCP authentication request
 * 
 * @param ctx TELNET context
 * @param auth_type Authentication type
 * @param credentials Authentication credentials (JSON format)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_send_auth(
    telnetty_context_t* ctx,
    telnetty_mcp_auth_type_t auth_type,
    const char* credentials
);

/**
 * Send MCP negotiation request
 * 
 * @param ctx TELNET context
 * @param packages Array of supported packages
 * @param package_count Number of packages
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_send_negotiate(
    telnetty_context_t* ctx,
    const telnetty_mcp_package_t* packages,
    size_t package_count
);

/**
 * Send MCP error message
 * 
 * @param ctx TELNET context
 * @param package Package that caused error (optional)
 * @param message Error message
 * @param code Error code (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_send_error(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    int code
);

/**
 * Send MCP close message
 * 
 * @param ctx TELNET context
 * @param reason Close reason (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_send_close(
    telnetty_context_t* ctx,
    const char* reason
);

/* ============================================================================
 * Package Management Functions
 * ============================================================================ */

/**
 * Get MCP package by name
 * 
 * @param ctx TELNET context
 * @param name Package name
 * @return Package structure or NULL if not found
 */
static TELNETTY_UNUSED telnetty_mcp_package_t* telnetty_mcp_get_package(
    telnetty_context_t* ctx,
    const char* name
);

/**
 * Check if package is active
 * 
 * @param ctx TELNET context
 * @param name Package name
 * @return true if package is active, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_mcp_is_package_active(
    telnetty_context_t* ctx,
    const char* name
);

/**
 * Activate MCP package
 * 
 * @param ctx TELNET context
 * @param name Package name
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_activate_package(
    telnetty_context_t* ctx,
    const char* name
);

/**
 * Deactivate MCP package
 * 
 * @param ctx TELNET context
 * @param name Package name
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_deactivate_package(
    telnetty_context_t* ctx,
    const char* name
);

/**
 * Set default package handler
 * 
 * @param ctx TELNET context
 * @param handler Default handler function
 * @param user_data User data for handler
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_set_default_handler(
    telnetty_context_t* ctx,
    telnetty_mcp_package_handler_t handler,
    void* user_data
);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get MCP state
 * 
 * @param ctx TELNET context
 * @return Current MCP state
 */
static TELNETTY_UNUSED telnetty_mcp_state_t telnetty_mcp_get_state(telnetty_context_t* ctx);

/**
 * Check if MCP is enabled
 * 
 * @param ctx TELNET context
 * @return true if MCP is enabled, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_mcp_is_enabled(telnetty_context_t* ctx);

/**
 * Get number of registered packages
 * 
 * @param ctx TELNET context
 * @return Number of packages
 */
static TELNETTY_UNUSED size_t telnetty_mcp_get_package_count(telnetty_context_t* ctx);

/**
 * Get package list
 * 
 * @param ctx TELNET context
 * @param packages Output array for packages
 * @param max_packages Maximum number of packages to return
 * @return Number of packages returned
 */
static TELNETTY_UNUSED size_t telnetty_mcp_get_packages(
    telnetty_context_t* ctx,
    telnetty_mcp_package_t* packages,
    size_t max_packages
);

/**
 * Generate unique MCP message ID
 * 
 * @param ctx TELNET context
 * @return New message ID
 */
static TELNETTY_UNUSED int telnetty_mcp_generate_message_id(telnetty_context_t* ctx);

/**
 * Parse MCP message
 * 
@brief Parse an MCP message from raw data
 * 
 * @param data Raw message data
 * @param length Data length
 * @param message Output parsed message structure
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_parse_message(
    const uint8_t* data,
    size_t length,
    telnetty_mcp_message_t* message
);

/**
 * Build MCP message
 * 
 * @param message Message to build
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of bytes written or -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_build_message(
    const telnetty_mcp_message_t* message,
    uint8_t* buffer,
    size_t buffer_size
);

/* ============================================================================
 * Built-in MCP Packages
 * ============================================================================ */

/**
 * Register built-in MCP packages
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mcp_register_builtin_packages(telnetty_context_t* ctx);

/**
 * Placeholder handlers for the builtin packages: mcp-core, simple-edit,
 * redirect, dns. They are registered inactive and do nothing; see
 * telnetty_mcp_register_builtin_packages.
 * 
 * @param ctx TELNET context
 * @param package Package name
 * @param message Message type
 * @param data Message data
 * @param user_data User data
 */
static TELNETTY_UNUSED void telnetty_mcp_core_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
);
static TELNETTY_UNUSED void telnetty_mcp_simpleedit_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
);
static TELNETTY_UNUSED void telnetty_mcp_redirect_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
);
static TELNETTY_UNUSED void telnetty_mcp_dns_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
);

#endif /* TELNETTY_MCP_IMPLEMENTATION */

/* ============================================================================
 * Implementation
 * ============================================================================ */

#ifdef TELNETTY_MCP_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

static TELNETTY_UNUSED int telnetty__mcp_write_line(telnetty_context_t* ctx, const char* line);
static TELNETTY_UNUSED void telnetty__mcp_on_data(telnetty_context_t* ctx, const uint8_t* data, size_t len);
static TELNETTY_UNUSED void telnetty__mcp_send_negotiate_all(telnetty_context_t* ctx, telnetty_mcp_data_t* d);

/* Memory allocation wrappers */
#ifndef TELNETTY_MCP_MALLOC
#define TELNETTY_MCP_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_MCP_FREE
#define TELNETTY_MCP_FREE(ptr) free(ptr)
#endif

/* strdup / strndup are POSIX, not C99, and allocate with libc malloc
 * rather than TELNETTY_MCP_MALLOC, which every MCP string is freed with. */
static TELNETTY_UNUSED char* telnetty__mcp_strndup(const char* s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char* out = (char*)TELNETTY_MCP_MALLOC(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static TELNETTY_UNUSED char* telnetty__mcp_strdup(const char* s) {
    return telnetty__mcp_strndup(s, (size_t)-1);
}

#ifndef TELNETTY_MCP_REALLOC
#define TELNETTY_MCP_REALLOC(ptr, size) realloc(ptr, size)
#endif

/* Forward declarations for internal functions */
static TELNETTY_UNUSED telnetty_mcp_data_t* telnetty_mcp_data_create(void);
static TELNETTY_UNUSED void telnetty_mcp_data_destroy(telnetty_mcp_data_t* data);
static TELNETTY_UNUSED telnetty_mcp_package_t* telnetty_mcp_package_create(
    const char* name,
    const char* version,
    const char* description,
    telnetty_mcp_package_handler_t handler,
    void* user_data
);
static TELNETTY_UNUSED void telnetty_mcp_package_destroy(telnetty_mcp_package_t* package);
static TELNETTY_UNUSED int telnetty_mcp_send_subnegotiation(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
);

/* ============================================================================
 * MCP Data Management
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_mcp_data_t* telnetty_mcp_data_create(void) {
    telnetty_mcp_data_t* data = (telnetty_mcp_data_t*)TELNETTY_MCP_MALLOC(sizeof(telnetty_mcp_data_t));
    if (!data) return NULL;
    
    memset(data, 0, sizeof(telnetty_mcp_data_t));
    data->state = TELNETTY_MCP_STATE_DISABLED;
    data->package_capacity = 16; /* Initial capacity */
    data->packages = (telnetty_mcp_package_t**)TELNETTY_MCP_MALLOC(
        data->package_capacity * sizeof(telnetty_mcp_package_t*)
    );
    
    if (!data->packages) {
        TELNETTY_MCP_FREE(data);
        return NULL;
    }
    
    memset(data->packages, 0, data->package_capacity * sizeof(telnetty_mcp_package_t*));
    
    return data;
}

static TELNETTY_UNUSED void telnetty_mcp_data_destroy(telnetty_mcp_data_t* data) {
    if (!data) return;
    
    /* Free all packages */
    for (size_t i = 0; i < data->package_count; i++) {
        if (data->packages[i]) {
            telnetty_mcp_package_destroy(data->packages[i]);
        }
    }
    
    /* Free package array */
    TELNETTY_MCP_FREE(data->packages);
    
    /* Free authentication data */
    if (data->auth.key) {
        TELNETTY_MCP_FREE(data->auth.key);
    }
    if (data->auth.username) {
        TELNETTY_MCP_FREE(data->auth.username);
    }
    if (data->auth.password) {
        TELNETTY_MCP_FREE(data->auth.password);
    }
    if (data->server_key) {
        TELNETTY_MCP_FREE(data->server_key);
    }
    if (data->session_key && data->session_key != data->auth_key) {
        TELNETTY_MCP_FREE(data->session_key);
    }
    if (data->auth_key) {
        TELNETTY_MCP_FREE(data->auth_key);
    }
    if (data->line_acc) {
        TELNETTY_MCP_FREE(data->line_acc);
    }
    for (size_t i = 0; i < sizeof(data->cords)/sizeof(data->cords[0]); i++) {
        TELNETTY_MCP_FREE(data->cords[i].tag);
        TELNETTY_MCP_FREE(data->cords[i].accum);
        TELNETTY_MCP_FREE(data->cords[i].package);
        TELNETTY_MCP_FREE(data->cords[i].message);
        TELNETTY_MCP_FREE(data->cords[i].args);
    }

    TELNETTY_MCP_FREE(data);
}

static TELNETTY_UNUSED telnetty_mcp_package_t* telnetty_mcp_package_create(
    const char* name,
    const char* version,
    const char* description,
    telnetty_mcp_package_handler_t handler,
    void* user_data
) {
    if (!name) return NULL;
    
    telnetty_mcp_package_t* package = (telnetty_mcp_package_t*)TELNETTY_MCP_MALLOC(sizeof(telnetty_mcp_package_t));
    if (!package) return NULL;
    
    memset(package, 0, sizeof(telnetty_mcp_package_t));
    
    /* Copy name */
    package->name = telnetty__mcp_strdup(name);
    if (!package->name) {
        TELNETTY_MCP_FREE(package);
        return NULL;
    }
    
    /* Copy version */
    if (version) {
        package->version = telnetty__mcp_strdup(version);
        if (!package->version) {
            TELNETTY_MCP_FREE(package->name);
            TELNETTY_MCP_FREE(package);
            return NULL;
        }
    }
    
    /* Copy description */
    if (description) {
        package->description = telnetty__mcp_strdup(description);
        if (!package->description) {
            TELNETTY_MCP_FREE(package->name);
            TELNETTY_MCP_FREE(package->version);
            TELNETTY_MCP_FREE(package);
            return NULL;
        }
    }
    
    package->handler = handler;
    package->user_data = user_data;
    
    return package;
}

static TELNETTY_UNUSED void telnetty_mcp_package_destroy(telnetty_mcp_package_t* package) {
    if (!package) return;
    
    if (package->name) {
        TELNETTY_MCP_FREE(package->name);
    }
    if (package->version) {
        TELNETTY_MCP_FREE(package->version);
    }
    if (package->description) {
        TELNETTY_MCP_FREE(package->description);
    }
    
    TELNETTY_MCP_FREE(package);
}

/* ============================================================================
 * MCP Core Implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_mcp_data_free_cb(void* p);

static TELNETTY_UNUSED int telnetty_enable_mcp(
    telnetty_context_t* ctx,
    bool server_mode,
    telnetty_mcp_auth_type_t auth_type,
    const char* key
) {
    if (!ctx) return -1;
    
    /* Create MCP data */
    telnetty_mcp_data_t* mcp_data = telnetty_mcp_data_create();
    if (!mcp_data) return -1;
    
    mcp_data->server_mode = server_mode;
    mcp_data->auth.type = auth_type;
    
    /* Set authentication key if provided */
    if (key) {
        mcp_data->server_key = telnetty__mcp_strdup(key);
        if (!mcp_data->server_key) {
            telnetty_mcp_data_destroy(mcp_data);
            return -1;
        }
    }
    
    /* MCP 2.1 is in-band. We don't call telnetty_send_option — we only
     * pin state to the bookkeeping option slot so the freer path runs on
     * destroy. Registering a handler for TELNETTY_MCP_STATE_TELOPT is
     * defensive: if a peer *does* send WILL 207 by accident we still
     * respond correctly, and the handler is a no-op for MCP proper. */
    telnetty_set_option_support(ctx, TELNETTY_MCP_STATE_TELOPT, 0, 0);
    telnetty_register_option(ctx, TELNETTY_MCP_STATE_TELOPT,
                             telnetty_mcp_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_MCP_STATE_TELOPT,
                                  mcp_data, telnetty_mcp_data_free_cb);
    telnetty_mcp_register_builtin_packages(ctx);
    telnetty__mcp_data_hook = telnetty__mcp_on_data;
    /* The server speaks first: version range, no authentication key.
     * The client waits for that line before sending anything. */
    if (server_mode) {
        telnetty__mcp_write_line(ctx, "mcp version: 2.1 to: 2.1");
    }
    return 0;
}

static TELNETTY_UNUSED void telnetty_mcp_data_free_cb(void* p) {
    telnetty_mcp_data_destroy((telnetty_mcp_data_t*)p);
}

static TELNETTY_UNUSED telnetty_mcp_data_t* telnetty_mcp_of(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_MCP_STATE_TELOPT];
    return (opt && opt->user_data) ? (telnetty_mcp_data_t*)opt->user_data : NULL;
}

static TELNETTY_UNUSED int telnetty_disable_mcp(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    /* MCP is in-band — there is no TELNET option to WONT. Stop
     * reassembling #$# lines on this context only: the data hook is shared
     * by every context in the translation unit, so it stays installed and
     * checks the per-context flag. user_data is freed on destroy. */
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d) return -1;
    d->disabled = 1;
    d->line_len = 0;
    d->line_mode = 0;
    return 0;
}

static TELNETTY_UNUSED void telnetty_mcp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (!ctx || option != TELNETTY_MCP_STATE_TELOPT) return;
    
    telnetty_mcp_data_t* mcp_data = user_data
        ? (telnetty_mcp_data_t*)user_data
        : telnetty_mcp_of(ctx);
    if (!mcp_data) return;

    /* MCP 2.1 itself is in-band. The private telopt only tracks whether the
     * peer is willing to speak it; the version handshake is #$#mcp. */
    switch (command) {
        case TELNETTY_WILL:
        case TELNETTY_DO:
            if (mcp_data->state == TELNETTY_MCP_STATE_DISABLED)
                mcp_data->state = TELNETTY_MCP_STATE_NEGOTIATING;
            break;

        case TELNETTY_WONT:
        case TELNETTY_DONT:
            mcp_data->state = TELNETTY_MCP_STATE_DISABLED;
            break;
    }
}

/* ============================================================================
 * Package Management Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_mcp_register_package(
    telnetty_context_t* ctx,
    const char* name,
    const char* version,
    const char* description,
    telnetty_mcp_package_handler_t handler,
    void* user_data
) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d || !name) return -1;
    if (telnetty_mcp_get_package(ctx, name)) return 0;
    if (d->package_count >= d->package_capacity) {
        size_t nc = d->package_capacity ? d->package_capacity * 2 : 8;
        telnetty_mcp_package_t** np = (telnetty_mcp_package_t**)TELNETTY_MCP_REALLOC(
            d->packages, nc * sizeof(*np));
        if (!np) return -1;
        d->packages = np;
        d->package_capacity = nc;
    }
    telnetty_mcp_package_t* pkg = telnetty_mcp_package_create(name, version, description, handler, user_data);
    if (!pkg) return -1;
    pkg->active = true;
    d->packages[d->package_count++] = pkg;
    return 0;
}

static TELNETTY_UNUSED int telnetty_mcp_unregister_package(
    telnetty_context_t* ctx,
    const char* name
) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d || !name) return -1;
    for (size_t i = 0; i < d->package_count; i++) {
        if (d->packages[i] && d->packages[i]->name && strcmp(d->packages[i]->name, name) == 0) {
            telnetty_mcp_package_destroy(d->packages[i]);
            memmove(&d->packages[i], &d->packages[i + 1],
                    (d->package_count - i - 1) * sizeof(d->packages[i]));
            d->package_count--;
            return 0;
        }
    }
    return -1;
}

static TELNETTY_UNUSED telnetty_mcp_package_t* telnetty_mcp_get_package(
    telnetty_context_t* ctx,
    const char* name
) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d || !name) return NULL;
    for (size_t i = 0; i < d->package_count; i++) {
        if (d->packages[i] && d->packages[i]->name && strcmp(d->packages[i]->name, name) == 0)
            return d->packages[i];
    }
    return NULL;
}

static TELNETTY_UNUSED bool telnetty_mcp_is_package_active(
    telnetty_context_t* ctx,
    const char* name
) {
    telnetty_mcp_package_t* package = telnetty_mcp_get_package(ctx, name);
    return package ? package->active : false;
}

static TELNETTY_UNUSED int telnetty_mcp_activate_package(
    telnetty_context_t* ctx,
    const char* name
) {
    telnetty_mcp_package_t* package = telnetty_mcp_get_package(ctx, name);
    if (!package) return -1;
    
    package->active = true;
    return 0;
}

static TELNETTY_UNUSED int telnetty_mcp_deactivate_package(
    telnetty_context_t* ctx,
    const char* name
) {
    telnetty_mcp_package_t* package = telnetty_mcp_get_package(ctx, name);
    if (!package) return -1;
    
    package->active = false;
    return 0;
}

static TELNETTY_UNUSED int telnetty_mcp_set_default_handler(
    telnetty_context_t* ctx,
    telnetty_mcp_package_handler_t handler,
    void* user_data
) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d) return -1;
    d->default_handler = handler;
    d->default_handler_data = user_data;
    return 0;
}

/* ============================================================================
 * Message Functions Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_mcp_send_subnegotiation(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
) {
    if (!ctx || !data) return -1;
    
    return telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_MCP, data, length);
}

/* Quote an MCP simple value. Caller frees the result. */
static TELNETTY_UNUSED char* telnetty__mcp_quote(const char* s) {
    size_t n = 0;
    for (const char* p = s ? s : ""; *p; p++) {
        if (*p == '"' || *p == '\\') n++;
        n++;
    }
    char* out = (char*)TELNETTY_MCP_MALLOC(n + 3);
    if (!out) return NULL;
    char* w = out;
    *w++ = '"';
    for (const char* p = s ? s : ""; *p; p++) {
        if (*p == '"' || *p == '\\') *w++ = '\\';
        *w++ = *p;
    }
    *w++ = '"';
    *w = '\0';
    return out;
}

/* True when `data` is already a `key: value` argument list. A leading '{'
 * or '[' is a legacy JSON blob and is wrapped instead. */
static TELNETTY_UNUSED int telnetty__mcp_looks_like_keyvals(const char* data) {
    if (!data || data[0] == '{' || data[0] == '[') return 0;
    const char* p = data;
    if (!isalpha((unsigned char)*p) && *p != '_') return 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) p++;
    return *p == ':';
}

static TELNETTY_UNUSED const char* telnetty__mcp_wire_key(telnetty_context_t* ctx, telnetty_mcp_data_t* d);

static TELNETTY_UNUSED int telnetty_mcp_send(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data
) {
    if (!ctx || !package || !message) return -1;
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d) return -1;

    const char* key = telnetty__mcp_wire_key(ctx, d);
    if (!key) return -1;

    char name[256];
    int nn;
    if (message[0] == '\0' || strcmp(package, message) == 0)
        nn = snprintf(name, sizeof(name), "%s", package);
    else
        nn = snprintf(name, sizeof(name), "%s-%s", package, message);
    if (nn < 0 || (size_t)nn >= sizeof(name)) return -1;

    char* args = NULL;
    if (data && data[0]) {
        if (telnetty__mcp_looks_like_keyvals(data)) {
            args = telnetty__mcp_strdup(data);
        } else {
            char* quoted = telnetty__mcp_quote(data);
            if (!quoted) return -1;
            size_t qn = strlen(quoted);
            args = (char*)TELNETTY_MCP_MALLOC(qn + 7);
            if (!args) { TELNETTY_MCP_FREE(quoted); return -1; }
            memcpy(args, "data: ", 6);
            memcpy(args + 6, quoted, qn + 1);
            TELNETTY_MCP_FREE(quoted);
        }
        if (!args) return -1;
    }

    size_t need = strlen(name) + 1 + strlen(key) + (args ? 1 + strlen(args) : 0) + 1;
    char* line = (char*)TELNETTY_MCP_MALLOC(need);
    if (!line) { TELNETTY_MCP_FREE(args); return -1; }
    if (args)
        snprintf(line, need, "%s %s %s", name, key, args);
    else
        snprintf(line, need, "%s %s", name, key);

    int r = telnetty__mcp_write_line(ctx, line);
    TELNETTY_MCP_FREE(line);
    TELNETTY_MCP_FREE(args);
    return r < 0 ? -1 : 0;
}

static TELNETTY_UNUSED int telnetty_mcp_sendf(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* fmt,
    ...
) {
    if (!ctx || !package || !message) return -1;
    
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
            data = (char*)TELNETTY_MCP_MALLOC(size + 1);
            if (data) {
                vsnprintf(data, size + 1, fmt, args);
            }
        }
        
        va_end(args);
    }
    
    /* Send MCP message */
    int result = telnetty_mcp_send(ctx, package, message, data);
    
    if (data) {
        TELNETTY_MCP_FREE(data);
    }
    
    return result;
}

static TELNETTY_UNUSED int telnetty_mcp_send_auth(
    telnetty_context_t* ctx,
    telnetty_mcp_auth_type_t auth_type,
    const char* credentials
) {
    if (!ctx) return -1;
    
    const char* auth_type_str = NULL;
    switch (auth_type) {
        case TELNETTY_MCP_AUTH_NONE:
            auth_type_str = "none";
            break;
        case TELNETTY_MCP_AUTH_KEY:
            auth_type_str = "key";
            break;
        case TELNETTY_MCP_AUTH_USER:
            auth_type_str = "user";
            break;
        default:
            return -1;
    }
    
    return telnetty_mcp_sendf(ctx, "mcp", "auth", 
        "{\"type\": \"%s\"%s%s}",
        auth_type_str,
        credentials ? ", \"credentials\": " : "",
        credentials ? credentials : "");
}

static TELNETTY_UNUSED int telnetty_mcp_send_negotiate(
    telnetty_context_t* ctx,
    const telnetty_mcp_package_t* packages,
    size_t package_count
) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d) return -1;

    if (!packages || package_count == 0) {
        telnetty__mcp_send_negotiate_all(ctx, d);
        return 0;
    }

    const char* key = telnetty__mcp_wire_key(ctx, d);
    if (!key) return -1;
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "mcp-negotiate-can %s package: mcp-negotiate min-version: 1.0 max-version: 2.0",
        key);
    if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
    for (size_t i = 0; i < package_count; i++) {
        const telnetty_mcp_package_t* pkg = &packages[i];
        if (!pkg->name) continue;
        const char* ver = pkg->version ? pkg->version : "1.0";
        n = snprintf(buf, sizeof(buf),
            "mcp-negotiate-can %s package: %s min-version: %s max-version: %s",
            key, pkg->name, ver, ver);
        if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
    }
    n = snprintf(buf, sizeof(buf), "mcp-negotiate-end %s", key);
    if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
    return 0;
}

static TELNETTY_UNUSED int telnetty_mcp_send_error(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    int code
) {
    if (!ctx || !message) return -1;
    
    char code_buf[32];
    
    if (code >= 0) {
        snprintf(code_buf, sizeof(code_buf), "%d", code);
    }
    
    return telnetty_mcp_sendf(ctx, "mcp", "error",
        "{\"message\": \"%s\"%s%s%s%s}",
        message,
        package ? ", \"package\": \"" : "",
        package ? package : "",
        package ? "\"" : "",
        code >= 0 ? code_buf : "");
}

static TELNETTY_UNUSED int telnetty_mcp_send_close(
    telnetty_context_t* ctx,
    const char* reason
) {
    if (!ctx) return -1;
    
    const char* reason_str = reason ? reason : "";
    
    return telnetty_mcp_sendf(ctx, "mcp", "close",
        reason ? "{\"reason\": \"%s\"}" : "{}",
        reason_str);
}

/* ============================================================================
 * Utility Functions Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_mcp_state_t telnetty_mcp_get_state(telnetty_context_t* ctx) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    return d ? d->state : TELNETTY_MCP_STATE_DISABLED;
}

static TELNETTY_UNUSED bool telnetty_mcp_is_enabled(telnetty_context_t* ctx) {
    telnetty_mcp_state_t s = telnetty_mcp_get_state(ctx);
    return s == TELNETTY_MCP_STATE_ENABLED || s == TELNETTY_MCP_STATE_NEGOTIATING;
}

static TELNETTY_UNUSED size_t telnetty_mcp_get_package_count(telnetty_context_t* ctx) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    return d ? d->package_count : 0;
}

/* --- MCP 2.1 in-band line parser --- */

/* Cap for a single MCP line — anything larger is rejected as a protocol
 * violation. Override at compile time if you need to accept oversized cords. */
#ifndef TELNETTY_MCP_MAX_LINE
#define TELNETTY_MCP_MAX_LINE 65536
#endif

/* Per-context PRNG seed for message IDs and auth keys. Not
 * cryptographically strong — good enough to be unpredictable to a peer that
 * doesn't already own the process. */
static TELNETTY_UNUSED uint64_t telnetty__mcp_seed(telnetty_context_t* ctx) {
    uint64_t s = (uint64_t)time(NULL);
    s ^= (uint64_t)(uintptr_t)ctx;
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return s;
}

/* Return a caller-owned base-36 string of `n_chars` characters seeded from
 * `s`. Suitable for keys / message IDs; deterministic given the seed. */
static TELNETTY_UNUSED char* telnetty__mcp_random_string(uint64_t* s, size_t n_chars) {
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char* out = (char*)TELNETTY_MCP_MALLOC(n_chars + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n_chars; i++) {
        *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
        out[i] = alphabet[(*s >> 33) % 36];
    }
    out[n_chars] = '\0';
    return out;
}

/* Lazily materialize the key we would offer as a client. */
static TELNETTY_UNUSED const char* telnetty__mcp_ensure_key(telnetty_context_t* ctx, telnetty_mcp_data_t* d) {
    if (d->auth_key) return d->auth_key;
    uint64_t s = telnetty__mcp_seed(ctx);
    d->auth_key = telnetty__mcp_random_string(&s, 12);
    return d->auth_key;
}

/* Key that belongs on every message after the opening mcp line.
 * Falls back to a locally generated key so telnetty_mcp_send works
 * before the handshake has finished. */
static TELNETTY_UNUSED const char* telnetty__mcp_wire_key(telnetty_context_t* ctx, telnetty_mcp_data_t* d) {
    if (d->session_key) return d->session_key;
    return telnetty__mcp_ensure_key(ctx, d);
}

static TELNETTY_UNUSED int telnetty__mcp_casecmp(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return (int)tolower((unsigned char)*a) - (int)tolower((unsigned char)*b);
}

static TELNETTY_UNUSED int telnetty__mcp_parse_ver(const char* s, int* maj, int* min) {
    if (!s) return -1;
    int a = 0, b = 0;
    if (sscanf(s, "%d.%d", &a, &b) != 2) return -1;
    if (a < 0 || b < 0) return -1;
    *maj = a;
    *min = b;
    return 0;
}

static TELNETTY_UNUSED int telnetty__mcp_ver_geq(int a_maj, int a_min, int b_maj, int b_min) {
    if (a_maj != b_maj) return a_maj > b_maj;
    return a_min >= b_min;
}

/* Ranges are inclusive. Missing `to` means a single version. */
static TELNETTY_UNUSED int telnetty__mcp_range_overlap(const char* a_min, const char* a_max,
                                       const char* b_min, const char* b_max) {
    int aM, am, bM, bm, cM, cm, dM, dm;
    if (telnetty__mcp_parse_ver(a_min, &aM, &am) != 0) return 0;
    if (!a_max) { bM = aM; bm = am; }
    else if (telnetty__mcp_parse_ver(a_max, &bM, &bm) != 0) return 0;
    if (telnetty__mcp_parse_ver(b_min, &cM, &cm) != 0) return 0;
    if (!b_max) { dM = cM; dm = cm; }
    else if (telnetty__mcp_parse_ver(b_max, &dM, &dm) != 0) return 0;
    /* overlap when each range's max is >= the other's min */
    return telnetty__mcp_ver_geq(dM, dm, aM, am) && telnetty__mcp_ver_geq(bM, bm, cM, cm);
}

static TELNETTY_UNUSED void telnetty__mcp_cord_release(telnetty_mcp_cord_t* c);

/* Look up a cord slot by tag; NULL means "not open". */
static TELNETTY_UNUSED telnetty_mcp_cord_t* telnetty__mcp_cord_find(telnetty_mcp_data_t* d, const char* tag) {
    if (!tag) return NULL;
    for (size_t i = 0; i < sizeof(d->cords)/sizeof(d->cords[0]); i++) {
        if (d->cords[i].tag && strcmp(d->cords[i].tag, tag) == 0) {
            return &d->cords[i];
        }
    }
    return NULL;
}

/* Reserve an empty cord slot, expiring stale ones first. */
static TELNETTY_UNUSED telnetty_mcp_cord_t* telnetty__mcp_cord_new(telnetty_mcp_data_t* d, const char* tag) {
    time_t now = time(NULL);
    for (size_t i = 0; i < sizeof(d->cords)/sizeof(d->cords[0]); i++) {
        if (d->cords[i].tag && d->cords[i].deadline > 0 && d->cords[i].deadline < now) {
            telnetty__mcp_cord_release(&d->cords[i]);
        }
    }
    for (size_t i = 0; i < sizeof(d->cords)/sizeof(d->cords[0]); i++) {
        if (!d->cords[i].tag) {
            d->cords[i].tag = telnetty__mcp_strdup(tag);
            if (!d->cords[i].tag) return NULL;
            d->cords[i].deadline = now + 60;
            return &d->cords[i];
        }
    }
    return NULL;
}

static TELNETTY_UNUSED void telnetty__mcp_cord_release(telnetty_mcp_cord_t* c) {
    if (!c) return;
    TELNETTY_MCP_FREE(c->tag);
    TELNETTY_MCP_FREE(c->accum);
    TELNETTY_MCP_FREE(c->package);
    TELNETTY_MCP_FREE(c->message);
    TELNETTY_MCP_FREE(c->args);
    memset(c, 0, sizeof(*c));
}

/* Append `n` bytes to the cord's accumulator. Grows on demand up to
 * TELNETTY_MCP_MAX_LINE. */
static TELNETTY_UNUSED int telnetty__mcp_cord_append(telnetty_mcp_cord_t* c, const char* s, size_t n) {
    size_t need = c->accum_len + n + 1;
    if (need > TELNETTY_MCP_MAX_LINE) return -1;
    if (need > c->accum_cap) {
        size_t nc = c->accum_cap ? c->accum_cap : 256;
        while (nc < need) nc *= 2;
        char* nb = (char*)TELNETTY_MCP_REALLOC(c->accum, nc);
        if (!nb) return -1;
        c->accum = nb;
        c->accum_cap = nc;
    }
    memcpy(c->accum + c->accum_len, s, n);
    c->accum_len += n;
    c->accum[c->accum_len] = '\0';
    return 0;
}

/* Advance `*p` past ASCII whitespace. */
static TELNETTY_UNUSED void telnetty__mcp_skip_ws(const char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

/* Extract the next MCP token from `*p`. If the value is quoted, unescape
 * \" and \\. Returns a caller-owned nul-terminated string, or NULL if the
 * input is malformed. Advances `*p` past the token. */
static TELNETTY_UNUSED char* telnetty__mcp_next_token(const char** p) {
    telnetty__mcp_skip_ws(p);
    if (**p == '\0') return NULL;

    if (**p == '"') {
        (*p)++;  /* consume opening quote */
        size_t cap = 32;
        char* out = (char*)TELNETTY_MCP_MALLOC(cap);
        if (!out) return NULL;
        size_t n = 0;
        while (**p && **p != '"') {
            char c = **p;
            if (c == '\\' && (*p)[1]) {
                (*p)++;
                c = **p;
            }
            if (n + 2 > cap) {
                size_t nc = cap * 2;
                char* nb = (char*)TELNETTY_MCP_REALLOC(out, nc);
                if (!nb) { TELNETTY_MCP_FREE(out); return NULL; }
                out = nb; cap = nc;
            }
            out[n++] = c;
            (*p)++;
        }
        if (**p == '"') (*p)++;
        out[n] = '\0';
        return out;
    }

    /* Bare token — non-space, non-colon terminates it. */
    const char* start = *p;
    while (**p && **p != ' ' && **p != '\t' && **p != ':') (*p)++;
    size_t n = (size_t)(*p - start);
    char* out = (char*)TELNETTY_MCP_MALLOC(n + 1);
    if (!out) return NULL;
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* Extract the value tied to a `key:` — either a quoted string or a bare
 * token up to the next whitespace. Returns caller-owned string. */
static TELNETTY_UNUSED char* telnetty__mcp_next_value(const char** p) {
    telnetty__mcp_skip_ws(p);
    if (**p == '"') {
        return telnetty__mcp_next_token(p);
    }
    const char* start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    size_t n = (size_t)(*p - start);
    char* out = (char*)TELNETTY_MCP_MALLOC(n + 1);
    if (!out) return NULL;
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* Scan a key/value string for `key: value` and return a caller-owned copy
 * of the value, or NULL if not present. */
static TELNETTY_UNUSED char* telnetty__mcp_kv_get(const char* rest, const char* key) {
    if (!rest || !key) return NULL;
    size_t klen = strlen(key);
    const char* p = rest;
    while (*p) {
        telnetty__mcp_skip_ws(&p);
        if (!*p) break;
        const char* kstart = p;
        while (*p && *p != ':' && *p != ' ' && *p != '\t') p++;
        size_t kn = (size_t)(p - kstart);
        if (*p == ':' && kn == klen && memcmp(kstart, key, klen) == 0) {
            p++;  /* skip ':' */
            return telnetty__mcp_next_value(&p);
        }
        /* Skip through the value we didn't want. */
        if (*p == ':') p++;
        char* discard = telnetty__mcp_next_value(&p);
        if (discard) TELNETTY_MCP_FREE(discard);
    }
    return NULL;
}

/* Emit `#$#<line>\r\n` on the wire, bypassing telnetty_send's IAC-escape
 * since MCP lines are ASCII and IAC-free by construction. */
static TELNETTY_UNUSED int telnetty__mcp_write_line(telnetty_context_t* ctx, const char* line) {
    if (!ctx || !line) return -1;
    size_t n = strlen(line);
    int r = telnetty_send(ctx, (const uint8_t*)"#$#", 3);
    if (r < 0) return -1;
    r = telnetty_send(ctx, (const uint8_t*)line, n);
    if (r < 0) return -1;
    return telnetty_send(ctx, (const uint8_t*)"\r\n", 2);
}

/* Advertise mcp-negotiate itself (required by MCP 2.1) and every active
 * registered package at its own version, then `#$#mcp-negotiate-end`. */
static TELNETTY_UNUSED void telnetty__mcp_send_negotiate_all(telnetty_context_t* ctx, telnetty_mcp_data_t* d) {
    char buf[512];
    const char* key = telnetty__mcp_wire_key(ctx, d);
    int n = snprintf(buf, sizeof(buf),
        "mcp-negotiate-can %s package: mcp-negotiate min-version: 1.0 max-version: 2.0", key);
    if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
    for (size_t i = 0; i < d->package_count; i++) {
        telnetty_mcp_package_t* pkg = d->packages[i];
        if (!pkg || !pkg->name || !pkg->active) continue;
        const char* ver = (pkg->version && *pkg->version) ? pkg->version : "1.0";
        n = snprintf(buf, sizeof(buf),
            "mcp-negotiate-can %s package: %s min-version: %s max-version: %s",
            key, pkg->name, ver, ver);
        if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
    }
    n = snprintf(buf, sizeof(buf), "mcp-negotiate-end %s", key);
    if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
}

/* Replace the session key (the key every later message must carry). */
static TELNETTY_UNUSED void telnetty__mcp_set_session_key(telnetty_mcp_data_t* d, const char* key) {
    if (d->session_key && d->session_key != d->auth_key)
        TELNETTY_MCP_FREE(d->session_key);
    d->session_key = NULL;
    if (key == d->auth_key) { d->session_key = d->auth_key; return; }
    size_t n = strlen(key);
    char* copy = (char*)TELNETTY_MCP_MALLOC(n + 1);
    if (!copy) return;
    memcpy(copy, key, n + 1);
    d->session_key = copy;
}

/* MCP 2.1 messages are `#$#keyword key: value ...` lines in the data stream,
 * not telopt 200. Returns 1 if the line was an MCP message.
 *
 * Handles the four wire cases:
 *   `#$#mcp version: 2.1 to: 2.1`               - initial handshake
 *   `#$#mcp-negotiate-can <key> package: X ...` - peer offers a package
 *   `#$#mcp-negotiate-end <key>`                - peer finished offering
 *   `#$#package-message <key> key: value ...`   - regular message
 *   `#$#* <tag> key: value`                     - cord continuation
 *   `#$#: <tag>`                                - cord terminator (fires dispatch)
 */
static TELNETTY_UNUSED int telnetty_mcp_handle_line(telnetty_context_t* ctx, const char* line) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d || !line || strncmp(line, "#$#", 3) != 0) return 0;

    size_t line_len = strlen(line);
    if (line_len > TELNETTY_MCP_MAX_LINE) {
        telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL, "MCP line exceeds TELNETTY_MCP_MAX_LINE");
        return 1;
    }

    const char* p = line + 3;
    telnetty__mcp_skip_ws(&p);

    /* Cord continuation: `#$#* <tag> <key>: <value>` — append to the
     * matching cord's accumulator so a later dispatch can consume it. */
    if (*p == '*' && (p[1] == ' ' || p[1] == '\t')) {
        p++;
        telnetty__mcp_skip_ws(&p);
        char* tag = telnetty__mcp_next_token(&p);
        if (!tag) return 1;
        telnetty_mcp_cord_t* c = telnetty__mcp_cord_find(d, tag);
        TELNETTY_MCP_FREE(tag);
        if (!c) return 1;
        telnetty__mcp_skip_ws(&p);
        if (telnetty__mcp_cord_append(c, p, strlen(p)) == 0) {
            (void)telnetty__mcp_cord_append(c, "\n", 1);
        }
        return 1;
    }

    /* Cord terminator: `#$#: <tag>` — the cord is complete. */
    if (*p == ':' && (p[1] == ' ' || p[1] == '\t')) {
        p++;
        telnetty__mcp_skip_ws(&p);
        char* tag = telnetty__mcp_next_token(&p);
        if (!tag) return 1;
        telnetty_mcp_cord_t* c = telnetty__mcp_cord_find(d, tag);
        if (c) telnetty__mcp_cord_release(c);
        TELNETTY_MCP_FREE(tag);
        return 1;
    }

    /* Ordinary keyword. */
    char* keyword = telnetty__mcp_next_token(&p);
    if (!keyword) return 1;

    if (strcmp(keyword, "mcp") == 0) {
        TELNETTY_MCP_FREE(keyword);
        /* Version ranges must overlap our 2.1. */
        char* ver = telnetty__mcp_kv_get(p, "version");
        char* to = telnetty__mcp_kv_get(p, "to");
        int ok = ver && telnetty__mcp_range_overlap(ver, to, "2.1", "2.1");
        TELNETTY_MCP_FREE(ver);
        TELNETTY_MCP_FREE(to);
        if (!ok) return 1;

        if (d->server_mode) {
            /* Client's reply: it chose the key; every later message in
             * either direction carries it. */
            char* key = telnetty__mcp_kv_get(p, "authentication-key");
            if (!key || !*key) { TELNETTY_MCP_FREE(key); return 1; }
            telnetty__mcp_set_session_key(d, key);
            TELNETTY_MCP_FREE(key);
        } else {
            /* Server's opening line: pick our key and answer with it. A
             * repeated opening line is ignored rather than re-keying. */
            if (d->state == TELNETTY_MCP_STATE_ENABLED ||
                d->state == TELNETTY_MCP_STATE_NEGOTIATING) return 1;
            telnetty__mcp_set_session_key(d, telnetty__mcp_ensure_key(ctx, d));
            char buf[256];
            int n = snprintf(buf, sizeof(buf),
                "mcp authentication-key: %s version: 2.1 to: 2.1",
                telnetty__mcp_wire_key(ctx, d));
            if (n > 0 && (size_t)n < sizeof(buf)) telnetty__mcp_write_line(ctx, buf);
        }
        if (!d->session_key) return 1;
        d->state = TELNETTY_MCP_STATE_ENABLED;
        telnetty__mcp_send_negotiate_all(ctx, d);
        return 1;
    }

    /* Every other message must carry the session key (MCP 2.1: messages
     * with a wrong key are ignored). This is what stops ordinary text
     * that happens to start with #$# from being taken as a message. */
    const char* args = p;   /* arguments after the key token */
    {
        const char* q = p;
        telnetty__mcp_skip_ws(&q);
        char* key = telnetty__mcp_next_token(&q);
        args = q;
        int ok = key && d->session_key && strcmp(key, d->session_key) == 0;
        TELNETTY_MCP_FREE(key);
        if (!ok) {
            telnetty_log(ctx, TELNETTY_LOG_DEBUG, "mcp",
                         "ignoring '%s' with wrong or missing key", keyword);
            TELNETTY_MCP_FREE(keyword);
            return 1;
        }
    }

    if (strcmp(keyword, "mcp-negotiate-can") == 0) {
        d->state = TELNETTY_MCP_STATE_NEGOTIATING;
        char* pkgname = telnetty__mcp_kv_get(args, "package");
        if (pkgname) {
            telnetty_mcp_package_t* pkg = telnetty_mcp_get_package(ctx, pkgname);
            if (pkg) pkg->peer_supports = true;
            TELNETTY_MCP_FREE(pkgname);
        }
        TELNETTY_MCP_FREE(keyword);
        return 1;
    }
    if (strcmp(keyword, "mcp-negotiate-end") == 0) {
        d->negotiate_done = 1;
        d->state = TELNETTY_MCP_STATE_ENABLED;
        TELNETTY_MCP_FREE(keyword);
        return 1;
    }

    /* Everything else is `package-message <key> args...`. Longest active
     * package name that prefixes the keyword wins. */
    telnetty_mcp_package_t* best = NULL;
    size_t best_n = 0;
    for (size_t i = 0; i < d->package_count; i++) {
        telnetty_mcp_package_t* pkg = d->packages[i];
        if (!pkg || !pkg->name || !pkg->active) continue;
        size_t n = strlen(pkg->name);
        if (n > best_n && strncmp(keyword, pkg->name, n) == 0 &&
            (keyword[n] == '\0' || keyword[n] == '-')) {
            best = pkg;
            best_n = n;
        }
    }

    /* If the peer supplied `_data-tag`, remember the tag so future
     * `#$#* <tag> ...` continuations accumulate correctly. */
    telnetty__mcp_skip_ws(&p);
    const char* rest = p;

    /* Skip the auth-key token — it's the first bare argument. */
    char* auth = telnetty__mcp_next_token(&p);
    if (auth) TELNETTY_MCP_FREE(auth);
    telnetty__mcp_skip_ws(&p);
    rest = p;

    char* data_tag = telnetty__mcp_kv_get(rest, "_data-tag");
    if (data_tag) {
        telnetty__mcp_cord_new(d, data_tag);
        TELNETTY_MCP_FREE(data_tag);
    }

    if (best && best->handler) {
        const char* msg = keyword + best_n;
        if (*msg == '-') msg++;
        best->handler(ctx, best->name, msg, rest, best->user_data);
    } else if (d->default_handler) {
        d->default_handler(ctx, keyword, "", rest, d->default_handler_data);
    }
    TELNETTY_MCP_FREE(keyword);
    return 1;
}

/* Assemble `#$#` lines from decoded in-band bytes. Installed on
 * telnetty__mcp_data_hook by telnetty_enable_mcp. `data` is only valid
 * for this call, so anything we keep is copied into d->line_acc.
 *
 * line_mode: 0 = start of line, 1 = skip (not MCP), 2 = collecting. */
static TELNETTY_UNUSED void telnetty__mcp_on_data(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d || d->disabled || !data || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = data[i];

        if (ch == '\r' || ch == '\n') {
            if (d->line_mode == 2 && d->line_len >= 3 &&
                d->line_acc && memcmp(d->line_acc, "#$#", 3) == 0) {
                if (d->line_len + 1 > d->line_cap) {
                    char* nbuf = (char*)TELNETTY_MCP_REALLOC(d->line_acc, d->line_len + 1);
                    if (nbuf) {
                        d->line_acc = nbuf;
                        d->line_cap = d->line_len + 1;
                    }
                }
                if (d->line_acc && d->line_len < d->line_cap) {
                    d->line_acc[d->line_len] = '\0';
                    (void)telnetty_mcp_handle_line(ctx, d->line_acc);
                }
            }
            d->line_len = 0;
            d->line_mode = 0;
            if (ch == '\r' && i + 1 < len && data[i + 1] == '\n') i++;
            continue;
        }

        if (d->line_mode == 1) continue;

        if (d->line_mode == 0) {
            if (ch == '#') d->line_mode = 2;
            else { d->line_mode = 1; continue; }
        }

        if (d->line_len >= TELNETTY_MCP_MAX_LINE) {
            telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL,
                                  "MCP line exceeds TELNETTY_MCP_MAX_LINE");
            d->line_len = 0;
            d->line_mode = 1;
            continue;
        }

        if (!d->line_acc || d->line_len + 1 >= d->line_cap) {
            size_t ncap = d->line_cap ? d->line_cap * 2 : 128;
            if (ncap > TELNETTY_MCP_MAX_LINE + 1) ncap = TELNETTY_MCP_MAX_LINE + 1;
            if (ncap < d->line_len + 2) ncap = d->line_len + 2;
            char* nbuf = (char*)TELNETTY_MCP_REALLOC(d->line_acc, ncap);
            if (!nbuf) {
                d->line_len = 0;
                d->line_mode = 1;
                continue;
            }
            d->line_acc = nbuf;
            d->line_cap = ncap;
        }

        d->line_acc[d->line_len++] = (char)ch;
        if (d->line_len == 3 && memcmp(d->line_acc, "#$#", 3) != 0) {
            d->line_len = 0;
            d->line_mode = 1;
        }
    }
}

/* Shallow copies: the string pointers stay owned by the context and are
 * valid until the package is unregistered or the context destroyed. */
static TELNETTY_UNUSED size_t telnetty_mcp_get_packages(
    telnetty_context_t* ctx,
    telnetty_mcp_package_t* packages,
    size_t max_packages
) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d || !packages) return 0;
    size_t n = 0;
    for (size_t i = 0; i < d->package_count && n < max_packages; i++) {
        if (d->packages[i]) packages[n++] = *d->packages[i];
    }
    return n;
}

/* Per-context counter (was a process-wide static shared by every
 * connection). */
static TELNETTY_UNUSED int telnetty_mcp_generate_message_id(telnetty_context_t* ctx) {
    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    if (!d) return -1;
    if (d->last_message_id == INT_MAX) d->last_message_id = 0;
    return ++d->last_message_id;
}

static TELNETTY_UNUSED int telnetty_mcp_parse_message(
    const uint8_t* data,
    size_t length,
    telnetty_mcp_message_t* message
) {
    if (!data || !message || length == 0) return -1;
    
    /* Simple MCP message parsing */
    /* Format: #package message data\r\n */
    
    if (data[0] != '#') return -1;  /* Invalid MCP message */
    
    /* Find message components */
    size_t package_start = 1;
    size_t package_end = 0;
    size_t message_start = 0;
    size_t message_end = 0;
    size_t data_start = 0;
    size_t data_end = length;
    
    /* Find package end */
    for (size_t i = package_start; i < length; i++) {
        if (data[i] == ' ') {
            package_end = i;
            message_start = i + 1;
            break;
        }
    }
    
    /* Find message end */
    if (message_start > 0) {
        for (size_t i = message_start; i < length; i++) {
            if (data[i] == ' ') {
                message_end = i;
                data_start = i + 1;
                break;
            } else if (data[i] == '\r') {
                message_end = i;
                data_end = i;
                break;
            }
        }
    }
    
    /* Extract components */
    if (package_end > package_start) {
        message->package = telnetty__mcp_strndup((const char*)data + package_start, 
                                   package_end - package_start);
    }
    
    if (message_end > message_start) {
        message->message = telnetty__mcp_strndup((const char*)data + message_start, 
                                   message_end - message_start);
    }
    
    if (data_start > 0 && data_end > data_start) {
        message->data = telnetty__mcp_strndup((const char*)data + data_start, 
                                data_end - data_start);
    }
    
    message->timestamp = time(NULL);
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_mcp_build_message(
    const telnetty_mcp_message_t* message,
    uint8_t* buffer,
    size_t buffer_size
) {
    if (!message || !buffer) return -1;
    
    /* Calculate required size */
    size_t required_size = 1; /* # */
    size_t package_len = message->package ? strlen(message->package) : 0;
    size_t message_len = message->message ? strlen(message->message) : 0;
    size_t data_len = message->data ? strlen(message->data) : 0;
    if (package_len > SIZE_MAX - required_size) return -1;
    required_size += package_len;
    if (required_size == SIZE_MAX) return -1;
    required_size += 1; /* space */
    if (message_len > SIZE_MAX - required_size) return -1;
    required_size += message_len;
    if (message->data) {
        if (required_size == SIZE_MAX) return -1;
        if (data_len > SIZE_MAX - required_size - 1) return -1;
        required_size += 1; /* space */
        required_size += data_len;
    }
    if (required_size > SIZE_MAX - 2 || required_size + 2 > (size_t)INT_MAX) return -1;
    required_size += 2; /* \r\n */
    
    if (buffer_size < required_size) return -1;
    
    /* Build message */
    size_t offset = 0;
    buffer[offset++] = '#';
    
    if (message->package) {
        memcpy(buffer + offset, message->package, package_len);
        offset += package_len;
    }
    
    buffer[offset++] = ' ';
    
    if (message->message) {
        memcpy(buffer + offset, message->message, message_len);
        offset += message_len;
    }
    
    if (message->data) {
        buffer[offset++] = ' ';
        memcpy(buffer + offset, message->data, data_len);
        offset += data_len;
    }
    
    buffer[offset++] = '\r';
    buffer[offset++] = '\n';
    
    return (int)offset;
}

/* ============================================================================
 * Built-in Package Implementation
 * ============================================================================ */

/* The builtins are placeholders with empty handlers, so they are
 * registered inactive: not advertised in mcp-negotiate-can, and not
 * matched on dispatch. Active, "dns" would prefix-match every real
 * dns-org-mud-moo-* message and swallow it before the default handler.
 * Install real handlers and call telnetty_mcp_activate_package to use
 * them. */
static TELNETTY_UNUSED int telnetty_mcp_register_builtin_packages(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    static const char* const builtin[] = { "mcp-core", "simple-edit", "redirect", "dns" };
    
    /* Register Core package */
    telnetty_mcp_register_package(ctx, "mcp-core", "2.1", 
        "MCP Core Package", 
        telnetty_mcp_core_handler, NULL);
    
    /* Register SimpleEdit package */
    telnetty_mcp_register_package(ctx, "simple-edit", "1.0", 
        "SimpleEdit package", 
        telnetty_mcp_simpleedit_handler, NULL);
    
    /* Register Redirect package */
    telnetty_mcp_register_package(ctx, "redirect", "1.0", 
        "Redirect package", 
        telnetty_mcp_redirect_handler, NULL);
    
    /* Register DNS package */
    telnetty_mcp_register_package(ctx, "dns", "1.0", 
        "DNS package", 
        telnetty_mcp_dns_handler, NULL);

    for (size_t i = 0; i < sizeof(builtin) / sizeof(builtin[0]); i++)
        telnetty_mcp_deactivate_package(ctx, builtin[i]);
    
    return 0;
}

/* mcp and mcp-negotiate are handled by telnetty_mcp_handle_line itself;
 * the others are application protocols (editing, redirection) that only
 * the application can implement. Replace the handler and activate the
 * package to use one. */
static TELNETTY_UNUSED void telnetty_mcp_core_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
) {
    (void)ctx; (void)package; (void)message; (void)data; (void)user_data;
}

static TELNETTY_UNUSED void telnetty_mcp_simpleedit_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
) {
    (void)ctx; (void)package; (void)message; (void)data; (void)user_data;
}

static TELNETTY_UNUSED void telnetty_mcp_redirect_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
) {
    (void)ctx; (void)package; (void)message; (void)data; (void)user_data;
}

static TELNETTY_UNUSED void telnetty_mcp_dns_handler(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const char* data,
    void* user_data
) {
    (void)ctx; (void)package; (void)message; (void)data; (void)user_data;
}

#endif /* TELNETTY_MCP_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_MCP_H */
