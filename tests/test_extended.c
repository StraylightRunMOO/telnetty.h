/**
 * @file test_extended.c
 * @brief Extended-surface regression tests.
 *
 * Covers the 1.1 bug fixes, additive API (log/timeout/rate-limit),
 * MTTS three-cycle handshake, color state, MCP 2.1 parser, extended event
 * system, RFC option round-trips (BINARY/STATUS/TSPEED/LFLOW/LINEMODE),
 * and split-input torture. Every new bug fix or protocol addition should
 * land a case here.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_EVENTS_IMPLEMENTATION
#define TELNETTY_BUFFER_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_MUD_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION
#define TELNETTY_COLOR_IMPLEMENTATION
#define TELNETTY_MCP_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_events.h"
#include "telnetty_buffer.h"
#include "telnetty_options.h"
#include "telnetty_mud.h"
#include "telnetty_compression.h"
#include "telnetty_color.h"
#include "telnetty_mcp.h"

static int passed = 0;
static int failed = 0;

#define OK(cond, msg) do { \
    if (cond) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

/* ----- Q-method WANTNO_OP transient must NOT fire settled events ----- */

static int wantno_op_event_count[16];

static void wantno_op_cb(telnetty_context_t* ctx, telnetty_event_type_t event,
                        const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx; (void)d; (void)ud;
    if ((int)event < 16) wantno_op_event_count[event]++;
}

static void test_qmethod_wantno_op_no_spurious_event(void) {
    memset(wantno_op_event_count, 0, sizeof(wantno_op_event_count));
    telnetty_context_t* ctx = telnetty_create(wantno_op_cb, NULL);
    OK(ctx != NULL, "ctx create");

    /* Drive HIM into Q_WANTNO_OP: peer WILL -> we accept (support_him=1) so
     * him=Q_YES, we send DONT (him=Q_WANTNO), then user asks DO (him=Q_WANTNO_OP).
     * Peer then WONTs — under the fixed code this should re-request DO but
     * NOT fire EVENT_DO or EVENT_WILL until the peer confirms with WILL. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ECHO, 1, 1);

    /* Peer WILL -> we accept -> him=Q_YES */
    const uint8_t peer_will[] = { TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_ECHO };
    telnetty_process(ctx, peer_will, sizeof(peer_will));
    OK(wantno_op_event_count[TELNETTY_EVENT_WILL] == 1, "initial WILL fires WILL event");

    /* Our WONT: him YES -> WANTNO, sends DONT on wire */
    telnetty_send_option(ctx, TELNETTY_DONT, TELNETTY_TELOPT_ECHO);
    /* Our follow-up DO before peer replies: him WANTNO -> WANTNO_OP */
    telnetty_send_option(ctx, TELNETTY_DO, TELNETTY_TELOPT_ECHO);

    int will_before = wantno_op_event_count[TELNETTY_EVENT_WILL];
    int do_before   = wantno_op_event_count[TELNETTY_EVENT_DO];

    /* Peer confirms the earlier DONT with WONT. Fixed code: transitions to
     * Q_WANTYES, re-requests DO on wire — but MUST NOT fire EVENT_DO. */
    const uint8_t peer_wont[] = { TELNETTY_IAC, TELNETTY_WONT, TELNETTY_TELOPT_ECHO };
    telnetty_process(ctx, peer_wont, sizeof(peer_wont));

    OK(wantno_op_event_count[TELNETTY_EVENT_WILL] == will_before,
       "no spurious WILL event during WANTNO_OP re-request");
    OK(wantno_op_event_count[TELNETTY_EVENT_DO] == do_before,
       "no spurious DO event during WANTNO_OP re-request");

    telnetty_destroy(ctx);
}

/* ----- SB overflow fires EVENT_ERROR and abandons the frame ----- */

static int overflow_error_fires = 0;
static telnetty_error_t overflow_last = TELNETTY_OK;

static void overflow_cb(telnetty_context_t* ctx, telnetty_event_type_t event,
                       const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx; (void)ud;
    if (event == TELNETTY_EVENT_ERROR) {
        overflow_error_fires++;
        overflow_last = (telnetty_error_t)d->error.code;
    }
}

static void test_sb_overflow_reports_error(void) {
    overflow_error_fires = 0;
    overflow_last = TELNETTY_OK;
    telnetty_context_t* ctx = telnetty_create(overflow_cb, NULL);
    OK(ctx != NULL, "ctx create");

    /* Feed IAC SB <opt> then a payload larger than TELNETTY_MAX_SB_LENGTH.
     * We rely on the bulk-append fast path — allocate a 128 KiB payload of
     * a benign byte so no embedded IAC breaks the run. */
    size_t big = TELNETTY_MAX_SB_LENGTH + 4096;
    uint8_t* pad = (uint8_t*)malloc(big);
    OK(pad != NULL, "malloc payload");
    memset(pad, 'A', big);

    const uint8_t head[] = { TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_STATUS };
    telnetty_process(ctx, head, sizeof(head));
    telnetty_process(ctx, pad, big);

    OK(overflow_error_fires > 0, "SB overflow fires EVENT_ERROR");
    OK(overflow_last == TELNETTY_ERR_SB_OVERFLOW, "last_error is SB_OVERFLOW");
    OK(telnetty_last_error(ctx) == TELNETTY_ERR_SB_OVERFLOW, "last_error accessor");

    free(pad);
    telnetty_destroy(ctx);
}

/* ----- Chain split within a buffer preserves total length ----- */

static void test_chain_split_within_buffer(void) {
    telnetty_buffer_chain_t* chain = telnetty_buffer_chain_create();
    OK(chain != NULL, "chain create");

    telnetty_buffer_t* b = telnetty_buffer_create(16);
    OK(b != NULL, "buffer create");
    telnetty_buffer_append(b, (const uint8_t*)"0123456789", 10);
    telnetty_buffer_chain_append(chain, b);
    OK(chain->total_length == 10, "total_length before split");

    telnetty_buffer_chain_t* right = telnetty_buffer_chain_split(chain, 3);
    OK(right != NULL, "split within buffer");
    OK(chain->total_length == 3, "left length after split");
    OK(right->total_length == 7, "right length after split");

    telnetty_buffer_chain_destroy(chain);
    telnetty_buffer_chain_destroy(right);
}

/* ----- buffer_find memchr fast-path ----- */

