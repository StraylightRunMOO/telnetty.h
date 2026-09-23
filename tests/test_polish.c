/**
 * @file test_polish.c
 * @brief Regression tests for the 1.1 polish patch series.
 *
 * Focused on the concrete bug fixes and additive API added in 1.1.0. Not a
 * general protocol suite — see test_basic.c / test_protocols.c for that.
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

#include "telnetty_core.h"
#include "telnetty_events.h"
#include "telnetty_buffer.h"
#include "telnetty_options.h"
#include "telnetty_mud.h"
#include "telnetty_compression.h"
#include "telnetty_color.h"

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

int main(void) {
    printf("--- Telnetty 1.1 Polish Tests ---\n");
    test_version_macros();
    test_qmethod_wantno_op_no_spurious_event();
    test_sb_overflow_reports_error();
    test_chain_split_within_buffer();
    test_buffer_find_pattern();
    test_color_emitter_bounds();
    test_pool_exhaustion_fallback();
    test_default_option_policy();
    test_input_hook();

    printf("\nPassed: %d\nFailed: %d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
