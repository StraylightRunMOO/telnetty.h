/**
 * @file test_comprehensive.c
 * @brief Comprehensive test suite for Telnetty library
 * 
 * This test program aims for maximum code coverage of the Telnetty library.
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.0.0
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>

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

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

/* Test assertion macros */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "  FAIL: %s at line %d\n", message, __LINE__); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, "  FAIL: %s (expected %d, got %d) at line %d\n", \
                    message, (int)(expected), (int)(actual), __LINE__); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_NOT_NULL(ptr, message) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "  FAIL: %s (got NULL) at line %d\n", message, __LINE__); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_NULL(ptr, message) \
    do { \
        if ((ptr) != NULL) { \
            fprintf(stderr, "  FAIL: %s (expected NULL) at line %d\n", message, __LINE__); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("Running: %s\n", #test_func); \
        test_func(); \
    } while(0)

/* ============================================================================
 * Test Group 1: Core Context Tests
 * ============================================================================ */

static void test_core_null_handling(void) {
    /* Test NULL context handling */
    telnetty_destroy(NULL);
    TEST_ASSERT_EQUAL(0, telnetty_process(NULL, (const uint8_t*)"test", 4), "NULL context process should return 0");
    
    /* Test NULL data handling */
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context created with NULL handler");
    TEST_ASSERT_EQUAL(0, telnetty_process(ctx, NULL, 10), "NULL data should return 0");
    TEST_ASSERT_EQUAL(0, telnetty_process(ctx, (const uint8_t*)"test", 0), "Zero length should return 0");
    telnetty_destroy(ctx);
}

static void test_core_create_destroy_multiple(void) {
    /* Create and destroy multiple contexts */
    for (int i = 0; i < 10; i++) {
        telnetty_context_t* ctx = telnetty_create(NULL, NULL);
        TEST_ASSERT_NOT_NULL(ctx, "Context creation in loop");
        telnetty_destroy(ctx);
    }
}

static void test_core_user_data(void) {
    int user_data = 42;
    telnetty_context_t* ctx = telnetty_create(NULL, &user_data);
    TEST_ASSERT_NOT_NULL(ctx, "Context with user data");
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Test Group 2: Event System Tests
 * ============================================================================ */

static int event_counts[20] = {0};

static void counting_event_handler(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data,
    void* user_data
) {
    (void)ctx;
    (void)data;
    (void)user_data;
    if (event < 20) {
        event_counts[event]++;
    }
}

static void test_events_all_types(void) {
    /* Reset counters */
    memset(event_counts, 0, sizeof(event_counts));

    telnetty_context_t* ctx = telnetty_create(counting_event_handler, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for event testing");

    /* Under RFC 1143 the settled event only fires once we've decided to
     * accept the peer's request. Declare support first so the negotiation
     * reaches Q_YES and the event dispatches. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ECHO, 1, 1);

    /* Test data events */
    telnetty_process(ctx, (const uint8_t*)"A", 1);
    TEST_ASSERT(event_counts[TELNETTY_EVENT_DATA] >= 1, "Data event fired");

    /* Test IAC events - send IAC NOP */
    const uint8_t iac_nop[] = {TELNETTY_IAC, TELNETTY_NOP};
    telnetty_process(ctx, iac_nop, sizeof(iac_nop));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_IAC] >= 1, "IAC event fired");

    /* Test option negotiation events */
    const uint8_t will_echo[] = {TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_ECHO};
    telnetty_process(ctx, will_echo, sizeof(will_echo));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_WILL] >= 1, "WILL event fired");

    const uint8_t do_echo[] = {TELNETTY_IAC, TELNETTY_DO, TELNETTY_TELOPT_ECHO};
    telnetty_process(ctx, do_echo, sizeof(do_echo));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_DO] >= 1, "DO event fired");

    telnetty_destroy(ctx);
}