static void test_buffer_find_pattern(void) {
    telnetty_buffer_t* b = telnetty_buffer_create(32);
    OK(b != NULL, "buffer create");
    const char* haystack = "xxAB xxABC xxABCDE";
    telnetty_buffer_append(b, (const uint8_t*)haystack, strlen(haystack));

    /* "xxAB xxABC xxABCDE" — "ABC" first occurs at offset 7. */
    int p = telnetty_buffer_find(b, (const uint8_t*)"ABC", 3, 0);
    OK(p == 7, "find 'ABC' at offset 7");

    p = telnetty_buffer_find(b, (const uint8_t*)"ABCDE", 5, 0);
    OK(p == 13, "find 'ABCDE' at offset 13");

    p = telnetty_buffer_find(b, (const uint8_t*)"ZZ", 2, 0);
    OK(p == -1, "missing pattern returns -1");

    telnetty_buffer_destroy(b);
}

/* ----- Color emitters bound-check on tight buffers ----- */

static void test_color_emitter_bounds(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "ctx create");

    char big[64];
    int n = telnetty_color_string_256(ctx, 196, 21, big, sizeof(big));
    OK(n > 0, "256-color into ample buffer");
    OK(strstr(big, "38;5;196") != NULL, "256-color fg present");
    OK(strstr(big, "48;5;21") != NULL, "256-color bg present");

    /* Buffer too small: expect -1, no OOB write */
    char tight[4];
    memset(tight, 0xAA, sizeof(tight));
    int r = telnetty_color_string_256(ctx, 196, 21, tight, sizeof(tight));
    OK(r < 0, "tight buffer returns -1");

    /* Zero-size buffer: must return -1, must not write */
    r = telnetty_color_string_256(ctx, 196, 21, tight, 0);
    OK(r < 0, "zero-size buffer returns -1");

    telnetty_destroy(ctx);
}

/* ----- Buffer pool: exhaustion falls back to fresh allocation ----- */

static void test_pool_exhaustion_fallback(void) {
    telnetty_buffer_pool_t* pool = telnetty_buffer_pool_create(2, 64, 0);
    OK(pool != NULL, "pool create");
    telnetty_buffer_t* a = telnetty_buffer_pool_get(pool);
    telnetty_buffer_t* b = telnetty_buffer_pool_get(pool);
    telnetty_buffer_t* c = telnetty_buffer_pool_get(pool);
    OK(a != NULL, "first pool get");
    OK(b != NULL, "second pool get");
    OK(c != NULL, "third get after exhaustion falls back to fresh alloc");
    telnetty_buffer_pool_put(pool, a);
    telnetty_buffer_pool_put(pool, b);
    telnetty_buffer_pool_put(pool, c);
    telnetty_buffer_pool_destroy(pool);
}

/* ----- Version macros present ----- */

static void test_version_macros(void) {
    OK(TELNETTY_VERSION_MAJOR == 1, "major version");
    OK(TELNETTY_VERSION_MINOR == 1, "minor version");
    OK(strcmp(TELNETTY_VERSION_STRING, "1.1.0") == 0, "version string");
    OK(TELNETTY_VERSION_HEX == ((1 << 16) | (1 << 8) | 0), "version hex");
}

/* ----- Default option policy sets support flags ----- */

static void test_default_option_policy(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "ctx create");
    const uint8_t allow[] = { TELNETTY_TELOPT_ECHO, TELNETTY_TELOPT_SGA };
    const uint8_t deny[]  = { TELNETTY_TELOPT_LINEMODE };
    int r = telnetty_default_option_policy(ctx, allow, 2, deny, 1);
    OK(r == 0, "policy install");

    telnetty_option_t* o = ctx->options[TELNETTY_TELOPT_ECHO];
    OK(o && o->support_us && o->support_him, "echo allowed both");
    o = ctx->options[TELNETTY_TELOPT_LINEMODE];
    OK(o && !o->support_us && !o->support_him, "linemode denied both");
    telnetty_destroy(ctx);
}

/* ----- Input hook routing ----- */

static uint8_t inbound_capture[256];
static size_t inbound_capture_len = 0;

static int capture_in_hook(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    (void)ctx;
    size_t take = len < sizeof(inbound_capture) - inbound_capture_len
        ? len : sizeof(inbound_capture) - inbound_capture_len;
    memcpy(inbound_capture + inbound_capture_len, data, take);
    inbound_capture_len += take;
    return (int)len;
}

static void test_input_hook(void) {
    inbound_capture_len = 0;
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "ctx create");
    telnetty_set_input_hook(ctx, capture_in_hook, NULL);
    const uint8_t bytes[] = { 'a', 'b', 'c', 'd' };
    int n = telnetty_process_inbound(ctx, bytes, sizeof(bytes));
    OK(n == 4, "hook returned 4");
    OK(inbound_capture_len == 4, "hook captured 4 bytes");
    OK(memcmp(inbound_capture, bytes, 4) == 0, "hook data intact");
    telnetty_destroy(ctx);
}

/* ----- Extended event system: register/fire/priority-order/unregister ----- */

static int ext_seq[8];
static int ext_seq_len = 0;

static void ext_cb_high(telnetty_context_t* ctx, telnetty_event_type_t ev,
                        const telnetty_event_data_union_t* d, void* ud,
                        telnetty_event_priority_t prio) {
    (void)ctx; (void)ev; (void)d; (void)ud; (void)prio;
    if (ext_seq_len < 8) ext_seq[ext_seq_len++] = 1;
}
static void ext_cb_low(telnetty_context_t* ctx, telnetty_event_type_t ev,
                       const telnetty_event_data_union_t* d, void* ud,
                       telnetty_event_priority_t prio) {
    (void)ctx; (void)ev; (void)d; (void)ud; (void)prio;
    if (ext_seq_len < 8) ext_seq[ext_seq_len++] = 2;
}

static void test_extended_events(void) {
    ext_seq_len = 0;
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "ext: ctx create");

    /* Register LOW first, then HIGH. Priority-ordered dispatch should fire
     * HIGH before LOW regardless of registration order. */
    int r1 = telnetty_register_event_handler(ctx, TELNETTY_EVENT_DATA,
                                             ext_cb_low, NULL,
                                             TELNETTY_PRIORITY_LOW);
    int r2 = telnetty_register_event_handler(ctx, TELNETTY_EVENT_DATA,
                                             ext_cb_high, NULL,
                                             TELNETTY_PRIORITY_HIGH);
    OK(r1 == 0 && r2 == 0, "ext: register both");

    telnetty_event_data_union_t d;
    memset(&d, 0, sizeof(d));
    telnetty_fire_event_ex(ctx, TELNETTY_EVENT_DATA,
                           TELNETTY_PRIORITY_NORMAL, &d);
    OK(ext_seq_len == 2, "ext: both handlers ran");
    OK(ext_seq[0] == 1 && ext_seq[1] == 2, "ext: HIGH before LOW");

    /* Unregister HIGH, fire again. */
    ext_seq_len = 0;
    int r3 = telnetty_unregister_event_handler(ctx, TELNETTY_EVENT_DATA,
                                               ext_cb_high);
    OK(r3 == 0, "ext: unregister HIGH");
    telnetty_fire_event_ex(ctx, TELNETTY_EVENT_DATA,
                           TELNETTY_PRIORITY_NORMAL, &d);
    OK(ext_seq_len == 1 && ext_seq[0] == 2, "ext: only LOW remains");

    telnetty_destroy(ctx);
}

