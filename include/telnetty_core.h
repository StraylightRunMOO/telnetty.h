/**
 * @file telnetty_core.h
 * @brief Core TELNET protocol implementation
 * 
 * This header provides the foundation for TELNET protocol handling,
 * including IAC sequence parsing, state management, and core event handling.
 * 
 * Features:
 * - RFC 854/855 compliant TELNET protocol
 * - Event-driven architecture
 * - State machine implementation
 * - Zero-copy buffer operations
 * - Performance optimized parsing
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.0.0
 * 
 */

#ifndef TELNETTY_CORE_H
#define TELNETTY_CORE_H

#include <stdint.h>
#include <limits.h>
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
 * Configuration and Compilation Options
 * ============================================================================ */

#ifndef TELNETTY_MAX_BUFFER_SIZE
#define TELNETTY_MAX_BUFFER_SIZE (64 * 1024)  /* 64KB max buffer size */
#endif

#ifndef TELNETTY_INITIAL_BUFFER_SIZE
#define TELNETTY_INITIAL_BUFFER_SIZE 4096     /* Initial buffer allocation */
#endif

#ifndef TELNETTY_SMALL_BUFFER_SIZE
#define TELNETTY_SMALL_BUFFER_SIZE 256        /* Small buffer optimization */
#endif

#ifndef TELNETTY_MAX_OPTION_LENGTH
#define TELNETTY_MAX_OPTION_LENGTH 256        /* Legacy alias; see MAX_SB_LENGTH */
#endif

/* Maximum subnegotiation payload the parser will accept. MSSP / GMCP / MCCP
 * exchanges routinely exceed the historical 256-byte cap, so we raise it to
 * 64 KiB and surface an EVENT_ERROR on overflow rather than silently drop. */
#ifndef TELNETTY_MAX_SB_LENGTH
#define TELNETTY_MAX_SB_LENGTH (64 * 1024)
#endif

#ifndef TELNETTY_MAX_IAC_SEQUENCES
#define TELNETTY_MAX_IAC_SEQUENCES 100        /* Maximum pending IAC sequences */
#endif

/* ============================================================================
 * Library Version
 * ============================================================================ */

#define TELNETTY_VERSION_MAJOR  1
#define TELNETTY_VERSION_MINOR  1
#define TELNETTY_VERSION_PATCH  0
#define TELNETTY_VERSION_STRING "1.1.0"
#define TELNETTY_VERSION_HEX \
    ((TELNETTY_VERSION_MAJOR << 16) | (TELNETTY_VERSION_MINOR << 8) | TELNETTY_VERSION_PATCH)

/* Error codes surfaced via EVENT_ERROR / telnetty_last_error. Zero is OK so
 * a fresh context reports no error. Additive — new codes may be appended in
 * later minors but existing values never change. */
typedef enum {
    TELNETTY_OK = 0,
    TELNETTY_ERR_ALLOC,
    TELNETTY_ERR_SB_OVERFLOW,
    TELNETTY_ERR_PROTOCOL,
    TELNETTY_ERR_TIMEOUT,
    TELNETTY_ERR_RATE_LIMIT,
    TELNETTY_ERR_INVAL,
    TELNETTY_ERR_NOT_IMPLEMENTED,
    TELNETTY_ERR_CHARSET_UNSUPPORTED,
    TELNETTY_ERR_COMPRESSION
} telnetty_error_t;

/* Log severity levels for telnetty_set_log_callback. TRACE is per-byte noisy;
 * DEBUG is per-frame; INFO is per-session milestone; WARN is a recoverable
 * anomaly; ERROR mirrors an EVENT_ERROR fire. Numerical order matters: the
 * callback fires only for messages at or above the threshold set at register
 * time. */
typedef enum {
    TELNETTY_LOG_TRACE = 0,
    TELNETTY_LOG_DEBUG = 1,
    TELNETTY_LOG_INFO  = 2,
    TELNETTY_LOG_WARN  = 3,
    TELNETTY_LOG_ERROR = 4
} telnetty_log_level_t;

struct telnetty_context;
typedef void (*telnetty_log_callback_t)(
    struct telnetty_context* ctx,
    telnetty_log_level_t level,
    const char* category,
    const char* message,
    void* user_data
);

/* Branch hints for the hot IAC scanner. Fall back to no-ops elsewhere. */
#ifndef TELNETTY_LIKELY
#if defined(__GNUC__) || defined(__clang__)
#define TELNETTY_LIKELY(x)   __builtin_expect(!!(x), 1)
#define TELNETTY_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define TELNETTY_LIKELY(x)   (x)
#define TELNETTY_UNLIKELY(x) (x)
#endif
#endif

#ifndef TELNETTY_RESTRICT
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define TELNETTY_RESTRICT restrict
#elif defined(__GNUC__) || defined(__clang__)
#define TELNETTY_RESTRICT __restrict__
#else
#define TELNETTY_RESTRICT
#endif
#endif

/* Enable performance optimizations by default */
#ifndef TELNETTY_DISABLE_FAST_PATH
#define TELNETTY_ENABLE_FAST_PATH 1
#endif

/* Enable memory debugging if requested */
#ifdef TELNETTY_DEBUG_MEMORY
#include <stdio.h>
#define TELNETTY_DEBUG(fmt, ...) fprintf(stderr, "[TELNET] " fmt "\n", __VA_ARGS__)
#else
#define TELNETTY_DEBUG(fmt, ...) do {} while(0)
#endif

/* ============================================================================
 * TELNET Protocol Constants (RFC 854/855)
 * ============================================================================ */

/* TELNET Commands */
#define TELNETTY_IAC     255    /* Interpret As Command */
#define TELNETTY_DONT    254    /* Request to disable option */
#define TELNETTY_DO      253    /* Request to enable option */
#define TELNETTY_WONT    252    /* Refuse to enable option */
#define TELNETTY_WILL    251    /* Agree to enable option */
#define TELNETTY_SB      250    /* Subnegotiation begin */
#define TELNETTY_GA      249    /* Go ahead */
#define TELNETTY_EL      248    /* Erase line */
#define TELNETTY_EC      247    /* Erase character */
#define TELNETTY_AYT     246    /* Are you there? */
#define TELNETTY_AO      245    /* Abort output */
#define TELNETTY_IP      244    /* Interrupt process */
#define TELNETTY_BREAK   243    /* Break */
#define TELNETTY_DM      242    /* Data mark */
#define TELNETTY_NOP     241    /* No operation */
#define TELNETTY_SE      240    /* Subnegotiation end */
#define TELNETTY_EOR     239    /* End of record */
#define TELNETTY_ABORT   238    /* Abort process */
#define TELNETTY_SUSP    237    /* Suspend process */
#define TELNETTY_EOF     236    /* End of file */

/* Standard TELNET Options */
#define TELNETTY_TELOPT_BINARY           0   /* Binary transmission */
#define TELNETTY_TELOPT_ECHO             1   /* Echo */
#define TELNETTY_TELOPT_RCP              2   /* Reconnection */
#define TELNETTY_TELOPT_SGA              3   /* Suppress Go Ahead */
#define TELNETTY_TELOPT_NAMS             4   /* Approximate message size */
#define TELNETTY_TELOPT_STATUS           5   /* Status */
#define TELNETTY_TELOPT_TM               6   /* Timing mark */
#define TELNETTY_TELOPT_RCTE             7   /* Remote controlled trans and echo */
#define TELNETTY_TELOPT_NAOL             8   /* Output line width */
#define TELNETTY_TELOPT_NAOP             9   /* Output page size */
#define TELNETTY_TELOPT_NAOCRD          10   /* Output carriage return disposition */
#define TELNETTY_TELOPT_NAOHTS          11   /* Output horizontal tabstops */
#define TELNETTY_TELOPT_NAOHTD          12   /* Output horizontal tab disposition */
#define TELNETTY_TELOPT_NAOFFD          13   /* Output formfeed disposition */
#define TELNETTY_TELOPT_NAOVTS          14   /* Output vertical tabstops */
#define TELNETTY_TELOPT_NAOVTD          15   /* Output vertical tab disposition */
#define TELNETTY_TELOPT_NAOLFD          16   /* Output linefeed disposition */
#define TELNETTY_TELOPT_XASCII          17   /* Extended ASCII */
#define TELNETTY_TELOPT_LOGOUT          18   /* Logout */
#define TELNETTY_TELOPT_BM              19   /* Byte macro */
#define TELNETTY_TELOPT_DET             20   /* Data entry terminal */
#define TELNETTY_TELOPT_SUPDUP          21   /* SUPDUP */
#define TELNETTY_TELOPT_SUPDUPOUTPUT    22   /* SUPDUP output */
#define TELNETTY_TELOPT_SNDLOC          23   /* Send location */
#define TELNETTY_TELOPT_TTYPE           24   /* Terminal type */
#define TELNETTY_TELOPT_EOR             25   /* End of record */
#define TELNETTY_TELOPT_TUID            26   /* TACACS user identification */
#define TELNETTY_TELOPT_OUTMRK          27   /* Output marking */
#define TELNETTY_TELOPT_TTYLOC          28   /* Terminal location number */
#define TELNETTY_TELOPT_3270REGIME      29   /* 3270 regime */
#define TELNETTY_TELOPT_X3PAD           30   /* X.3 PAD */
#define TELNETTY_TELOPT_NAWS            31   /* Negotiate about window size */
#define TELNETTY_TELOPT_TERM_SPEED      32   /* Terminal speed */
#define TELNETTY_TELOPT_REMOTE_FLOW     33   /* Remote flow control */
#define TELNETTY_TELOPT_LINEMODE        34   /* Linemode */
#define TELNETTY_TELOPT_XDISPLOC        35   /* X display location */
#define TELNETTY_TELOPT_OLD_ENVIRON     36   /* Old environment variables */
#define TELNETTY_TELOPT_AUTH            37   /* Authentication */
#define TELNETTY_TELOPT_ENCRYPT         38   /* Encryption */
#define TELNETTY_TELOPT_NEW_ENVIRON     39   /* New environment variables */
#define TELNETTY_TELOPT_TN3270E         40   /* TN3270E */
#define TELNETTY_TELOPT_CHARSET         42   /* Charset (RFC 2066) */
#define TELNETTY_TELOPT_EOL             45   /* End of line */
#define TELNETTY_TELOPT_STARTTLS        46   /* StartTLS (draft-altman) */