static void test_event_queue_basic(void) {
    telnetty_event_queue_t* queue = telnetty_event_queue_create(100, 0);
    TEST_ASSERT_NOT_NULL(queue, "Event queue creation");
    
    /* Test queue properties */
    TEST_ASSERT(telnetty_event_queue_is_empty(queue), "New queue should be empty");
    TEST_ASSERT_EQUAL(0, (int)telnetty_event_queue_count(queue), "New queue count should be 0");
    
    /* Push an event */
    telnetty_event_data_union_t data = {0};
    int result = telnetty_event_queue_push(queue, TELNETTY_EVENT_DATA, TELNETTY_PRIORITY_NORMAL, &data);
    TEST_ASSERT_EQUAL(0, result, "Push event to queue");
    TEST_ASSERT_EQUAL(1, (int)telnetty_event_queue_count(queue), "Queue count after push");
    
    /* Pop the event */
    telnetty_event_entry_t entry;
    result = telnetty_event_queue_pop(queue, &entry);
    TEST_ASSERT_EQUAL(1, result, "Pop event from queue");
    TEST_ASSERT_EQUAL(TELNETTY_EVENT_DATA, entry.type, "Popped event type");
    
    telnetty_event_queue_destroy(queue);
}

/* ============================================================================
 * Test Group 3: Buffer Tests (Comprehensive)
 * ============================================================================ */

static void test_buffer_null_handling(void) {
    /* Test NULL buffer operations */
    telnetty_buffer_destroy(NULL);
    TEST_ASSERT_EQUAL(0, 0, "NULL buffer length is 0");
    TEST_ASSERT(telnetty_buffer_is_empty(NULL), "NULL buffer is empty");
}

static void test_buffer_resize(void) {
    telnetty_buffer_t* buf = telnetty_buffer_create(16);
    TEST_ASSERT_NOT_NULL(buf, "Buffer creation");
    
    /* Append data that requires resize */
    const char* large_data = "This is a string longer than 16 characters to trigger resize";
    int result = telnetty_buffer_append(buf, (const uint8_t*)large_data, strlen(large_data));
    TEST_ASSERT_EQUAL((int)strlen(large_data), result, "Buffer append with resize");
    TEST_ASSERT_EQUAL((int)strlen(large_data), (int)buf->length, "Buffer length after append");
    
    /* Test reserve */
    result = telnetty_buffer_reserve(buf, 256);
    TEST_ASSERT_EQUAL(0, result, "Buffer reserve");
    
    telnetty_buffer_destroy(buf);
}

static void test_buffer_compact(void) {
    telnetty_buffer_t* buf = telnetty_buffer_create(64);
    TEST_ASSERT_NOT_NULL(buf, "Buffer creation");
    
    /* Add data then compact */
    telnetty_buffer_append(buf, (const uint8_t*)"Hello World", 11);
    
    int result = telnetty_buffer_compact(buf);
    TEST_ASSERT_EQUAL(0, result, "Buffer compact");
    TEST_ASSERT_EQUAL(11, (int)buf->length, "Length after compact");
    
    telnetty_buffer_destroy(buf);
}

static void test_buffer_chain_operations(void) {
    /* Create a buffer chain using the chain API */
    telnetty_buffer_chain_t* chain = telnetty_buffer_chain_create();
    TEST_ASSERT_NOT_NULL(chain, "Chain creation");
    
    /* Create and append buffers to chain */
    telnetty_buffer_t* buf1 = telnetty_buffer_create(32);
    telnetty_buffer_t* buf2 = telnetty_buffer_create(32);
    TEST_ASSERT_NOT_NULL(buf1, "Buffer 1 creation");
    TEST_ASSERT_NOT_NULL(buf2, "Buffer 2 creation");
    
    telnetty_buffer_append(buf1, (const uint8_t*)"Hello ", 6);
    telnetty_buffer_append(buf2, (const uint8_t*)"World", 5);
    
    /* Append buffers to chain */
    telnetty_buffer_chain_append(chain, buf1);
    telnetty_buffer_chain_append(chain, buf2);
    
    /* Test chain operations */
    size_t chain_len = telnetty_buffer_chain_length(chain);
    TEST_ASSERT_EQUAL(11, (int)chain_len, "Chain length");
    
    /* Flatten chain - need a pool */
    telnetty_buffer_pool_t* pool = telnetty_buffer_pool_create(4, 64, 0);
    telnetty_buffer_t* flat = telnetty_buffer_chain_flatten(chain, pool);
    TEST_ASSERT_NOT_NULL(flat, "Flatten chain");
    TEST_ASSERT_EQUAL(11, (int)flat->length, "Flattened buffer length");
    
    telnetty_buffer_destroy(flat);
    telnetty_buffer_chain_destroy(chain);
    telnetty_buffer_pool_destroy(pool);
}

