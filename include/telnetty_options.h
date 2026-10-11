/**
 * @file telnetty_options.h
 * @brief Standard TELNET option implementations
 * 
 * This header provides implementations for standard TELNET options as defined
 * in various RFCs. Each option is implemented as a separate handler that can
 * be registered with the core TELNET context.
 * 
 * Supported Options:
 * - Binary Transmission (RFC 856)
 * - Echo (RFC 857)
 * - Suppress Go Ahead (RFC 858)
 * - Status (RFC 859)
 * - Timing Mark (RFC 860)
 * - Terminal Type (RFC 1091)
 * - Window Size (RFC 1073)
 * - Terminal Speed (RFC 1079)
 * - Remote Flow Control (RFC 1372)
 * - Linemode (RFC 1184)
 * - Environment Variables (RFC 1572)
 * - Charset (RFC 2066)
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.2.1
 * 
 */

#ifndef TELNETTY_OPTIONS_H
#define TELNETTY_OPTIONS_H

#include "telnetty_core.h"
#include "telnetty_events.h"

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
 * Option Configuration
 * ============================================================================ */

/* Default option timeouts (in milliseconds) */
#ifndef TELNETTY_OPTION_TIMEOUT
#define TELNETTY_OPTION_TIMEOUT 5000
#endif

/* Maximum number of option negotiation retries */
#ifndef TELNETTY_OPTION_MAX_RETRIES
#define TELNETTY_OPTION_MAX_RETRIES 3
#endif

/* ============================================================================
 * Option State Management
 * ============================================================================ */

/* Option states for tracking negotiation */
typedef enum {
    TELNETTY_OPTION_STATE_DISABLED = 0,   /**< Option is disabled */
    TELNETTY_OPTION_STATE_ENABLED = 1,    /**< Option is enabled */
    TELNETTY_OPTION_STATE_NEGOTIATING = 2, /**< Option negotiation in progress */
    TELNETTY_OPTION_STATE_FAILED = 3,     /**< Option negotiation failed */
    TELNETTY_OPTION_STATE_UNSUPPORTED = 4 /**< Option not supported by peer */
} telnetty_option_state_t;

/* Option negotiation tracking structure */
typedef struct {
    uint8_t option;                     /**< Option code */
    telnetty_option_state_t local_state;  /**< Local state */
    telnetty_option_state_t remote_state; /**< Remote state */
    int retry_count;                    /**< Number of retries */
    time_t last_attempt;                /**< Last negotiation attempt */
    void* option_data;                  /**< Option-specific data */
    telnetty_option_handler_t handler;    /**< Option handler */
    void* user_data;                    /**< User data for handler */
} telnetty_option_tracker_t;

/* ============================================================================
 * Binary Transmission Option (RFC 856)
 * ============================================================================ */

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION
/**
 * Binary transmission option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_BINARY)
 * @param command Command received
 * @param user_data User data
 */
static TELNETTY_UNUSED void telnetty_binary_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable binary transmission mode
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_binary(telnetty_context_t* ctx);

/**
 * Disable binary transmission mode
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_binary(telnetty_context_t* ctx);

/* ============================================================================
 * Echo Option (RFC 857)
 * ============================================================================ */

/**
 * Echo option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_ECHO)
 * @param command Command received
 * @param user_data User data
 */
static TELNETTY_UNUSED void telnetty_echo_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable local echo
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_echo(telnetty_context_t* ctx);

/**
 * Disable local echo
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_echo(telnetty_context_t* ctx);

/**
 * Check if echo is enabled
 * 
 * @param ctx TELNET context
 * @return true if echo is enabled, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_is_echo_enabled(telnetty_context_t* ctx);

/* ============================================================================
 * Suppress Go Ahead Option (RFC 858)
 * ============================================================================ */

/**
 * Suppress Go Ahead option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_SGA)
 * @param command Command received
 * @param user_data User data
 */
static TELNETTY_UNUSED void telnetty_sga_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable suppress go ahead
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_sga(telnetty_context_t* ctx);

/**
 * Disable suppress go ahead
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_sga(telnetty_context_t* ctx);
#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

/* ============================================================================
 * Terminal Type Option (RFC 1091)
 * ============================================================================ */

/* Terminal type data structure */
typedef struct {
    char* terminal_type;                /**< Terminal type string */
    size_t max_length;                  /**< Maximum terminal type length */
    bool query_sent;                    /**< Query has been sent */
} telnetty_terminal_type_data_t;

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION
/**
 * Terminal type option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_TTYPE)
 * @param command Command received
 * @param user_data User data (should be telnetty_terminal_type_data_t*)
 */
static TELNETTY_UNUSED void telnetty_ttype_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable terminal type option
 * 
 * @param ctx TELNET context
 * @param max_type_length Maximum terminal type length
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_ttype(telnetty_context_t* ctx, size_t max_type_length);

/**
 * Query terminal type from peer
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_query_terminal_type(telnetty_context_t* ctx);

/**
 * Get negotiated terminal type
 * 
 * @param ctx TELNET context
 * @return Terminal type string or NULL if not negotiated
 */
static TELNETTY_UNUSED const char* telnetty_get_terminal_type(telnetty_context_t* ctx);
#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

/* ============================================================================
 * Window Size Option (RFC 1073 - NAWS)
 * ============================================================================ */

/* Window size data structure */
typedef struct {
    uint16_t width;                     /**< Terminal width */
    uint16_t height;                    /**< Terminal height */
    bool size_received;                 /**< Size has been received */
} telnetty_window_size_data_t;

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION
/**
 * Window size option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_NAWS)
 * @param command Command received
 * @param user_data User data (should be telnetty_window_size_data_t*)
 */
static TELNETTY_UNUSED void telnetty_naws_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable window size option
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_naws(telnetty_context_t* ctx);

/**
 * Get window size
 * 
 * @param ctx TELNET context
 * @param width Output width (optional)
 * @param height Output height (optional)
 * @return 0 if size is available, -1 otherwise
 */
static TELNETTY_UNUSED int telnetty_get_window_size(
    telnetty_context_t* ctx,
    uint16_t* width,
    uint16_t* height
);

/**
 * Send window size to peer
 * 
 * @param ctx TELNET context
 * @param width Window width
 * @param height Window height
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_send_window_size(
    telnetty_context_t* ctx,
    uint16_t width,
    uint16_t height
);
#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

/* ============================================================================
 * Environment Variables Option (RFC 1572 - NEW-ENVIRON)
 * ============================================================================ */

/* Environment variable types */
typedef enum {
    TELNETTY_ENV_VAR = 0,                 /**< Environment variable */
    TELNETTY_ENV_VALUE = 1,               /**< Environment value */
    TELNETTY_ENV_ESC = 2,                 /**< Escaped character */
    TELNETTY_ENV_USERVAR = 3              /**< User-defined variable */
} telnetty_env_type_t;

/* RFC 1572 subnegotiation commands (first payload byte). */
#define TELNETTY_ENV_IS    0
#define TELNETTY_ENV_SEND  1
#define TELNETTY_ENV_INFO  2

/* Environment data structure */
typedef struct {
    char** variables;                   /**< Environment variables */
    char** values;                      /**< Environment values */
    size_t count;                       /**< Number of variables */
    size_t capacity;                    /**< Array capacity */
    bool info_sent;                     /**< INFO has been sent */
} telnetty_environ_data_t;

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION
/**
 * Environment option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_NEW_ENVIRON)
 * @param command Command received
 * @param user_data User data (should be telnetty_environ_data_t*)
 */
static TELNETTY_UNUSED void telnetty_environ_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable environment option
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_environ(telnetty_context_t* ctx);