/* RFC 1143 Q-method negotiation states (libtelnet-compatible). */
#define TELNETTY_Q_NO         0   /* Option disabled */
#define TELNETTY_Q_YES        1   /* Option enabled */
#define TELNETTY_Q_WANTNO     2   /* Want to disable */
#define TELNETTY_Q_WANTYES    3   /* Want to enable */
#define TELNETTY_Q_WANTNO_OP  4   /* Want disable, then opposite */
#define TELNETTY_Q_WANTYES_OP 5   /* Want enable, then opposite */
/* Back-compat alias (typo in early drafts) */
#define TELNETTY_Q_OPOSITE    TELNETTY_Q_WANTNO_OP

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct telnetty_context telnetty_context_t;
typedef struct telnetty_buffer telnetty_buffer_t;
typedef struct telnetty_option telnetty_option_t;

/* ============================================================================
 * Protocol State Machine
 * ============================================================================ */

/* TELNET protocol parser states */
typedef enum {
    TELNETTY_STATE_DATA = 0,              /**< Normal data mode */
    TELNETTY_STATE_IAC,                   /**< IAC received */
    TELNETTY_STATE_NEGOTIATION,           /**< Option negotiation */
    TELNETTY_STATE_SUBNEG,                /**< Subnegotiation */
    TELNETTY_STATE_SUBNEG_IAC             /**< IAC during subnegotiation */
} telnetty_state_t;

/* ============================================================================
 * Statistics
 * ============================================================================ */

/* TELNET connection statistics */
typedef struct {
    uint64_t bytes_sent;                /**< Total bytes sent */
    uint64_t bytes_received;            /**< Total bytes received */
    uint64_t commands_sent;             /**< IAC commands sent */
    uint64_t commands_received;         /**< IAC commands received */
    uint64_t options_negotiated;        /**< Options negotiated */
    uint64_t subnegotiations;           /**< Subnegotiations processed */
    uint64_t errors;                    /**< Protocol errors */
} telnetty_stats_t;

/* ============================================================================
 * Event System
 * ============================================================================ */

/* Event types for the event-driven architecture */
typedef enum {
    TELNETTY_EVENT_DATA = 0,              /**< Raw data received */
    TELNETTY_EVENT_IAC,                   /**< IAC command received */
    TELNETTY_EVENT_WILL,                  /**< WILL option negotiation */
    TELNETTY_EVENT_WONT,                  /**< WONT option negotiation */
    TELNETTY_EVENT_DO,                    /**< DO option negotiation */
    TELNETTY_EVENT_DONT,                  /**< DONT option negotiation */
    TELNETTY_EVENT_SB,                    /**< Subnegotiation begin */
    TELNETTY_EVENT_SE,                    /**< Subnegotiation end */
    TELNETTY_EVENT_ERROR,                 /**< Protocol error */
    TELNETTY_EVENT_TIMEOUT,               /**< Timeout event */
    TELNETTY_EVENT_CUSTOM_START = 100     /**< Custom events start here */
} telnetty_event_type_t;

/* Data structures for events */
typedef struct {
    const uint8_t* data;                /**< Data buffer */
    size_t length;                      /**< Data length */
} telnetty_event_data_t;

typedef struct {
    uint8_t command;                    /**< IAC command */
} telnetty_event_iac_t;

typedef struct {
    uint8_t option;                     /**< Option code */
} telnetty_event_option_t;

typedef struct {
    uint8_t option;                     /**< Option code */
    const uint8_t* data;                /**< Subnegotiation data */
    size_t length;                      /**< Data length */
} telnetty_event_subnegotiation_t;

typedef struct {
    int code;                           /**< Error code */
    const char* message;                /**< Error message */
} telnetty_event_error_t;

/* Extended event data placeholder (for use by telnetty_events.h) */
typedef struct {
    char data[32];                      /**< Reserved for extended event data */
} telnetty_event_extended_t;

/* Event union */
typedef union {
    telnetty_event_data_t data;
    telnetty_event_iac_t iac;
    telnetty_event_option_t option;
    telnetty_event_subnegotiation_t sub;
    telnetty_event_error_t error;
    telnetty_event_extended_t ext;        /**< Extended event data */
} telnetty_event_data_union_t;

/* Event callback function type */
typedef void (*telnetty_event_callback_t)(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data,
    void* user_data
);

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

/* Buffer structure for efficient memory management */
struct telnetty_buffer {
    uint8_t* data;                      /**< Buffer data */
    size_t size;                        /**< Total buffer size */
    size_t length;                      /**< Current data length */
    size_t offset;                      /**< Read offset */
    struct telnetty_buffer* next;         /**< Next buffer in chain */
    int flags;                          /**< Buffer flags */
};

/* Buffer flags */
#define TELNETTY_BUFFER_FLAG_STATIC   0x01    /**< Static buffer, don't free */
#define TELNETTY_BUFFER_FLAG_DYNAMIC  0x02    /**< Dynamic buffer, free on release */
#define TELNETTY_BUFFER_FLAG_CHAINED  0x04    /**< Part of a buffer chain */
#define TELNETTY_BUFFER_FLAG_COMPRESS 0x08    /**< Contains compressed data */

/* ============================================================================
 * Option Management
 * ============================================================================ */