/* ----- MTTS three-cycle handshake round-trip ----- */

static void test_mtts_three_cycle(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "mtts: ctx create");
    OK(telnetty_enable_mtts(ctx) == 0, "mtts: enable");

    telnetty_mtts_data_t* d = telnetty_mtts_of(ctx);
    OK(d != NULL, "mtts: data attached");
    OK(d->cycle == 0, "mtts: initial cycle 0");

    /* Feed IAC SB TTYPE SEND IAC SE three times; each drives one cycle. */
    const uint8_t send_sb[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_TTYPE,
        TELNETTY_TTYPE_SEND,
        TELNETTY_IAC, TELNETTY_SE
    };

    /* Clear out any negotiation bytes the enable_mtts call may have queued. */
    telnetty_buffer_reset(ctx->out_buffer);

    /* Cycle 0 -> client name response. */
    telnetty_process(ctx, send_sb, sizeof(send_sb));
    OK(d->cycle == 1, "mtts: cycle advanced to 1 after 1st SEND");
    size_t after_c0 = ctx->out_buffer->length;
    OK(after_c0 > 0, "mtts: cycle 0 response emitted");

    /* Cycle 1 -> terminal type response. */
    telnetty_process(ctx, send_sb, sizeof(send_sb));
    OK(d->cycle == 2, "mtts: cycle advanced to 2 after 2nd SEND");
    OK(ctx->out_buffer->length > after_c0, "mtts: cycle 1 response emitted");
    size_t after_c1 = ctx->out_buffer->length;

    /* Cycle 2 -> MTTS <flags> response. Must contain "MTTS ". */
    telnetty_process(ctx, send_sb, sizeof(send_sb));
    OK(ctx->out_buffer->length > after_c1, "mtts: cycle 2 response emitted");
    OK(d->negotiation_complete, "mtts: negotiation complete after cycle 2");

    /* The flags payload should include the literal "MTTS " prefix. */
    const uint8_t* wire = ctx->out_buffer->data;
    size_t wlen = ctx->out_buffer->length;
    int found_mtts = 0;
    if (wlen >= 5) {
        for (size_t i = 0; i + 5 <= wlen; i++) {
            if (memcmp(wire + i, "MTTS ", 5) == 0) { found_mtts = 1; break; }
        }
    }
    OK(found_mtts, "mtts: 'MTTS ' prefix present in wire");

    telnetty_destroy(ctx);
}

/* ----- Color state attachment: set_foreground emits SGR into out_buffer ----- */

static void test_color_state_emits(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "color: ctx create");
    OK(telnetty_color_init(ctx, true) == 0, "color: init");

    telnetty_buffer_reset(ctx->out_buffer);

    OK(telnetty_color_set_foreground(ctx, TELNETTY_COLOR_RED) == 0,
       "color: set fg red");
    OK(ctx->out_buffer->length >= 4, "color: SGR emitted");

    /* Look for ESC[31m. */
    const uint8_t* wire = ctx->out_buffer->data;
    size_t wlen = ctx->out_buffer->length;
    int found = 0;
    for (size_t i = 0; i + 4 <= wlen; i++) {
        if (wire[i] == 0x1B && wire[i+1] == '[' &&
            wire[i+2] == '3' && wire[i+3] == '1') { found = 1; break; }
    }
    OK(found, "color: ESC[31 present in wire");

    /* reset emits ESC[0m. */
    telnetty_buffer_reset(ctx->out_buffer);
    OK(telnetty_color_reset(ctx) == 0, "color: reset");
    wire = ctx->out_buffer->data;
    wlen = ctx->out_buffer->length;
    found = 0;
    for (size_t i = 0; i + 4 <= wlen; i++) {
        if (wire[i] == 0x1B && wire[i+1] == '[' &&
            wire[i+2] == '0' && wire[i+3] == 'm') { found = 1; break; }
    }
    OK(found, "color: ESC[0m present after reset");

    telnetty_destroy(ctx);
}

/* ----- MCP 2.1 line parser: quoted args, cord tracking, negotiate reply ----- */

static char mcp_pkg_msg[128];
static char mcp_pkg_args[512];
static int  mcp_pkg_calls = 0;

static void mcp_pkg_handler(telnetty_context_t* ctx, const char* package,
                            const char* message, const char* data,
                            void* user_data) {
    (void)ctx; (void)user_data; (void)package;
    mcp_pkg_calls++;
    snprintf(mcp_pkg_msg,  sizeof(mcp_pkg_msg),  "%s", message ? message : "");
    snprintf(mcp_pkg_args, sizeof(mcp_pkg_args), "%s", data    ? data    : "");
}