/**
 * Request environment variables from peer
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_request_environ(telnetty_context_t* ctx);

/**
 * Send environment variables to peer as IAC SB NEW-ENVIRON IS VAR ...
 * (the reply to a SEND). Use telnetty_send_environ_info() for unsolicited
 * updates.
 *
 * @param ctx TELNET context
 * @param vars Array of variable names
 * @param values Array of variable values
 * @param count Number of variables
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_send_environ(
    telnetty_context_t* ctx,
    const char* const* vars,
    const char* const* values,
    size_t count
);

/**
 * Get an environment variable the peer reported via IS or INFO. VAR and
 * USERVAR names share one namespace. A variable sent without a VALUE
 * reads back as "".
 *
 * @param ctx TELNET context
 * @param name Variable name
 * @return Variable value or NULL if not received
 */
static TELNETTY_UNUSED const char* telnetty_get_environ(
    telnetty_context_t* ctx,
    const char* name
);
#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

/* MNES (MUD NEW-ENVIRON Standard) — well-known variable names carried
 * over the existing RFC 1572 NEW-ENVIRON option (TELNETTY_TELOPT_NEW_ENVIRON).
 * The spec defines a canonical vocabulary so MUD clients and servers
 * exchange a shared set of metadata: client name, terminal type,
 * language, screen reader accessibility, and so on. No new option code —
 * pass these strings into `telnetty_send_environ` on the SEND request. */
#define TELNETTY_MNES_CLIENT_NAME      "CLIENT_NAME"
#define TELNETTY_MNES_CLIENT_VERSION   "CLIENT_VERSION"
#define TELNETTY_MNES_TERMINAL_TYPE    "TERMINAL_TYPE"
#define TELNETTY_MNES_CHARSET          "CHARSET"
#define TELNETTY_MNES_MTTS             "MTTS"
#define TELNETTY_MNES_XTERM_TITLE      "XTERM_TITLE"
#define TELNETTY_MNES_SCREEN_READER    "SCREEN_READER"
#define TELNETTY_MNES_IPADDRESS        "IPADDRESS"
#define TELNETTY_MNES_UTF_8            "UTF-8"

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION
/**
 * Convenience wrapper that pushes the six most-often-requested MNES
 * variables into NEW-ENVIRON in one call. Any parameter may be NULL to
 * skip that var. mtts is a bitfield encoded as its decimal representation.
 * Requires telnetty_enable_environ() to have run first.
 */
static TELNETTY_UNUSED int telnetty_send_mnes(
    telnetty_context_t* ctx,
    const char* client_name,
    const char* client_version,
    const char* terminal_type,
    const char* charset,
    uint32_t    mtts,
    int         screen_reader
);
#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

/* ============================================================================
 * Charset Option (RFC 2066)
 * ============================================================================ */

/* RFC 2066 CHARSET subnegotiation codes */
#define TELNETTY_CHARSET_REQUEST   1
#define TELNETTY_CHARSET_ACCEPTED  2
#define TELNETTY_CHARSET_REJECTED  3
#define TELNETTY_CHARSET_TTABLE_IS 4
#define TELNETTY_CHARSET_TTABLE_REJECTED 5
#define TELNETTY_CHARSET_TTABLE_ACK 6
#define TELNETTY_CHARSET_TTABLE_NAK 7

/* Charset data structure */
typedef struct {
    char** supported_charsets;          /**< Supported character sets (owned) */
    size_t charset_count;               /**< Number of supported charsets */
    char* selected_charset;             /**< Selected character set (owned) */
    bool negotiation_complete;          /**< Negotiation is complete */
    bool request_sent;                  /**< SB REQUEST already sent once */
} telnetty_charset_data_t;

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION
/**
 * Charset option handler
 * 
 * @param ctx TELNET context
 * @param option Option code (should be TELNETTY_TELOPT_CHARSET)
 * @param command Command received
 * @param user_data User data (should be telnetty_charset_data_t*)
 */
static TELNETTY_UNUSED void telnetty_charset_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable charset option
 * 
 * @param ctx TELNET context
 * @param charsets Array of supported charset names
 * @param count Number of charsets
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_charset(
    telnetty_context_t* ctx,
    const char* const* charsets,
    size_t count
);

/**
 * Get negotiated charset
 * 
 * @param ctx TELNET context
 * @return Selected charset or NULL if not negotiated
 */
static TELNETTY_UNUSED const char* telnetty_get_charset(telnetty_context_t* ctx);

/* ============================================================================
 * End of Record Option (RFC 885)
 * ============================================================================ */

/**
 * Enable End of Record option
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_eor(telnetty_context_t* ctx);

/**
 * Send End of Record marker
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_send_eor(telnetty_context_t* ctx);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Enable all standard TELNET options
 * 
 * @param ctx TELNET context
 * @return Number of options successfully enabled
 */
static TELNETTY_UNUSED int telnetty_enable_all_options(telnetty_context_t* ctx);

/**
 * Disable all standard TELNET options
 * 
 * @param ctx TELNET context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_all_options(telnetty_context_t* ctx);

/**
 * Check if option is enabled
 * 
 * @param ctx TELNET context
 * @param option Option code
 * @return true if option is enabled, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_is_option_enabled(telnetty_context_t* ctx, uint8_t option);

/* Note: telnetty_get_option_state is defined in telnetty_core.h */

/**
 * Wait for option negotiation to complete
 * 
 * @param ctx TELNET context
 * @param option Option code
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if negotiation complete, -1 on timeout or error
 */
static TELNETTY_UNUSED int telnetty_wait_for_option(
    telnetty_context_t* ctx,
    uint8_t option,
    int timeout_ms
);
#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

/* ============================================================================
 * Implementation
 * ============================================================================ */

#ifdef TELNETTY_OPTIONS_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* Memory allocation wrappers */
#ifndef TELNETTY_OPTIONS_MALLOC
#define TELNETTY_OPTIONS_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_OPTIONS_FREE
#define TELNETTY_OPTIONS_FREE(ptr) free(ptr)
#endif

#ifndef TELNETTY_OPTIONS_REALLOC
#define TELNETTY_OPTIONS_REALLOC(ptr, size) realloc(ptr, size)
#endif

/* Forward declarations for internal functions */
static TELNETTY_UNUSED telnetty_option_tracker_t* telnetty_option_tracker_create(
    uint8_t option,
    telnetty_option_handler_t handler,
    void* user_data
);
static TELNETTY_UNUSED void telnetty_option_tracker_destroy(telnetty_option_tracker_t* tracker);
static TELNETTY_UNUSED int telnetty_option_send_negotiation(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command
);

/* ============================================================================
 * Binary Transmission Implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_binary_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    /* Q-method in core owns IAC replies. */
    (void)ctx; (void)option; (void)command; (void)user_data;
}

static TELNETTY_UNUSED int telnetty_enable_binary(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_BINARY, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_BINARY, telnetty_binary_handler, NULL);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_BINARY);
}

static TELNETTY_UNUSED int telnetty_disable_binary(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Send WONT to disable binary transmission */
    return telnetty_send_option(ctx, TELNETTY_WONT, TELNETTY_TELOPT_BINARY);
}

/* ============================================================================
 * Echo Implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_echo_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)ctx; (void)option; (void)command; (void)user_data;
}

static TELNETTY_UNUSED int telnetty_enable_echo(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ECHO, 1, 0);
    telnetty_register_option(ctx, TELNETTY_TELOPT_ECHO, telnetty_echo_handler, NULL);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_ECHO);
}

static TELNETTY_UNUSED int telnetty_disable_echo(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Send WONT to disable echo */
    return telnetty_send_option(ctx, TELNETTY_WONT, TELNETTY_TELOPT_ECHO);
}