/* Option handler function type (WILL/WONT/DO/DONT after Q-method settles) */
typedef void (*telnetty_option_handler_t)(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/* Subnegotiation handler (option already stripped from payload) */
typedef void (*telnetty_sb_handler_t)(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
);

/* Option structure for managing TELNET options */
struct telnetty_option {
    uint8_t option;                     /**< Option code */
    uint8_t us;                         /**< Our state (RFC 1143 Q-method) */
    uint8_t him;                        /**< Peer state (RFC 1143 Q-method) */
    uint8_t support_us;                 /**< 1 = we may enable locally (accept DO) */
    uint8_t support_him;                /**< 1 = we may allow remote (accept WILL) */
    uint8_t sb_term;                    /**< Subnegotiation terminator */
    telnetty_buffer_t* sb_data;         /**< Subnegotiation buffer */
    telnetty_option_handler_t handler;  /**< Option command handler */
    telnetty_sb_handler_t sb_handler;   /**< Subnegotiation handler */
    void* user_data;                    /**< User data for handlers */
    void (*user_data_free)(void*);      /**< Optional freer for user_data */
    struct telnetty_option* next;       /**< Next option in list */
    uint64_t deadline_ms;               /**< Wall-clock deadline for in-flight
                                         *   negotiation (0 = no deadline).
                                         *   Fires EVENT_ERROR/TELNETTY_ERR_TIMEOUT
                                         *   when telnetty_tick sees now_ms >= deadline. */
};

/* ============================================================================
 * Main Context Structure
 * ============================================================================ */

/* Main TELNET context structure */
struct telnetty_context {
    /* Core state */
    telnetty_state_t state;               /**< Current TELNET state */
    telnetty_event_callback_t event_cb;   /**< Event callback */
    void* user_data;                    /**< User data for callbacks */
    
    /* Buffer management */
    telnetty_buffer_t* in_buffer;         /**< Input buffer */
    telnetty_buffer_t* out_buffer;        /**< Output buffer */
    telnetty_buffer_t* sb_buffer;         /**< Subnegotiation buffer */
    
    /* Option management */
    telnetty_option_t* options[256];      /**< Option handlers by code */
    
    /* State tracking */
    uint8_t current_option;             /**< Current option being negotiated / SB option */
    uint8_t iac_state;                  /**< IAC parsing state (WILL/DO/…) */
    uint8_t sb_got_option;              /**< 1 once SB option byte consumed */
    size_t sb_length;                   /**< Subnegotiation payload length */
    
    /* Performance counters */
    telnetty_stats_t stats;               /**< Connection statistics */
    
    /* Configuration */
    struct {
        int flags;                      /**< Context flags */
        size_t max_buffer_size;         /**< Maximum buffer size */
        size_t initial_buffer_size;     /**< Initial buffer size */
        int timeout_ms;                 /**< Timeout in milliseconds */
    } config;

    /* Optional outbound filter (MCCP). NULL writes straight to out_buffer.
     * The hook must append to out_buffer itself and must not call back
     * into telnetty_send. Return bytes accepted, or -1. */
    int (*out_hook)(struct telnetty_context* ctx, const uint8_t* data, size_t len);

    /* Optional inbound filter (e.g. MCCP-inflate on the client side). When
     * set, telnetty_process_inbound() will route the raw peer bytes here
     * and the hook is responsible for calling telnetty_process() on the
     * inflated result. NULL means no inbound filter. */
    int (*in_hook)(struct telnetty_context* ctx, const uint8_t* data, size_t len);
    void* in_hook_user_data;

    /* Last error surfaced through EVENT_ERROR; read via telnetty_last_error(). */
    telnetty_error_t last_error;

    /* Priority-ordered linked list of extended-event handlers (registered
     * through telnetty_events.h). Opaque here — treated as an untyped
     * pointer so telnetty_core can walk / free without depending on the
     * events header. Cast at use sites. */
    void* event_handlers;

    /* Attached color state (opaque here — filled in when
     * telnetty_color_init / _attach installs a telnetty_color_state_t). */
    void* color_state;
    void (*color_state_free)(void*);

    /* Structured logging sink. NULL by default; when set, telnetty_log() and
     * the internal TELNETTY_LOG(...) macro route messages here instead of
     * fprintf(stderr,...). Only messages at or above log_threshold fire. */
    telnetty_log_callback_t log_cb;
    void* log_user_data;
    telnetty_log_level_t log_threshold;

    /* Negotiation timeout (item 30). Zero disables. Whenever send_option
     * pushes an option into a WANT* state, we stamp opt->deadline_ms with
     * now_ms + this. telnetty_tick(ctx, now_ms) is the caller's clock. */
    uint32_t negotiation_timeout_ms;
    uint64_t now_ms;                    /**< Last tick timestamp. 0 = never ticked. */

    /* Rate limits (item 31). Token-bucket state per byte-stream and per-IAC
     * frame. Rates in units-per-second; zero disables that bucket. Requires
     * telnetty_tick() to advance now_ms — an unticked context skips
     * enforcement so callers who never opt in pay nothing. */
    uint32_t rate_bytes_per_sec;
    uint32_t rate_iac_per_sec;
    int64_t  bytes_tokens;              /**< Signed so an over-limit burst
                                         *   goes negative and we know to trip. */
    int64_t  iac_tokens;
    uint64_t rate_refill_ms;            /**< Last bucket refill timestamp. */
};

/* Context flags */
#define TELNETTY_FLAG_QUIET           0x01    /**< Suppress error callbacks */
#define TELNETTY_FLAG_NVT             0x02    /**< Network Virtual Terminal mode */
#define TELNETTY_FLAG_PROXY           0x04    /**< Acting as proxy */
#define TELNETTY_FLAG_SERVER          0x08    /**< Server mode */
#define TELNETTY_FLAG_CLIENT          0x10    /**< Client mode */
#define TELNETTY_FLAG_DEBUG           0x20    /**< Debug mode */

/* ============================================================================
 * Core API Functions
 * ============================================================================ */

/**
 * Create a new TELNET context
 * 
 * @param event_cb Event callback function
 * @param user_data User data passed to callbacks
 * @return New TELNET context or NULL on failure
 */
static telnetty_context_t* telnetty_create(
    telnetty_event_callback_t event_cb,
    void* user_data
);

/**
 * Destroy a TELNET context and free all resources
 * 
 * @param ctx TELNET context to destroy
 */
static TELNETTY_UNUSED void telnetty_destroy(telnetty_context_t* ctx);

/**
 * Process incoming TELNET data
 * 
 * @param ctx TELNET context
 * @param data Input data buffer
 * @param length Length of input data
 * @return Number of bytes processed or -1 on error
 */
static int telnetty_process(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
);

/**
 * Send data through the TELNET connection
 * 
 * @param ctx TELNET context
 * @param data Data to send
 * @param length Length of data
 * @return Number of bytes queued or -1 on error
 */
static int telnetty_send(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
);

/**
 * Send a TELNET command
 * 
 * @param ctx TELNET context
 * @param command Command to send
 * @return 0 on success or -1 on error
 */
static int telnetty_send_command(
    telnetty_context_t* ctx,
    uint8_t command
);

/**
 * Send option negotiation using RFC 1143 Q-method.
 *
 * Only emits IAC bytes when the desired state actually changes — repeated
 * WILL/DO of an already-enabled option is a no-op (prevents client spam).
 *
 * @param ctx TELNET context
 * @param command Command (WILL/WONT/DO/DONT)
 * @param option Option code
 * @return 0 on success or -1 on error
 */
static int telnetty_send_option(
    telnetty_context_t* ctx,
    uint8_t command,
    uint8_t option
);

/**
 * Declare whether we support enabling an option on our side (us) and/or
 * the peer (him). Must be set before negotiation; unanswered DO/WILL for
 * unsupported options are refused with WONT/DONT.
 *
 * @return 0 on success or -1 on error
 */
static int telnetty_set_option_support(
    telnetty_context_t* ctx,
    uint8_t option,
    int support_us,
    int support_him
);

/**
 * Ensure an option slot exists (creates with Q_NO/Q_NO if missing).
 */
static telnetty_option_t* telnetty_ensure_option(
    telnetty_context_t* ctx,
    uint8_t option
);

/**
 * Send subnegotiation data
 * 
 * @param ctx TELNET context
 * @param option Option code
 * @param data Subnegotiation data
 * @param length Data length
 * @return 0 on success or -1 on error
 */
static int telnetty_send_subnegotiation(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length
);

/**
 * Register an option command handler (WILL/DO/...). Creates the option
 * slot if needed. Does not by itself mark the option supported — call
 * telnetty_set_option_support() as well.
 *
 * @return 0 on success or -1 on error
 */
static int telnetty_register_option(
    telnetty_context_t* ctx,
    uint8_t option,
    telnetty_option_handler_t handler,
    void* user_data
);

/**
 * Register a subnegotiation handler for an option (payload excludes the
 * option byte).
 */
static int telnetty_register_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    telnetty_sb_handler_t handler,
    void* user_data
);

/**
 * Get output buffer for sending
 * 
 * @param ctx TELNET context
 * @return Output buffer or NULL on error
 */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_get_output_buffer(telnetty_context_t* ctx);

/**
 * Flush output buffer
 * 
 * @param ctx TELNET context
 * @return 0 on success or -1 on error
 */
static TELNETTY_UNUSED int telnetty_flush(telnetty_context_t* ctx);

/** Install or clear the outbound byte filter. NULL restores raw writes. */
static TELNETTY_UNUSED void telnetty_set_out_hook(
    telnetty_context_t* ctx,
    int (*hook)(telnetty_context_t* ctx, const uint8_t* data, size_t len)
);

/** Install or clear the inbound byte filter. When set, callers use
 *  telnetty_process_inbound() so the peer's raw bytes are handed to `hook`
 *  first (e.g. an inflate step for MCCP). Hooks call telnetty_process()
 *  themselves on the decoded output. NULL restores the pass-through. */
static TELNETTY_UNUSED void telnetty_set_input_hook(
    telnetty_context_t* ctx,
    int (*hook)(telnetty_context_t* ctx, const uint8_t* data, size_t len),
    void* user_data
);

/** Route peer bytes through the inbound hook when set, else straight into
 *  telnetty_process(). Return matches telnetty_process(): bytes accepted or -1. */
static TELNETTY_UNUSED int telnetty_process_inbound(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
);

/** Install a generic RFC 1143 responder: allow[] options get support_us +
 *  support_him = 1, deny[] options get both = 0, everything else stays at
 *  its current setting. Pass NULL / 0 to skip an axis. Removes the
 *  30-line boilerplate switch from every example server/client. */
static TELNETTY_UNUSED int telnetty_default_option_policy(
    telnetty_context_t* ctx,
    const uint8_t* allow, size_t n_allow,
    const uint8_t* deny,  size_t n_deny
);

/**
 * Get context statistics
 * 
 * @param ctx TELNET context
 * @param stats Output statistics structure
 * @return 0 on success or -1 on error
 */
static int telnetty_get_stats(
    telnetty_context_t* ctx,
    telnetty_stats_t* stats
);

/* ============================================================================
 * Inline Helper Functions
 * ============================================================================ */

/**
 * Check if we're in an option negotiation state
 */
static inline TELNETTY_UNUSED bool telnetty_is_negotiating(telnetty_context_t* ctx, uint8_t option) {
    telnetty_option_t* opt = ctx->options[option];
    return opt && (opt->us == TELNETTY_Q_WANTYES || opt->us == TELNETTY_Q_WANTNO ||
                   opt->him == TELNETTY_Q_WANTYES || opt->him == TELNETTY_Q_WANTNO);
}

/**
 * Check if an option is enabled
 */
static inline TELNETTY_UNUSED bool telnetty_is_option_enabled(telnetty_context_t* ctx, uint8_t option) {
    telnetty_option_t* opt = ctx->options[option];
    return opt && (opt->us == TELNETTY_Q_YES || opt->him == TELNETTY_Q_YES);
}

/**
 * Get the current state of an option
 */
static inline TELNETTY_UNUSED uint8_t telnetty_get_option_state(telnetty_context_t* ctx, uint8_t option, bool local) {
    telnetty_option_t* opt = ctx->options[option];
    if (!opt) return TELNETTY_Q_NO;
    return local ? opt->us : opt->him;
}

/* Extended-event dispatch hook installed by telnetty_events.h. NULL when
 * the events implementation isn't compiled in. */