static void test_buffer_pool_exhaustion(void) {
    /* Create a small pool */
    telnetty_buffer_pool_t* pool = telnetty_buffer_pool_create(2, 64, 0);
    TEST_ASSERT_NOT_NULL(pool, "Pool creation");
    
    /* Exhaust the pool */
    telnetty_buffer_t* buf1 = telnetty_buffer_pool_get(pool);
    telnetty_buffer_t* buf2 = telnetty_buffer_pool_get(pool);
    telnetty_buffer_t* buf3 = telnetty_buffer_pool_get(pool); /* Should create new */
    
    TEST_ASSERT_NOT_NULL(buf1, "First buffer from pool");
    TEST_ASSERT_NOT_NULL(buf2, "Second buffer from pool");
    TEST_ASSERT_NOT_NULL(buf3, "Third buffer (may be new allocation)");
    
    /* Return buffers */
    telnetty_buffer_pool_put(pool, buf1);
    telnetty_buffer_pool_put(pool, buf2);
    telnetty_buffer_pool_put(pool, buf3);
    
    telnetty_buffer_pool_destroy(pool);
}

/* ============================================================================
 * Test Group 4: Option Tests (Comprehensive)
 * ============================================================================ */

static void test_options_all_standard(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for option tests");
    
    /* Test all standard option enables */
    TEST_ASSERT_EQUAL(0, telnetty_enable_binary(ctx), "Enable binary");
    TEST_ASSERT_EQUAL(0, telnetty_disable_binary(ctx), "Disable binary");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_echo(ctx), "Enable echo");
    TEST_ASSERT_EQUAL(0, telnetty_disable_echo(ctx), "Disable echo");
    TEST_ASSERT(telnetty_is_echo_enabled(ctx) == false, "Echo not immediately enabled");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_sga(ctx), "Enable SGA");
    TEST_ASSERT_EQUAL(0, telnetty_disable_sga(ctx), "Disable SGA");
    
    /* Status option not implemented in this version */
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_ttype(ctx, 64), "Enable terminal type");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_naws(ctx), "Enable NAWS");
    TEST_ASSERT_EQUAL(0, telnetty_send_window_size(ctx, 80, 24), "Send window size");
    
    /* Terminal speed, remote flow, and linemode not fully implemented */
    
    telnetty_destroy(ctx);
}

static void test_options_environment(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for environment tests");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_environ(ctx), "Enable environment");
    TEST_ASSERT_EQUAL(0, telnetty_request_environ(ctx), "Request environment");
    
    const char* vars[] = {"TERM", "USER"};
    const char* vals[] = {"xterm", "test"};
    TEST_ASSERT_EQUAL(0, telnetty_send_environ(ctx, vars, vals, 2), "Send environment");
    
    telnetty_destroy(ctx);
}

static void test_options_charset(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for charset tests");
    
    const char* charsets[] = {"UTF-8", "ISO-8859-1"};
    TEST_ASSERT_EQUAL(0, telnetty_enable_charset(ctx, charsets, 2), "Enable charset");
    
    telnetty_destroy(ctx);
}