static void test_mcp_line_parser(void) {
    mcp_pkg_calls = 0;
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "mcp: ctx create");
    OK(telnetty_enable_mcp(ctx, false, TELNETTY_MCP_AUTH_KEY, NULL) == 0,
       "mcp: enable");
    OK(telnetty_mcp_register_package(ctx, "test-pkg", "1.0", NULL,
                                     mcp_pkg_handler, NULL) == 0,
       "mcp: register package");

    telnetty_mcp_data_t* d = telnetty_mcp_of(ctx);
    OK(d != NULL, "mcp: data attached");

    /* 1. Initial handshake: peer sends `#$#mcp` — we should reply with our
     *    own `mcp` line and advance state to ENABLED. */
    telnetty_buffer_reset(ctx->out_buffer);
    telnetty_mcp_handle_line(ctx, "#$#mcp authentication-key: peerkey version: 2.1 to: 2.1");
    OK(d->state == TELNETTY_MCP_STATE_ENABLED, "mcp: state enabled after handshake");
    OK(ctx->out_buffer->length > 0, "mcp: reply emitted for handshake");

    /* The reply must be an in-band `#$#mcp ` line, not IAC SB. */
    const uint8_t* wire = ctx->out_buffer->data;
    OK(ctx->out_buffer->length >= 4 &&
       wire[0] == '#' && wire[1] == '$' && wire[2] == '#',
       "mcp: reply starts with #$#");

    /* 2. Package dispatch: quoted arg with embedded space must land as one
     *    key-value pair on `data`. */
    d->packages[0]->active = 1;  /* force-active for test isolation */
    mcp_pkg_calls = 0;
    telnetty_mcp_handle_line(ctx,
        "#$#test-pkg-do peerkey name: \"hello world\" count: 42");
    OK(mcp_pkg_calls == 1, "mcp: package handler called once");
    OK(strcmp(mcp_pkg_msg, "do") == 0, "mcp: message parsed as 'do'");
    OK(strstr(mcp_pkg_args, "name:") != NULL, "mcp: name key present in args");
    OK(strstr(mcp_pkg_args, "count: 42") != NULL, "mcp: count value present");

    /* 3. Cord flow: message with `_data-tag`, then a `#$#* <tag>` continuation,
     *    then `#$#: <tag>` terminator. Cord should be created and released. */
    mcp_pkg_calls = 0;
    telnetty_mcp_handle_line(ctx,
        "#$#test-pkg-multi peerkey _data-tag: t1");
    OK(mcp_pkg_calls == 1, "mcp: multi message dispatched");

    telnetty_mcp_cord_t* c = telnetty__mcp_cord_find(d, "t1");
    OK(c != NULL, "mcp: cord t1 opened");

    telnetty_mcp_handle_line(ctx, "#$#* t1 body: continuation1");
    c = telnetty__mcp_cord_find(d, "t1");
    OK(c != NULL, "mcp: cord t1 still open after continuation");

    telnetty_mcp_handle_line(ctx, "#$#: t1");
    c = telnetty__mcp_cord_find(d, "t1");
    OK(c == NULL, "mcp: cord t1 released by terminator");

    /* 4. mcp-negotiate-end must advance state (from ENABLED to NEGOTIATING
     *    per the current impl — the important thing is the line is accepted
     *    without dispatching to a package handler). */
    mcp_pkg_calls = 0;
    telnetty_mcp_handle_line(ctx, "#$#mcp-negotiate-end peerkey");
    OK(mcp_pkg_calls == 0, "mcp: negotiate-end not dispatched to package");

    telnetty_destroy(ctx);
}

/* ----- Log callback: threshold + payload ----- */

static int log_calls;
static telnetty_log_level_t log_last_level;
static char log_last_msg[128];
static char log_last_cat[32];

static void capture_log(telnetty_context_t* ctx, telnetty_log_level_t level,
                        const char* category, const char* message, void* ud) {
    (void)ctx; (void)ud;
    log_calls++;
    log_last_level = level;
    snprintf(log_last_msg, sizeof(log_last_msg), "%s", message ? message : "");
    snprintf(log_last_cat, sizeof(log_last_cat), "%s", category ? category : "");
}

static void test_log_callback(void) {
    log_calls = 0;
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "log: ctx create");

    telnetty_set_log_callback(ctx, TELNETTY_LOG_WARN, capture_log, NULL);

    /* DEBUG below threshold — nothing fires. */
    telnetty_log(ctx, TELNETTY_LOG_DEBUG, "core", "quiet-%d", 1);
    OK(log_calls == 0, "log: below-threshold suppressed");

    /* WARN at threshold — fires. */
    telnetty_log(ctx, TELNETTY_LOG_WARN, "core", "warn-%d", 42);
    OK(log_calls == 1, "log: at-threshold fires");
    OK(log_last_level == TELNETTY_LOG_WARN, "log: level captured");
    OK(strcmp(log_last_cat, "core") == 0, "log: category captured");
    OK(strcmp(log_last_msg, "warn-42") == 0, "log: message formatted");

    /* report_error routes through log at ERROR level. */
    log_calls = 0;
    telnetty_report_error(ctx, TELNETTY_ERR_PROTOCOL, "test error");
    OK(log_calls == 1, "log: report_error fired log");
    OK(log_last_level == TELNETTY_LOG_ERROR, "log: report_error uses ERROR level");

    /* NULL cb clears the sink. */
    log_calls = 0;
    telnetty_set_log_callback(ctx, TELNETTY_LOG_TRACE, NULL, NULL);
    telnetty_log(ctx, TELNETTY_LOG_ERROR, "x", "y");
    OK(log_calls == 0, "log: NULL cb clears sink");

    telnetty_destroy(ctx);
}

/* ----- Negotiation timeout + tick ----- */

static int timeout_error_hits = 0;
static telnetty_error_t timeout_last_err = TELNETTY_OK;

static void timeout_cb(telnetty_context_t* ctx, telnetty_event_type_t ev,
                       const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx; (void)ud;
    if (ev == TELNETTY_EVENT_ERROR && d) {
        timeout_last_err = (telnetty_error_t)d->error.code;
        if (d->error.code == TELNETTY_ERR_TIMEOUT) timeout_error_hits++;
    }
}

static void test_negotiation_timeout(void) {
    timeout_error_hits = 0;
    telnetty_context_t* ctx = telnetty_create(timeout_cb, NULL);
    OK(ctx != NULL, "timeout: ctx create");

    telnetty_tick(ctx, 1000);
    telnetty_set_negotiation_timeout(ctx, 500);  /* 500 ms */

    /* Initiate a WILL on ECHO: local goes to WANTYES, deadline stamped. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ECHO, 1, 1);
    telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_ECHO);

    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_ECHO];
    OK(opt != NULL, "timeout: option created");
    OK(opt->deadline_ms == 1500, "timeout: deadline = now + 500");

    /* Advance clock past deadline — tick fires TIMEOUT. */
    telnetty_tick(ctx, 1600);
    OK(timeout_error_hits == 1, "timeout: TIMEOUT fired once");
    OK(opt->deadline_ms == 0, "timeout: deadline cleared after fire");
    OK(opt->us == TELNETTY_Q_NO, "timeout: us reset to Q_NO after timeout");

    telnetty_destroy(ctx);
}

/* ----- Rate limits: byte + IAC bucket ----- */

static int rate_error_hits = 0;

static void rate_cb(telnetty_context_t* ctx, telnetty_event_type_t ev,
                    const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx; (void)ud;
    if (ev == TELNETTY_EVENT_ERROR && d && d->error.code == TELNETTY_ERR_RATE_LIMIT)
        rate_error_hits++;
}