extern void (*telnetty__extended_dispatch_hook)(
    telnetty_context_t*, telnetty_event_type_t, const telnetty_event_data_union_t*);

/**
 * Fire an event to the callback. Also runs any registered extended handlers
 * when the telnetty_events.h implementation is compiled in.
 */
static inline void telnetty_fire_event(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data
) {
    if (ctx->event_cb) {
        ctx->event_cb(ctx, event, data, ctx->user_data);
    }
    if (telnetty__extended_dispatch_hook && ctx->event_handlers) {
        telnetty__extended_dispatch_hook(ctx, event, data);
    }
}

/** Return the last error surfaced through EVENT_ERROR. Zero if none. */
static inline TELNETTY_UNUSED telnetty_error_t telnetty_last_error(const telnetty_context_t* ctx) {
    return ctx ? ctx->last_error : TELNETTY_ERR_INVAL;
}

/**
 * Install a structured log sink. `cb` receives (level, category, message).
 * `level` sets the *minimum* severity that fires the callback; a NULL `cb`
 * clears the sink (messages fall silent — no stderr fallback). Category is
 * a short static tag such as "core", "options", "mccp".
 */
static inline TELNETTY_UNUSED void telnetty_set_log_callback(
    telnetty_context_t* ctx,
    telnetty_log_level_t level,
    telnetty_log_callback_t cb,
    void* user_data
) {
    if (!ctx) return;
    ctx->log_cb = cb;
    ctx->log_user_data = user_data;
    ctx->log_threshold = level;
}

/** Emit a formatted log line. Silently discards when no sink is set or the
 *  level falls below the threshold — the format string is only rendered when
 *  the sink will actually receive it. */
static TELNETTY_UNUSED void telnetty_log(
    telnetty_context_t* ctx,
    telnetty_log_level_t level,
    const char* category,
    const char* fmt,
    ...
);

/**
 * Set the negotiation timeout in milliseconds. Zero (the default) disables
 * timeouts entirely. Options currently mid-negotiation stay uncapped until
 * they next enter a WANT* state after a fresh send_option — the timeout
 * applies to future initiations, not retroactively.
 */
static inline TELNETTY_UNUSED void telnetty_set_negotiation_timeout(
    telnetty_context_t* ctx,
    uint32_t ms
) {
    if (ctx) ctx->negotiation_timeout_ms = ms;
}

/**
 * Advance the context's monotonic clock. Callers drive this from their
 * event loop (libuv timer, poll deadline, whatever). Options whose
 * deadline_ms is non-zero and has expired fire EVENT_ERROR with
 * TELNETTY_ERR_TIMEOUT and are cleared back to Q_NO on the timed-out side
 * so the caller can retry cleanly. Also refills rate-limit buckets.
 */
static TELNETTY_UNUSED void telnetty_tick(
    telnetty_context_t* ctx,
    uint64_t now_ms
);

/**
 * Configure inbound rate limits. `bytes_per_sec` caps total bytes fed
 * through telnetty_process(); `iac_per_sec` caps IAC-prefixed frames
 * (commands + option negotiations + SB). Zero disables that bucket.
 * Enforcement requires the caller to be driving telnetty_tick(); an
 * unticked context skips enforcement entirely. Trips fire EVENT_ERROR
 * with TELNETTY_ERR_RATE_LIMIT and the offending bytes/frame are dropped.
 */
static inline TELNETTY_UNUSED void telnetty_set_rate_limit(
    telnetty_context_t* ctx,
    uint32_t bytes_per_sec,
    uint32_t iac_per_sec
) {
    if (!ctx) return;
    ctx->rate_bytes_per_sec = bytes_per_sec;
    ctx->rate_iac_per_sec   = iac_per_sec;
    /* Initial bucket = one second's worth so the first burst isn't a trip. */
    ctx->bytes_tokens = (int64_t)bytes_per_sec;
    ctx->iac_tokens   = (int64_t)iac_per_sec;
    ctx->rate_refill_ms = ctx->now_ms;
}

/** Internal helper: record and dispatch an EVENT_ERROR. `msg` may be NULL. */
static inline void telnetty_report_error(
    telnetty_context_t* ctx,
    telnetty_error_t code,
    const char* msg
) {
    if (!ctx) return;
    ctx->last_error = code;
    ctx->stats.errors++;
    if (ctx->log_cb && ctx->log_threshold <= TELNETTY_LOG_ERROR) {
        ctx->log_cb(ctx, TELNETTY_LOG_ERROR, "error", msg ? msg : "",
                    ctx->log_user_data);
    }
    if (ctx->config.flags & TELNETTY_FLAG_QUIET) return;
    telnetty_event_error_t e = { (int)code, msg };
    telnetty_event_data_union_t u = { .error = e };
    telnetty_fire_event(ctx, TELNETTY_EVENT_ERROR, &u);
}

/* ============================================================================
 * Implementation
 * ============================================================================ */

/* Include the implementation if requested */
#ifdef TELNETTY_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

static TELNETTY_UNUSED void telnetty_log(
    telnetty_context_t* ctx,
    telnetty_log_level_t level,
    const char* category,
    const char* fmt,
    ...
) {
    if (!ctx || !ctx->log_cb || level < ctx->log_threshold || !fmt) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    ctx->log_cb(ctx, level, category ? category : "", buf, ctx->log_user_data);
}

static inline void telnetty__rate_refill(telnetty_context_t* ctx);

static TELNETTY_UNUSED void telnetty_tick(telnetty_context_t* ctx, uint64_t now_ms) {
    if (!ctx) return;
    ctx->now_ms = now_ms;
    telnetty__rate_refill(ctx);
    for (int i = 0; i < 256; i++) {
        telnetty_option_t* opt = ctx->options[i];
        if (!opt || !opt->deadline_ms) continue;
        if (now_ms < opt->deadline_ms) continue;

        /* Timed out. Drop back to Q_NO on any pending side so the caller
         * can re-initiate cleanly. */
        uint8_t us  = opt->us, him = opt->him;
        int us_pending  = (us  == TELNETTY_Q_WANTYES || us  == TELNETTY_Q_WANTNO ||
                           us  == TELNETTY_Q_WANTYES_OP || us  == TELNETTY_Q_WANTNO_OP);
        int him_pending = (him == TELNETTY_Q_WANTYES || him == TELNETTY_Q_WANTNO ||
                           him == TELNETTY_Q_WANTYES_OP || him == TELNETTY_Q_WANTNO_OP);
        if (us_pending)  opt->us  = TELNETTY_Q_NO;
        if (him_pending) opt->him = TELNETTY_Q_NO;
        opt->deadline_ms = 0;

        telnetty_report_error(ctx, TELNETTY_ERR_TIMEOUT,
                              "option negotiation timed out");
    }
}

/* Memory allocation wrappers */
#ifndef TELNETTY_MALLOC
#define TELNETTY_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_FREE
#define TELNETTY_FREE(ptr) free(ptr)
#endif

#ifndef TELNETTY_REALLOC
#define TELNETTY_REALLOC(ptr, size) realloc(ptr, size)
#endif

/* Forward declarations for internal functions */
static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_create(size_t initial_size);
static TELNETTY_UNUSED void telnetty_buffer_destroy(telnetty_buffer_t* buffer);
static TELNETTY_UNUSED int telnetty_buffer_append(telnetty_buffer_t* buffer, const uint8_t* data, size_t length);
static TELNETTY_UNUSED int telnetty_buffer_reserve(telnetty_buffer_t* buffer, size_t size);
static TELNETTY_UNUSED void telnetty_buffer_reset(telnetty_buffer_t* buffer);
static TELNETTY_UNUSED telnetty_option_t* telnetty_option_create(uint8_t option);
static TELNETTY_UNUSED void telnetty_option_destroy(telnetty_option_t* opt);

/* ============================================================================
 * Buffer Management Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_buffer_create(size_t initial_size) {
    telnetty_buffer_t* buffer = (telnetty_buffer_t*)TELNETTY_MALLOC(sizeof(telnetty_buffer_t));
    if (!buffer) return NULL;
    
    if (initial_size == 0) {
        initial_size = TELNETTY_INITIAL_BUFFER_SIZE;
    }
    
    buffer->data = (uint8_t*)TELNETTY_MALLOC(initial_size);
    if (!buffer->data) {
        TELNETTY_FREE(buffer);
        return NULL;
    }
    
    buffer->size = initial_size;
    buffer->length = 0;
    buffer->offset = 0;
    buffer->next = NULL;
    buffer->flags = TELNETTY_BUFFER_FLAG_DYNAMIC;
    
    return buffer;
}

static TELNETTY_UNUSED void telnetty_buffer_destroy(telnetty_buffer_t* buffer) {
    if (!buffer) return;
    
    if (buffer->flags & TELNETTY_BUFFER_FLAG_DYNAMIC && buffer->data) {
        TELNETTY_FREE(buffer->data);
    }
    
    if (buffer->flags & TELNETTY_BUFFER_FLAG_CHAINED && buffer->next) {
        telnetty_buffer_destroy(buffer->next);
    }
    
    TELNETTY_FREE(buffer);
}

static TELNETTY_UNUSED int telnetty_buffer_append(telnetty_buffer_t* buffer, const uint8_t* data, size_t length) {
    if (!buffer || !data || length == 0) return 0;

    /* Keep both the size arithmetic and legacy int return value bounded. */
    if (length > TELNETTY_MAX_BUFFER_SIZE ||
        buffer->length > TELNETTY_MAX_BUFFER_SIZE - length ||
        length > (size_t)INT_MAX) return -1;
    size_t required = buffer->length + length;

    /* Check if we need to grow the buffer */
    if (required > buffer->size) {
        size_t new_size = buffer->size;
        if (new_size == 0) new_size = TELNETTY_INITIAL_BUFFER_SIZE;
        while (new_size < required) {
            if (new_size > TELNETTY_MAX_BUFFER_SIZE / 2) {
                new_size = TELNETTY_MAX_BUFFER_SIZE;
                break;
            }
            new_size *= 2;
        }
        
        /* Cap at maximum buffer size */
        if (new_size > TELNETTY_MAX_BUFFER_SIZE) {
            new_size = TELNETTY_MAX_BUFFER_SIZE;
        }
        
        if (new_size < required) {
            return -1; /* Would exceed maximum size */
        }
        
        uint8_t* new_data = (uint8_t*)TELNETTY_REALLOC(buffer->data, new_size);
        if (!new_data) return -1;
        
        buffer->data = new_data;
        buffer->size = new_size;
    }
    
    /* Append the data */
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    
    return (int)length;
}