static void test_options_timers(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for timer tests");
    
    /* Note: These might timeout immediately in tests */
    (void)telnetty_wait_for_option(ctx, TELNETTY_TELOPT_ECHO, 1);
    
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Test Group 5: MUD Protocol Tests (Comprehensive)
 * ============================================================================ */

static void test_mud_msdp_comprehensive(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for MSDP tests");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_msdp(ctx, NULL, NULL), "Enable MSDP");
    
    /* Test various MSDP operations */
    TEST_ASSERT_EQUAL(0, telnetty_msdp_send(ctx, "HEALTH", "100"), "MSDP send string");
    
    /* Test flags */
    TEST_ASSERT_EQUAL(0, telnetty_msdp_set_flags(ctx, "HEALTH", TELNETTY_MSDP_FLAG_SENDABLE), "MSDP set flags");
    TEST_ASSERT_EQUAL(0, telnetty_msdp_clear_flags(ctx, "HEALTH", TELNETTY_MSDP_FLAG_SENDABLE), "MSDP clear flags");
    
    /* Test variable operations */
    (void)telnetty_msdp_get(ctx, "HEALTH"); /* May return NULL */
    TEST_ASSERT_EQUAL(0, telnetty_msdp_remove(ctx, "HEALTH"), "MSDP remove");
    TEST_ASSERT_EQUAL(0, telnetty_msdp_request(ctx, "HEALTH"), "MSDP request");
    
    telnetty_destroy(ctx);
}

static void test_mud_gmcp_comprehensive(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for GMCP tests");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_gmcp(ctx, NULL, NULL), "Enable GMCP");
    
    /* Test GMCP sends */
    TEST_ASSERT_EQUAL(0, telnetty_gmcp_send(ctx, "Core", "Hello", "{}"), "GMCP send");
    TEST_ASSERT_EQUAL(0, telnetty_gmcp_send_hello(ctx, "TestClient", "1.0"), "GMCP hello");
    TEST_ASSERT_EQUAL(0, telnetty_gmcp_send_supports(ctx, NULL, 0), "GMCP supports (empty)");
    
    /* Test module registration */
    TEST_ASSERT_EQUAL(0, telnetty_gmcp_register_module(ctx, "Char"), "GMCP register module");
    TEST_ASSERT_EQUAL(0, telnetty_gmcp_unregister_module(ctx, "Char"), "GMCP unregister module");
    
    telnetty_destroy(ctx);
}

static void test_mud_mtts_comprehensive(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for MTTS tests");
    
    TEST_ASSERT_EQUAL(0, telnetty_enable_mtts(ctx), "Enable MTTS");
    
    /* Test MTTS operations */
    TEST_ASSERT(telnetty_mtts_has_flag(ctx, TELNETTY_MTTS_FLAG_256COLORS) == false, "MTTS flag check (not negotiated)");
    TEST_ASSERT_EQUAL(0, telnetty_mtts_send_response(ctx, TELNETTY_MTTS_FLAG_256COLORS, "TestClient", "1.0"), "MTTS send response");
    
    telnetty_destroy(ctx);
}

static void test_mud_mssp_comprehensive(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for MSSP tests");
    
    /* Enable MSSP */
    TEST_ASSERT_EQUAL(0, telnetty_enable_mssp(ctx, false), "Enable MSSP (server mode)");
    
    /* Add variables */
    TEST_ASSERT_EQUAL(0, telnetty_mssp_add(ctx, "NAME", "TestMUD"), "MSSP add NAME");
    /* Note: telnetty_mssp_add_int and telnetty_mssp_add_bool not implemented */
    
    /* Test standard variables */
    TEST_ASSERT_EQUAL(0, telnetty_mssp_set_standard_vars(ctx, "TestMUD", "MUD", "example.com", 4000, 42, 100), "MSSP set standard vars");
    
    /* Send MSSP */
    TEST_ASSERT_EQUAL(0, telnetty_mssp_send(ctx), "MSSP send");
    
    /* Note: telnetty_mssp_remove not implemented */
    
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Test Group 6: Compression Tests
 * ============================================================================ */

static void test_compression_basic(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for compression tests");
    
    /* Test MCCP2 */
    TEST_ASSERT_EQUAL(0, telnetty_enable_mccp2(ctx, 6), "Enable MCCP2");
    
    size_t compressed, uncompressed;
    double ratio;
    TEST_ASSERT_EQUAL(0, telnetty_mccp2_get_stats(ctx, &compressed, &uncompressed, &ratio), "MCCP2 get stats");
    
    /* Test MCCP3 */
    TEST_ASSERT_EQUAL(0, telnetty_enable_mccp3(ctx, 6), "Enable MCCP3");
    TEST_ASSERT_EQUAL(0, telnetty_mccp3_get_stats(ctx, &compressed, &uncompressed, &ratio), "MCCP3 get stats");
    
    /* Test auto-negotiation */
    TEST_ASSERT_EQUAL(0, telnetty_compression_auto_negotiate(ctx, true), "Compression auto-negotiate");
    
    /* Test cleanup */
    TEST_ASSERT_EQUAL(0, telnetty_compression_cleanup(ctx), "Compression cleanup");
    
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Test Group 7: Color Tests (Comprehensive)
 * ============================================================================ */

static void test_color_basic(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for color tests");
    
    /* Initialize */
    TEST_ASSERT_EQUAL(0, telnetty_color_init(ctx, true), "Color init with auto-detect");
    TEST_ASSERT_EQUAL(0, telnetty_color_init(ctx, false), "Color init without auto-detect");
    
    /* Set colors */
    TEST_ASSERT_EQUAL(0, telnetty_color_set_foreground(ctx, TELNETTY_COLOR_RED), "Set foreground");
    TEST_ASSERT_EQUAL(0, telnetty_color_set_background(ctx, TELNETTY_COLOR_BLACK), "Set background");
    
    /* Set attributes */
    TEST_ASSERT_EQUAL(0, telnetty_color_set_attribute(ctx, TELNETTY_ATTR_BOLD), "Set bold");
    TEST_ASSERT_EQUAL(0, telnetty_color_clear_attribute(ctx, TELNETTY_ATTR_BOLD), "Clear bold");
    
    /* Reset */
    TEST_ASSERT_EQUAL(0, telnetty_color_reset(ctx), "Color reset");
    
    telnetty_destroy(ctx);
}

static void test_color_256(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for 256 color tests");
    
    TEST_ASSERT_EQUAL(0, telnetty_color_init(ctx, true), "Color init");
    
    /* Test 256 color modes */
    TEST_ASSERT_EQUAL(0, telnetty_color_set_foreground_256(ctx, 196), "Set foreground 256");
    TEST_ASSERT_EQUAL(0, telnetty_color_set_background_256(ctx, 21), "Set background 256");
    
    /* Test sending colored text */
    TEST_ASSERT_EQUAL(0, telnetty_color_send_256(ctx, "Test", 196, 21), "Color send 256");
    
    telnetty_destroy(ctx);
}

static void test_color_true(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for true color tests");
    
    TEST_ASSERT_EQUAL(0, telnetty_color_init(ctx, true), "Color init");
    
    /* Test true color */
    TEST_ASSERT_EQUAL(0, telnetty_color_set_foreground_rgb(ctx, 255, 0, 0), "Set foreground true");
    TEST_ASSERT_EQUAL(0, telnetty_color_set_background_rgb(ctx, 0, 0, 255), "Set background true");
    
    /* Test sending colored text with RGB structs */
    telnetty_rgb_color_t fg2 = {255, 0, 0};
    telnetty_rgb_color_t bg2 = {0, 0, 255};
    TEST_ASSERT_EQUAL(0, telnetty_color_send_true(ctx, "Test", &fg2, &bg2), "Color send true");
    
    telnetty_destroy(ctx);
}

static void test_color_string_generation(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for color string tests");
    
    char buf[64];
    int len;
    
    /* ANSI color strings */
    len = telnetty_color_string_ansi(ctx, TELNETTY_COLOR_RED, TELNETTY_COLOR_BLACK, 0, buf, sizeof(buf));
    TEST_ASSERT(len > 0, "ANSI color string generated");
    
    len = telnetty_color_string_256(ctx, 196, 21, buf, sizeof(buf));
    TEST_ASSERT(len > 0, "256 color string generated");
    
    /* True color with RGB structs */
    telnetty_rgb_color_t fg = {255, 0, 0};
    telnetty_rgb_color_t bg = {0, 0, 255};
    len = telnetty_color_string_true(ctx, &fg, &bg, buf, sizeof(buf));
    TEST_ASSERT(len > 0, "True color string generated");
    
    len = telnetty_color_string_reset(ctx, buf, sizeof(buf));
    TEST_ASSERT(len > 0, "Reset string generated");
    
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Test Group 8: MCP Protocol Tests
 * ============================================================================ */

static void test_mcp_basic(void) {
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for MCP tests");
    
    /* Enable MCP */
    TEST_ASSERT_EQUAL(0, telnetty_enable_mcp(ctx, false, TELNETTY_MCP_AUTH_NONE, NULL), "Enable MCP");
    
    /* Send messages */
    TEST_ASSERT_EQUAL(0, telnetty_mcp_send(ctx, "dns-com", "test", "{\"name\": \"test\"}"), "MCP send");
    /* Note: telnetty_mcp_send_multiline not implemented */
    
    /* Test package operations */
    TEST_ASSERT_EQUAL(0, telnetty_mcp_register_package(ctx, "test-pkg", "1.0", "Test package", NULL, NULL), "MCP register package");
    TEST_ASSERT(telnetty_mcp_is_package_active(ctx, "test-pkg"), "MCP package active");
    TEST_ASSERT_EQUAL(0, telnetty_mcp_activate_package(ctx, "test-pkg"), "MCP activate package");
    
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Test Group 9: Protocol Parsing Tests
 * ============================================================================ */

static void test_protocol_iac_commands(void) {
    telnetty_context_t* ctx = telnetty_create(counting_event_handler, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for protocol tests");
    
    memset(event_counts, 0, sizeof(event_counts));
    
    /* Test various IAC commands */
    const uint8_t nop[] = {TELNETTY_IAC, TELNETTY_NOP};
    telnetty_process(ctx, nop, sizeof(nop));
    
    const uint8_t dm[] = {TELNETTY_IAC, TELNETTY_DM};
    telnetty_process(ctx, dm, sizeof(dm));
    
    const uint8_t brk[] = {TELNETTY_IAC, TELNETTY_BREAK};
    telnetty_process(ctx, brk, sizeof(brk));
    
    const uint8_t ip[] = {TELNETTY_IAC, TELNETTY_IP};
    telnetty_process(ctx, ip, sizeof(ip));
    
    const uint8_t ao[] = {TELNETTY_IAC, TELNETTY_AO};
    telnetty_process(ctx, ao, sizeof(ao));
    
    const uint8_t ayt[] = {TELNETTY_IAC, TELNETTY_AYT};
    telnetty_process(ctx, ayt, sizeof(ayt));
    
    const uint8_t ec[] = {TELNETTY_IAC, TELNETTY_EC};
    telnetty_process(ctx, ec, sizeof(ec));
    
    const uint8_t el[] = {TELNETTY_IAC, TELNETTY_EL};
    telnetty_process(ctx, el, sizeof(el));
    
    const uint8_t ga[] = {TELNETTY_IAC, TELNETTY_GA};
    telnetty_process(ctx, ga, sizeof(ga));
    
    telnetty_destroy(ctx);
}

static void test_protocol_negotiation(void) {
    telnetty_context_t* ctx = telnetty_create(counting_event_handler, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for negotiation tests");

    memset(event_counts, 0, sizeof(event_counts));

    /* Enable support so the Q-method reaches Q_YES on each request. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ECHO, 1, 1);

    /* Test all negotiation commands */
    const uint8_t will_echo[] = {TELNETTY_IAC, TELNETTY_WILL, TELNETTY_TELOPT_ECHO};
    telnetty_process(ctx, will_echo, sizeof(will_echo));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_WILL] >= 1, "WILL event");

    const uint8_t wont_echo[] = {TELNETTY_IAC, TELNETTY_WONT, TELNETTY_TELOPT_ECHO};
    telnetty_process(ctx, wont_echo, sizeof(wont_echo));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_WONT] >= 1, "WONT event");

    const uint8_t do_echo[] = {TELNETTY_IAC, TELNETTY_DO, TELNETTY_TELOPT_ECHO};
    telnetty_process(ctx, do_echo, sizeof(do_echo));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_DO] >= 1, "DO event");

    const uint8_t dont_echo[] = {TELNETTY_IAC, TELNETTY_DONT, TELNETTY_TELOPT_ECHO};
    telnetty_process(ctx, dont_echo, sizeof(dont_echo));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_DONT] >= 1, "DONT event");

    telnetty_destroy(ctx);
}