static void test_rate_limits(void) {
    rate_error_hits = 0;
    telnetty_context_t* ctx = telnetty_create(rate_cb, NULL);
    OK(ctx != NULL, "rate: ctx create");

    telnetty_tick(ctx, 1000);
    telnetty_set_rate_limit(ctx, 10 /* bytes/sec */, 0 /* iac disabled */);

    /* First 10 bytes fit the initial bucket. */
    uint8_t chunk[10];
    memset(chunk, 'A', sizeof(chunk));
    int n = telnetty_process(ctx, chunk, sizeof(chunk));
    OK(n == 10, "rate: first 10 bytes accepted");
    OK(rate_error_hits == 0, "rate: no trip");

    /* Next byte trips the bucket without any tick advance. */
    uint8_t one = 'X';
    n = telnetty_process(ctx, &one, 1);
    OK(rate_error_hits == 1, "rate: trip fired");
    OK(n == 0, "rate: over-limit call drops all bytes");

    telnetty_destroy(ctx);
}

/* ----- MSDP recursive callback: TABLE_OPEN/CLOSE + path ----- */

typedef struct {
    int event_types[16];
    char paths[16][64];
    char values[16][64];
    int depths[16];
    int count;
} msdp_capture_t;

static void rec_capture(telnetty_context_t* ctx, int depth,
                        telnetty_msdp_event_t type, const char* path,
                        const char* value, void* ud) {
    (void)ctx;
    msdp_capture_t* c = (msdp_capture_t*)ud;
    if (c->count >= 16) return;
    c->event_types[c->count] = (int)type;
    c->depths[c->count] = depth;
    snprintf(c->paths[c->count],  sizeof(c->paths[0]),  "%s", path  ? path  : "");
    snprintf(c->values[c->count], sizeof(c->values[0]), "%s", value ? value : "");
    c->count++;
}

static void test_msdp_recursive(void) {
    msdp_capture_t cap;
    memset(&cap, 0, sizeof(cap));

    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "msdp-rec: ctx create");
    OK(telnetty_enable_msdp(ctx, NULL, NULL) == 0, "msdp-rec: enable");
    OK(telnetty_msdp_set_recursive_callback(ctx, rec_capture, &cap) == 0,
       "msdp-rec: set callback");

    /* Build a nested payload manually:
     *   VAR ROOM
     *     TABLE_OPEN
     *       VAR NAME VAL Alcove
     *       VAR EXITS
     *         ARRAY_OPEN VAL north VAL south ARRAY_CLOSE
     *     TABLE_CLOSE
     */
    uint8_t p[128];
    size_t i = 0;
    p[i++] = TELNETTY_MSDP_VAR; memcpy(p+i, "ROOM", 4); i += 4;
    p[i++] = TELNETTY_MSDP_TABLE_OPEN;
    p[i++] = TELNETTY_MSDP_VAR; memcpy(p+i, "NAME", 4); i += 4;
    p[i++] = TELNETTY_MSDP_VAL; memcpy(p+i, "Alcove", 6); i += 6;
    p[i++] = TELNETTY_MSDP_VAR; memcpy(p+i, "EXITS", 5); i += 5;
    p[i++] = TELNETTY_MSDP_ARRAY_OPEN;
    p[i++] = TELNETTY_MSDP_VAL; memcpy(p+i, "north", 5); i += 5;
    p[i++] = TELNETTY_MSDP_VAL; memcpy(p+i, "south", 5); i += 5;
    p[i++] = TELNETTY_MSDP_ARRAY_CLOSE;
    p[i++] = TELNETTY_MSDP_TABLE_CLOSE;

    telnetty_msdp_sb(ctx, TELNETTY_TELOPT_MSDP, p, i,
                     ctx->options[TELNETTY_TELOPT_MSDP]->user_data);

    OK(cap.count >= 8, "msdp-rec: got expected event count");
    /* Path is the ancestor chain, not the current VAR name. Root VAR has empty
     * path; nested VAR at depth 1 has path = parent's name. */
    OK(cap.event_types[0] == TELNETTY_MSDP_EV_VAR && cap.depths[0] == 0,
       "msdp-rec: root VAR at depth 0");
    OK(cap.event_types[1] == TELNETTY_MSDP_EV_TABLE_OPEN &&
       cap.depths[1] == 0,
       "msdp-rec: TABLE_OPEN at depth 0");
    /* After TABLE_OPEN, depth becomes 1 for children. */
    OK(cap.event_types[2] == TELNETTY_MSDP_EV_VAR &&
       cap.depths[2] == 1 &&
       strcmp(cap.paths[2], "ROOM") == 0,
       "msdp-rec: nested VAR at depth 1 has parent path");
    OK(cap.event_types[3] == TELNETTY_MSDP_EV_VAL &&
       strcmp(cap.values[3], "Alcove") == 0,
       "msdp-rec: nested VAL captured");

    /* Look for the ARRAY_OPEN and its two VAL children. */
    int found_arr_open = 0, arr_val_count = 0;
    for (int k = 0; k < cap.count; k++) {
        if (cap.event_types[k] == TELNETTY_MSDP_EV_ARRAY_OPEN) found_arr_open = 1;
        if (found_arr_open && cap.event_types[k] == TELNETTY_MSDP_EV_VAL &&
            cap.depths[k] >= 2)
            arr_val_count++;
    }
    OK(found_arr_open, "msdp-rec: ARRAY_OPEN fired");
    OK(arr_val_count == 2, "msdp-rec: two array VALs at depth 2");

    telnetty_destroy(ctx);
}

/* ----- Split-input torture: same bytes, fed in every possible chunking,
 *       must produce identical event streams (same DATA payload, same SB,
 *       same negotiation state). ----- */

typedef struct {
    uint8_t data[512];
    size_t  data_len;
    int     will_ttype;
    int     do_ttype;
    int     sb_option;
    uint8_t sb_payload[512];
    size_t  sb_len;
    int     errors;
} split_capture_t;

static void split_cb(telnetty_context_t* ctx, telnetty_event_type_t ev,
                     const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx;
    split_capture_t* c = (split_capture_t*)ud;
    switch (ev) {
    case TELNETTY_EVENT_DATA:
        if (d && c->data_len + d->data.length <= sizeof(c->data)) {
            memcpy(c->data + c->data_len, d->data.data, d->data.length);
            c->data_len += d->data.length;
        }
        break;
    case TELNETTY_EVENT_WILL:
        if (d && d->option.option == TELNETTY_TELOPT_TTYPE) c->will_ttype++;
        break;
    case TELNETTY_EVENT_DO:
        if (d && d->option.option == TELNETTY_TELOPT_TTYPE) c->do_ttype++;
        break;
    case TELNETTY_EVENT_SB:
        if (d) {
            c->sb_option = d->sub.option;
            if (d->sub.length && d->sub.length <= sizeof(c->sb_payload)) {
                memcpy(c->sb_payload, d->sub.data, d->sub.length);
                c->sb_len = d->sub.length;
            }
        }
        break;
    case TELNETTY_EVENT_ERROR:
        c->errors++;
        break;
    default: break;
    }
}