static TELNETTY_UNUSED int telnetty_buffer_reserve(telnetty_buffer_t* buffer, size_t size) {
    if (!buffer) return -1;
    
    if (size <= buffer->size) return 0;
    
    uint8_t* new_data = (uint8_t*)TELNETTY_REALLOC(buffer->data, size);
    if (!new_data) return -1;
    
    buffer->data = new_data;
    buffer->size = size;
    
    return 0;
}

static TELNETTY_UNUSED void telnetty_buffer_reset(telnetty_buffer_t* buffer) {
    if (!buffer) return;
    buffer->length = 0;
    buffer->offset = 0;
}

/* ============================================================================
 * Option Management Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_option_t* telnetty_option_create(uint8_t option) {
    telnetty_option_t* opt = (telnetty_option_t*)TELNETTY_MALLOC(sizeof(telnetty_option_t));
    if (!opt) return NULL;
    
    memset(opt, 0, sizeof(*opt));
    opt->option = option;
    opt->us = TELNETTY_Q_NO;
    opt->him = TELNETTY_Q_NO;
    /* support_us/him default 0 — refuse until set or initiated by us */
    
    return opt;
}

/* Ensure option slot exists on the context. */
static telnetty_option_t* telnetty_ensure_option(
    telnetty_context_t* ctx,
    uint8_t option
) {
    if (!ctx) return NULL;
    if (!ctx->options[option]) {
        ctx->options[option] = telnetty_option_create(option);
    }
    return ctx->options[option];
}

/* All outbound bytes go through here so MCCP can wrap the stream. */
static int telnetty_out_write(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    if (!ctx || !data || len == 0) return 0;
    if (ctx->out_hook)
        return ctx->out_hook(ctx, data, len);
    if (telnetty_buffer_append(ctx->out_buffer, data, len) < 0)
        return -1;
    return (int)len;
}

static TELNETTY_UNUSED void telnetty_set_out_hook(
    telnetty_context_t* ctx,
    int (*hook)(telnetty_context_t* ctx, const uint8_t* data, size_t len)
) {
    if (ctx) ctx->out_hook = hook;
}

static TELNETTY_UNUSED void telnetty_set_input_hook(
    telnetty_context_t* ctx,
    int (*hook)(telnetty_context_t* ctx, const uint8_t* data, size_t len),
    void* user_data
) {
    if (!ctx) return;
    ctx->in_hook = hook;
    ctx->in_hook_user_data = user_data;
}

/* Forward decl for use in process_inbound. */
static int telnetty_process(telnetty_context_t* ctx,
                            const uint8_t* TELNETTY_RESTRICT data,
                            size_t length);

static TELNETTY_UNUSED int telnetty_process_inbound(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
) {
    if (!ctx) return -1;
    if (ctx->in_hook)
        return ctx->in_hook(ctx, data, length);
    return telnetty_process(ctx, data, length);
}

static TELNETTY_UNUSED int telnetty_default_option_policy(
    telnetty_context_t* ctx,
    const uint8_t* allow, size_t n_allow,
    const uint8_t* deny,  size_t n_deny
) {
    if (!ctx) return -1;
    if (allow) {
        for (size_t i = 0; i < n_allow; i++)
            telnetty_set_option_support(ctx, allow[i], 1, 1);
    }
    if (deny) {
        for (size_t i = 0; i < n_deny; i++)
            telnetty_set_option_support(ctx, deny[i], 0, 0);
    }
    return 0;
}

/* Raw 3-byte IAC command (bypasses Q-method — for internal use only). */
static void telnetty_send_negotiate_raw(
    telnetty_context_t* ctx,
    uint8_t command,
    uint8_t option
) {
    uint8_t sequence[3] = { TELNETTY_IAC, command, option };
    if (telnetty_out_write(ctx, sequence, 3) < 0)
        return;
    ctx->stats.bytes_sent += 3;
    ctx->stats.commands_sent++;
}

static int telnetty_set_option_support(
    telnetty_context_t* ctx,
    uint8_t option,
    int support_us,
    int support_him
) {
    telnetty_option_t* opt = telnetty_ensure_option(ctx, option);
    if (!opt) return -1;
    opt->support_us = support_us ? 1 : 0;
    opt->support_him = support_him ? 1 : 0;
    return 0;
}

static TELNETTY_UNUSED void telnetty_option_destroy(telnetty_option_t* opt) {
    if (!opt) return;
    
    if (opt->sb_data) {
        telnetty_buffer_destroy(opt->sb_data);
    }
    if (opt->user_data && opt->user_data_free) {
        opt->user_data_free(opt->user_data);
        opt->user_data = NULL;
    }
    
    TELNETTY_FREE(opt);
}

/** Attach owned user_data with a freer invoked from option/context destroy. */
static TELNETTY_UNUSED void telnetty_option_set_user_data(
    telnetty_context_t* ctx,
    uint8_t option,
    void* user_data,
    void (*freer)(void*)
) {
    telnetty_option_t* opt = telnetty_ensure_option(ctx, option);
    if (!opt) return;
    if (opt->user_data && opt->user_data_free)
        opt->user_data_free(opt->user_data);
    opt->user_data = user_data;
    opt->user_data_free = freer;
}

/* ============================================================================
 * Core API Implementation
 * ============================================================================ */

static telnetty_context_t* telnetty_create(
    telnetty_event_callback_t event_cb,
    void* user_data
) {
    telnetty_context_t* ctx = (telnetty_context_t*)TELNETTY_MALLOC(sizeof(telnetty_context_t));
    if (!ctx) return NULL;
    
    /* Initialize basic fields */
    memset(ctx, 0, sizeof(telnetty_context_t));
    
    ctx->event_cb = event_cb;
    ctx->user_data = user_data;
    ctx->state = TELNETTY_STATE_DATA;
    
    /* Initialize buffers */
    ctx->in_buffer = telnetty_buffer_create(TELNETTY_INITIAL_BUFFER_SIZE);
    ctx->out_buffer = telnetty_buffer_create(TELNETTY_INITIAL_BUFFER_SIZE);
    ctx->sb_buffer = telnetty_buffer_create(TELNETTY_SMALL_BUFFER_SIZE);
    
    if (!ctx->in_buffer || !ctx->out_buffer || !ctx->sb_buffer) {
        telnetty_destroy(ctx);
        return NULL;
    }
    
    /* Initialize configuration */
    ctx->config.max_buffer_size = TELNETTY_MAX_BUFFER_SIZE;
    ctx->config.initial_buffer_size = TELNETTY_INITIAL_BUFFER_SIZE;
    ctx->config.timeout_ms = 0; /* No timeout by default */
    
    TELNETTY_DEBUG("Created new TELNET context: %p", (void*)ctx);
    
    return ctx;
}

/* Optional hooks populated by later headers (events, color) when their
 * IMPLEMENTATION macro is defined in the same TU. NULL means the subsystem
 * is not compiled in and the destroy path skips it. */
static void (*telnetty__event_handlers_free_hook)(void*) = NULL;

/* Extended-event dispatcher. Called from telnetty_fire_event once the
 * events implementation installs it. */
void (*telnetty__extended_dispatch_hook)(
    telnetty_context_t*, telnetty_event_type_t, const telnetty_event_data_union_t*) = NULL;

/* MCP 2.1 line observer. telnetty_mcp.h installs this on enable so decoded
 * data bytes can be assembled into #$# lines. NULL when that implementation
 * is not compiled into this translation unit. */
void (*telnetty__mcp_data_hook)(
    telnetty_context_t*, const uint8_t*, size_t) = NULL;

/* Deliver a run of in-band bytes. The MCP hook, if installed, sees the
 * bytes before the DATA event and must copy anything it wants to keep:
 * `p` is only valid for the duration of the call. */
static void telnetty__emit_data(telnetty_context_t* ctx, const uint8_t* p, size_t n) {
    if (!ctx || !p || n == 0) return;
    if (telnetty__mcp_data_hook)
        telnetty__mcp_data_hook(ctx, p, n);
    telnetty_event_data_t event_data = { p, n };
    telnetty_event_data_union_t event = { .data = event_data };
    telnetty_fire_event(ctx, TELNETTY_EVENT_DATA, &event);
}

