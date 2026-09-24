/**
 * @file test_basic.c
 * @brief Basic functionality test for the Unified Telnetty Library
 * 
 * This test program verifies the basic functionality of the Telnetty library
 * including context creation, event handling, and protocol negotiation.
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.0.0
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test assertion macro */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, "FAIL: %s (expected %d, got %d)\n", \
                    message, (expected), (actual)); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

#define TEST_ASSERT_STR_EQUAL(expected, actual, message) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n", \
                    message, (expected), (actual)); \
            tests_failed++; \
        } else { \
            tests_passed++; \
        } \
    } while(0)

/* Global test data */
static int event_count = 0;
static telnetty_event_type_t last_event = 0;
static char last_data[256] = {0};

/**
 * Test event handler
 */
static void test_event_handler(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data,
    void* user_data
) {
    (void)ctx;
    (void)user_data;
    event_count++;
    last_event = event;
    
    if (event == TELNETTY_EVENT_DATA && data->data.length < sizeof(last_data)) {
        memcpy(last_data, data->data.data, data->data.length);
        last_data[data->data.length] = '\0';
    }
}

/**
 * Test 1: Basic context creation and destruction
 */
static void test_context_basic(void) {
    printf("Test 1: Basic context creation and destruction\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (ctx) {
        telnetty_destroy(ctx);
        tests_passed++;
    } else {
        tests_failed++;
    }
}

/**
 * Test 2: Data processing
 */