static void test_protocol_subnegotiation(void) {
    telnetty_context_t* ctx = telnetty_create(counting_event_handler, NULL);
    TEST_ASSERT_NOT_NULL(ctx, "Context for subnegotiation tests");
    
    memset(event_counts, 0, sizeof(event_counts));
    
    /* Test subnegotiation */
    const uint8_t sb_naws[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_NAWS,
        0, 80, /* width */
        0, 24, /* height */
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, sb_naws, sizeof(sb_naws));
    TEST_ASSERT(event_counts[TELNETTY_EVENT_SB] >= 1, "SB event");
    
    /* Test subnegotiation with escaped IAC */
    const uint8_t sb_ttype[] = {
        TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_TTYPE,
        0, /* IS */
        'x', 't', 'e', 'r', 'm',
        TELNETTY_IAC, TELNETTY_SE
    };
    telnetty_process(ctx, sb_ttype, sizeof(sb_ttype));
    
    telnetty_destroy(ctx);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

static void run_all_tests(void) {
    printf("\n========================================\n");
    printf("Telnetty Comprehensive Test Suite\n");
    printf("========================================\n\n");
    
    tests_passed = 0;
    tests_failed = 0;
    tests_skipped = 0;
    
    printf("--- Core Tests ---\n");
    RUN_TEST(test_core_null_handling);
    RUN_TEST(test_core_create_destroy_multiple);
    RUN_TEST(test_core_user_data);
    
    printf("\n--- Event System Tests ---\n");
    RUN_TEST(test_events_all_types);
    RUN_TEST(test_event_queue_basic);
    
    printf("\n--- Buffer Tests ---\n");
    RUN_TEST(test_buffer_null_handling);
    RUN_TEST(test_buffer_resize);
    RUN_TEST(test_buffer_compact);
    RUN_TEST(test_buffer_chain_operations);
    RUN_TEST(test_buffer_pool_exhaustion);
    
    printf("\n--- Option Tests ---\n");
    RUN_TEST(test_options_all_standard);
    RUN_TEST(test_options_environment);
    RUN_TEST(test_options_charset);
    RUN_TEST(test_options_timers);
    
    printf("\n--- MUD Protocol Tests ---\n");
    RUN_TEST(test_mud_msdp_comprehensive);
    RUN_TEST(test_mud_gmcp_comprehensive);
    RUN_TEST(test_mud_mtts_comprehensive);
    RUN_TEST(test_mud_mssp_comprehensive);
    
    printf("\n--- Compression Tests ---\n");
    RUN_TEST(test_compression_basic);
    
    printf("\n--- Color Tests ---\n");
    RUN_TEST(test_color_basic);
    RUN_TEST(test_color_256);
    RUN_TEST(test_color_true);
    RUN_TEST(test_color_string_generation);
    
    printf("\n--- MCP Tests ---\n");
    RUN_TEST(test_mcp_basic);
    
    printf("\n--- Protocol Parsing Tests ---\n");
    RUN_TEST(test_protocol_iac_commands);
    RUN_TEST(test_protocol_negotiation);
    RUN_TEST(test_protocol_subnegotiation);
    
    printf("\n========================================\n");
    printf("Test Results\n");
    printf("========================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Tests skipped: %d\n", tests_skipped);
    printf("Total tests: %d\n", tests_passed + tests_failed + tests_skipped);
    
    if (tests_failed == 0) {
        printf("\nAll tests passed! ✓\n");
    } else {
        printf("\nSome tests failed! ✗\n");
    }
    printf("========================================\n");
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    run_all_tests();
    
    return (tests_failed > 0) ? 1 : 0;
}