static TELNETTY_UNUSED void telnetty_destroy(telnetty_context_t* ctx) {
    if (!ctx) return;

    TELNETTY_DEBUG("Destroying TELNET context: %p", (void*)ctx);

    /* Free buffers */
    if (ctx->in_buffer) telnetty_buffer_destroy(ctx->in_buffer);
    if (ctx->out_buffer) telnetty_buffer_destroy(ctx->out_buffer);
    if (ctx->sb_buffer) telnetty_buffer_destroy(ctx->sb_buffer);

    /* Free option handlers */
    for (int i = 0; i < 256; i++) {
        if (ctx->options[i]) {
            telnetty_option_destroy(ctx->options[i]);
        }
    }

    /* Free the extended-event handler chain if the events implementation
     * is present in this TU. */
    if (ctx->event_handlers && telnetty__event_handlers_free_hook) {
        telnetty__event_handlers_free_hook(ctx->event_handlers);
        ctx->event_handlers = NULL;
    }

    /* Release attached color state. */
    if (ctx->color_state && ctx->color_state_free) {
        ctx->color_state_free(ctx->color_state);
        ctx->color_state = NULL;
    }

    TELNETTY_FREE(ctx);
}

/* Forward declaration for helper function */
static TELNETTY_UNUSED int telnetty_process_byte(telnetty_context_t* ctx, uint8_t byte);

/* Refill token buckets based on elapsed ms since last refill. Caps at one
 * full second's worth so an idle connection can't stockpile burst credit. */
static inline void telnetty__rate_refill(telnetty_context_t* ctx) {
    if (!ctx->now_ms) return;
    if (!ctx->rate_refill_ms) { ctx->rate_refill_ms = ctx->now_ms; return; }
    if (ctx->now_ms <= ctx->rate_refill_ms) return;
    uint64_t elapsed = ctx->now_ms - ctx->rate_refill_ms;
    if (ctx->rate_bytes_per_sec) {
        int64_t add = (int64_t)((elapsed * ctx->rate_bytes_per_sec) / 1000ULL);
        ctx->bytes_tokens += add;
        if (ctx->bytes_tokens > (int64_t)ctx->rate_bytes_per_sec)
            ctx->bytes_tokens = (int64_t)ctx->rate_bytes_per_sec;
    }
    if (ctx->rate_iac_per_sec) {
        int64_t add = (int64_t)((elapsed * ctx->rate_iac_per_sec) / 1000ULL);
        ctx->iac_tokens += add;
        if (ctx->iac_tokens > (int64_t)ctx->rate_iac_per_sec)
            ctx->iac_tokens = (int64_t)ctx->rate_iac_per_sec;
    }
    ctx->rate_refill_ms = ctx->now_ms;
}

static int telnetty_process(
    telnetty_context_t* ctx,
    const uint8_t* TELNETTY_RESTRICT data,
    size_t length
) {
    if (!ctx || !data || length == 0) return 0;

    /* Rate limit: byte bucket. Enforcement only kicks in once the caller
     * has driven telnetty_tick(); an unticked context skips it. */
    if (ctx->now_ms && ctx->rate_bytes_per_sec) {
        telnetty__rate_refill(ctx);
        if (ctx->bytes_tokens < (int64_t)length) {
            telnetty_report_error(ctx, TELNETTY_ERR_RATE_LIMIT,
                                  "inbound byte rate exceeded");
            /* Consume whatever tokens remain and drop the frame. */
            if (ctx->bytes_tokens > 0) ctx->bytes_tokens = 0;
            return 0;
        }
        ctx->bytes_tokens -= (int64_t)length;
    }

    /* Rate limit: IAC bucket — count IACs in this batch up front. */
    if (ctx->now_ms && ctx->rate_iac_per_sec) {
        telnetty__rate_refill(ctx);
        int64_t iac_count = 0;
        for (size_t k = 0; k < length; k++) if (data[k] == TELNETTY_IAC) iac_count++;
        if (iac_count > ctx->iac_tokens) {
            telnetty_report_error(ctx, TELNETTY_ERR_RATE_LIMIT,
                                  "inbound IAC rate exceeded");
            if (ctx->iac_tokens > 0) ctx->iac_tokens = 0;
            return 0;
        }
        ctx->iac_tokens -= iac_count;
    }

    int processed = 0;
    size_t i = 0;

    TELNETTY_DEBUG("Processing %zu bytes of data", length);

    while (i < length) {
        /* DATA fast path: coalesce a run of plain bytes into one event. */
        if (ctx->state == TELNETTY_STATE_DATA && TELNETTY_LIKELY(data[i] != TELNETTY_IAC)) {
            const uint8_t* p = data + i;
            size_t rem = length - i;
            const uint8_t* iac = (const uint8_t*)memchr(p, TELNETTY_IAC, rem);
            size_t run = iac ? (size_t)(iac - p) : rem;
            telnetty__emit_data(ctx, p, run);
            processed += (int)run;
            i += run;
            continue;
        }

        /* SB payload fast path: once the option byte is consumed, drain a
         * run of non-IAC bytes with a single buffer_append. This dodges the
         * per-byte callback + capacity-check cost that dominated MSSP /
         * GMCP frames on Cortex-A57. */
        if (ctx->state == TELNETTY_STATE_SUBNEG && ctx->sb_got_option
            && TELNETTY_LIKELY(data[i] != TELNETTY_IAC)) {
            const uint8_t* p = data + i;
            size_t rem = length - i;
            const uint8_t* iac = (const uint8_t*)memchr(p, TELNETTY_IAC, rem);
            size_t run = iac ? (size_t)(iac - p) : rem;

            size_t cap_left = (ctx->sb_length < TELNETTY_MAX_SB_LENGTH)
                ? (TELNETTY_MAX_SB_LENGTH - ctx->sb_length) : 0;
            size_t take = (run < cap_left) ? run : cap_left;

            if (take > 0) {
                if (telnetty_buffer_append(ctx->sb_buffer, p, take) < 0) {
                    telnetty_report_error(ctx, TELNETTY_ERR_ALLOC,
                                          "SB buffer append failed");
                    return -1;
                }
                ctx->sb_length += take;
            }

            if (take < run) {
                telnetty_report_error(ctx, TELNETTY_ERR_SB_OVERFLOW,
                                      "SB payload exceeds TELNETTY_MAX_SB_LENGTH");
                telnetty_buffer_reset(ctx->sb_buffer);
                ctx->sb_length = 0;
                ctx->current_option = 0;
                ctx->sb_got_option = 0;
                ctx->state = TELNETTY_STATE_DATA;
            }

            processed += (int)run;
            i += run;
            continue;
        }

        int result = telnetty_process_byte(ctx, data[i]);
        if (result < 0) return result;
        processed++;
        i++;
    }

    ctx->stats.bytes_received += processed;
    return processed;
}