/* Feed `frame` in chunks of size `chunk_sz`. chunk_sz==0 means one shot. */
static void split_feed(telnetty_context_t* ctx, const uint8_t* frame,
                       size_t frame_len, size_t chunk_sz) {
    if (chunk_sz == 0) { telnetty_process(ctx, frame, frame_len); return; }
    for (size_t i = 0; i < frame_len; i += chunk_sz) {
        size_t take = frame_len - i;
        if (take > chunk_sz) take = chunk_sz;
        telnetty_process(ctx, frame + i, take);
    }
}

static void test_split_input_iac(void) {
    /* IAC WILL TTYPE (peer offers TTYPE). Must fire EVENT_WILL exactly once
     * regardless of chunking, given support_him is set. */
    const uint8_t frame[] = { TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_TTYPE };

    for (size_t chunk = 0; chunk <= 3; chunk++) {
        split_capture_t cap; memset(&cap, 0, sizeof(cap));
        telnetty_context_t* ctx = telnetty_create(split_cb, &cap);
        telnetty_set_option_support(ctx, TELNETTY_TELOPT_TTYPE, 1, 1);
        split_feed(ctx, frame, sizeof(frame), chunk);
        OK(cap.will_ttype == 1, "split-iac: EVENT_WILL fired once");
        OK(cap.errors == 0, "split-iac: no errors");
        telnetty_destroy(ctx);
    }
}

static void test_split_input_iac_escape(void) {
    /* Data payload with an escaped 0xFF: 'a', IAC, IAC, 'b'. Peer meant to
     * send a literal 0xFF sandwiched between 'a' and 'b'. Every chunking
     * must produce the SAME 3-byte payload {'a', 0xFF, 'b'}. */
    const uint8_t frame[] = { 'a', TELNETTY_IAC, TELNETTY_IAC, 'b' };
    const uint8_t expect[] = { 'a', 0xFF, 'b' };

    for (size_t chunk = 0; chunk <= 4; chunk++) {
        split_capture_t cap; memset(&cap, 0, sizeof(cap));
        telnetty_context_t* ctx = telnetty_create(split_cb, &cap);
        split_feed(ctx, frame, sizeof(frame), chunk);
        OK(cap.data_len == 3, "split-esc: 3 payload bytes");
        OK(memcmp(cap.data, expect, 3) == 0, "split-esc: {a, 0xFF, b}");
        OK(cap.errors == 0, "split-esc: no errors");
        telnetty_destroy(ctx);
    }
}

static void test_split_input_sb(void) {
    /* IAC SB TTYPE IS "XTERM" IAC SE — small NAWS-shaped SB. Chunk sizes 1..7. */
    const uint8_t frame[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_TTYPE,
        0 /* IS */, 'X', 'T', 'E', 'R', 'M',
        TELNETTY_IAC, TELNETTY_SE
    };
    const uint8_t expect_payload[] = { 0, 'X', 'T', 'E', 'R', 'M' };

    for (size_t chunk = 0; chunk <= 7; chunk++) {
        split_capture_t cap; memset(&cap, 0, sizeof(cap));
        telnetty_context_t* ctx = telnetty_create(split_cb, &cap);
        split_feed(ctx, frame, sizeof(frame), chunk);
        OK(cap.sb_option == TELNETTY_TELOPT_TTYPE, "split-sb: option preserved");
        OK(cap.sb_len == sizeof(expect_payload), "split-sb: length preserved");
        OK(memcmp(cap.sb_payload, expect_payload, sizeof(expect_payload)) == 0,
           "split-sb: payload preserved");
        OK(cap.errors == 0, "split-sb: no errors");
        telnetty_destroy(ctx);
    }
}

static void test_split_input_sb_with_iac_escape(void) {
    /* SB payload containing IAC IAC (literal 0xFF byte) — must decode to
     * one 0xFF and reach the SB event, not terminate the SB frame. */
    const uint8_t frame[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_TTYPE,
        0, 'A', TELNETTY_IAC, TELNETTY_IAC, 'B',
        TELNETTY_IAC, TELNETTY_SE
    };
    const uint8_t expect_payload[] = { 0, 'A', 0xFF, 'B' };

    for (size_t chunk = 0; chunk <= 5; chunk++) {
        split_capture_t cap; memset(&cap, 0, sizeof(cap));
        telnetty_context_t* ctx = telnetty_create(split_cb, &cap);
        split_feed(ctx, frame, sizeof(frame), chunk);
        OK(cap.sb_len == sizeof(expect_payload),
           "split-sb-esc: length after IAC-IAC decode");
        OK(memcmp(cap.sb_payload, expect_payload, sizeof(expect_payload)) == 0,
           "split-sb-esc: payload after IAC-IAC decode");
        OK(cap.errors == 0, "split-sb-esc: no errors");
        telnetty_destroy(ctx);
    }
}

static void test_split_input_binary_transparency(void) {
    /* All 256 byte values that are NOT 0xFF, fed byte-at-a-time. Every byte
     * must land in the DATA callback unchanged. Verifies the 8-bit-clean
     * DATA path even in the byte-by-byte cadence. */
    uint8_t frame[255];
    size_t j = 0;
    for (int b = 0; b < 256; b++) {
        if (b == 0xFF) continue;
        frame[j++] = (uint8_t)b;
    }

    split_capture_t cap; memset(&cap, 0, sizeof(cap));
    telnetty_context_t* ctx = telnetty_create(split_cb, &cap);
    split_feed(ctx, frame, sizeof(frame), 1);  /* byte at a time */
    OK(cap.data_len == sizeof(frame), "split-bin: all 255 bytes delivered");
    OK(memcmp(cap.data, frame, sizeof(frame)) == 0, "split-bin: payload identical");
    OK(cap.errors == 0, "split-bin: no errors");
    telnetty_destroy(ctx);
}

/* ----- RFC option enablers: BINARY, STATUS, TSPEED, LFLOW, LINEMODE ----- */

static int rfc_will_seen[256];
static int rfc_do_seen[256];
static int rfc_sb_option = -1;

static void rfc_cb(telnetty_context_t* ctx, telnetty_event_type_t ev,
                   const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx; (void)ud;
    if (ev == TELNETTY_EVENT_WILL && d) rfc_will_seen[d->option.option]++;
    if (ev == TELNETTY_EVENT_DO   && d) rfc_do_seen[d->option.option]++;
    if (ev == TELNETTY_EVENT_SB   && d) rfc_sb_option = d->sub.option;
}

static void reset_rfc(void) {
    memset(rfc_will_seen, 0, sizeof(rfc_will_seen));
    memset(rfc_do_seen,   0, sizeof(rfc_do_seen));
    rfc_sb_option = -1;
}