static TELNETTY_UNUSED bool telnetty_is_echo_enabled(telnetty_context_t* ctx) {
    if (!ctx) return false;
    
    /* Check if echo option is enabled */
    return telnetty_is_option_enabled(ctx, TELNETTY_TELOPT_ECHO);
}

/* ============================================================================
 * Suppress Go Ahead Implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_sga_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    /* Q-method in core already emitted any IAC reply. Handler is a no-op
     * hook for applications that stack on top of SGA. */
    (void)ctx; (void)option; (void)command; (void)user_data;
}

static TELNETTY_UNUSED int telnetty_enable_sga(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    /* Bidirectional SGA is universal for MUD servers. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_SGA, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_SGA, telnetty_sga_handler, NULL);
    telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_SGA);
    telnetty_send_option(ctx, TELNETTY_DO,   TELNETTY_TELOPT_SGA);
    return 0;
}

static TELNETTY_UNUSED int telnetty_disable_sga(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Send WONT to disable suppress go ahead */
    return telnetty_send_option(ctx, TELNETTY_WONT, TELNETTY_TELOPT_SGA);
}

/* ============================================================================
 * Terminal Type Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_query_terminal_type(telnetty_context_t* ctx);

static TELNETTY_UNUSED void telnetty_ttype_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)option;
    if (!ctx) return;
    telnetty_terminal_type_data_t* td = (telnetty_terminal_type_data_t*)user_data;
    /* After peer WILL settles (him=YES), request the type once via SEND. */
    if (command == TELNETTY_WILL && (!td || !td->query_sent)) {
        if (telnetty_query_terminal_type(ctx) == 0 && td) td->query_sent = true;
    }
}

/* IAC SB TTYPE IS <name> IAC SE: keep the latest name, truncated to
 * max_length. Repeated SENDs (MTTS cycling) overwrite it in turn. */
static TELNETTY_UNUSED void telnetty_ttype_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    (void)ctx; (void)option;
    telnetty_terminal_type_data_t* td = (telnetty_terminal_type_data_t*)user_data;
    if (!td || length < 1 || data[0] != 0 /* IS */) return;
    size_t n = length - 1;
    if (n > td->max_length) n = td->max_length;
    char* name = (char*)TELNETTY_OPTIONS_MALLOC(n + 1);
    if (!name) return;
    memcpy(name, data + 1, n);
    name[n] = '\0';
    TELNETTY_OPTIONS_FREE(td->terminal_type);
    td->terminal_type = name;
}

static TELNETTY_UNUSED void telnetty_ttype_data_free(void* p) {
    telnetty_terminal_type_data_t* td = (telnetty_terminal_type_data_t*)p;
    if (!td) return;
    TELNETTY_OPTIONS_FREE(td->terminal_type);
    TELNETTY_OPTIONS_FREE(td);
}

static TELNETTY_UNUSED int telnetty_enable_ttype(telnetty_context_t* ctx, size_t max_type_length) {
    if (!ctx) return -1;
    
    telnetty_terminal_type_data_t* ttype_data = (telnetty_terminal_type_data_t*)TELNETTY_OPTIONS_MALLOC(
        sizeof(telnetty_terminal_type_data_t)
    );
    if (!ttype_data) return -1;
    
    memset(ttype_data, 0, sizeof(telnetty_terminal_type_data_t));
    ttype_data->max_length = max_type_length > 0 ? max_type_length : 64;

    /* Server typically DO TTYPE (ask client), not WILL. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_TTYPE, 0, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_TTYPE, telnetty_ttype_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_TTYPE, telnetty_ttype_sb_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_TTYPE, ttype_data, telnetty_ttype_data_free);
    return telnetty_send_option(ctx, TELNETTY_DO, TELNETTY_TELOPT_TTYPE);
}

static TELNETTY_UNUSED int telnetty_query_terminal_type(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Send subnegotiation to query terminal type */
    uint8_t query_data[1] = { 1 }; /* SEND code */
    return telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_TTYPE, query_data, 1);
}

static TELNETTY_UNUSED const char* telnetty_get_terminal_type(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_TTYPE];
    if (!opt || opt->sb_handler != telnetty_ttype_sb_handler || !opt->user_data) return NULL;
    return ((telnetty_terminal_type_data_t*)opt->user_data)->terminal_type;
}

/* ============================================================================
 * Window Size Implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_naws_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)command;
    (void)user_data;
    if (!ctx || option != TELNETTY_TELOPT_NAWS) return;
    /* NAWS carries its data in subnegotiation; see telnetty_naws_sb_handler. */
}

/* IAC SB NAWS <w16> <h16> IAC SE. The core parser has already collapsed
 * doubled IACs, so a 255 byte in either value arrives as one byte. */
static TELNETTY_UNUSED void telnetty_naws_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    (void)option;
    telnetty_window_size_data_t* wd = (telnetty_window_size_data_t*)user_data;
    if (!wd) return;
    if (length != 4) {
        telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL, "NAWS payload is not 4 bytes");
        return;
    }
    wd->width = (uint16_t)((data[0] << 8) | data[1]);
    wd->height = (uint16_t)((data[2] << 8) | data[3]);
    wd->size_received = true;
}

static TELNETTY_UNUSED void telnetty_naws_data_free(void* p) {
    TELNETTY_OPTIONS_FREE(p);
}

static TELNETTY_UNUSED int telnetty_enable_naws(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_window_size_data_t* wd = (telnetty_window_size_data_t*)TELNETTY_OPTIONS_MALLOC(
        sizeof(telnetty_window_size_data_t));
    if (!wd) return -1;
    memset(wd, 0, sizeof(*wd));
    /* Server asks client for window size (DO), does not WILL NAWS. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_NAWS, 0, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_NAWS, telnetty_naws_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_NAWS, telnetty_naws_sb_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_NAWS, wd, telnetty_naws_data_free);
    return telnetty_send_option(ctx, TELNETTY_DO, TELNETTY_TELOPT_NAWS);
}

static TELNETTY_UNUSED int telnetty_get_window_size(
    telnetty_context_t* ctx,
    uint16_t* width,
    uint16_t* height
) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_NAWS];
    if (!opt || opt->sb_handler != telnetty_naws_sb_handler || !opt->user_data) return -1;
    telnetty_window_size_data_t* wd = (telnetty_window_size_data_t*)opt->user_data;
    if (!wd->size_received) return -1;
    if (width) *width = wd->width;
    if (height) *height = wd->height;
    return 0;
}

static TELNETTY_UNUSED int telnetty_send_window_size(
    telnetty_context_t* ctx,
    uint16_t width,
    uint16_t height
) {
    if (!ctx) return -1;
    
    /* Pack window size data */
    uint8_t size_data[4];
    size_data[0] = (width >> 8) & 0xFF;
    size_data[1] = width & 0xFF;
    size_data[2] = (height >> 8) & 0xFF;
    size_data[3] = height & 0xFF;
    
    return telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_NAWS, size_data, 4);
}

/* ============================================================================
 * Environment Variables Implementation
 * ============================================================================ */

static TELNETTY_UNUSED void telnetty_environ_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)command;
    (void)user_data;
    if (!ctx || option != TELNETTY_TELOPT_NEW_ENVIRON) return;
    /* Q-method in core owns the replies; IS/INFO data arrives through
     * telnetty_environ_sb_handler. Answering SEND is up to the
     * application (telnetty_send_environ / telnetty_send_mnes). */
}

static TELNETTY_UNUSED void telnetty_environ_data_free(void* p) {
    telnetty_environ_data_t* ed = (telnetty_environ_data_t*)p;
    if (!ed) return;
    for (size_t i = 0; i < ed->count; i++) {
        TELNETTY_OPTIONS_FREE(ed->variables[i]);
        TELNETTY_OPTIONS_FREE(ed->values[i]);
    }
    TELNETTY_OPTIONS_FREE(ed->variables);
    TELNETTY_OPTIONS_FREE(ed->values);
    TELNETTY_OPTIONS_FREE(ed);
}

