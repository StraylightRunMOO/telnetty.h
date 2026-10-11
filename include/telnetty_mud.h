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
 * @version 1.2.1
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

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * MSDP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_MSDP)
 * @param command Command received
 * @param user_data User data (should be telnetty_msdp_data_t*)
 */
static TELNETTY_UNUSED void telnetty_msdp_handler(
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
static TELNETTY_UNUSED int telnetty_enable_msdp(
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
static TELNETTY_UNUSED int telnetty_msdp_send(
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
static TELNETTY_UNUSED int telnetty_msdp_sendf(
    telnetty_context_t* ctx,
    const char* var,
    const char* fmt,
    ...
);

/**
 * Ask the server for a variable's current value: VAR "SEND" VAL <var>.
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_msdp_request(
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
static TELNETTY_UNUSED int telnetty_msdp_set_flags(
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
static TELNETTY_UNUSED int telnetty_msdp_clear_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
);

/**
 * Get the last known value of an MSDP variable: the last scalar value the
 * peer sent for it, or the last value we sent with telnetty_msdp_send().
 * Table and array values are not stored (use the recursive callback).
 * 
 * @param ctx TELNET context
 * @param var Variable name
 * @return Variable value or NULL if not found
 */
static TELNETTY_UNUSED const char* telnetty_msdp_get(
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
static TELNETTY_UNUSED int telnetty_msdp_remove(
    telnetty_context_t* ctx,
    const char* var
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

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

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * GMCP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_GMCP)
 * @param command Command received
 * @param user_data User data (should be telnetty_gmcp_data_t*)
 */
static TELNETTY_UNUSED void telnetty_gmcp_handler(
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
static TELNETTY_UNUSED int telnetty_enable_gmcp(
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
static TELNETTY_UNUSED int telnetty_gmcp_send(
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
static TELNETTY_UNUSED int telnetty_gmcp_sendf(
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
static TELNETTY_UNUSED int telnetty_gmcp_register_module(
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
static TELNETTY_UNUSED int telnetty_gmcp_unregister_module(
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
static TELNETTY_UNUSED int telnetty_gmcp_send_hello(
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
static TELNETTY_UNUSED int telnetty_gmcp_send_supports(
    telnetty_context_t* ctx,
    const char* const* modules,
    size_t count
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

/* ============================================================================
 * MTTS (MUD Terminal Type Standard)
 * ============================================================================ */

/* MTTS option code */
#define TELNETTY_TELOPT_MTTS 24  /* Uses same code as TTYPE */

/* MTTS flags */
typedef enum {
    /* Canonical MTTS bit assignments per the MTTS 3.0 standard. Do NOT
     * renumber existing values — MUDs match on the raw bit weight, not
     * the enum name, so a client that advertises "MTTS 265" (=1|8|256)
     * must land on ANSI | 256COLORS | TRUECOLOR everywhere. */
    TELNETTY_MTTS_FLAG_ANSI          = 1 << 0,  /**< ANSI color support */
    TELNETTY_MTTS_FLAG_VT100         = 1 << 1,  /**< VT100 emulation */
    TELNETTY_MTTS_FLAG_UTF8          = 1 << 2,  /**< UTF-8 support */
    TELNETTY_MTTS_FLAG_256COLORS     = 1 << 3,  /**< 256-color support */
    TELNETTY_MTTS_FLAG_MOUSETRACKING = 1 << 4,  /**< Mouse tracking */
    TELNETTY_MTTS_FLAG_COLORPALETTE  = 1 << 5,  /**< OSC color-palette query */
    TELNETTY_MTTS_FLAG_SCREENREADER  = 1 << 6,  /**< Screen reader / accessibility */
    TELNETTY_MTTS_FLAG_PROXY         = 1 << 7,  /**< Client is a proxy */
    TELNETTY_MTTS_FLAG_TRUECOLOR     = 1 << 8,  /**< 24-bit truecolor SGR */
    TELNETTY_MTTS_FLAG_MNES          = 1 << 9,  /**< MUD NEW-ENVIRON Standard */
    TELNETTY_MTTS_FLAG_MSLP          = 1 << 10  /**< Mud Server Link Protocol */
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

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * MTTS option handler
 * 
 * @param ctx TELNET context
 * @param option Option code
 * @param command Command received
 * @param user_data User data (should be telnetty_mtts_data_t*)
 */
static TELNETTY_UNUSED void telnetty_mtts_handler(
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
static TELNETTY_UNUSED bool telnetty_mtts_has_flag(
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
static TELNETTY_UNUSED int telnetty_mtts_get_client_info(
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
static TELNETTY_UNUSED int telnetty_mtts_send_response(
    telnetty_context_t* ctx,
    uint64_t flags,
    const char* client_name,
    const char* client_version
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

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

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * MSSP option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_MSSP)
 * @param command Command received
 * @param user_data User data (should be telnetty_mssp_data_t*)
 */
static TELNETTY_UNUSED void telnetty_mssp_handler(
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
static TELNETTY_UNUSED int telnetty_enable_mssp(
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
static TELNETTY_UNUSED int telnetty_mssp_add(
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
static TELNETTY_UNUSED int telnetty_mssp_set_standard_vars(
    telnetty_context_t* ctx,
    const char* server_name,
    const char* server_type,
    const char* server_url,
    int server_port,
    int player_count,
    int max_players
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

/* ============================================================================
 * MSP (MUD Sound Protocol) — TELOPT 90
 *
 * Peer offers TELOPT 90 (WILL). We ACK with DO. From then on, MSP triggers
 * arrive INLINE in the ordinary data stream as `!!SOUND(...)` and
 * `!!MUSIC(...)` sequences terminated by the closing paren, e.g.
 *
 *   !!SOUND(cast.wav V=75 L=1 P=50 U=http://foo/wav/ T=magic)
 *
 * We scan every DATA event before it reaches the app callback, extract
 * matching triggers, and fire them through a dedicated MSP callback.
 * ============================================================================ */

#define TELNETTY_TELOPT_MSP 90

typedef enum {
    TELNETTY_MSP_KIND_SOUND = 0,
    TELNETTY_MSP_KIND_MUSIC = 1
} telnetty_msp_kind_t;

/* Parsed MSP trigger. Fields point into caller-owned storage on the
 * dispatch — copy anything you need to keep past the callback. */
typedef struct {
    telnetty_msp_kind_t kind;
    const char* file;    /* filename or "Off" for cancel */
    int volume;          /* V=... (0-100), or -1 if absent */
    int loops;           /* L=... (0, -1, or n), or -1 if absent */
    int priority;        /* P=... (0-100), or -1 if absent */
    int cont;            /* C=... (0/1), or -1 if absent (MUSIC only) */
    const char* type;    /* T=... category, or NULL */
    const char* url;     /* U=... base url, or NULL */
} telnetty_msp_trigger_t;

typedef void (*telnetty_msp_callback_t)(
    telnetty_context_t* ctx,
    const telnetty_msp_trigger_t* trigger,
    void* user_data
);

typedef struct {
    telnetty_msp_callback_t cb;
    void* user_data;
    bool enabled;
} telnetty_msp_data_t;

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * Enable MSP: register the TELOPT 90 handler and install a data-stream
 * hook that scans for !!SOUND / !!MUSIC triggers before they reach the
 * app-level DATA callback. Triggers are consumed (removed from the DATA
 * payload) and dispatched to `cb`. Everything else passes through.
 */
static TELNETTY_UNUSED int telnetty_enable_msp(
    telnetty_context_t* ctx,
    telnetty_msp_callback_t cb,
    void* user_data
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

/* ============================================================================
 * ATCP (Achaea Telnet Client Protocol) — TELOPT 200
 *
 * The historical owner of TELOPT 200 (predates GMCP). Iron Realms MUDs
 * still speak this. Frames arrive as:
 *
 *   IAC SB 200 <package>.<message> <payload> IAC SE
 *
 * where <package>.<message> is a space-terminated token and <payload>
 * is opaque bytes (often, but not always, printable text). The
 * telnetty_atcp_callback_t receives the split package/message names and
 * the raw payload; the callback owns nothing (all buffers are transient).
 *
 * Sending: telnetty_atcp_send(ctx, package, message, payload_len, payload)
 * builds the SB frame and emits it via telnetty_send_subnegotiation.
 * ============================================================================ */

#define TELNETTY_TELOPT_ATCP 200

typedef void (*telnetty_atcp_callback_t)(
    telnetty_context_t* ctx,
    const char* package,      /* e.g. "Char" */
    const char* message,      /* e.g. "Vitals", or "" if none */
    const uint8_t* payload,
    size_t payload_len,
    void* user_data
);

typedef struct {
    telnetty_atcp_callback_t cb;
    void* user_data;
    bool enabled;
} telnetty_atcp_data_t;

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * Enable ATCP (TELOPT 200). Registers WILL, installs the SB handler,
 * and dispatches inbound frames to `cb`. `cb` may be NULL; in that case
 * negotiation still works but no dispatch happens.
 */
static TELNETTY_UNUSED int telnetty_enable_atcp(
    telnetty_context_t* ctx,
    telnetty_atcp_callback_t cb,
    void* user_data
);

/**
 * Send an ATCP frame. `payload` may be NULL / `payload_len` zero for
 * message-only frames. Returns 0 on success, -1 on failure. Payloads
 * larger than TELNETTY_MAX_SB_LENGTH minus the header are rejected.
 */
static TELNETTY_UNUSED int telnetty_atcp_send(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const uint8_t* payload,
    size_t payload_len
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

/* ============================================================================
 * ZMP (Zenith MUD Protocol) — TELOPT 93
 *
 * Command-oriented protocol used by Discworld and derivatives. Frames:
 *
 *   IAC SB 93 <cmd> NUL <arg1> NUL <arg2> NUL ... IAC SE
 *
 * where <cmd> is a dot-separated name (e.g. "core.hello", "zmp.check")
 * and each argument is a NUL-terminated string. First-contact handshake:
 * either side may send `zmp.check <feature>` to probe support; the peer
 * replies with `zmp.support <feature>` or `zmp.no-support <feature>`.
 *
 * The callback receives the command name plus an argv-style array of
 * NUL-terminated argument strings that all point into transient storage.
 * ============================================================================ */

#define TELNETTY_TELOPT_ZMP 93

/* MXP (MUD eXtended Protocol) uses inline markup in ordinary DATA. The
 * scanner reports complete tags while preserving the original DATA bytes. */
#define TELNETTY_TELOPT_MXP 91

typedef enum {
    TELNETTY_MXP_MODE_OPEN = 0,
    TELNETTY_MXP_MODE_SECURE = 1,
    TELNETTY_MXP_MODE_LOCKED = 2
} telnetty_mxp_mode_t;

typedef void (*telnetty_mxp_tag_callback_t)(
    telnetty_context_t* ctx, const char* name, const char* attributes,
    bool closing, telnetty_mxp_mode_t mode, void* user_data
);

typedef struct {
    telnetty_mxp_tag_callback_t cb;
    void* user_data;
    size_t length;
    char tag[1024];
    bool in_tag;
    bool quoted;
    char quote;
    bool enabled;
    telnetty_mxp_mode_t mode;
} telnetty_mxp_data_t;

#ifdef TELNETTY_MUD_IMPLEMENTATION
/** Enable the MXP option and scan negotiated DATA for complete inline tags.
 * Names and attributes are transient, read-only strings. This is a tokenizer:
 * applications remain responsible for deciding which received tags to trust.
 */
static TELNETTY_UNUSED int telnetty_enable_mxp(
    telnetty_context_t* ctx, telnetty_mxp_tag_callback_t cb, void* user_data
);
/** Set the mode value reported to later MXP callbacks. */
static TELNETTY_UNUSED int telnetty_mxp_set_mode(
    telnetty_context_t* ctx, telnetty_mxp_mode_t mode
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

typedef void (*telnetty_zmp_callback_t)(
    telnetty_context_t* ctx,
    const char* command,       /* dot-separated, e.g. "core.hello" */
    const char* const* argv,   /* argv[argc] = NULL sentinel */
    size_t argc,
    void* user_data
);

typedef struct {
    telnetty_zmp_callback_t cb;
    void* user_data;
    bool enabled;
} telnetty_zmp_data_t;

#ifdef TELNETTY_MUD_IMPLEMENTATION
/**
 * Enable ZMP (TELOPT 93). `cb` may be NULL to negotiate without a
 * dispatcher. `argv` inside the callback is transient — copy anything
 * you need to keep past the call.
 */
static TELNETTY_UNUSED int telnetty_enable_zmp(
    telnetty_context_t* ctx,
    telnetty_zmp_callback_t cb,
    void* user_data
);

/**
 * Send a ZMP command. `argv` is a NULL-terminated array of C strings;
 * pass `argv = NULL` for a command with no arguments. Returns 0 on
 * success, -1 on failure. The whole frame (command + NULs + args) is
 * capped at TELNETTY_MAX_SB_LENGTH.
 */
static TELNETTY_UNUSED int telnetty_zmp_send(
    telnetty_context_t* ctx,
    const char* command,
    const char* const* argv
);
#endif /* TELNETTY_MUD_IMPLEMENTATION */

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

/* strdup is POSIX, not C99, and allocates with libc malloc rather than
 * TELNETTY_MUD_MALLOC, which these strings are freed with. */
static TELNETTY_UNUSED char* telnetty__mud_strdup(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)TELNETTY_MUD_MALLOC(n + 1);
    if (out) memcpy(out, s, n + 1);
    return out;
}

/* Forward declarations for internal functions */
static TELNETTY_UNUSED telnetty_msdp_data_t* telnetty_msdp_data_create(void);
static TELNETTY_UNUSED void telnetty_msdp_data_destroy(telnetty_msdp_data_t* data);
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

static TELNETTY_UNUSED void telnetty_msdp_data_free(void* p) {
    telnetty_msdp_data_destroy((telnetty_msdp_data_t*)p);
}

/* Q-method already answered. This only records the outcome. */
static TELNETTY_UNUSED void telnetty_msdp_handler(
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
static TELNETTY_UNUSED void telnetty__msdp_walk_recursive(
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

static TELNETTY_UNUSED telnetty_msdp_data_t* telnetty__msdp_of(telnetty_context_t* ctx) {
    telnetty_option_t* opt = ctx ? ctx->options[TELNETTY_TELOPT_MSDP] : NULL;
    return (opt && opt->user_data) ? (telnetty_msdp_data_t*)opt->user_data : NULL;
}

static TELNETTY_UNUSED telnetty_msdp_variable_t* telnetty__msdp_find(telnetty_msdp_data_t* d, const char* name) {
    for (size_t i = 0; i < d->variable_count; i++) {
        if (strcmp(d->variables[i].name, name) == 0) return &d->variables[i];
    }
    return NULL;
}

/* Find or append `name` in the variable table. */
static TELNETTY_UNUSED telnetty_msdp_variable_t* telnetty__msdp_slot(telnetty_msdp_data_t* d, const char* name) {
    telnetty_msdp_variable_t* v = telnetty__msdp_find(d, name);
    if (v) return v;
    if (d->variable_count == d->capacity) {
        size_t ncap = d->capacity ? d->capacity * 2 : 16;
        telnetty_msdp_variable_t* nv = (telnetty_msdp_variable_t*)TELNETTY_MUD_REALLOC(
            d->variables, ncap * sizeof(telnetty_msdp_variable_t));
        if (!nv) return NULL;
        d->variables = nv;
        d->capacity = ncap;
    }
    size_t n = strlen(name);
    char* copy = (char*)TELNETTY_MUD_MALLOC(n + 1);
    if (!copy) return NULL;
    memcpy(copy, name, n + 1);
    v = &d->variables[d->variable_count++];
    memset(v, 0, sizeof(*v));
    v->name = copy;
    return v;
}

/* Record a scalar value; on allocation failure the old value stays. */
static TELNETTY_UNUSED telnetty_msdp_variable_t* telnetty__msdp_store(telnetty_msdp_data_t* d,
                                                      const char* name, const char* value) {
    telnetty_msdp_variable_t* v = telnetty__msdp_slot(d, name);
    if (!v) return NULL;
    size_t n = strlen(value);
    char* copy = (char*)TELNETTY_MUD_MALLOC(n + 1);
    if (!copy) return NULL;
    memcpy(copy, value, n + 1);
    TELNETTY_MUD_FREE(v->value);
    v->value = copy;
    v->last_updated = time(NULL);
    return v;
}

static TELNETTY_UNUSED void telnetty_msdp_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_msdp_data_t* msdp = (telnetty_msdp_data_t*)user_data;
    if (!msdp || !data) return;

    /* Recursive pass (structured) — runs when a recursive handler is set. */
    telnetty__msdp_walk_recursive(ctx, msdp, data, length);

    /* Flat pass (legacy compatibility): records scalar values for
     * telnetty_msdp_get and feeds the flat handler, so existing callers see
     * the same var/val stream even when a recursive handler is set. */
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
        int scalar = 0;
        if (i < length && data[i] == TELNETTY_MSDP_VAL) {
            i++;
            scalar = !(i < length && (data[i] == TELNETTY_MSDP_TABLE_OPEN ||
                                      data[i] == TELNETTY_MSDP_ARRAY_OPEN));
            size_t vls = i;
            while (i < length && data[i] > TELNETTY_MSDP_ARRAY_CLOSE) i++;
            size_t vln = i - vls;
            if (vln >= sizeof(val)) vln = sizeof(val) - 1;
            memcpy(val, data + vls, vln);
            val[vln] = '\0';
        }
        if (scalar) {
            telnetty_msdp_variable_t* v = telnetty__msdp_store(msdp, var, val);
            if (v) v->flags |= TELNETTY_MSDP_FLAG_UPDATED;
        }
        if (msdp->handler) msdp->handler(ctx, var, val, msdp->user_data);
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

static TELNETTY_UNUSED int telnetty_enable_msdp(
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

static TELNETTY_UNUSED int telnetty_msdp_send(
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

    telnetty_msdp_data_t* d = telnetty__msdp_of(ctx);
    if (result == 0 && d && val) {
        telnetty_msdp_variable_t* v = telnetty__msdp_store(d, var, val);
        if (v) v->flags = (v->flags & ~(uint32_t)TELNETTY_MSDP_FLAG_UPDATED)
                        | TELNETTY_MSDP_FLAG_REPORTED;
    }

    return result;
}

static TELNETTY_UNUSED int telnetty_msdp_sendf(
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

static TELNETTY_UNUSED int telnetty_msdp_request(
    telnetty_context_t* ctx,
    const char* var
) {
    if (!ctx || !var) return -1;

    /* MSDP spec: the client asks with VAR "SEND" VAL "<name>". */
    static const char cmd[] = "SEND";
    size_t var_len = strlen(var);
    size_t total_size = 2 + (sizeof(cmd) - 1) + var_len;

    uint8_t* buffer = (uint8_t*)TELNETTY_MUD_MALLOC(total_size);
    if (!buffer) return -1;

    size_t off = 0;
    buffer[off++] = TELNETTY_MSDP_VAR;
    memcpy(buffer + off, cmd, sizeof(cmd) - 1);
    off += sizeof(cmd) - 1;
    buffer[off++] = TELNETTY_MSDP_VAL;
    memcpy(buffer + off, var, var_len);
    off += var_len;

    int result = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_MSDP, buffer, off);

    TELNETTY_MUD_FREE(buffer);

    return result;
}

/* Flags live on the variable table entry; setting flags on an unknown
 * variable creates it (with no value) so a server can declare REPORTABLE /
 * SENDABLE variables up front. */
static TELNETTY_UNUSED int telnetty_msdp_set_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
) {
    telnetty_msdp_data_t* d = telnetty__msdp_of(ctx);
    if (!d || !var) return -1;
    telnetty_msdp_variable_t* v = telnetty__msdp_slot(d, var);
    if (!v) return -1;
    v->flags |= flags;
    return 0;
}

static TELNETTY_UNUSED int telnetty_msdp_clear_flags(
    telnetty_context_t* ctx,
    const char* var,
    uint32_t flags
) {
    telnetty_msdp_data_t* d = telnetty__msdp_of(ctx);
    if (!d || !var) return -1;
    telnetty_msdp_variable_t* v = telnetty__msdp_find(d, var);
    if (!v) return -1;
    v->flags &= ~flags;
    return 0;
}

static TELNETTY_UNUSED const char* telnetty_msdp_get(
    telnetty_context_t* ctx,
    const char* var
) {
    telnetty_msdp_data_t* d = telnetty__msdp_of(ctx);
    if (!d || !var) return NULL;
    telnetty_msdp_variable_t* v = telnetty__msdp_find(d, var);
    return v ? v->value : NULL;
}

static TELNETTY_UNUSED int telnetty_msdp_remove(
    telnetty_context_t* ctx,
    const char* var
) {
    telnetty_msdp_data_t* d = telnetty__msdp_of(ctx);
    if (!d || !var) return -1;
    telnetty_msdp_variable_t* v = telnetty__msdp_find(d, var);
    if (!v) return -1;
    TELNETTY_MUD_FREE(v->name);
    TELNETTY_MUD_FREE(v->value);
    size_t idx = (size_t)(v - d->variables);
    memmove(v, v + 1, (d->variable_count - idx - 1) * sizeof(*v));
    d->variable_count--;
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

static TELNETTY_UNUSED void telnetty_gmcp_data_free(void* p) {
    telnetty_gmcp_data_destroy((telnetty_gmcp_data_t*)p);
}

static TELNETTY_UNUSED void telnetty_gmcp_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_gmcp_data_t* gd = (telnetty_gmcp_data_t*)user_data;
    if (!gd || !gd->handler || !data || length == 0) return;
    const char* sp = (const char*)memchr(data, ' ', length);
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
    /* The JSON body is copied whole: payloads such as Room.Info routinely
     * exceed any fixed stack buffer, and truncated JSON is unparseable. */
    char* jsonbuf = NULL;
    if (sp && (size_t)(sp - (const char*)data) + 1 < length) {
        size_t jl = length - (size_t)(sp + 1 - (const char*)data);
        jsonbuf = (char*)TELNETTY_MUD_MALLOC(jl + 1);
        if (!jsonbuf) {
            telnetty_report_error(ctx, TELNETTY_ERR_ALLOC, "GMCP payload allocation failed");
            return;
        }
        memcpy(jsonbuf, sp + 1, jl);
        jsonbuf[jl] = '\0';
        json = jsonbuf;
    }
    gd->handler(ctx, package, message, json, gd->user_data);
    TELNETTY_MUD_FREE(jsonbuf);
}

static TELNETTY_UNUSED void telnetty_gmcp_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (option != TELNETTY_TELOPT_GMCP) return;
    telnetty_gmcp_data_t* gmcp_data = (telnetty_gmcp_data_t*)user_data;
    if (!gmcp_data) return;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_GMCP];
    gmcp_data->negotiation_complete = opt &&
        (opt->us == TELNETTY_Q_YES || opt->him == TELNETTY_Q_YES);
    /* Core.Hello and Core.Supports.Set are client -> server messages. We
     * act as the client when the peer offered GMCP (its WILL settled). */
    if (command == TELNETTY_WILL) {
        telnetty_gmcp_send_hello(ctx, "Telnetty", TELNETTY_VERSION_STRING);
        if (gmcp_data->module_count > 0)
            telnetty_gmcp_send_supports(ctx, (const char* const*)gmcp_data->supported_modules,
                                        gmcp_data->module_count);
    }
}

static TELNETTY_UNUSED int telnetty_enable_gmcp(
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

static TELNETTY_UNUSED int telnetty_gmcp_send(
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

static TELNETTY_UNUSED int telnetty_gmcp_sendf(
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

static TELNETTY_UNUSED int telnetty__gmcp_send_supports_verb(
    telnetty_context_t* ctx,
    const char* verb,
    const char* const* modules,
    size_t count
);

static TELNETTY_UNUSED telnetty_gmcp_data_t* telnetty__gmcp_of(telnetty_context_t* ctx) {
    telnetty_option_t* opt = ctx ? ctx->options[TELNETTY_TELOPT_GMCP] : NULL;
    return (opt && opt->user_data) ? (telnetty_gmcp_data_t*)opt->user_data : NULL;
}

/* Track a module we support. Once GMCP is up the change is announced with
 * Core.Supports.Add / .Remove; modules registered before that go out in
 * the Core.Supports.Set that follows Core.Hello. */
static TELNETTY_UNUSED int telnetty_gmcp_register_module(
    telnetty_context_t* ctx,
    const char* module
) {
    telnetty_gmcp_data_t* d = telnetty__gmcp_of(ctx);
    if (!d || !module || !*module) return -1;
    for (size_t i = 0; i < d->module_count; i++)
        if (strcmp(d->supported_modules[i], module) == 0) return 0;
    if (d->module_count == d->capacity) {
        size_t ncap = d->capacity ? d->capacity * 2 : 8;
        char** nm = (char**)TELNETTY_MUD_REALLOC(d->supported_modules, ncap * sizeof(char*));
        if (!nm) return -1;
        d->supported_modules = nm;
        d->capacity = ncap;
    }
    size_t n = strlen(module);
    char* copy = (char*)TELNETTY_MUD_MALLOC(n + 1);
    if (!copy) return -1;
    memcpy(copy, module, n + 1);
    d->supported_modules[d->module_count++] = copy;
    if (!d->negotiation_complete) return 0;
    const char* one[1] = { copy };
    return telnetty__gmcp_send_supports_verb(ctx, "Add", one, 1);
}

static TELNETTY_UNUSED int telnetty_gmcp_unregister_module(
    telnetty_context_t* ctx,
    const char* module
) {
    telnetty_gmcp_data_t* d = telnetty__gmcp_of(ctx);
    if (!d || !module) return -1;
    for (size_t i = 0; i < d->module_count; i++) {
        if (strcmp(d->supported_modules[i], module) != 0) continue;
        int rc = 0;
        if (d->negotiation_complete) {
            const char* one[1] = { d->supported_modules[i] };
            rc = telnetty__gmcp_send_supports_verb(ctx, "Remove", one, 1);
        }
        TELNETTY_MUD_FREE(d->supported_modules[i]);
        memmove(&d->supported_modules[i], &d->supported_modules[i + 1],
                (d->module_count - i - 1) * sizeof(char*));
        d->module_count--;
        return rc;
    }
    return -1;
}

static TELNETTY_UNUSED int telnetty_gmcp_send_hello(
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

/* Core.Supports.<verb> ["mod", ...] with JSON string escaping. */
static TELNETTY_UNUSED int telnetty__gmcp_send_supports_verb(
    telnetty_context_t* ctx,
    const char* verb,
    const char* const* modules,
    size_t count
) {
    if (!ctx || (count && !modules)) return -1;

    /* Build a JSON array, escaping quotes, backslashes and control bytes. */
    size_t json_size = 2; /* [] */
    for (size_t i = 0; i < count; i++) {
        if (!modules[i]) return -1;
        size_t item = 2; /* quotes */
        for (const unsigned char* p = (const unsigned char*)modules[i]; *p; p++) {
            size_t n = (*p == '"' || *p == '\\') ? 2 : (*p < 0x20 ? 6 : 1);
            if (item > TELNETTY_MAX_SB_LENGTH - n) return -1;
            item += n;
        }
        if (json_size > TELNETTY_MAX_SB_LENGTH - item) return -1;
        json_size += item;
        if (i + 1 < count) {
            if (json_size > TELNETTY_MAX_SB_LENGTH - 2) return -1;
            json_size += 2;
        }
    }
    
    char* json_data = (char*)TELNETTY_MUD_MALLOC(json_size + 1);
    if (!json_data) return -1;
    
    size_t offset = 0;
    json_data[offset++] = '[';
    
    for (size_t i = 0; i < count; i++) {
        json_data[offset++] = '"';
        for (const unsigned char* p = (const unsigned char*)modules[i]; *p; p++) {
            if (*p == '"' || *p == '\\') {
                json_data[offset++] = '\\';
                json_data[offset++] = (char)*p;
            } else if (*p < 0x20) {
                static const char hex[] = "0123456789abcdef";
                json_data[offset++] = '\\'; json_data[offset++] = 'u';
                json_data[offset++] = '0'; json_data[offset++] = '0';
                json_data[offset++] = hex[*p >> 4]; json_data[offset++] = hex[*p & 0x0f];
            } else {
                json_data[offset++] = (char)*p;
            }
        }
        json_data[offset++] = '"';
        
        if (i < count - 1) {
            json_data[offset++] = ',';
            json_data[offset++] = ' ';
        }
    }
    
    json_data[offset++] = ']';
    json_data[offset] = '\0';
    
    int result = telnetty_gmcp_send(ctx, "Core.Supports", verb, json_data);
    
    TELNETTY_MUD_FREE(json_data);
    
    return result;
}

static TELNETTY_UNUSED int telnetty_gmcp_send_supports(
    telnetty_context_t* ctx,
    const char* const* modules,
    size_t count
) {
    return telnetty__gmcp_send_supports_verb(ctx, "Set", modules, count);
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

static TELNETTY_UNUSED void telnetty_mtts_data_free(void* p) {
    telnetty_mtts_data_destroy((telnetty_mtts_data_t*)p);
}

static TELNETTY_UNUSED telnetty_mtts_data_t* telnetty_mtts_of(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_TTYPE];
    return (opt && opt->user_data) ? (telnetty_mtts_data_t*)opt->user_data : NULL;
}

/* MTTS TTYPE subnegotiation codes. Same as RFC 1091. */
#define TELNETTY_TTYPE_IS   0
#define TELNETTY_TTYPE_SEND 1

/* Send one cycle of the three-cycle TTYPE IS response as a client. */
static TELNETTY_UNUSED void telnetty_mtts_send_cycle(telnetty_context_t* ctx, telnetty_mtts_data_t* d) {
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
static TELNETTY_UNUSED void telnetty_mtts_sb_handler(
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
static TELNETTY_UNUSED void telnetty_mtts_handler(
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

static TELNETTY_UNUSED bool telnetty_mtts_has_flag(
    telnetty_context_t* ctx,
    telnetty_mtts_flag_t flag
) {
    uint64_t flags = telnetty_mtts_get_flags(ctx);
    return (flags & (uint64_t)flag) != 0;
}

static TELNETTY_UNUSED int telnetty_mtts_get_client_info(
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
static TELNETTY_UNUSED int telnetty_mtts_send_response(
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

static TELNETTY_UNUSED void telnetty_mssp_data_free(void* p) {
    telnetty_mssp_data_destroy((telnetty_mssp_data_t*)p);
}

static TELNETTY_UNUSED telnetty_mssp_data_t* telnetty_mssp_of(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MSSP];
    return (opt && opt->user_data) ? (telnetty_mssp_data_t*)opt->user_data : NULL;
}

static TELNETTY_UNUSED void telnetty_mssp_handler(
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

static TELNETTY_UNUSED int telnetty_enable_mssp(
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

static TELNETTY_UNUSED int telnetty_mssp_add(
    telnetty_context_t* ctx,
    const char* name,
    const char* value
) {
    telnetty_mssp_data_t* data = telnetty_mssp_of(ctx);
    if (!data || !name) return -1;
    for (size_t i = 0; i < data->variable_count; i++) {
        if (data->variables[i].name && strcmp(data->variables[i].name, name) == 0) {
            TELNETTY_MUD_FREE(data->variables[i].value);
            data->variables[i].value = value ? telnetty__mud_strdup(value) : telnetty__mud_strdup("");
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
    slot->name = telnetty__mud_strdup(name);
    slot->value = telnetty__mud_strdup(value ? value : "");
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

static TELNETTY_UNUSED int telnetty_mssp_set_standard_vars(
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

/* ============================================================================
 * MSP (MUD Sound Protocol) — TELOPT 90 implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_msp_data_free(void* p) { TELNETTY_MUD_FREE(p); }

static TELNETTY_UNUSED telnetty_msp_data_t* telnetty__msp_of(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MSP];
    return (opt && opt->user_data) ? (telnetty_msp_data_t*)opt->user_data : NULL;
}

/* Parse a single K=V argument. `key` is one uppercase letter; value is
 * the token that follows up to space or ')'. Numeric args land in *ival;
 * string args (T=, U=) copy into out with size cap. */
static TELNETTY_UNUSED int telnetty__msp_arg(
    const char* p, const char* end,
    char key, int* ival, char* out, size_t out_sz
) {
    if ((size_t)(end - p) < 2 || p[0] != key || p[1] != '=') return 0;
    p += 2;
    if (ival) {
        int v = 0, sign = 1;
        if (p < end && *p == '-') { sign = -1; p++; }
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0'); p++;
        }
        *ival = v * sign;
    }
    if (out) {
        size_t n = 0;
        while (p < end && *p != ' ' && *p != ')' && n + 1 < out_sz) {
            out[n++] = *p++;
        }
        out[n] = '\0';
    }
    return 1;
}

/* Scan `data` for one !!SOUND(...) or !!MUSIC(...) trigger starting at
 * `start`. Returns the offset just past the closing ')' on match, or 0
 * on no match. Fills *trig on match. `file_buf`, `type_buf`, `url_buf`
 * are caller-supplied scratch storage. */
static TELNETTY_UNUSED size_t telnetty__msp_parse_one(
    const char* data, size_t len, size_t start,
    telnetty_msp_trigger_t* trig,
    char* file_buf, size_t file_sz,
    char* type_buf, size_t type_sz,
    char* url_buf,  size_t url_sz
) {
    if (start + 8 > len) return 0;
    if (data[start] != '!' || data[start + 1] != '!') return 0;
    telnetty_msp_kind_t kind;
    size_t hdr;
    if (memcmp(data + start + 2, "SOUND(", 6) == 0) {
        kind = TELNETTY_MSP_KIND_SOUND; hdr = 8;
    } else if (memcmp(data + start + 2, "MUSIC(", 6) == 0) {
        kind = TELNETTY_MSP_KIND_MUSIC; hdr = 8;
    } else {
        return 0;
    }
    const char* p = data + start + hdr;
    const char* end = data + len;
    /* Locate closing paren (no nested parens per spec). */
    const char* rp = (const char*)memchr(p, ')', (size_t)(end - p));
    if (!rp) return 0;

    memset(trig, 0, sizeof(*trig));
    trig->kind = kind;
    trig->volume = trig->loops = trig->priority = trig->cont = -1;

    /* First token: filename (or "Off") — up to first space or ')'. */
    size_t fn = 0;
    while (p < rp && *p != ' ' && fn + 1 < file_sz) {
        file_buf[fn++] = *p++;
    }
    file_buf[fn] = '\0';
    trig->file = file_buf;

    /* Remaining tokens: V=/L=/P=/C=/T=/U=. */
    while (p < rp) {
        while (p < rp && *p == ' ') p++;
        if (p >= rp) break;
        if      (telnetty__msp_arg(p, rp, 'V', &trig->volume,   NULL, 0));
        else if (telnetty__msp_arg(p, rp, 'L', &trig->loops,    NULL, 0));
        else if (telnetty__msp_arg(p, rp, 'P', &trig->priority, NULL, 0));
        else if (telnetty__msp_arg(p, rp, 'C', &trig->cont,     NULL, 0));
        else if (telnetty__msp_arg(p, rp, 'T', NULL, type_buf, type_sz)) {
            trig->type = type_buf;
        }
        else if (telnetty__msp_arg(p, rp, 'U', NULL, url_buf, url_sz)) {
            trig->url = url_buf;
        }
        /* Skip to next space. */
        while (p < rp && *p != ' ') p++;
    }
    return (size_t)(rp - data) + 1;
}

/* HIGH-priority extended DATA handler: peek the payload for MSP triggers
 * and dispatch each. The original DATA callback still fires afterward
 * with the full untouched payload (we don't rewrite in-flight data). */
static TELNETTY_UNUSED void telnetty__msp_data_hook(
    telnetty_context_t* ctx, telnetty_event_type_t ev,
    const telnetty_event_data_union_t* d, void* ud,
    telnetty_event_priority_t prio
) {
    (void)prio;
    if (ev != TELNETTY_EVENT_DATA || !d || d->data.length < 4) return;
    telnetty_msp_data_t* m = (telnetty_msp_data_t*)ud;
    if (!m || !m->cb) return;

    const char* text = (const char*)d->data.data;
    size_t len = d->data.length;
    /* Scan every '!' looking for "!!SOUND(" / "!!MUSIC(". */
    for (size_t i = 0; i + 2 < len; ) {
        const char* bang = (const char*)memchr(text + i, '!', len - i);
        if (!bang) break;
        size_t off = (size_t)(bang - text);
        telnetty_msp_trigger_t trig;
        char fbuf[256], tbuf[64], ubuf[256];
        size_t nxt = telnetty__msp_parse_one(text, len, off, &trig,
                                             fbuf, sizeof(fbuf),
                                             tbuf, sizeof(tbuf),
                                             ubuf, sizeof(ubuf));
        if (nxt) {
            m->cb(ctx, &trig, m->user_data);
            i = nxt;
        } else {
            i = off + 1;
        }
    }
}

static TELNETTY_UNUSED void telnetty_msp_handler(
    telnetty_context_t* ctx, uint8_t option, uint8_t command, void* user_data
) {
    (void)ctx; (void)option; (void)command;
    telnetty_msp_data_t* m = (telnetty_msp_data_t*)user_data;
    if (!m) return;
    if (command == TELNETTY_DO || command == TELNETTY_WILL) {
        m->enabled = true;
    } else if (command == TELNETTY_DONT || command == TELNETTY_WONT) {
        m->enabled = false;
    }
}

static TELNETTY_UNUSED int telnetty_enable_msp(
    telnetty_context_t* ctx,
    telnetty_msp_callback_t cb,
    void* user_data
) {
    if (!ctx) return -1;
#ifndef TELNETTY_EVENTS_IMPLEMENTATION
    /* The trigger scanner is an extended-event DATA handler, which needs
     * TELNETTY_EVENTS_IMPLEMENTATION in this translation unit. */
    (void)cb; (void)user_data;
    return -1;
#else
    telnetty_msp_data_t* m = (telnetty_msp_data_t*)TELNETTY_MUD_MALLOC(sizeof(*m));
    if (!m) return -1;
    memset(m, 0, sizeof(*m));
    m->cb = cb;
    m->user_data = user_data;

    /* HIGH-priority DATA scanner fires before the app's own handler. */
    if (telnetty_register_event_handler(ctx, TELNETTY_EVENT_DATA,
                                        telnetty__msp_data_hook, m,
                                        TELNETTY_PRIORITY_HIGH) != 0) {
        TELNETTY_MUD_FREE(m);
        return -1;
    }
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_MSP, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_MSP,
                             telnetty_msp_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_MSP, m,
                                  telnetty_msp_data_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MSP);
#endif
}

/* ============================================================================
 * ATCP (TELOPT 200) implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_atcp_data_free(void* p) { TELNETTY_MUD_FREE(p); }

static TELNETTY_UNUSED void telnetty_atcp_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_atcp_data_t* a = (telnetty_atcp_data_t*)user_data;
    if (!a || !a->cb || !data || length == 0) return;

    /* Split "<package>.<message>[ <payload>]". The name run ends at
     * the first space, tab, or NUL; the payload is whatever follows a
     * single delimiter. */
    size_t name_end = 0;
    while (name_end < length && data[name_end] != ' '
                             && data[name_end] != '\t'
                             && data[name_end] != '\0') {
        name_end++;
    }

    /* Copy the name onto the stack so we can split on '.' with a
     * writable buffer. Cap at 128 chars — ATCP names are always short. */
    char name[128];
    size_t nn = name_end < sizeof(name) - 1 ? name_end : sizeof(name) - 1;
    memcpy(name, data, nn);
    name[nn] = '\0';

    char* dot = strchr(name, '.');
    const char* pkg;
    const char* msg;
    if (dot) {
        *dot = '\0';
        pkg = name;
        msg = dot + 1;
    } else {
        pkg = name;
        msg = "";
    }

    /* Skip the single delimiter, if any. */
    size_t payload_off = name_end;
    if (payload_off < length &&
        (data[payload_off] == ' ' || data[payload_off] == '\t')) {
        payload_off++;
    }
    const uint8_t* payload = payload_off < length ? data + payload_off : NULL;
    size_t payload_len = payload_off < length ? length - payload_off : 0;

    a->cb(ctx, pkg, msg, payload, payload_len, a->user_data);
}

static TELNETTY_UNUSED void telnetty_atcp_handler(
    telnetty_context_t* ctx, uint8_t option, uint8_t command, void* user_data
) {
    (void)ctx; (void)option;
    telnetty_atcp_data_t* a = (telnetty_atcp_data_t*)user_data;
    if (!a) return;
    if (command == TELNETTY_DO || command == TELNETTY_WILL) a->enabled = true;
    else if (command == TELNETTY_DONT || command == TELNETTY_WONT) a->enabled = false;
}

static TELNETTY_UNUSED int telnetty_enable_atcp(
    telnetty_context_t* ctx,
    telnetty_atcp_callback_t cb,
    void* user_data
) {
    if (!ctx) return -1;
    telnetty_atcp_data_t* a = (telnetty_atcp_data_t*)TELNETTY_MUD_MALLOC(sizeof(*a));
    if (!a) return -1;
    memset(a, 0, sizeof(*a));
    a->cb = cb;
    a->user_data = user_data;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ATCP, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_ATCP,
                             telnetty_atcp_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_ATCP,
                                 telnetty_atcp_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_ATCP, a,
                                  telnetty_atcp_data_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_ATCP);
}

static TELNETTY_UNUSED int telnetty_atcp_send(
    telnetty_context_t* ctx,
    const char* package,
    const char* message,
    const uint8_t* payload,
    size_t payload_len
) {
    if (!ctx || !package) return -1;
    size_t pkg_len = strlen(package);
    size_t msg_len = message ? strlen(message) : 0;
    /* Frame: <pkg>[.<msg>][ <payload>] — dot omitted when message empty. */
    size_t total = pkg_len + (msg_len ? 1 + msg_len : 0)
                           + (payload_len ? 1 + payload_len : 0);
    if (total > TELNETTY_MAX_SB_LENGTH) return -1;

    uint8_t* buf = (uint8_t*)TELNETTY_MUD_MALLOC(total);
    if (!buf) return -1;
    size_t off = 0;
    memcpy(buf + off, package, pkg_len); off += pkg_len;
    if (msg_len) {
        buf[off++] = '.';
        memcpy(buf + off, message, msg_len); off += msg_len;
    }
    if (payload_len) {
        buf[off++] = ' ';
        memcpy(buf + off, payload, payload_len); off += payload_len;
    }
    int r = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_ATCP, buf, off);
    TELNETTY_MUD_FREE(buf);
    return r;
}

/* ============================================================================
 * ZMP (TELOPT 93) implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty__mxp_free(void* p) { TELNETTY_MUD_FREE(p); }

static TELNETTY_UNUSED void telnetty__mxp_tag(telnetty_context_t* ctx, telnetty_mxp_data_t* m) {
    size_t a = 0, b = m->length;
    bool closing = a < b && m->tag[a] == '/';
    if (closing) a++;
    while (a < b && (m->tag[a] == ' ' || m->tag[a] == '\t')) a++;
    while (b > a && (m->tag[b-1] == ' ' || m->tag[b-1] == '\t' || m->tag[b-1] == '/')) b--;
    size_t n = a;
    while (n < b && m->tag[n] != ' ' && m->tag[n] != '\t') n++;
    if (n == a || !m->cb) return;
    char saved = m->tag[n]; m->tag[n] = '\0';
    const char* attrs = n < b ? m->tag + n + 1 : "";
    while (*attrs == ' ' || *attrs == '\t') attrs++;
    m->cb(ctx, m->tag + a, attrs, closing, m->mode, m->user_data);
    m->tag[n] = saved;
}

static TELNETTY_UNUSED void telnetty__mxp_data(telnetty_context_t* ctx, telnetty_event_type_t ev,
    const telnetty_event_data_union_t* d, void* ud, telnetty_event_priority_t prio) {
    (void)prio;
    telnetty_mxp_data_t* m = (telnetty_mxp_data_t*)ud;
    if (ev != TELNETTY_EVENT_DATA || !d || !m || !m->cb || !m->enabled) return;
    for (size_t i = 0; i < d->data.length; i++) {
        unsigned char c = d->data.data[i];
        if (!m->in_tag) { if (c == '<') { m->in_tag = true; m->length = 0; } continue; }
        if (m->quoted) { if (c == (unsigned char)m->quote) m->quoted = false; }
        else if (c == '\'' || c == '"') { m->quoted = true; m->quote = (char)c; }
        else if (c == '>') { m->tag[m->length] = '\0'; telnetty__mxp_tag(ctx, m); m->in_tag = false; m->length = 0; continue; }
        else if (c == '<') m->length = 0;
        if (m->length + 1 < sizeof(m->tag)) m->tag[m->length++] = (char)c;
        else { m->in_tag = false; m->length = 0; m->quoted = false; }
    }
}

static TELNETTY_UNUSED void telnetty__mxp_option(telnetty_context_t* ctx, uint8_t option,
                                  uint8_t command, void* ud) {
    (void)ctx; (void)option;
    telnetty_mxp_data_t* m = (telnetty_mxp_data_t*)ud;
    if (m) m->enabled = command == TELNETTY_DO || command == TELNETTY_WILL;
}

static TELNETTY_UNUSED int telnetty_enable_mxp(telnetty_context_t* ctx,
    telnetty_mxp_tag_callback_t cb, void* user_data) {
    if (!ctx) return -1;
    telnetty_option_t* existing = ctx->options[TELNETTY_TELOPT_MXP];
    telnetty_mxp_data_t* prior = existing ? (telnetty_mxp_data_t*)existing->user_data : NULL;
    if (prior) {
        prior->cb = cb; prior->user_data = user_data;
        prior->length = 0; prior->in_tag = false; prior->quoted = false;
        return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MXP);
    }
#ifndef TELNETTY_EVENTS_IMPLEMENTATION
    /* The tag tokenizer is an extended-event DATA handler, which needs
     * TELNETTY_EVENTS_IMPLEMENTATION in this translation unit. */
    return -1;
#else
    telnetty_mxp_data_t* m = (telnetty_mxp_data_t*)TELNETTY_MUD_MALLOC(sizeof(*m));
    if (!m) return -1;
    memset(m, 0, sizeof(*m)); m->cb = cb; m->user_data = user_data;
    if (telnetty_set_option_support(ctx, TELNETTY_TELOPT_MXP, 1, 1) != 0 ||
        telnetty_register_option(ctx, TELNETTY_TELOPT_MXP, telnetty__mxp_option, NULL) != 0) {
        TELNETTY_MUD_FREE(m); return -1;
    }
    if (telnetty_register_event_handler(ctx, TELNETTY_EVENT_DATA, telnetty__mxp_data,
                                        m, TELNETTY_PRIORITY_HIGH) != 0) {
        TELNETTY_MUD_FREE(m); return -1;
    }
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_MXP, m, telnetty__mxp_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MXP);
#endif
}

static TELNETTY_UNUSED int telnetty_mxp_set_mode(telnetty_context_t* ctx,
                                                  telnetty_mxp_mode_t mode) {
    if (!ctx || mode < TELNETTY_MXP_MODE_OPEN || mode > TELNETTY_MXP_MODE_LOCKED) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MXP];
    telnetty_mxp_data_t* m = opt ? (telnetty_mxp_data_t*)opt->user_data : NULL;
    if (!m) return -1;
    m->mode = mode;
    return 0;
}

static TELNETTY_UNUSED void telnetty_zmp_data_free(void* p) { TELNETTY_MUD_FREE(p); }

/* Small stack-friendly argv cap. ZMP messages in the wild are typically
 * a handful of args; anything past this cap is silently truncated to
 * argv_cap - 1 rather than dropping the whole frame. */
#define TELNETTY_ZMP_MAX_ARGV 32

static TELNETTY_UNUSED void telnetty_zmp_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_zmp_data_t* z = (telnetty_zmp_data_t*)user_data;
    if (!z || !z->enabled || !z->cb || !data || length < 2) return;

    /* First NUL-terminated string is the command. Every subsequent NUL-
     * terminated run is an argument. We index into `data` in place — safe
     * because the SB payload is guaranteed to have been reassembled with
     * IAC-IAC escapes decoded, and the trailing NUL requirement means the
     * payload's own bytes are the strings the callback sees. */
    const char* cmd = (const char*)data;
    size_t i = 0;
    while (i < length && data[i] != 0) i++;
    if (i >= length) return;  /* command wasn't NUL-terminated */
    i++;

    const char* argv[TELNETTY_ZMP_MAX_ARGV];
    size_t argc = 0;
    while (i < length && argc + 1 < TELNETTY_ZMP_MAX_ARGV) {
        argv[argc++] = (const char*)(data + i);
        while (i < length && data[i] != 0) i++;
        if (i >= length) {
            /* Unterminated last arg — drop the whole frame for safety;
             * ZMP frames MUST end with a NUL after the last arg per the
             * spec, and honoring a truncated arg would let a peer smuggle
             * bytes past the string boundary. */
            return;
        }
        i++;
    }
    argv[argc] = NULL;

    z->cb(ctx, cmd, argv, argc, z->user_data);
}

static TELNETTY_UNUSED void telnetty_zmp_handler(
    telnetty_context_t* ctx, uint8_t option, uint8_t command, void* user_data
) {
    (void)ctx; (void)option;
    telnetty_zmp_data_t* z = (telnetty_zmp_data_t*)user_data;
    if (!z) return;
    if (command == TELNETTY_DO || command == TELNETTY_WILL) z->enabled = true;
    else if (command == TELNETTY_DONT || command == TELNETTY_WONT) z->enabled = false;
}

static TELNETTY_UNUSED int telnetty_enable_zmp(
    telnetty_context_t* ctx,
    telnetty_zmp_callback_t cb,
    void* user_data
) {
    if (!ctx) return -1;
    telnetty_zmp_data_t* z = (telnetty_zmp_data_t*)TELNETTY_MUD_MALLOC(sizeof(*z));
    if (!z) return -1;
    memset(z, 0, sizeof(*z));
    z->cb = cb;
    z->user_data = user_data;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ZMP, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_ZMP,
                             telnetty_zmp_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_ZMP,
                                 telnetty_zmp_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_ZMP, z,
                                  telnetty_zmp_data_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_ZMP);
}

static TELNETTY_UNUSED int telnetty_zmp_send(
    telnetty_context_t* ctx,
    const char* command,
    const char* const* argv
) {
    if (!ctx || !command) return -1;
    /* Size: <command> NUL <argv[0]> NUL ... <argv[n-1]> NUL. */
    size_t cmd_len = strlen(command);
    if (cmd_len == 0 || cmd_len >= TELNETTY_MAX_SB_LENGTH) return -1;
    size_t total = cmd_len + 1;
    if (argv) {
        for (const char* const* p = argv; *p; p++) {
            size_t n = strlen(*p);
            if (n >= TELNETTY_MAX_SB_LENGTH - total) return -1;
            total += n + 1;
        }
    }
    if (total > TELNETTY_MAX_SB_LENGTH) return -1;

    uint8_t* buf = (uint8_t*)TELNETTY_MUD_MALLOC(total);
    if (!buf) return -1;
    size_t off = 0;
    memcpy(buf + off, command, cmd_len); off += cmd_len;
    buf[off++] = 0;
    if (argv) {
        for (const char* const* p = argv; *p; p++) {
            size_t al = strlen(*p);
            memcpy(buf + off, *p, al); off += al;
            buf[off++] = 0;
        }
    }
    int r = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_ZMP, buf, off);
    TELNETTY_MUD_FREE(buf);
    return r;
}

#endif /* TELNETTY_MUD_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_MUD_H */