static void test_rfc_binary_roundtrip(void) {
    reset_rfc();
    telnetty_context_t* ctx = telnetty_create(rfc_cb, NULL);
    OK(telnetty_enable_binary(ctx) == 0, "rfc-binary: enable");

    /* Simulate peer confirming with DO BINARY. */
    const uint8_t peer_do[] = { TELNETTY_IAC, TELNETTY_DO, TELNETTY_TELOPT_BINARY };
    telnetty_process(ctx, peer_do, sizeof(peer_do));
    OK(rfc_do_seen[TELNETTY_TELOPT_BINARY] == 1, "rfc-binary: EVENT_DO fired");

    telnetty_destroy(ctx);
}

static void test_rfc_status_reply(void) {
    reset_rfc();
    telnetty_context_t* ctx = telnetty_create(rfc_cb, NULL);
    OK(telnetty_enable_status(ctx) == 0, "rfc-status: enable");

    /* Peer requests a STATUS report via IAC SB STATUS SEND IAC SE.
     * The handler should reply with IAC SB STATUS IS ... IAC SE — verify
     * something got appended to out_buffer. */
    telnetty_buffer_reset(ctx->out_buffer);
    const uint8_t peer_will[] = { TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_STATUS };
    telnetty_process(ctx, peer_will, sizeof(peer_will));

    const uint8_t peer_send[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_STATUS,
        1 /* SEND */,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, peer_send, sizeof(peer_send));
    OK(ctx->out_buffer->length > 0, "rfc-status: response bytes emitted");

    telnetty_destroy(ctx);
}

static void test_rfc_tspeed_capture(void) {
    telnetty_context_t* ctx = telnetty_create(rfc_cb, NULL);
    OK(telnetty_enable_tspeed(ctx) == 0, "rfc-tspeed: enable");

    /* Peer sends WILL TSPEED then IS "38400,38400". */
    const uint8_t peer_will[] = { TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_TERM_SPEED };
    telnetty_process(ctx, peer_will, sizeof(peer_will));

    const uint8_t peer_is[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_TERM_SPEED,
        0 /* IS */, '3','8','4','0','0',',','3','8','4','0','0',
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, peer_is, sizeof(peer_is));

    const char* tx = NULL; const char* rx = NULL;
    OK(telnetty_get_term_speed(ctx, &tx, &rx) == 0, "rfc-tspeed: got speed");
    OK(tx && strcmp(tx, "38400") == 0, "rfc-tspeed: tx = 38400");
    OK(rx && strcmp(rx, "38400") == 0, "rfc-tspeed: rx = 38400");

    telnetty_destroy(ctx);
}

static void test_rfc_lflow_capture(void) {
    telnetty_context_t* ctx = telnetty_create(rfc_cb, NULL);
    OK(telnetty_enable_lflow(ctx) == 0, "rfc-lflow: enable");

    /* Peer accepts DO LFLOW then sends SB LFLOW OFF (0). */
    const uint8_t peer_do[] = { TELNETTY_IAC, TELNETTY_DO, TELNETTY_TELOPT_REMOTE_FLOW };
    telnetty_process(ctx, peer_do, sizeof(peer_do));

    const uint8_t peer_sb_off[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_REMOTE_FLOW,
        0 /* OFF */,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, peer_sb_off, sizeof(peer_sb_off));
    OK(telnetty_get_flow(ctx) == 0, "rfc-lflow: OFF captured");

    const uint8_t peer_sb_on[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_REMOTE_FLOW,
        1 /* ON */,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, peer_sb_on, sizeof(peer_sb_on));
    OK(telnetty_get_flow(ctx) == 1, "rfc-lflow: ON captured");

    telnetty_destroy(ctx);
}

static void test_rfc_linemode_enables(void) {
    reset_rfc();
    telnetty_context_t* ctx = telnetty_create(rfc_cb, NULL);
    OK(telnetty_enable_linemode(ctx) == 0, "rfc-linemode: enable");

    /* Peer sends WILL LINEMODE. */
    const uint8_t peer_will[] = { TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_LINEMODE };
    telnetty_process(ctx, peer_will, sizeof(peer_will));
    OK(rfc_will_seen[TELNETTY_TELOPT_LINEMODE] == 1, "rfc-linemode: WILL fired");

    telnetty_destroy(ctx);
}

/* ----- LINEMODE/SLC deep-dive ----- */

static void test_linemode_mode_ack(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(ctx != NULL, "linemode-mode: ctx create");
    OK(telnetty_enable_linemode(ctx) == 0, "linemode-mode: enable");

    /* Peer offers MODE (EDIT | TRAPSIG), no ACK. We must reply with the
     * same bits + MODE_ACK. */
    telnetty_buffer_reset(ctx->out_buffer);
    const uint8_t offer[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_LINEMODE,
        TELNETTY_LM_MODE,
        (uint8_t)(TELNETTY_LM_MODE_EDIT | TELNETTY_LM_MODE_TRAPSIG),
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, offer, sizeof(offer));

    OK(telnetty_get_linemode(ctx) ==
       (TELNETTY_LM_MODE_EDIT | TELNETTY_LM_MODE_TRAPSIG),
       "linemode-mode: mode bits stored");

    /* Reply should contain IAC SB LINEMODE MODE (bits|ACK) IAC SE. */
    const uint8_t* w = ctx->out_buffer->data;
    size_t wl = ctx->out_buffer->length;
    int found = 0;
    uint8_t want = TELNETTY_LM_MODE_EDIT | TELNETTY_LM_MODE_TRAPSIG
                 | TELNETTY_LM_MODE_ACK;
    for (size_t i = 0; i + 7 <= wl; i++) {
        if (w[i]==TELNETTY_IAC && w[i+1]==TELNETTY_SB &&
            w[i+2]==TELNETTY_TELOPT_LINEMODE &&
            w[i+3]==TELNETTY_LM_MODE && w[i+4]==want &&
            w[i+5]==TELNETTY_IAC && w[i+6]==TELNETTY_SE) {
            found = 1; break;
        }
    }
    OK(found, "linemode-mode: MODE|ACK reply emitted");

    /* Second offer with ACK already set — we must NOT echo again. */
    telnetty_buffer_reset(ctx->out_buffer);
    const uint8_t offer_ack[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_LINEMODE,
        TELNETTY_LM_MODE, want,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, offer_ack, sizeof(offer_ack));
    OK(ctx->out_buffer->length == 0,
       "linemode-mode: peer-ACK MODE does not re-echo");

    telnetty_destroy(ctx);
}