/* Insert or replace name=value. Takes ownership of both strings. */
static TELNETTY_UNUSED int telnetty__environ_store(telnetty_environ_data_t* ed, char* name, char* value) {
    for (size_t i = 0; i < ed->count; i++) {
        if (strcmp(ed->variables[i], name) == 0) {
            TELNETTY_OPTIONS_FREE(name);
            TELNETTY_OPTIONS_FREE(ed->values[i]);
            ed->values[i] = value;
            return 0;
        }
    }
    if (ed->count == ed->capacity) {
        size_t ncap = ed->capacity ? ed->capacity * 2 : 8;
        char** nv = (char**)TELNETTY_OPTIONS_REALLOC(ed->variables, ncap * sizeof(char*));
        if (!nv) goto fail;
        ed->variables = nv;
        char** nval = (char**)TELNETTY_OPTIONS_REALLOC(ed->values, ncap * sizeof(char*));
        if (!nval) goto fail;
        ed->values = nval;
        ed->capacity = ncap;
    }
    ed->variables[ed->count] = name;
    ed->values[ed->count] = value;
    ed->count++;
    return 0;
fail:
    TELNETTY_OPTIONS_FREE(name);
    TELNETTY_OPTIONS_FREE(value);
    return -1;
}

/* Copy one escaped NEW-ENVIRON field starting at data[*pos], stopping at
 * the next unescaped VAR/VALUE/USERVAR marker. Returns a NUL-terminated
 * heap string. */
static TELNETTY_UNUSED char* telnetty__environ_field(const uint8_t* data, size_t length, size_t* pos) {
    char* out = (char*)TELNETTY_OPTIONS_MALLOC(length - *pos + 1);
    if (!out) return NULL;
    size_t n = 0;
    while (*pos < length) {
        uint8_t c = data[*pos];
        if (c == TELNETTY_ENV_VAR || c == TELNETTY_ENV_VALUE || c == TELNETTY_ENV_USERVAR)
            break;
        if (c == TELNETTY_ENV_ESC) {
            (*pos)++;
            if (*pos >= length) break;
            c = data[*pos];
        }
        out[n++] = (char)c;
        (*pos)++;
    }
    out[n] = '\0';
    return out;
}

/* IAC SB NEW-ENVIRON IS|INFO [VAR|USERVAR name [VALUE value]]* IAC SE */
static TELNETTY_UNUSED void telnetty_environ_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    (void)ctx; (void)option;
    telnetty_environ_data_t* ed = (telnetty_environ_data_t*)user_data;
    if (!ed || length < 1) return;
    if (data[0] != TELNETTY_ENV_IS && data[0] != TELNETTY_ENV_INFO) return;

    size_t pos = 1;
    while (pos < length) {
        uint8_t type = data[pos++];
        if (type != TELNETTY_ENV_VAR && type != TELNETTY_ENV_USERVAR) {
            /* Stray VALUE (or garbage): skip to the next marker. */
            char* skip = telnetty__environ_field(data, length, &pos);
            TELNETTY_OPTIONS_FREE(skip);
            continue;
        }
        char* name = telnetty__environ_field(data, length, &pos);
        if (!name) return;
        char* value = NULL;
        if (pos < length && data[pos] == TELNETTY_ENV_VALUE) {
            pos++;
            value = telnetty__environ_field(data, length, &pos);
        } else {
            /* Defined but no value: RFC 1572 distinguishes this from
             * undefined; we report it as the empty string. */
            value = (char*)TELNETTY_OPTIONS_MALLOC(1);
            if (value) value[0] = '\0';
        }
        if (!value) { TELNETTY_OPTIONS_FREE(name); return; }
        if (telnetty__environ_store(ed, name, value) < 0) return;
    }
}

static TELNETTY_UNUSED int telnetty_enable_environ(telnetty_context_t* ctx) {
    if (!ctx) return -1;

    telnetty_environ_data_t* ed = (telnetty_environ_data_t*)TELNETTY_OPTIONS_MALLOC(
        sizeof(telnetty_environ_data_t));
    if (!ed) return -1;
    memset(ed, 0, sizeof(*ed));

    /* Either side may carry variables: we WILL (send ours) and accept the
     * peer's WILL so its IS/INFO replies are recorded. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_NEW_ENVIRON, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_NEW_ENVIRON, telnetty_environ_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_NEW_ENVIRON, telnetty_environ_sb_handler, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_NEW_ENVIRON, ed, telnetty_environ_data_free);

    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_NEW_ENVIRON);
}

static TELNETTY_UNUSED int telnetty_request_environ(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Send subnegotiation to request environment */
    uint8_t request_data[1] = { 1 }; /* SEND code */
    return telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_NEW_ENVIRON, request_data, 1);
}

static TELNETTY_UNUSED int telnetty__send_environ_cmd(
    telnetty_context_t* ctx,
    uint8_t command,
    const char* const* vars,
    const char* const* values,
    size_t count
) {
    if (!ctx || !vars || !values) return -1;
    
    /* Calculate escaped RFC 1572 payload size without overflowing. The
     * leading byte is the IS / INFO command. */
    size_t total_size = 1;
    for (size_t i = 0; i < count; i++) {
        if (!vars[i] || !values[i]) return -1;
        const unsigned char* fields[2] = {
            (const unsigned char*)vars[i], (const unsigned char*)values[i]
        };
        for (size_t f = 0; f < 2; f++) {
            size_t field_size = 1; /* VAR or VALUE marker */
            for (const unsigned char* p = fields[f]; *p; p++) {
                size_t add = (*p == TELNETTY_ENV_VAR || *p == TELNETTY_ENV_VALUE ||
                              *p == TELNETTY_ENV_ESC || *p == TELNETTY_ENV_USERVAR) ? 2 : 1;
                if (field_size > TELNETTY_MAX_SB_LENGTH - add) return -1;
                field_size += add;
            }
            if (total_size > TELNETTY_MAX_SB_LENGTH - field_size) return -1;
            total_size += field_size;
        }
    }
    
    /* Allocate temporary buffer */
    uint8_t* buffer = (uint8_t*)TELNETTY_OPTIONS_MALLOC(total_size);
    if (!buffer) return -1;
    
    /* Build environment data, escaping reserved NEW-ENVIRON marker bytes. */
    size_t offset = 0;
    buffer[offset++] = command;
    for (size_t i = 0; i < count; i++) {
        buffer[offset++] = TELNETTY_ENV_VAR;
        for (const unsigned char* p = (const unsigned char*)vars[i]; *p; p++) {
            if (*p == TELNETTY_ENV_VAR || *p == TELNETTY_ENV_VALUE ||
                *p == TELNETTY_ENV_ESC || *p == TELNETTY_ENV_USERVAR)
                buffer[offset++] = TELNETTY_ENV_ESC;
            buffer[offset++] = *p;
        }
        
        buffer[offset++] = TELNETTY_ENV_VALUE;
        for (const unsigned char* p = (const unsigned char*)values[i]; *p; p++) {
            if (*p == TELNETTY_ENV_VAR || *p == TELNETTY_ENV_VALUE ||
                *p == TELNETTY_ENV_ESC || *p == TELNETTY_ENV_USERVAR)
                buffer[offset++] = TELNETTY_ENV_ESC;
            buffer[offset++] = *p;
        }
    }
    
    /* Send subnegotiation */
    int result = telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_NEW_ENVIRON, buffer, offset);
    
    TELNETTY_OPTIONS_FREE(buffer);
    
    return result;
}

