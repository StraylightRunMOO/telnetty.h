/**
 * @file server_libuv.c
 * @brief Telnetty on a libuv event loop
 *
 * A small, complete server showing the pieces every libuv integration
 * needs:
 *
 * - one telnetty context per connection, owned by the loop's thread
 * - reads fed to telnetty_process(), which also inflates MCCP3 input
 * - everything telnetty queued (echo, negotiation, MCCP2-compressed output)
 *   copied into a uv_write request after each read, then the buffer flushed
 * - telnetty_tick() driven from a uv_timer for negotiation timeouts
 * - idle timeouts, Ctrl+C shutdown, and teardown that frees everything
 *
 * libuv handles belong to the loop that created them. To use several cores,
 * run one loop per thread, each with its own listening socket
 * (SO_REUSEPORT) or accepted sockets handed over with uv_pipe IPC; never
 * touch a context or handle from another thread.
 *
 * Usage: server_libuv [port]   (default 4000)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <uv.h>

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_MUD_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION
#define TELNETTY_COLOR_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_options.h"
#include "telnetty_mud.h"
#include "telnetty_compression.h"
#include "telnetty_color.h"

#define DEFAULT_PORT        4000
#define IDLE_TIMEOUT_MS     (5 * 60 * 1000)
#define TICK_INTERVAL_MS    250
#define NEGOTIATION_TIMEOUT 5000

typedef struct client {
    uv_tcp_t handle;
    telnetty_context_t* telnet;
    uint64_t last_activity_ms;
    char name[64];
    struct client* next;
} client_t;

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

static uv_loop_t* loop;
static uv_tcp_t server;
static uv_timer_t tick_timer;
static uv_signal_t sigint_handle;
static client_t* clients;

/* ------------------------------------------------------------------------ */

static void close_client(client_t* c);

static void on_write_done(uv_write_t* req, int status) {
    write_req_t* wr = (write_req_t*)req;
    if (status < 0 && status != UV_ECANCELED)
        fprintf(stderr, "write error: %s\n", uv_strerror(status));
    free(wr->buf.base);
    free(wr);
}

/* Hand everything telnetty queued to libuv, then reset the buffer. uv_write
 * keeps its own copy of the bytes until the write completes, so the
 * telnetty buffer can be reused immediately. */
static void flush_output(client_t* c) {
    telnetty_buffer_t* out = telnetty_get_output_buffer(c->telnet);
    if (!out || out->length == 0) return;

    write_req_t* wr = (write_req_t*)malloc(sizeof(*wr));
    char* copy = (char*)malloc(out->length);
    if (!wr || !copy) {
        free(wr);
        free(copy);
        close_client(c);
        return;
    }
    memcpy(copy, out->data, out->length);
    wr->buf = uv_buf_init(copy, (unsigned int)out->length);
    telnetty_flush(c->telnet);

    int rc = uv_write(&wr->req, (uv_stream_t*)&c->handle, &wr->buf, 1, on_write_done);
    if (rc < 0) {
        fprintf(stderr, "uv_write: %s\n", uv_strerror(rc));
        free(copy);
        free(wr);
        close_client(c);
    }
}

static void send_text(client_t* c, const char* text) {
    telnetty_send(c->telnet, (const uint8_t*)text, strlen(text));
}

/* ------------------------------------------------------------------------ */

static void on_telnet_event(telnetty_context_t* ctx, telnetty_event_type_t event,
                            const telnetty_event_data_union_t* data, void* user_data) {
    client_t* c = (client_t*)user_data;
    switch (event) {
    case TELNETTY_EVENT_DATA:
        /* Echo the input back. Telnetty escapes any IAC bytes for us. */
        telnetty_send(ctx, data->data.data, data->data.length);
        break;
    case TELNETTY_EVENT_ERROR:
        fprintf(stderr, "[%s] telnet error %d: %s\n", c->name,
                data->error.code, data->error.message ? data->error.message : "");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */

static void on_closed(uv_handle_t* handle) {
    client_t* c = (client_t*)handle->data;
    printf("[%s] disconnected\n", c->name);
    telnetty_destroy(c->telnet);
    free(c);
}

static void close_client(client_t* c) {
    if (uv_is_closing((uv_handle_t*)&c->handle)) return;
    for (client_t** p = &clients; *p; p = &(*p)->next) {
        if (*p == c) { *p = c->next; break; }
    }
    uv_close((uv_handle_t*)&c->handle, on_closed);
}

static void alloc_read(uv_handle_t* handle, size_t suggested, uv_buf_t* buf) {
    (void)handle;
    buf->base = (char*)malloc(suggested);
    buf->len = buf->base ? suggested : 0;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    client_t* c = (client_t*)stream->data;
    if (nread > 0) {
        c->last_activity_ms = uv_now(loop);
        if (telnetty_process(c->telnet, (const uint8_t*)buf->base, (size_t)nread) < 0) {
            /* Corrupt compression stream or similar: already reported
             * through EVENT_ERROR. Send what we have and hang up. */
            flush_output(c);
            close_client(c);
        } else {
            flush_output(c);
        }
    } else if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "[%s] read error: %s\n", c->name, uv_strerror((int)nread));
        close_client(c);
    }
    free(buf->base);
}

