/**
 * test_protocols.c — coalesced DATA, STATUS, MSDP, MSSP, MCP lines, MCCP2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TELNETTY_HAVE_ZLIB
#include <zlib.h>
#endif

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_EVENTS_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_MUD_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION
#define TELNETTY_MCP_IMPLEMENTATION
#include "telnetty_core.h"
#include "telnetty_options.h"
#include "telnetty_mud.h"
#include "telnetty_compression.h"
#include "telnetty_mcp.h"

static int fails;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } \
    else printf("  ok: %s\n", msg); \
} while (0)

static int data_events;
static char data_buf[256];
static size_t data_len;

static void on_ev(telnetty_context_t* ctx, telnetty_event_type_t ev,
                  const telnetty_event_data_union_t* data, void* ud) {
    (void)ctx; (void)ud;
    if (ev == TELNETTY_EVENT_DATA && data && data->data.data) {
        data_events++;
        size_t n = data->data.length;
        if (data_len + n < sizeof(data_buf)) {
            memcpy(data_buf + data_len, data->data.data, n);
            data_len += n;
            data_buf[data_len] = '\0';
        }
    }
}

static char msdp_var[64];
static char msdp_val[64];
static char gmcp_mod[64];
static char gmcp_msg[64];
static char gmcp_data[128];
static void on_gmcp(telnetty_context_t* ctx, const char* module,
                    const char* message, const char* data, void* ud) {
    (void)ctx; (void)ud;
    snprintf(gmcp_mod, sizeof gmcp_mod, "%s", module ? module : "");
    snprintf(gmcp_msg, sizeof gmcp_msg, "%s", message ? message : "");
    snprintf(gmcp_data, sizeof gmcp_data, "%s", data ? data : "");
}
static void on_msdp(telnetty_context_t* ctx, const char* var, const char* val, void* ud) {
    (void)ctx; (void)ud;
    snprintf(msdp_var, sizeof(msdp_var), "%s", var ? var : "");
    snprintf(msdp_val, sizeof(msdp_val), "%s", val ? val : "");
}

static void reset_out(telnetty_context_t* ctx) {
    telnetty_buffer_reset(telnetty_get_output_buffer(ctx));
}

static void test_coalesce(void) {
    printf("test_coalesce\n");
    data_events = 0;
    data_len = 0;
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    const uint8_t raw[] = { 'A', 'B', 255, 255, 'C' };
    int n = telnetty_process(ctx, raw, sizeof(raw));
    CHECK(n == 5, "processed 5");
    CHECK(data_events == 3, "AB, escaped FF, C are three events");
    CHECK(data_len == 4 && data_buf[0] == 'A' && data_buf[1] == 'B' &&
          (uint8_t)data_buf[2] == 255 && data_buf[3] == 'C', "payload");
    telnetty_destroy(ctx);
}

static void test_status(void) {
    printf("test_status\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_sga(ctx) == 0, "enable sga");
    CHECK(telnetty_enable_status(ctx) == 0, "enable status");
    uint8_t peer[] = { 255, 253, 3, 255, 253, 5 }; /* DO SGA, DO STATUS */
    telnetty_process(ctx, peer, sizeof(peer));
    reset_out(ctx);
    uint8_t ask[] = { 255, 250, 5, 1, 255, 240 }; /* SB STATUS SEND SE */
    telnetty_process(ctx, ask, sizeof(ask));
    telnetty_buffer_t* b = telnetty_get_output_buffer(ctx);
    int saw_will_sga = 0;
    for (size_t i = 0; i + 2 < b->length; i++) {
        if (b->data[i] == TELNETTY_WILL && b->data[i + 1] == TELNETTY_TELOPT_SGA)
            saw_will_sga = 1;
    }
    CHECK(saw_will_sga, "STATUS IS lists WILL SGA");
    telnetty_destroy(ctx);
}

