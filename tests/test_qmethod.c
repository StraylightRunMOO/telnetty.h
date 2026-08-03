/**
 * test_qmethod.c — RFC 1143 Q-method + CHARSET once-only tests.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#include "telnetty_core.h"
#include "telnetty_options.h"

static int g_events;
static telnetty_event_type_t g_last_ev;

static void on_ev(telnetty_context_t* ctx, telnetty_event_type_t ev,
                  const telnetty_event_data_union_t* data, void* ud) {
    (void)ctx; (void)data; (void)ud;
    g_events++;
    g_last_ev = ev;
}

static int fail_count;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); fail_count++; } \
    else { printf("  ok: %s\n", msg); } \
} while (0)

/* Count IAC WILL/DO/DONT/WONT + SB sequences in out_buffer */
static int count_iac_cmds(telnetty_context_t* ctx, uint8_t cmd) {
    telnetty_buffer_t* b = telnetty_get_output_buffer(ctx);
    if (!b) return 0;
    int n = 0;
    for (size_t i = 0; i + 2 < b->length; i++) {
        if (b->data[i] == TELNETTY_IAC && b->data[i+1] == cmd)
            n++;
    }
    return n;
}

static int count_sb(telnetty_context_t* ctx) {
    return count_iac_cmds(ctx, TELNETTY_SB);
}

static void reset_out(telnetty_context_t* ctx) {
    telnetty_buffer_reset(telnetty_get_output_buffer(ctx));
}

static void test_qmethod_no_redo(void) {
    printf("test_qmethod_no_redo\n");
    g_events = 0;
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    CHECK(ctx != NULL, "create");

    telnetty_set_option_support(ctx, TELNETTY_TELOPT_SGA, 1, 1);
    telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_SGA);
    CHECK(count_iac_cmds(ctx, TELNETTY_WILL) == 1, "first WILL sent");

    /* Second WILL of same option must be silent */
    size_t before = telnetty_get_output_buffer(ctx)->length;
    telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_SGA);
    CHECK(telnetty_get_output_buffer(ctx)->length == before, "re-WILL is no-op");

    /* Peer DO confirms — us becomes YES, no further WILL */
    reset_out(ctx);
    unsigned char do_sga[] = { 255, 253, 3 }; /* IAC DO SGA */
    telnetty_process(ctx, do_sga, 3);
    CHECK(telnetty_is_option_enabled(ctx, TELNETTY_TELOPT_SGA), "SGA enabled after DO");
    CHECK(count_iac_cmds(ctx, TELNETTY_WILL) == 0, "no WILL on confirm DO");

    /* Peer re-DO must not re-WILL */
    reset_out(ctx);
    telnetty_process(ctx, do_sga, 3);
    CHECK(telnetty_get_output_buffer(ctx)->length == 0, "re-DO is silent");

    telnetty_destroy(ctx);
}

static void test_refuse_unsupported(void) {
    printf("test_refuse_unsupported\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    /* No support set for ECHO */
    unsigned char will_echo[] = { 255, 251, 1 }; /* IAC WILL ECHO */
    reset_out(ctx);
    telnetty_process(ctx, will_echo, 3);
    CHECK(count_iac_cmds(ctx, TELNETTY_DONT) == 1, "refuse unsupported WILL with DONT");
    telnetty_destroy(ctx);
}

static void test_charset_once(void) {
    printf("test_charset_once\n");
    telnetty_context_t* ctx = telnetty_create(on_ev, NULL);
    const char* sets[] = { "UTF-8" };
    CHECK(telnetty_enable_charset(ctx, sets, 1) == 0, "enable_charset");
    CHECK(count_iac_cmds(ctx, TELNETTY_WILL) == 1, "WILL CHARSET once");

    /* Peer DO CHARSET — triggers one REQUEST */
    reset_out(ctx);
    unsigned char do_cs[] = { 255, 253, 42 };
    telnetty_process(ctx, do_cs, 3);
    CHECK(count_sb(ctx) == 1, "one CHARSET REQUEST after DO");

    /* Peer re-DO — must not re-REQUEST */
    reset_out(ctx);
    telnetty_process(ctx, do_cs, 3);
    CHECK(telnetty_get_output_buffer(ctx)->length == 0, "re-DO CHARSET silent");

    /* Peer ACCEPTED UTF-8 */
    unsigned char acc[] = {
        255, 250, 42, 2, 'U','T','F','-','8', 255, 240
    };
    telnetty_process(ctx, acc, sizeof(acc));
    const char* sel = telnetty_get_charset(ctx);
    CHECK(sel && strcmp(sel, "UTF-8") == 0, "selected UTF-8");

    /* Peer re-REQUEST — must not re-ACCEPT */
    reset_out(ctx);
    unsigned char req[] = {
        255, 250, 42, 1, ';', 'U','T','F','-','8', 255, 240
    };
    telnetty_process(ctx, req, sizeof(req));
    CHECK(telnetty_get_output_buffer(ctx)->length == 0, "re-REQUEST silent after done");

    /* Peer WILL CHARSET after we're done — DO may fire if him was NO;
     * REQUEST must still be suppressed. */
    reset_out(ctx);
    unsigned char will_cs[] = { 255, 251, 42 };
    telnetty_process(ctx, will_cs, 3);
    CHECK(count_sb(ctx) == 0, "no second REQUEST on WILL after complete");

    telnetty_destroy(ctx);
}

static uint8_t g_sb_opt;
static size_t g_sb_len;

static void on_sb(telnetty_context_t* c, telnetty_event_type_t ev,
                  const telnetty_event_data_union_t* d, void* ud) {
    (void)c; (void)ud;
    if (ev == TELNETTY_EVENT_SB) {
        g_sb_opt = d->sub.option;
        g_sb_len = d->sub.length;
    }
}

static void test_sb_option_byte(void) {
    printf("test_sb_option_byte\n");
    g_sb_opt = 0; g_sb_len = 0;
    telnetty_context_t* ctx = telnetty_create(on_sb, NULL);
    /* IAC SB NAWS 0 80 0 24 IAC SE */
    unsigned char naws[] = { 255, 250, 31, 0, 80, 0, 24, 255, 240 };
    telnetty_process(ctx, naws, sizeof(naws));
    CHECK(g_sb_opt == 31, "SB option is NAWS (31)");
    CHECK(g_sb_len == 4, "SB payload is 4 bytes (no option byte)");
    telnetty_destroy(ctx);
}

int main(void) {
    fail_count = 0;
    test_qmethod_no_redo();
    test_refuse_unsupported();
    test_charset_once();
    test_sb_option_byte();
    printf("\n%d failure(s)\n", fail_count);
    return fail_count ? 1 : 0;
}