/* Helper function to process a single byte */
static TELNETTY_UNUSED int telnetty_process_byte(telnetty_context_t* ctx, uint8_t byte) {
    /* Fast path for normal data when not in IAC sequence */
    if (ctx->state == TELNETTY_STATE_DATA && byte != TELNETTY_IAC) {
        telnetty__emit_data(ctx, &byte, 1);
        return 1;
    }
    
    /* Handle IAC sequences */
    if (TELNETTY_UNLIKELY(byte == TELNETTY_IAC)) {
        if (ctx->state == TELNETTY_STATE_DATA) {
            ctx->state = TELNETTY_STATE_IAC;
            return 1;
        } else if (ctx->state == TELNETTY_STATE_IAC) {
            /* Escaped IAC (255) - treat as data */
            ctx->state = TELNETTY_STATE_DATA;
            uint8_t data_byte = TELNETTY_IAC;
            telnetty__emit_data(ctx, &data_byte, 1);
            return 1;
        }
    }
    
    /* Handle IAC commands */
    if (ctx->state == TELNETTY_STATE_IAC) {
        switch (byte) {
            case TELNETTY_WILL:
            case TELNETTY_WONT:
            case TELNETTY_DO:
            case TELNETTY_DONT:
                ctx->state = TELNETTY_STATE_NEGOTIATION;
                ctx->current_option = 0;
                ctx->iac_state = byte;
                break;
                
            case TELNETTY_SB:
                ctx->state = TELNETTY_STATE_SUBNEG;
                ctx->sb_length = 0;
                ctx->current_option = 0;
                ctx->sb_got_option = 0;
                if (ctx->sb_buffer)
                    telnetty_buffer_reset(ctx->sb_buffer);
                break;
                
            case TELNETTY_IAC: /* Double IAC - escaped */
                ctx->state = TELNETTY_STATE_DATA;
                {
                    uint8_t data_byte = TELNETTY_IAC;
                    telnetty__emit_data(ctx, &data_byte, 1);
                }
                break;
                
            default:
                /* Single-byte command */
                ctx->state = TELNETTY_STATE_DATA;
                {
                    telnetty_event_iac_t event_data = { byte };
                    telnetty_event_data_union_t event = { .iac = event_data };
                    telnetty_fire_event(ctx, TELNETTY_EVENT_IAC, &event);
                }
                break;
        }
        return 1;
    }
    
    /* Handle option negotiation — full RFC 1143 Q-method (libtelnet-compatible).
     * Replies only on state transitions; events fire only when state changes
     * to a confirmed YES/NO relevant to the peer request. */
    if (ctx->state == TELNETTY_STATE_NEGOTIATION) {
        uint8_t option = byte;
        uint8_t command = ctx->iac_state;
        ctx->state = TELNETTY_STATE_DATA;

        telnetty_option_t* opt = telnetty_ensure_option(ctx, option);
        if (!opt) return -1;

        int fire = 0;           /* fire user event? */
        telnetty_event_type_t ev = TELNETTY_EVENT_WILL;
        uint8_t hcmd = command; /* command passed to option handler */

        switch (command) {
        case TELNETTY_WILL: /* peer wants him enabled */
            switch (opt->him) {
            case TELNETTY_Q_NO:
                if (opt->support_him) {
                    opt->him = TELNETTY_Q_YES;
                    telnetty_send_negotiate_raw(ctx, TELNETTY_DO, option);
                    fire = 1; ev = TELNETTY_EVENT_WILL;
                } else {
                    telnetty_send_negotiate_raw(ctx, TELNETTY_DONT, option);
                }
                break;
            case TELNETTY_Q_YES:
                /* already on — ignore (no re-DO) */
                break;
            case TELNETTY_Q_WANTNO:
                opt->him = TELNETTY_Q_NO;
                fire = 1; ev = TELNETTY_EVENT_WONT; hcmd = TELNETTY_WONT;
                break;
            case TELNETTY_Q_WANTNO_OP:
                opt->him = TELNETTY_Q_YES;
                fire = 1; ev = TELNETTY_EVENT_WILL;
                break;
            case TELNETTY_Q_WANTYES:
                opt->him = TELNETTY_Q_YES;
                fire = 1; ev = TELNETTY_EVENT_WILL;
                break;
            case TELNETTY_Q_WANTYES_OP:
                opt->him = TELNETTY_Q_WANTNO;
                telnetty_send_negotiate_raw(ctx, TELNETTY_DONT, option);
                fire = 1; ev = TELNETTY_EVENT_WILL;
                break;
            }
            break;

        case TELNETTY_WONT:
            switch (opt->him) {
            case TELNETTY_Q_YES:
                opt->him = TELNETTY_Q_NO;
                telnetty_send_negotiate_raw(ctx, TELNETTY_DONT, option);
                fire = 1; ev = TELNETTY_EVENT_WONT;
                break;
            case TELNETTY_Q_NO:
                break;
            case TELNETTY_Q_WANTNO:
                opt->him = TELNETTY_Q_NO;
                fire = 1; ev = TELNETTY_EVENT_WONT;
                break;
            case TELNETTY_Q_WANTNO_OP:
                /* Peer confirmed OFF while we're mid-flight requesting the
                 * OPPOSITE (on-then-off toggle). Re-request DO — but do not
                 * fire an EVENT_DO / call the option handler with hcmd=DO,
                 * because the option is *not* yet enabled. The eventual real
                 * WILL from the peer will drive the settled event. */
                opt->him = TELNETTY_Q_WANTYES;
                telnetty_send_negotiate_raw(ctx, TELNETTY_DO, option);
                break;
            case TELNETTY_Q_WANTYES:
            case TELNETTY_Q_WANTYES_OP:
                opt->him = TELNETTY_Q_NO;
                break;
            }
            break;

        case TELNETTY_DO: /* peer wants us enabled */
            switch (opt->us) {
            case TELNETTY_Q_NO:
                if (opt->support_us) {
                    opt->us = TELNETTY_Q_YES;
                    telnetty_send_negotiate_raw(ctx, TELNETTY_WILL, option);
                    fire = 1; ev = TELNETTY_EVENT_DO;
                } else {
                    telnetty_send_negotiate_raw(ctx, TELNETTY_WONT, option);
                }
                break;
            case TELNETTY_Q_YES:
                break;
            case TELNETTY_Q_WANTNO:
                opt->us = TELNETTY_Q_NO;
                fire = 1; ev = TELNETTY_EVENT_DONT; hcmd = TELNETTY_DONT;
                break;
            case TELNETTY_Q_WANTNO_OP:
                opt->us = TELNETTY_Q_YES;
                fire = 1; ev = TELNETTY_EVENT_DO;
                break;
            case TELNETTY_Q_WANTYES:
                opt->us = TELNETTY_Q_YES;
                fire = 1; ev = TELNETTY_EVENT_DO;
                break;
            case TELNETTY_Q_WANTYES_OP:
                opt->us = TELNETTY_Q_WANTNO;
                telnetty_send_negotiate_raw(ctx, TELNETTY_WONT, option);
                fire = 1; ev = TELNETTY_EVENT_DO;
                break;
            }
            break;

        case TELNETTY_DONT:
            switch (opt->us) {
            case TELNETTY_Q_YES:
                opt->us = TELNETTY_Q_NO;
                telnetty_send_negotiate_raw(ctx, TELNETTY_WONT, option);
                fire = 1; ev = TELNETTY_EVENT_DONT;
                break;
            case TELNETTY_Q_NO:
                break;
            case TELNETTY_Q_WANTNO:
                opt->us = TELNETTY_Q_NO;
                fire = 1; ev = TELNETTY_EVENT_WONT; hcmd = TELNETTY_WONT;
                break;
            case TELNETTY_Q_WANTNO_OP:
                /* Symmetric to the peer-side WANTNO_OP branch above: we
                 * requested OFF then flipped to want ON. Re-issue WILL but
                 * don't fire the user-facing settled event yet. */
                opt->us = TELNETTY_Q_WANTYES;
                telnetty_send_negotiate_raw(ctx, TELNETTY_WILL, option);
                break;
            case TELNETTY_Q_WANTYES:
            case TELNETTY_Q_WANTYES_OP:
                opt->us = TELNETTY_Q_NO;
                break;
            }
            break;
        }

        if (fire) {
            telnetty_event_option_t event_data = { option };
            telnetty_event_data_union_t event = { .option = event_data };
            telnetty_fire_event(ctx, ev, &event);
            if (opt->handler)
                opt->handler(ctx, option, hcmd, opt->user_data);
            ctx->stats.options_negotiated++;
        }

        /* Clear the pending deadline whenever both sides have settled to
         * a terminal state (NO or YES). WANT* stays uncleared and can time
         * out on the next tick. */
        {
            uint8_t us = opt->us, him = opt->him;
            int us_pending  = (us  == TELNETTY_Q_WANTYES || us  == TELNETTY_Q_WANTNO ||
                               us  == TELNETTY_Q_WANTYES_OP || us  == TELNETTY_Q_WANTNO_OP);
            int him_pending = (him == TELNETTY_Q_WANTYES || him == TELNETTY_Q_WANTNO ||
                               him == TELNETTY_Q_WANTYES_OP || him == TELNETTY_Q_WANTNO_OP);
            if (!us_pending && !him_pending) opt->deadline_ms = 0;
        }

        return 1;
    }
    
    /* Handle subnegotiation. First data byte is the option code (RFC 855). */
    if (ctx->state == TELNETTY_STATE_SUBNEG) {
        if (byte == TELNETTY_IAC) {
            ctx->state = TELNETTY_STATE_SUBNEG_IAC;
        } else if (!ctx->sb_got_option) {
            /* First SB byte = option; store separately, not in payload. */
            ctx->current_option = byte;
            ctx->sb_got_option = 1;
        } else if (ctx->sb_length < TELNETTY_MAX_SB_LENGTH) {
            if (telnetty_buffer_append(ctx->sb_buffer, &byte, 1) == 1) {
                ctx->sb_length++;
            }
        } else {
            /* Payload exceeds cap — abandon the frame and surface an error
             * so the caller can drop the peer (RFC 855 leaves the recovery
             * policy to the implementation). */
            telnetty_report_error(ctx, TELNETTY_ERR_SB_OVERFLOW,
                                  "SB payload exceeds TELNETTY_MAX_SB_LENGTH");
            telnetty_buffer_reset(ctx->sb_buffer);
            ctx->sb_length = 0;
            ctx->current_option = 0;
            ctx->sb_got_option = 0;
            ctx->state = TELNETTY_STATE_DATA;
        }
        return 1;
    }

    if (ctx->state == TELNETTY_STATE_SUBNEG_IAC) {
        if (byte == TELNETTY_SE) {
            /* End of subnegotiation */
            uint8_t option = ctx->current_option;
            ctx->state = TELNETTY_STATE_DATA;

            /* Fire subnegotiation event (payload excludes option byte).
             * Guard against NULL buffer data when the payload was empty. */
            const uint8_t* sb_data = ctx->sb_buffer && ctx->sb_buffer->data
                ? ctx->sb_buffer->data
                : (const uint8_t*)"";
            telnetty_event_subnegotiation_t event_data = {
                option, sb_data, ctx->sb_length
            };
            telnetty_event_data_union_t event = { .sub = event_data };
            telnetty_fire_event(ctx, TELNETTY_EVENT_SB, &event);

            telnetty_option_t* opt = ctx->options[option];
            if (opt && opt->sb_handler) {
                opt->sb_handler(ctx, option, sb_data, ctx->sb_length,
                                opt->user_data);
            }

            /* Reset subnegotiation buffer */
            telnetty_buffer_reset(ctx->sb_buffer);
            ctx->sb_length = 0;
            ctx->current_option = 0;
            ctx->sb_got_option = 0;
            ctx->stats.subnegotiations++;
        } else if (byte == TELNETTY_IAC) {
            /* Escaped IAC inside SB — append one 0xFF byte. */
            if (ctx->sb_length < TELNETTY_MAX_SB_LENGTH &&
                telnetty_buffer_append(ctx->sb_buffer, &byte, 1) == 1) {
                ctx->sb_length++;
            }
            ctx->state = TELNETTY_STATE_SUBNEG;
        } else {
            /* IAC + non-SE: RFC 855 leaves this undefined. Treat as a
             * protocol error rather than smuggling the stray bytes into
             * the payload, then resume in the SUBNEG state. */
            telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL,
                                  "IAC followed by non-SE / non-IAC inside SB");
            ctx->state = TELNETTY_STATE_SUBNEG;
        }
        return 1;
    }
    
    /* Should not reach here */
    return -1;
}