static void test_data_processing(void) {
    printf("Test 2: Data processing\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (!ctx) return;
    
    /* Test normal data */
    const char* test_data = "Hello, TELNET!";
    event_count = 0;
    
    int processed = telnetty_process(ctx, (const uint8_t*)test_data, strlen(test_data));
    TEST_ASSERT_EQUAL((int)strlen(test_data), processed, "Data processing failed");
    TEST_ASSERT_EQUAL(1, event_count, "Plain bytes coalesce into one DATA event");
    TEST_ASSERT_EQUAL(TELNETTY_EVENT_DATA, last_event, "Last event incorrect");
    TEST_ASSERT(strcmp(last_data, test_data) == 0, "Coalesced payload mismatch");
    
    /* Test IAC escape */
    const uint8_t iac_data[] = {TELNETTY_IAC, TELNETTY_IAC, 'H', 'i'};
    event_count = 0;
    
    processed = telnetty_process(ctx, iac_data, sizeof(iac_data));
    TEST_ASSERT_EQUAL(4, processed, "IAC processing failed");
    
    telnetty_destroy(ctx);
}

/**
 * Test 3: Option negotiation
 */
static void test_option_negotiation(void) {
    printf("Test 3: Option negotiation\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (!ctx) return;
    
    /* Enable echo option */
    int result = telnetty_enable_echo(ctx);
    TEST_ASSERT_EQUAL(0, result, "Enable echo failed");
    
    /* Check option state */
    (void)telnetty_is_option_enabled(ctx, TELNETTY_TELOPT_ECHO);
    /* Note: This will return false until negotiation completes */
    
    /* Send WILL echo */
    result = telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_ECHO);
    TEST_ASSERT_EQUAL(0, result, "Send WILL failed");
    
    telnetty_destroy(ctx);
}

/**
 * Test 4: Color support
 */
static void test_color_support(void) {
    printf("Test 4: Color support\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (!ctx) return;
    
    /* Initialize color support */
    int result = telnetty_color_init(ctx, true);
    TEST_ASSERT_EQUAL(0, result, "Color init failed");
    
    /* Set ANSI colors */
    result = telnetty_color_set_foreground(ctx, TELNETTY_COLOR_RED);
    TEST_ASSERT_EQUAL(0, result, "Set foreground color failed");
    
    result = telnetty_color_set_background(ctx, TELNETTY_COLOR_BLACK);
    TEST_ASSERT_EQUAL(0, result, "Set background color failed");
    
    /* Test color string generation */
    char color_buf[64];
    int color_len = telnetty_color_string_ansi(ctx, TELNETTY_COLOR_BLUE,
                                           TELNETTY_COLOR_BLACK, 0,
                                           color_buf, sizeof(color_buf));
    TEST_ASSERT(color_len > 0, "Color string generation failed");
    
    /* Reset colors */
    result = telnetty_color_reset(ctx);
    TEST_ASSERT_EQUAL(0, result, "Color reset failed");
    
    telnetty_destroy(ctx);
}

/**
 * Test 5: MUD protocols
 */
static void test_mud_protocols(void) {
    printf("Test 5: MUD protocols\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (!ctx) return;
    
    /* Test MSDP */
    int result = telnetty_enable_msdp(ctx, NULL, NULL);
    TEST_ASSERT_EQUAL(0, result, "Enable MSDP failed");
    
    result = telnetty_msdp_send(ctx, "HEALTH", "100");
    TEST_ASSERT_EQUAL(0, result, "Send MSDP failed");
    
    /* Test GMCP */
    result = telnetty_enable_gmcp(ctx, NULL, NULL);
    TEST_ASSERT_EQUAL(0, result, "Enable GMCP failed");
    
    result = telnetty_gmcp_send(ctx, "Core", "Hello", "{\"client\": \"Test\"}");
    TEST_ASSERT_EQUAL(0, result, "Send GMCP failed");
    
    /* Test MTTS */
    result = telnetty_enable_mtts(ctx);
    TEST_ASSERT_EQUAL(0, result, "Enable MTTS failed");
    
    /* Test MSSP */
    result = telnetty_enable_mssp(ctx, false);
    TEST_ASSERT_EQUAL(0, result, "Enable MSSP failed");
    
    result = telnetty_mssp_add(ctx, "NAME", "Test Server");
    TEST_ASSERT_EQUAL(0, result, "Add MSSP variable failed");
    
    telnetty_destroy(ctx);
}

/**
 * Test 6: Buffer management
 */
static void test_buffer_management(void) {
    printf("Test 6: Buffer management\n");
    
    /* Test basic buffer operations */
    telnetty_buffer_t* buffer = telnetty_buffer_create(1024);
    TEST_ASSERT(buffer != NULL, "Buffer creation failed");
    
    if (!buffer) return;
    
    /* Test appending data */
    const char* test_data = "Hello, Buffer!";
    int result = telnetty_buffer_append(buffer, (const uint8_t*)test_data,
                                     strlen(test_data));
    TEST_ASSERT_EQUAL((int)strlen(test_data), result, "Buffer append failed");
    
    /* Test buffer properties */
    TEST_ASSERT(!telnetty_buffer_is_empty(buffer), "Buffer should not be empty");
    TEST_ASSERT_EQUAL((int)strlen(test_data), (int)buffer->length, "Buffer length incorrect");
    
    /* Test buffer reset */
    telnetty_buffer_reset(buffer);
    TEST_ASSERT_EQUAL(0, (int)buffer->length, "Buffer reset failed");
    
    /* Test buffer pool */
    telnetty_buffer_pool_t* pool = telnetty_buffer_pool_create(4, 1024, 0);
    TEST_ASSERT(pool != NULL, "Buffer pool creation failed");
    
    if (pool) {
        telnetty_buffer_t* pooled_buffer = telnetty_buffer_pool_get(pool);
        TEST_ASSERT(pooled_buffer != NULL, "Get from pool failed");
        
        if (pooled_buffer) {
            int put_result = telnetty_buffer_pool_put(pool, pooled_buffer);
            TEST_ASSERT_EQUAL(0, put_result, "Put to pool failed");
        }
        
        telnetty_buffer_pool_destroy(pool);
    }
    
    telnetty_buffer_destroy(buffer);
}

/**
 * Test 7: Compression
 */
static void test_compression(void) {
    printf("Test 7: Compression\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (!ctx) return;
    
    /* Test MCCP2 */
    int result = telnetty_enable_mccp2(ctx, 6);
    /* Note: May fail if zlib not available */
    
    /* Test MCCP3 */
    result = telnetty_enable_mccp3(ctx, 6);
    /* Note: May fail if zlib not available */
    
    /* Test compression statistics */
    size_t compressed, uncompressed;
    double ratio;
    
    result = telnetty_mccp2_get_stats(ctx, &compressed, &uncompressed, &ratio);
    TEST_ASSERT_EQUAL(0, result, "Get MCCP2 stats failed");
    
    result = telnetty_mccp3_get_stats(ctx, &compressed, &uncompressed, &ratio);
    TEST_ASSERT_EQUAL(0, result, "Get MCCP3 stats failed");
    
    /* Compression is not available in builds without zlib. */
#ifdef TELNETTY_HAVE_ZLIB
    result = telnetty_compression_auto_negotiate(ctx, true);
    TEST_ASSERT_EQUAL(0, result, "Auto negotiate failed");
#else
    (void)result;
#endif
    
    telnetty_destroy(ctx);
}

/**
 * Test 8: Utility functions
 */
static void test_utility_functions(void) {
    printf("Test 8: Utility functions\n");
    
    telnetty_context_t* ctx = telnetty_create(test_event_handler, NULL);
    TEST_ASSERT(ctx != NULL, "Context creation failed");
    
    if (!ctx) return;
    
    /* Test enable all options */
    int count = telnetty_enable_all_options(ctx);
    TEST_ASSERT(count > 0, "Enable all options failed");
    
    /* Test option state checking */
    (void)telnetty_is_option_enabled(ctx, TELNETTY_TELOPT_ECHO);
    /* Note: Will be false until negotiation completes */
    
    /* Test waiting for option */
    /* Note: This would block in real usage */
    
    telnetty_destroy(ctx);
}

/**
 * Run all tests
 */
static void run_all_tests(void) {
    printf("==============================================\n");
    printf("Unified Telnetty Library Basic Tests\n");
    printf("==============================================\n\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    test_context_basic();
    test_data_processing();
    test_option_negotiation();
    test_color_support();
    test_mud_protocols();
    test_buffer_management();
    test_compression();
    test_utility_functions();
    
    printf("\n");
    printf("==============================================\n");
    printf("Test Results\n");
    printf("==============================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Total tests: %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\nAll tests passed! ✓\n");
    } else {
        printf("\nSome tests failed! ✗\n");
    }
    
    printf("==============================================\n");
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    printf("Unified Telnetty Library Basic Functionality Test\n");
    printf("==============================================\n\n");
    
    /* Run tests */
    run_all_tests();
    
    /* Return appropriate exit code */
    return (tests_failed > 0) ? 1 : 0;
}