static void test_msdp(void) {
    printf("test_msdp\n");
    msdp_var[0] = msdp_val[0] = 0;
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_msdp(ctx, on_msdp, NULL) == 0, "enable msdp");
    uint8_t dodo[] = { 255, 253, 69 };
    telnetty_process(ctx, dodo, sizeof(dodo));
    CHECK(telnetty_is_option_enabled(ctx, 69), "msdp on after DO");
    uint8_t sb[] = { 255, 250, 69, 1, 'H', 'P', 2, '1', '0', 255, 240 };
    telnetty_process(ctx, sb, sizeof(sb));
    CHECK(strcmp(msdp_var, "HP") == 0 && strcmp(msdp_val, "10") == 0, "msdp var/val");
    telnetty_destroy(ctx);
}

static int buf_has(telnetty_buffer_t* b, const char* s) {
    size_t n = strlen(s);
    if (!b || b->length < n) return 0;
    for (size_t i = 0; i + n <= b->length; i++) {
        if (memcmp(b->data + i, s, n) == 0) return 1;
    }
    return 0;
}

static void test_gmcp(void) {
    printf("test_gmcp\n");
    gmcp_mod[0] = gmcp_msg[0] = gmcp_data[0] = 0;
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_gmcp(ctx, on_gmcp, NULL) == 0, "enable gmcp");
    /* Server side: the client's DO must not trigger Core.Hello — that is
     * a client -> server message. */
    reset_out(ctx);
    uint8_t dodo[] = { 255, 253, 201 };
    telnetty_process(ctx, dodo, sizeof(dodo));
    telnetty_buffer_t* b = telnetty_get_output_buffer(ctx);
    CHECK(!buf_has(b, "Core.Hello"), "server does not send Core.Hello");
    /* Client side: the server's WILL triggers Core.Hello. */
    {
        telnetty_context_t* cli = telnetty_create(on_ev, NULL);
        telnetty_enable_gmcp(cli, on_gmcp, NULL);
        reset_out(cli);
        uint8_t will[] = { 255, 251, 201 };
        telnetty_process(cli, will, sizeof(will));
        CHECK(buf_has(telnetty_get_output_buffer(cli), "Core.Hello "),
              "hello is Package.Message json");
        telnetty_destroy(cli);
    }
    reset_out(ctx);
    CHECK(telnetty_gmcp_send(ctx, "Room", "Info", "{\"x\":1}") == 0, "gmcp send");
    b = telnetty_get_output_buffer(ctx);
    CHECK(buf_has(b, "Room.Info {\"x\":1}"), "send wire format");
    uint8_t sb[] = {
        255, 250, 201,
        'C','o','m','m','.','C','h','a','n','n','e','l',' ',
        '\"','h','i','\"',
        255, 240
    };
    telnetty_process(ctx, sb, sizeof(sb));
    CHECK(strcmp(gmcp_mod, "Comm") == 0, "gmcp module");
    CHECK(strcmp(gmcp_msg, "Channel") == 0, "gmcp message");
    CHECK(strcmp(gmcp_data, "\"hi\"") == 0, "gmcp json");
    telnetty_destroy(ctx);
}