static void test_linemode_slc_triples(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(telnetty_enable_linemode(ctx) == 0, "linemode-slc: enable");

    /* Feed a SLC frame with three triples:
     *   func=1 (SLC_SYNCH), flags=DEFAULT|FLUSHIN, value=0x18
     *   func=3 (SLC_IP),    flags=VARIABLE,        value=0x03
     *   func=5 (SLC_AO),    flags=DEFAULT,         value=0x0F
     * All missing ACK — we should echo an ACK for every triple. */
    telnetty_buffer_reset(ctx->out_buffer);
    const uint8_t frame[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_LINEMODE,
        TELNETTY_LM_SLC,
        1, (uint8_t)(TELNETTY_SLC_DEFAULT | TELNETTY_SLC_FLUSHIN), 0x18,
        3, TELNETTY_SLC_VARIABLE, 0x03,
        5, TELNETTY_SLC_DEFAULT,  0x0F,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, frame, sizeof(frame));

    /* Table lookups. */
    uint8_t flags = 0, value = 0;
    OK(telnetty_get_slc(ctx, 1, &flags, &value) == 0, "slc: func 1 got");
    OK(flags == (TELNETTY_SLC_DEFAULT | TELNETTY_SLC_FLUSHIN) &&
       value == 0x18, "slc: func 1 stored");
    OK(telnetty_get_slc(ctx, 3, &flags, &value) == 0, "slc: func 3 got");
    OK(flags == TELNETTY_SLC_VARIABLE && value == 0x03, "slc: func 3 stored");
    OK(telnetty_get_slc(ctx, 5, &flags, &value) == 0, "slc: func 5 got");
    OK(flags == TELNETTY_SLC_DEFAULT && value == 0x0F, "slc: func 5 stored");

    /* ACK reply: SB LINEMODE SLC (func flags|ACK value){3} IAC SE. */
    const uint8_t* w = ctx->out_buffer->data;
    size_t wl = ctx->out_buffer->length;
    int seen_ack = 0;
    for (size_t i = 0; i + 4 <= wl; i++) {
        if (w[i]==TELNETTY_IAC && w[i+1]==TELNETTY_SB &&
            w[i+2]==TELNETTY_TELOPT_LINEMODE &&
            w[i+3]==TELNETTY_LM_SLC) {
            /* Walk triples after the SLC tag looking for ACK. */
            for (size_t j = i + 4; j + 3 <= wl; j += 3) {
                if (j + 3 > wl) break;
                if (w[j] == TELNETTY_IAC) break;
                if (w[j+1] & TELNETTY_SLC_ACK) { seen_ack = 1; break; }
            }
            break;
        }
    }
    OK(seen_ack, "slc: ACK bit set in reply triple");

    /* Feed a peer-ACK triple back — we must NOT echo. */
    telnetty_buffer_reset(ctx->out_buffer);
    const uint8_t ack_frame[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_LINEMODE,
        TELNETTY_LM_SLC,
        1, (uint8_t)(TELNETTY_SLC_DEFAULT | TELNETTY_SLC_ACK), 0x18,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, ack_frame, sizeof(ack_frame));
    OK(ctx->out_buffer->length == 0, "slc: peer-ACK does not re-echo");

    telnetty_destroy(ctx);
}

static void test_linemode_slc_partial_triple(void) {
    /* Frame with 2 full triples + 1 stray byte. The trailing partial
     * triple must be ignored, not read past. */
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(telnetty_enable_linemode(ctx) == 0, "slc-partial: enable");

    const uint8_t frame[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_LINEMODE,
        TELNETTY_LM_SLC,
        2, TELNETTY_SLC_DEFAULT, 0x11,
        4, TELNETTY_SLC_VARIABLE, 0x22,
        7 /* dangling — only 1 byte of a triple */,
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, frame, sizeof(frame));

    uint8_t f = 0, v = 0;
    OK(telnetty_get_slc(ctx, 2, &f, &v) == 0 && v == 0x11,
       "slc-partial: triple 1 stored");
    OK(telnetty_get_slc(ctx, 4, &f, &v) == 0 && v == 0x22,
       "slc-partial: triple 2 stored");
    OK(telnetty_get_slc(ctx, 7, &f, &v) == 0 && v == 0x00,
       "slc-partial: dangling byte NOT applied");

    telnetty_destroy(ctx);
}

static void test_linemode_send_mode(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    OK(telnetty_enable_linemode(ctx) == 0, "linemode-send: enable");

    telnetty_buffer_reset(ctx->out_buffer);
    OK(telnetty_linemode_send_mode(ctx, TELNETTY_LM_MODE_EDIT) == 0,
       "linemode-send: send EDIT");
    /* Wire: IAC SB LINEMODE MODE EDIT IAC SE. */
    const uint8_t* w = ctx->out_buffer->data;
    size_t wl = ctx->out_buffer->length;
    int found = 0;
    for (size_t i = 0; i + 7 <= wl; i++) {
        if (w[i]==TELNETTY_IAC && w[i+1]==TELNETTY_SB &&
            w[i+2]==TELNETTY_TELOPT_LINEMODE &&
            w[i+3]==TELNETTY_LM_MODE &&
            w[i+4]==TELNETTY_LM_MODE_EDIT &&
            w[i+5]==TELNETTY_IAC && w[i+6]==TELNETTY_SE) {
            found = 1; break;
        }
    }
    OK(found, "linemode-send: MODE EDIT on wire");

    telnetty_destroy(ctx);
}

int main(void) {
    printf("--- Telnetty Extended-Surface Tests ---\n");
    test_version_macros();
    test_qmethod_wantno_op_no_spurious_event();
    test_sb_overflow_reports_error();
    test_chain_split_within_buffer();
    test_buffer_find_pattern();
    test_color_emitter_bounds();
    test_pool_exhaustion_fallback();
    test_default_option_policy();
    test_input_hook();
    test_extended_events();
    test_mtts_three_cycle();
    test_color_state_emits();
    test_mcp_line_parser();
    test_log_callback();
    test_negotiation_timeout();
    test_rate_limits();
    test_msdp_recursive();
    test_split_input_iac();
    test_split_input_iac_escape();
    test_split_input_sb();
    test_split_input_sb_with_iac_escape();
    test_split_input_binary_transparency();
    test_rfc_binary_roundtrip();
    test_rfc_status_reply();
    test_rfc_tspeed_capture();
    test_rfc_lflow_capture();
    test_rfc_linemode_enables();
    test_linemode_mode_ack();
    test_linemode_slc_triples();
    test_linemode_slc_partial_triple();
    test_linemode_send_mode();

    printf("\nPassed: %d\nFailed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