static TELNETTY_UNUSED int telnetty_send_environ(
    telnetty_context_t* ctx,
    const char* const* vars,
    const char* const* values,
    size_t count
) {
    return telnetty__send_environ_cmd(ctx, TELNETTY_ENV_IS, vars, values, count);
}

/** Unsolicited update: IAC SB NEW-ENVIRON INFO VAR ... IAC SE. */
static TELNETTY_UNUSED int telnetty_send_environ_info(
    telnetty_context_t* ctx,
    const char* const* vars,
    const char* const* values,
    size_t count
) {
    return telnetty__send_environ_cmd(ctx, TELNETTY_ENV_INFO, vars, values, count);
}

static TELNETTY_UNUSED const char* telnetty_get_environ(
    telnetty_context_t* ctx,
    const char* name
) {
    if (!ctx || !name) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_NEW_ENVIRON];
    if (!opt || opt->sb_handler != telnetty_environ_sb_handler || !opt->user_data) return NULL;
    telnetty_environ_data_t* ed = (telnetty_environ_data_t*)opt->user_data;
    for (size_t i = 0; i < ed->count; i++) {
        if (strcmp(ed->variables[i], name) == 0) return ed->values[i];
    }
    return NULL;
}

static TELNETTY_UNUSED int telnetty_send_mnes(
    telnetty_context_t* ctx,
    const char* client_name,
    const char* client_version,
    const char* terminal_type,
    const char* charset,
    uint32_t    mtts,
    int         screen_reader
) {
    if (!ctx) return -1;
    /* At most 6 slots; skip NULLs. */
    const char* names[6];
    const char* values[6];
    size_t n = 0;
    char mtts_buf[16];
    char sr_buf[4];
    if (client_name)    { names[n] = TELNETTY_MNES_CLIENT_NAME;    values[n++] = client_name; }
    if (client_version) { names[n] = TELNETTY_MNES_CLIENT_VERSION; values[n++] = client_version; }
    if (terminal_type)  { names[n] = TELNETTY_MNES_TERMINAL_TYPE;  values[n++] = terminal_type; }
    if (charset)        { names[n] = TELNETTY_MNES_CHARSET;        values[n++] = charset; }
    if (mtts) {
        snprintf(mtts_buf, sizeof(mtts_buf), "%u", (unsigned)mtts);
        names[n] = TELNETTY_MNES_MTTS; values[n++] = mtts_buf;
    }
    if (screen_reader >= 0) {
        snprintf(sr_buf, sizeof(sr_buf), "%d", screen_reader ? 1 : 0);
        names[n] = TELNETTY_MNES_SCREEN_READER; values[n++] = sr_buf;
    }
    if (n == 0) return 0;
    return telnetty_send_environ(ctx, names, values, n);
}

/* ============================================================================
 * Charset Implementation (RFC 2066)
 *
 * After WILL/DO settles to enabled, we send at most one SB REQUEST for
 * UTF-8 (or the first supported charset). Re-REQUEST is suppressed so
 * clients like Atlantis do not spam "Negotiated string encoding".
 * ============================================================================ */

/* Forward: SB handler */
static TELNETTY_UNUSED void telnetty_charset_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
);

/* Emit IAC SB CHARSET REQUEST ;UTF-8 IAC SE once. */
static TELNETTY_UNUSED void telnetty_charset_send_request(
    telnetty_context_t* ctx,
    telnetty_charset_data_t* cd
) {
    if (!ctx || !cd || cd->request_sent || cd->negotiation_complete)
        return;

    const char* name = "UTF-8";
    if (cd->charset_count > 0 && cd->supported_charsets && cd->supported_charsets[0])
        name = cd->supported_charsets[0];

    /* REQUEST + sep(';') + name */
    size_t nlen = strlen(name);
    uint8_t buf[64];
    if (nlen + 2 > sizeof(buf)) return;
    buf[0] = TELNETTY_CHARSET_REQUEST;
    buf[1] = ';';
    memcpy(buf + 2, name, nlen);

    if (telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_CHARSET, buf, nlen + 2) == 0)
        cd->request_sent = true;
}

static TELNETTY_UNUSED void telnetty_charset_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (!ctx || option != TELNETTY_TELOPT_CHARSET) return;
    telnetty_charset_data_t* cd = (telnetty_charset_data_t*)user_data;
    if (!cd) return;

    /* DO = peer accepted our WILL (we are enabled). WILL = peer offered. */
    if (command == TELNETTY_DO || command == TELNETTY_WILL) {
        telnetty_charset_send_request(ctx, cd);
    }
}

static TELNETTY_UNUSED void telnetty_charset_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    if (!ctx || option != TELNETTY_TELOPT_CHARSET || !data || length < 1)
        return;
    telnetty_charset_data_t* cd = (telnetty_charset_data_t*)user_data;
    if (!cd) return;

    uint8_t sub = data[0];
    if (sub == TELNETTY_CHARSET_ACCEPTED && length >= 2) {
        size_t n = length - 1;
        /* Validate the peer's chosen charset is one we actually offered.
         * A malicious peer could otherwise store any string into
         * cd->selected_charset which telnetty_get_charset() returns. */
        int accepted = 0;
        if (cd->supported_charsets && cd->charset_count > 0) {
            for (size_t i = 0; i < cd->charset_count; i++) {
                const char* s = cd->supported_charsets[i];
                if (s && strlen(s) == n && memcmp(s, data + 1, n) == 0) {
                    accepted = 1;
                    break;
                }
            }
        } else {
            /* No offered list means we accept any peer selection. */
            accepted = 1;
        }
        if (accepted) {
            char* sel = (char*)TELNETTY_OPTIONS_MALLOC(n + 1);
            if (sel) {
                memcpy(sel, data + 1, n);
                sel[n] = '\0';
                if (cd->selected_charset)
                    TELNETTY_OPTIONS_FREE(cd->selected_charset);
                cd->selected_charset = sel;
            }
            cd->negotiation_complete = true;
            cd->request_sent = true;
        } else {
            telnetty_report_error(ctx, TELNETTY_ERR_CHARSET_UNSUPPORTED,
                                  "Peer ACCEPTED an unoffered charset");
        }
    } else if (sub == TELNETTY_CHARSET_TTABLE_IS
            || sub == TELNETTY_CHARSET_TTABLE_REJECTED
            || sub == TELNETTY_CHARSET_TTABLE_ACK
            || sub == TELNETTY_CHARSET_TTABLE_NAK) {
        /* We do not implement translation tables (RFC 2066 §3.2). Reply
         * with TTABLE-REJECTED so the peer stops offering them; without
         * this the peer sees silence and may retry indefinitely. */
        uint8_t rej = TELNETTY_CHARSET_TTABLE_REJECTED;
        telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_CHARSET, &rej, 1);
    } else if (sub == TELNETTY_CHARSET_REQUEST) {
        /* Peer requests a charset — accept UTF-8 once. */
        if (!cd->negotiation_complete) {
            static const uint8_t acc[] = {
                TELNETTY_CHARSET_ACCEPTED,
                'U', 'T', 'F', '-', '8'
            };
            telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_CHARSET,
                                         acc, sizeof(acc));
            if (cd->selected_charset)
                TELNETTY_OPTIONS_FREE(cd->selected_charset);
            cd->selected_charset = (char*)TELNETTY_OPTIONS_MALLOC(6);
            if (cd->selected_charset)
                memcpy(cd->selected_charset, "UTF-8", 6);
            cd->negotiation_complete = true;
            cd->request_sent = true;
        }
        /* Already complete: silent — never re-ACCEPT. */
    } else if (sub == TELNETTY_CHARSET_REJECTED) {
        cd->negotiation_complete = false;
        /* Do NOT clear request_sent — avoids re-REQUEST storms. */
    }
}