#ifdef TELNETTY_HAVE_ZLIB
static void test_mccp3_flag(void) {
    printf("test_mccp3_flag\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(!telnetty_is_mccp3_enabled(ctx), "mccp3 off before enable");
    CHECK(telnetty_enable_mccp3(ctx, 6) == 0, "enable mccp3");
    CHECK(!telnetty_is_mccp3_enabled(ctx), "mccp3 waits for DO");
    uint8_t dodo[] = { 255, 253, 87 };
    telnetty_process(ctx, dodo, sizeof(dodo));
    CHECK(telnetty_is_mccp3_enabled(ctx), "mccp3 on after DO");
    telnetty_destroy(ctx);
}
#endif

static void test_mssp(void) {
    printf("test_mssp\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_mssp(ctx, false) == 0, "enable mssp");
    CHECK(telnetty_mssp_add(ctx, "NAME", "Eigen") == 0, "mssp add");
    reset_out(ctx);
    uint8_t dodo[] = { 255, 253, 70 };
    telnetty_process(ctx, dodo, sizeof(dodo));
    /* auto_send is false, so send explicitly */
    CHECK(telnetty_mssp_send(ctx) == 0, "mssp send");
    telnetty_buffer_t* b = telnetty_get_output_buffer(ctx);
    int saw = 0;
    for (size_t i = 0; i + 4 < b->length; i++) {
        if (b->data[i] == 255 && b->data[i + 1] == 250 && b->data[i + 2] == 70)
            saw = 1;
    }
    CHECK(saw, "MSSP subnegotiation emitted");
    telnetty_destroy(ctx);
}

static void test_mcp_line(void) {
    printf("test_mcp_line\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_mcp(ctx, true, TELNETTY_MCP_AUTH_NONE, NULL) == 0, "enable mcp");
    CHECK(telnetty_mcp_get_package_count(ctx) >= 1, "builtin packages registered");
    CHECK(telnetty_mcp_handle_line(ctx, "hello") == 0, "plain line ignored");
    /* Server mode: the client's reply carries the key it chose. */
    CHECK(telnetty_mcp_handle_line(ctx, "#$#mcp authentication-key: k1 version: 2.1 to: 2.1") == 1, "mcp line");
    CHECK(telnetty_mcp_get_state(ctx) == TELNETTY_MCP_STATE_ENABLED, "mcp enabled");
    telnetty_destroy(ctx);
}

static void test_mcp_data_hook(void) {
    printf("test_mcp_data_hook\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_mcp(ctx, true, TELNETTY_MCP_AUTH_NONE, NULL) == 0, "enable mcp");
    const char* a = "#$#mcp authentication-key: k1 ver";
    const char* b = "sion: 2.1 to: 2.1\r\nplain\n";
    telnetty_process(ctx, (const uint8_t*)a, strlen(a));
    CHECK(telnetty_mcp_get_state(ctx) != TELNETTY_MCP_STATE_ENABLED, "partial line held");
    telnetty_process(ctx, (const uint8_t*)b, strlen(b));
    CHECK(telnetty_mcp_get_state(ctx) == TELNETTY_MCP_STATE_ENABLED, "enabled via DATA hook");
    telnetty_destroy(ctx);
}

#ifdef TELNETTY_HAVE_ZLIB
static void test_mccp2(void) {
    printf("test_mccp2\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(telnetty_enable_mccp2(ctx, 6) == 0, "enable mccp2");
    uint8_t dodo[] = { 255, 253, 86 };
    telnetty_process(ctx, dodo, sizeof(dodo));
    CHECK(telnetty_is_mccp2_enabled(ctx), "compressing after DO");
    telnetty_buffer_t* b = telnetty_get_output_buffer(ctx);
    /* Find the start marker and inflate what follows. */
    size_t mark = 0;
    int found = 0;
    for (size_t i = 0; i + 4 < b->length; i++) {
        if (b->data[i] == 255 && b->data[i + 1] == 250 && b->data[i + 2] == 86 &&
            b->data[i + 3] == 255 && b->data[i + 4] == 240) {
            mark = i + 5;
            found = 1;
            break;
        }
    }
    CHECK(found, "COMPRESS2 start marker");
    const char* msg = "HelloHelloHello";
    size_t before = b->length;
    CHECK(telnetty_send(ctx, (const uint8_t*)msg, strlen(msg)) > 0, "send under mccp");
    CHECK(b->length > before, "compressed bytes appended");
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    CHECK(inflateInit(&strm) == Z_OK, "inflateInit");
    unsigned char out[64];
    strm.next_in = b->data + mark;
    strm.avail_in = (uInt)(b->length - mark);
    strm.next_out = out;
    strm.avail_out = sizeof(out);
    int ir = inflate(&strm, Z_SYNC_FLUSH);
    size_t got = sizeof(out) - strm.avail_out;
    inflateEnd(&strm);
    CHECK(ir == Z_OK || ir == Z_BUF_ERROR, "inflate");
    CHECK(got == strlen(msg) && memcmp(out, msg, got) == 0, "round-trip plaintext");
    telnetty_destroy(ctx);
}
#endif

int main(void) {
    test_coalesce();
    test_status();
    test_msdp();
    test_gmcp();
#ifdef TELNETTY_HAVE_ZLIB
    test_mccp3_flag();
#endif
    test_mssp();
    test_mcp_line();
    test_mcp_data_hook();
#ifdef TELNETTY_HAVE_ZLIB
    test_mccp2();
#endif
    printf("%s (%d failed)\n", fails ? "FAILED" : "OK", fails);
    return fails ? 1 : 0;
}