static void on_connection(uv_stream_t* listener, int status) {
    if (status < 0) {
        fprintf(stderr, "accept error: %s\n", uv_strerror(status));
        return;
    }
    client_t* c = (client_t*)calloc(1, sizeof(*c));
    if (!c) return;
    uv_tcp_init(loop, &c->handle);
    c->handle.data = c;
    if (uv_accept(listener, (uv_stream_t*)&c->handle) < 0) {
        uv_close((uv_handle_t*)&c->handle, (uv_close_cb)free);
        return;
    }

    struct sockaddr_storage addr;
    int len = (int)sizeof(addr);
    snprintf(c->name, sizeof(c->name), "client");
    if (uv_tcp_getpeername(&c->handle, (struct sockaddr*)&addr, &len) == 0)
        uv_ip_name((struct sockaddr*)&addr, c->name, sizeof(c->name));

    c->telnet = telnetty_create(on_telnet_event, c);
    if (!c->telnet) {
        uv_close((uv_handle_t*)&c->handle, (uv_close_cb)free);
        return;
    }
    c->last_activity_ms = uv_now(loop);
    c->next = clients;
    clients = c;

    /* Offer the usual server-side options. Telnetty's RFC 1143 negotiation
     * answers the peer; nothing else is needed for these. */
    telnetty_set_negotiation_timeout(c->telnet, NEGOTIATION_TIMEOUT);
    telnetty_enable_echo(c->telnet);
    telnetty_enable_sga(c->telnet);
    telnetty_enable_naws(c->telnet);
    telnetty_enable_ttype(c->telnet, 0);
    telnetty_compression_auto_negotiate(c->telnet, true);   /* MCCP2 + MCCP3 */

    send_text(c, "Welcome to the telnetty libuv echo server.\r\n");
    flush_output(c);

    uv_read_start((uv_stream_t*)&c->handle, alloc_read, on_read);
    printf("[%s] connected\n", c->name);
}

/* ------------------------------------------------------------------------ */

static void on_tick(uv_timer_t* timer) {
    (void)timer;
    uint64_t now = uv_now(loop);
    client_t* c = clients;
    while (c) {
        client_t* next = c->next;
        telnetty_tick(c->telnet, now);
        if (now - c->last_activity_ms > IDLE_TIMEOUT_MS) {
            send_text(c, "\r\nIdle timeout. Goodbye.\r\n");
            telnetty_compression_cleanup(c->telnet);   /* finish the MCCP2 stream */
            flush_output(c);
            close_client(c);
        } else {
            flush_output(c);   /* anything tick queued (timeout errors) */
        }
        c = next;
    }
}

static void on_signal(uv_signal_t* handle, int signum) {
    (void)signum;
    printf("\nshutting down\n");
    while (clients) {
        client_t* c = clients;
        send_text(c, "\r\nServer shutting down.\r\n");
        telnetty_compression_cleanup(c->telnet);
        flush_output(c);
        close_client(c);
    }
    uv_close((uv_handle_t*)&server, NULL);
    uv_close((uv_handle_t*)&tick_timer, NULL);
    uv_close((uv_handle_t*)handle, NULL);
}

int main(int argc, char** argv) {
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 1;
    }

    loop = uv_default_loop();

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);
    uv_tcp_init(loop, &server);
    int rc = uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    if (rc == 0) rc = uv_listen((uv_stream_t*)&server, 128, on_connection);
    if (rc < 0) {
        fprintf(stderr, "cannot listen on port %d: %s\n", port, uv_strerror(rc));
        return 1;
    }

    uv_timer_init(loop, &tick_timer);
    uv_timer_start(&tick_timer, on_tick, TICK_INTERVAL_MS, TICK_INTERVAL_MS);
    uv_signal_init(loop, &sigint_handle);
    uv_signal_start(&sigint_handle, on_signal, SIGINT);

    printf("telnetty %s libuv echo server on port %d (Ctrl+C to stop)\n",
           TELNETTY_VERSION_STRING, port);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
    return 0;
}