static TELNETTY_UNUSED void telnetty_charset_data_free(void* p) {
    telnetty_charset_data_t* cd = (telnetty_charset_data_t*)p;
    if (!cd) return;
    if (cd->supported_charsets) {
        for (size_t i = 0; i < cd->charset_count; i++)
            TELNETTY_OPTIONS_FREE(cd->supported_charsets[i]);
        TELNETTY_OPTIONS_FREE(cd->supported_charsets);
    }
    TELNETTY_OPTIONS_FREE(cd->selected_charset);
    TELNETTY_OPTIONS_FREE(cd);
}

static TELNETTY_UNUSED int telnetty_enable_charset(
    telnetty_context_t* ctx,
    const char* const* charsets,
    size_t count
) {
    if (!ctx) return -1;

    telnetty_charset_data_t* cd = (telnetty_charset_data_t*)TELNETTY_OPTIONS_MALLOC(
        sizeof(telnetty_charset_data_t)
    );
    if (!cd) return -1;
    memset(cd, 0, sizeof(*cd));

    if (charsets && count > 0) {
        cd->supported_charsets = (char**)TELNETTY_OPTIONS_MALLOC(
            count * sizeof(char*)
        );
        if (cd->supported_charsets) {
            cd->charset_count = count;
            for (size_t i = 0; i < count; i++) {
                size_t n = charsets[i] ? strlen(charsets[i]) : 0;
                cd->supported_charsets[i] = (char*)TELNETTY_OPTIONS_MALLOC(n + 1);
                if (cd->supported_charsets[i] && charsets[i])
                    memcpy(cd->supported_charsets[i], charsets[i], n + 1);
            }
        }
    }

    /* Support both sides; wire command + SB handlers into core option slot. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_CHARSET, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_CHARSET,
                             telnetty_charset_handler, cd);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_CHARSET,
                                 telnetty_charset_sb_handler, cd);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_CHARSET, cd,
                                  telnetty_charset_data_free);

    /* Server offers WILL only (not DO) — one direction avoids dual REQUEST. */
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_CHARSET);
}

static TELNETTY_UNUSED const char* telnetty_get_charset(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_CHARSET];
    if (!opt || !opt->user_data) return NULL;
    telnetty_charset_data_t* cd = (telnetty_charset_data_t*)opt->user_data;
    return cd->selected_charset;
}

/* ============================================================================
 * End of Record Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_enable_eor(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_EOR, 1, 1);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_EOR);
}

/* ============================================================================
 * STATUS (RFC 859), TM (RFC 860), TSPEED (RFC 1079),
 * LFLOW (RFC 1372), LINEMODE (RFC 1184) — the options the README names.
 * SLC/FORWARDMASK are accepted and stored; we do not edit the client's
 * special-character table.
 * ============================================================================ */

#define TELNETTY_STATUS_IS   0
#define TELNETTY_STATUS_SEND 1
#define TELNETTY_TSPEED_IS   0
#define TELNETTY_TSPEED_SEND 1
#define TELNETTY_LFLOW_OFF   0
#define TELNETTY_LFLOW_ON    1
#define TELNETTY_LFLOW_RESTART_ANY 2
#define TELNETTY_LFLOW_RESTART_XON 3
#define TELNETTY_LM_MODE     1
#define TELNETTY_LM_FORWARDMASK 2
#define TELNETTY_LM_SLC      3

/* RFC 1184 LINEMODE MODE bits */
#define TELNETTY_LM_MODE_EDIT       0x01
#define TELNETTY_LM_MODE_TRAPSIG    0x02
#define TELNETTY_LM_MODE_ACK        0x04
#define TELNETTY_LM_MODE_SOFT_TAB   0x08
#define TELNETTY_LM_MODE_LIT_ECHO   0x10

/* RFC 1184 SLC function codes (RFC 1184 §5.3). We store all 30 defined
 * function slots plus a small headroom. */
#define TELNETTY_SLC_NFUNCS         31

/* SLC flag bits (level in low 2 bits, modifiers in high) */
#define TELNETTY_SLC_NOSUPPORT  0x00
#define TELNETTY_SLC_CANTCHANGE 0x01
#define TELNETTY_SLC_VARIABLE   0x02
#define TELNETTY_SLC_DEFAULT    0x03
#define TELNETTY_SLC_LEVELBITS  0x03
#define TELNETTY_SLC_ACK        0x80
#define TELNETTY_SLC_FLUSHIN    0x40
#define TELNETTY_SLC_FLUSHOUT   0x20

typedef struct {
    char tx[32];
    char rx[32];
    int have;
} telnetty_tspeed_data_t;

typedef struct {
    int last;          /* last LFLOW command byte, or -1 */
} telnetty_lflow_data_t;

typedef struct {
    uint8_t flags;     /* Latest SLC flags for this function; 0 = unset */
    uint8_t value;     /* Character value the peer bound to this function */
} telnetty_slc_entry_t;

typedef struct {
    int mode;          /* LINEMODE MODE byte, or -1 */
    uint8_t slc[32];   /* Legacy: last SLC payload snapshot, truncated */
    size_t slc_len;
    telnetty_slc_entry_t slc_table[TELNETTY_SLC_NFUNCS];
    int slc_updates;   /* Number of SLC triples applied since enable */
} telnetty_linemode_data_t;

static TELNETTY_UNUSED void telnetty_tspeed_free(void* p) { TELNETTY_OPTIONS_FREE(p); }
static TELNETTY_UNUSED void telnetty_lflow_free(void* p) { TELNETTY_OPTIONS_FREE(p); }
static TELNETTY_UNUSED void telnetty_linemode_free(void* p) { TELNETTY_OPTIONS_FREE(p); }

static TELNETTY_UNUSED void telnetty_status_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option; (void)user_data;
    if (!ctx || length < 1 || data[0] != TELNETTY_STATUS_SEND) return;
    uint8_t reply[3 + 256 * 2];
    size_t n = 0;
    reply[n++] = TELNETTY_STATUS_IS;
    for (int i = 0; i < 256 && n + 2 < sizeof(reply); i++) {
        telnetty_option_t* opt = ctx->options[i];
        if (!opt) continue;
        if (opt->us == TELNETTY_Q_YES) {
            reply[n++] = TELNETTY_WILL;
            reply[n++] = (uint8_t)i;
        }
        if (opt->him == TELNETTY_Q_YES) {
            reply[n++] = TELNETTY_DO;
            reply[n++] = (uint8_t)i;
        }
    }
    telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_STATUS, reply, n);
}

static TELNETTY_UNUSED int telnetty_enable_status(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_STATUS, 1, 1);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_STATUS, telnetty_status_sb, NULL);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_STATUS);
}

static TELNETTY_UNUSED int telnetty_send_timing_mark(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_TM, 1, 1);
    return telnetty_send_option(ctx, TELNETTY_DO, TELNETTY_TELOPT_TM);
}

static TELNETTY_UNUSED int telnetty_enable_timing_mark(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_TM, 1, 1);
    return 0;
}

static TELNETTY_UNUSED void telnetty_tspeed_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)ctx; (void)option;
    telnetty_tspeed_data_t* td = (telnetty_tspeed_data_t*)user_data;
    if (!td || length < 2 || data[0] != TELNETTY_TSPEED_IS) return;
    const char* s = (const char*)(data + 1);
    size_t n = length - 1;
    const char* comma = (const char*)memchr(s, ',', n);
    size_t tx_n = comma ? (size_t)(comma - s) : n;
    size_t rx_n = comma ? n - tx_n - 1 : 0;
    if (tx_n >= sizeof(td->tx)) tx_n = sizeof(td->tx) - 1;
    if (rx_n >= sizeof(td->rx)) rx_n = sizeof(td->rx) - 1;
    memcpy(td->tx, s, tx_n);
    td->tx[tx_n] = '\0';
    if (comma) {
        memcpy(td->rx, comma + 1, rx_n);
        td->rx[rx_n] = '\0';
    } else {
        td->rx[0] = '\0';
    }
    td->have = 1;
}