static int telnetty_send(
    telnetty_context_t* ctx,
    const uint8_t* TELNETTY_RESTRICT data,
    size_t length
) {
    if (!ctx || !data || length == 0) return 0;

    /* Escape any IAC bytes in the data */
    const uint8_t* ptr = data;
    size_t remaining = length;
    int sent = 0;
    
    while (remaining > 0) {
        const uint8_t* iac = (const uint8_t*)memchr(ptr, TELNETTY_IAC, remaining);
        
        if (iac) {
            /* Send data before IAC */
            size_t before_iac = iac - ptr;
            if (before_iac > 0) {
                if (telnetty_out_write(ctx, ptr, before_iac) < 0) {
                    return -1;
                }
                sent += before_iac;
            }
            
            /* Send escaped IAC */
            uint8_t escaped_iac[2] = { TELNETTY_IAC, TELNETTY_IAC };
            if (telnetty_out_write(ctx, escaped_iac, 2) < 0) {
                return -1;
            }
            sent += 2;
            
            ptr = iac + 1;
            remaining -= (before_iac + 1);
        } else {
            /* Send remaining data */
            if (telnetty_out_write(ctx, ptr, remaining) < 0) {
                return -1;
            }
            sent += remaining;
            break;
        }
    }
    
    ctx->stats.bytes_sent += sent;
    
    return sent;
}

static TELNETTY_UNUSED int telnetty_send_command(telnetty_context_t* ctx, uint8_t command) {
    if (!ctx) return -1;
    
    uint8_t sequence[2] = { TELNETTY_IAC, command };
    
    if (telnetty_out_write(ctx, sequence, 2) < 0) {
        return -1;
    }
    
    ctx->stats.bytes_sent += 2;
    
    return 0;
}

/* RFC 1143 initiator — only emit IAC when state changes. */
static int telnetty_send_option(
    telnetty_context_t* ctx,
    uint8_t command,
    uint8_t option
) {
    if (!ctx) return -1;

    /* PROXY mode: pass-through */
    if (ctx->config.flags & TELNETTY_FLAG_PROXY) {
        telnetty_send_negotiate_raw(ctx, command, option);
        return 0;
    }

    telnetty_option_t* opt = telnetty_ensure_option(ctx, option);
    if (!opt) return -1;

    switch (command) {
    case TELNETTY_WILL:
        /* Initiating WILL implies we support local enable. */
        opt->support_us = 1;
        switch (opt->us) {
        case TELNETTY_Q_NO:
            opt->us = TELNETTY_Q_WANTYES;
            telnetty_send_negotiate_raw(ctx, TELNETTY_WILL, option);
            break;
        case TELNETTY_Q_YES:
            /* already enabled */
            break;
        case TELNETTY_Q_WANTNO:
            opt->us = TELNETTY_Q_WANTNO_OP;
            break;
        case TELNETTY_Q_WANTYES:
            break;
        case TELNETTY_Q_WANTYES_OP:
            opt->us = TELNETTY_Q_WANTYES;
            break;
        case TELNETTY_Q_WANTNO_OP:
            break;
        }
        break;

    case TELNETTY_WONT:
        switch (opt->us) {
        case TELNETTY_Q_YES:
            opt->us = TELNETTY_Q_WANTNO;
            telnetty_send_negotiate_raw(ctx, TELNETTY_WONT, option);
            break;
        case TELNETTY_Q_NO:
            break;
        case TELNETTY_Q_WANTYES:
            opt->us = TELNETTY_Q_WANTYES_OP;
            break;
        case TELNETTY_Q_WANTNO:
            break;
        case TELNETTY_Q_WANTNO_OP:
            opt->us = TELNETTY_Q_WANTNO;
            break;
        case TELNETTY_Q_WANTYES_OP:
            break;
        }
        break;

    case TELNETTY_DO:
        opt->support_him = 1;
        switch (opt->him) {
        case TELNETTY_Q_NO:
            opt->him = TELNETTY_Q_WANTYES;
            telnetty_send_negotiate_raw(ctx, TELNETTY_DO, option);
            break;
        case TELNETTY_Q_YES:
            break;
        case TELNETTY_Q_WANTNO:
            opt->him = TELNETTY_Q_WANTNO_OP;
            break;
        case TELNETTY_Q_WANTYES:
            break;
        case TELNETTY_Q_WANTYES_OP:
            opt->him = TELNETTY_Q_WANTYES;
            break;
        case TELNETTY_Q_WANTNO_OP:
            break;
        }
        break;

    case TELNETTY_DONT:
        switch (opt->him) {
        case TELNETTY_Q_YES:
            opt->him = TELNETTY_Q_WANTNO;
            telnetty_send_negotiate_raw(ctx, TELNETTY_DONT, option);
            break;
        case TELNETTY_Q_NO:
            break;
        case TELNETTY_Q_WANTYES:
            opt->him = TELNETTY_Q_WANTYES_OP;
            break;
        case TELNETTY_Q_WANTNO:
            break;
        case TELNETTY_Q_WANTNO_OP:
            opt->him = TELNETTY_Q_WANTNO;
            break;
        case TELNETTY_Q_WANTYES_OP:
            break;
        }
        break;

    default:
        telnetty_send_negotiate_raw(ctx, command, option);
        break;
    }

    /* Stamp a deadline whenever either side is now mid-negotiation (WANT*)
     * and no deadline is set. Idempotent: an already-pending option keeps
     * its earlier deadline until it settles. */
    if (ctx->negotiation_timeout_ms && !opt->deadline_ms) {
        uint8_t us = opt->us, him = opt->him;
        int us_pending  = (us  == TELNETTY_Q_WANTYES || us  == TELNETTY_Q_WANTNO ||
                           us  == TELNETTY_Q_WANTYES_OP || us  == TELNETTY_Q_WANTNO_OP);
        int him_pending = (him == TELNETTY_Q_WANTYES || him == TELNETTY_Q_WANTNO ||
                           him == TELNETTY_Q_WANTYES_OP || him == TELNETTY_Q_WANTNO_OP);
        if (us_pending || him_pending) {
            opt->deadline_ms = ctx->now_ms + (uint64_t)ctx->negotiation_timeout_ms;
        }
    }

    return 0;
}

static int telnetty_send_subnegotiation(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length
) {
    if (!ctx) return -1;
    
    /* Start subnegotiation */
    uint8_t start[3] = { TELNETTY_IAC, TELNETTY_SB, option };
    if (telnetty_out_write(ctx, start, 3) < 0) {
        return -1;
    }
    
    /* Send data with IAC escaping */
    if (telnetty_send(ctx, data, length) < 0) {
        return -1;
    }
    
    /* End subnegotiation */
    uint8_t end[2] = { TELNETTY_IAC, TELNETTY_SE };
    if (telnetty_out_write(ctx, end, 2) < 0) {
        return -1;
    }
    
    ctx->stats.bytes_sent += 5; /* IAC+SB+opt + IAC+SE */
    
    return 0;
}

static int telnetty_register_option(
    telnetty_context_t* ctx,
    uint8_t option,
    telnetty_option_handler_t handler,
    void* user_data
) {
    if (!ctx) return -1;

    telnetty_option_t* opt = telnetty_ensure_option(ctx, option);
    if (!opt) return -1;

    opt->handler = handler;
    /* Never clobber an owned user_data (installed via
     * telnetty_option_set_user_data with a freer) — that would silently leak
     * the previous payload and drop its destructor. Callers that want to
     * replace ownership must go through telnetty_option_set_user_data. */
    if (user_data && !opt->user_data)
        opt->user_data = user_data;
    return 0;
}

static int telnetty_register_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    telnetty_sb_handler_t handler,
    void* user_data
) {
    if (!ctx) return -1;

    telnetty_option_t* opt = telnetty_ensure_option(ctx, option);
    if (!opt) return -1;

    opt->sb_handler = handler;
    if (user_data && !opt->user_data)
        opt->user_data = user_data;
    return 0;
}

static TELNETTY_UNUSED telnetty_buffer_t* telnetty_get_output_buffer(telnetty_context_t* ctx) {
    if (!ctx) return NULL;
    return ctx->out_buffer;
}

static TELNETTY_UNUSED int telnetty_flush(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Reset output buffer for next batch */
    telnetty_buffer_reset(ctx->out_buffer);
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_get_stats(telnetty_context_t* ctx, telnetty_stats_t* stats) {
    if (!ctx || !stats) return -1;
    
    memcpy(stats, &ctx->stats, sizeof(telnetty_stats_t));
    
    return 0;
}

#endif /* TELNETTY_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_CORE_H */