static TELNETTY_UNUSED int telnetty_enable_tspeed(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_tspeed_data_t* td = (telnetty_tspeed_data_t*)TELNETTY_OPTIONS_MALLOC(sizeof(*td));
    if (!td) return -1;
    memset(td, 0, sizeof(*td));
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_TERM_SPEED, 0, 1);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_TERM_SPEED, telnetty_tspeed_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_TERM_SPEED, td, telnetty_tspeed_free);
    return telnetty_send_option(ctx, TELNETTY_DO, TELNETTY_TELOPT_TERM_SPEED);
}

static TELNETTY_UNUSED int telnetty_get_term_speed(telnetty_context_t* ctx, const char** tx, const char** rx) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_TERM_SPEED];
    telnetty_tspeed_data_t* td = (opt && opt->user_data) ? (telnetty_tspeed_data_t*)opt->user_data : NULL;
    if (!td || !td->have) return -1;
    if (tx) *tx = td->tx;
    if (rx) *rx = td->rx;
    return 0;
}

static TELNETTY_UNUSED void telnetty_lflow_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)ctx; (void)option;
    telnetty_lflow_data_t* fd = (telnetty_lflow_data_t*)user_data;
    if (!fd || length < 1) return;
    fd->last = data[0];
}

static TELNETTY_UNUSED int telnetty_enable_lflow(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_lflow_data_t* fd = (telnetty_lflow_data_t*)TELNETTY_OPTIONS_MALLOC(sizeof(*fd));
    if (!fd) return -1;
    fd->last = -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_REMOTE_FLOW, 1, 1);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_REMOTE_FLOW, telnetty_lflow_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_REMOTE_FLOW, fd, telnetty_lflow_free);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_REMOTE_FLOW);
}

static TELNETTY_UNUSED int telnetty_get_flow(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_REMOTE_FLOW];
    telnetty_lflow_data_t* fd = (opt && opt->user_data) ? (telnetty_lflow_data_t*)opt->user_data : NULL;
    return fd ? fd->last : -1;
}

/* Reply to a LINEMODE MODE offer with the mode byte plus MODE_ACK, so
 * the peer knows we honored its edit/trapsig choices. Idempotent: if the
 * peer already set the ACK bit it's re-asserting, and we still echo. */
static TELNETTY_UNUSED void telnetty__linemode_send_mode_ack(telnetty_context_t* ctx, uint8_t mode) {
    uint8_t reply[2];
    reply[0] = TELNETTY_LM_MODE;
    reply[1] = (uint8_t)(mode | TELNETTY_LM_MODE_ACK);
    telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_LINEMODE, reply, 2);
}

/* Reply to an SLC frame by echoing every triple back with the ACK bit set
 * (RFC 1184 §5.5). Called from the SB handler after we've stored the
 * peer's triples in slc_table. */
static TELNETTY_UNUSED void telnetty__linemode_send_slc_ack(
    telnetty_context_t* ctx,
    const uint8_t* triples, size_t triple_count
) {
    if (triple_count == 0) return;
    /* 1 byte SLC tag + 3 bytes per triple. Cap at 128 triples per frame
     * to stay well under any SB length limit. */
    if (triple_count > 128) triple_count = 128;
    uint8_t buf[1 + 128 * 3];
    size_t off = 0;
    buf[off++] = TELNETTY_LM_SLC;
    for (size_t i = 0; i < triple_count; i++) {
        buf[off++] = triples[i * 3 + 0];
        buf[off++] = (uint8_t)(triples[i * 3 + 1] | TELNETTY_SLC_ACK);
        buf[off++] = triples[i * 3 + 2];
    }
    telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_LINEMODE, buf, off);
}

static TELNETTY_UNUSED void telnetty_linemode_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option;
    telnetty_linemode_data_t* lm = (telnetty_linemode_data_t*)user_data;
    if (!lm || !data || length < 1) return;

    switch (data[0]) {
    case TELNETTY_LM_MODE:
        if (length < 2) return;
        lm->mode = data[1];
        /* If this MODE offer does NOT already carry ACK, echo it back with
         * ACK set. Peers that sent an ACK are confirming our earlier one
         * and don't want a second round. */
        if (ctx && !(data[1] & TELNETTY_LM_MODE_ACK)) {
            telnetty__linemode_send_mode_ack(ctx, data[1]);
        }
        break;

    case TELNETTY_LM_SLC: {
        /* Legacy blob snapshot (kept for backward-compat callers). */
        size_t n = length - 1;
        if (n > sizeof(lm->slc)) n = sizeof(lm->slc);
        memcpy(lm->slc, data + 1, n);
        lm->slc_len = n;

        /* SLC payload is a stream of {func, flags, value} triples per
         * RFC 1184 §5.3. Ignore trailing partial triples defensively. */
        const uint8_t* p = data + 1;
        size_t rem = length - 1;
        size_t triples = rem / 3;
        /* Buffer for the ACK echo. Bounded by triples. */
        uint8_t ack_buf[128 * 3];
        size_t ack_n = 0;

        for (size_t i = 0; i < triples; i++) {
            uint8_t func  = p[i * 3 + 0];
            uint8_t flags = p[i * 3 + 1];
            uint8_t value = p[i * 3 + 2];

            if (func < TELNETTY_SLC_NFUNCS) {
                lm->slc_table[func].flags = flags;
                lm->slc_table[func].value = value;
                lm->slc_updates++;
            }

            /* Peer-initiated triples (ACK not yet set) get echoed with
             * ACK; peer-ACK triples close a prior exchange and need no
             * reply. */
            if (!(flags & TELNETTY_SLC_ACK) && ack_n + 3 <= sizeof(ack_buf)) {
                ack_buf[ack_n++] = func;
                ack_buf[ack_n++] = flags;
                ack_buf[ack_n++] = value;
            }
        }

        if (ctx && ack_n > 0) {
            telnetty__linemode_send_slc_ack(ctx, ack_buf, ack_n / 3);
        }
        break;
    }

    case TELNETTY_LM_FORWARDMASK:
        /* Accepted but stored opaquely — libtelnet does the same. Users
         * who need bitmask editing hook the SB handler directly. */
        break;

    default:
        /* Unknown LINEMODE subcommand — quietly drop. RFC 1184 leaves
         * this "should be ignored". */
        break;
    }
}

/**
 * Fetch the LINEMODE SLC entry for one function code (RFC 1184 §5.3).
 * Returns 0 on hit, -1 if `func` is out of range or LINEMODE is not
 * enabled. Fills *flags and *value from the last triple the peer sent
 * for this function.
 */
static TELNETTY_UNUSED int telnetty_get_slc(
    telnetty_context_t* ctx, uint8_t func,
    uint8_t* flags, uint8_t* value
) {
    if (!ctx || func >= TELNETTY_SLC_NFUNCS) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_LINEMODE];
    telnetty_linemode_data_t* lm = (opt && opt->user_data)
        ? (telnetty_linemode_data_t*)opt->user_data : NULL;
    if (!lm) return -1;
    if (flags) *flags = lm->slc_table[func].flags;
    if (value) *value = lm->slc_table[func].value;
    return 0;
}

/**
 * Send a MODE offer to the peer with the given mode bits. The peer must
 * reply with the same bits and MODE_ACK set to confirm.
 */
static TELNETTY_UNUSED int telnetty_linemode_send_mode(
    telnetty_context_t* ctx, uint8_t mode
) {
    if (!ctx) return -1;
    uint8_t sb[2] = { TELNETTY_LM_MODE, mode };
    return telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_LINEMODE, sb, 2);
}

static TELNETTY_UNUSED int telnetty_enable_linemode(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_linemode_data_t* lm = (telnetty_linemode_data_t*)TELNETTY_OPTIONS_MALLOC(sizeof(*lm));
    if (!lm) return -1;
    memset(lm, 0, sizeof(*lm));
    lm->mode = -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_LINEMODE, 1, 1);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_LINEMODE, telnetty_linemode_sb, NULL);
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_LINEMODE, lm, telnetty_linemode_free);
    return telnetty_send_option(ctx, TELNETTY_DO, TELNETTY_TELOPT_LINEMODE);
}

static TELNETTY_UNUSED int telnetty_get_linemode(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_LINEMODE];
    telnetty_linemode_data_t* lm = (opt && opt->user_data) ? (telnetty_linemode_data_t*)opt->user_data : NULL;
    return lm ? lm->mode : -1;
}

static TELNETTY_UNUSED int telnetty_send_eor(telnetty_context_t* ctx) {
    if (!ctx) return -1;

    /* Send EOR command */
    return telnetty_send_command(ctx, TELNETTY_EOR);
}

/* ============================================================================
 * StartTLS (TELOPT 46, draft-altman-telnet-starttls)
 *
 * Signaling only. The actual TLS upgrade is caller's job:
 *   1. peer sends IAC WILL 46 (or we send IAC WILL 46)
 *   2. both sides settle to enabled
 *   3. one side sends IAC SB STARTTLS FOLLOWS IAC SE
 *   4. that side fires TELNETTY_EVENT_CUSTOM + TELNETTY_STARTTLS_EVENT_ID
 *   5. the app hands the socket to its TLS layer and calls
 *      telnetty_starttls_reset(ctx) to drop any queued cleartext
 *
 * We never touch bytes ourselves — this is purely a negotiation gateway.
 * ============================================================================ */

/* Reserve a stable custom-event id for STARTTLS. Callers who register
 * extended handlers for TELNETTY_EVENT_CUSTOM should check this id. */
#define TELNETTY_STARTTLS_EVENT_ID  0x54544c53u  /* 'TTLS' */

/* draft-altman-telnet-starttls SB subcommands */
#define TELNETTY_STARTTLS_FOLLOWS  1


static TELNETTY_UNUSED void telnetty__starttls_sb(
    telnetty_context_t* ctx, uint8_t option,
    const uint8_t* data, size_t length, void* user_data
) {
    (void)option; (void)user_data;
    if (!ctx || !data || length < 1) return;
    if (data[0] != TELNETTY_STARTTLS_FOLLOWS) return;
    /* Signal-only fire. We use telnetty_fire_event directly to avoid
     * dragging events.h into options.h — the union carries no payload
     * (length 0) since the "next bytes are TLS" is the whole message. */
    telnetty_event_data_union_t u;
    memset(&u, 0, sizeof(u));
    u.data.data = NULL;
    u.data.length = 0;
    telnetty_fire_event(ctx,
        (telnetty_event_type_t)TELNETTY_STARTTLS_EVENT_ID, &u);
}

static TELNETTY_UNUSED void telnetty__starttls_handler(
    telnetty_context_t* ctx, uint8_t option, uint8_t command, void* user_data
) {
    (void)ctx; (void)option; (void)command; (void)user_data;
    /* Nothing to do here — SB drives the FOLLOWS signal. */
}

/**
 * Enable StartTLS negotiation. After the peer accepts (DO 46), call
 * telnetty_starttls_send_follows() when your side is ready to switch,
 * OR watch for TELNETTY_EVENT_CUSTOM + TELNETTY_STARTTLS_EVENT_ID when
 * the peer signals its readiness.
 */
static TELNETTY_UNUSED int telnetty_enable_starttls(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_STARTTLS, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_STARTTLS,
                             telnetty__starttls_handler, NULL);
    telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_STARTTLS,
                                 telnetty__starttls_sb, NULL);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_STARTTLS);
}

/**
 * Announce "next bytes are TLS handshake" via IAC SB STARTTLS FOLLOWS
 * IAC SE. Caller must upgrade the socket to TLS immediately after this
 * call. The peer will fire its own TELNETTY_STARTTLS_EVENT_ID.
 */
static TELNETTY_UNUSED int telnetty_starttls_send_follows(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    uint8_t sb = TELNETTY_STARTTLS_FOLLOWS;
    return telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_STARTTLS, &sb, 1);
}

/* ============================================================================
 * Helper Functions Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_enable_all_options(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    int enabled = 0;
    
    /* Enable basic options */
    if (telnetty_enable_binary(ctx) == 0) enabled++;
    if (telnetty_enable_echo(ctx) == 0) enabled++;
    if (telnetty_enable_sga(ctx) == 0) enabled++;
    if (telnetty_enable_eor(ctx) == 0) enabled++;
    if (telnetty_enable_naws(ctx) == 0) enabled++;
    if (telnetty_enable_environ(ctx) == 0) enabled++;
    if (telnetty_enable_status(ctx) == 0) enabled++;
    if (telnetty_enable_timing_mark(ctx) == 0) enabled++;
    if (telnetty_enable_tspeed(ctx) == 0) enabled++;
    if (telnetty_enable_lflow(ctx) == 0) enabled++;
    
    return enabled;
}

static TELNETTY_UNUSED int telnetty_disable_all_options(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Send WONT/DONT for all options */
    for (int option = 0; option < 256; option++) {
        telnetty_send_option(ctx, TELNETTY_WONT, (uint8_t)option);
        telnetty_send_option(ctx, TELNETTY_DONT, (uint8_t)option);
    }
    
    return 0;
}

/* Note: telnetty_is_option_enabled and telnetty_get_option_state are defined in telnetty_core.h */

static TELNETTY_UNUSED int telnetty_wait_for_option(
    telnetty_context_t* ctx,
    uint8_t option,
    int timeout_ms
) {
    (void)option;
    (void)timeout_ms;
    if (!ctx) return -1;
    
    telnetty_option_t* opt = ctx->options[option];
    if (!opt) return 0;
    return (opt->us == TELNETTY_Q_YES || opt->him == TELNETTY_Q_YES) ? 1 : 0;
}

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_option_tracker_t* telnetty_option_tracker_create(
    uint8_t option,
    telnetty_option_handler_t handler,
    void* user_data
) {
    telnetty_option_tracker_t* tracker = (telnetty_option_tracker_t*)TELNETTY_OPTIONS_MALLOC(
        sizeof(telnetty_option_tracker_t)
    );
    
    if (!tracker) return NULL;
    
    memset(tracker, 0, sizeof(telnetty_option_tracker_t));
    tracker->option = option;
    tracker->handler = handler;
    tracker->user_data = user_data;
    tracker->local_state = TELNETTY_OPTION_STATE_DISABLED;
    tracker->remote_state = TELNETTY_OPTION_STATE_DISABLED;
    
    return tracker;
}

static TELNETTY_UNUSED void telnetty_option_tracker_destroy(telnetty_option_tracker_t* tracker) {
    if (!tracker) return;
    
    /* Free option-specific data */
    if (tracker->option_data) {
        TELNETTY_OPTIONS_FREE(tracker->option_data);
    }
    
    TELNETTY_OPTIONS_FREE(tracker);
}

static TELNETTY_UNUSED int telnetty_option_send_negotiation(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command
) {
    if (!ctx) return -1;
    
    return telnetty_send_option(ctx, command, option);
}

#endif /* TELNETTY_OPTIONS_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_OPTIONS_H */
