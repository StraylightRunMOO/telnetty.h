/*
 * Small Telnet echo server using Telnetty, Suspenders, and Memento.
 *
 * Build with -DBUILD_SUSPENDERS_EXAMPLE=ON and point CMake at the include
 * directories from the two optional dependencies. One Suspenders worker is
 * intentional: a Telnetty context belongs to one OS thread for its lifetime.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define MEMENTO_IMPLEMENTATION
#include "memento.h"
#define SUSPENDERS_IMPLEMENTATION
#include "suspenders.h"

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_EVENTS_IMPLEMENTATION
#define TELNETTY_BUFFER_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#include "telnetty_core.h"
#include "telnetty_events.h"
#include "telnetty_buffer.h"
#include "telnetty_options.h"

#define LISTEN_URI "tcp://0.0.0.0:4001"
#define IO_SIZE 4096

static volatile sig_atomic_t running = 1;

static void stop_server(int signal_number) {
    (void)signal_number;
    running = 0;
}

static void on_telnet_event(telnetty_context_t *ctx,
                            telnetty_event_type_t event,
                            const telnetty_event_data_union_t *data,
                            void *user_data) {
    (void)user_data;
    if (event == TELNETTY_EVENT_DATA && data && data->data.length)
        (void)telnetty_send(ctx, data->data.data, data->data.length);
}

static int write_pending(suspenders_hose_t *hose, telnetty_context_t *ctx) {
    telnetty_buffer_t *out = telnetty_get_output_buffer(ctx);
    size_t offset = 0;
    while (out && offset < out->length) {
        ssize_t n = suspenders_hose_write(hose, out->data + offset,
                                          out->length - offset);
        if (n <= 0) return -1;
        offset += (size_t)n;
    }
    return telnetty_flush(ctx);
}

static void serve_client(void *arg) {
    suspenders_hose_t *hose = (suspenders_hose_t *)arg;
    telnetty_context_t *ctx = telnetty_create(on_telnet_event, NULL);
    unsigned char input[IO_SIZE];

    if (!ctx) goto done;
    while (running) {
        ssize_t n = suspenders_hose_read_dl(hose, input, sizeof(input),
                suspenders_now_ns() + 100000000ULL);
        if (n < 0 && suspenders_errno == SUSPENDERS_TIMEDOUT) continue;
        if (n <= 0) break;
        if (telnetty_process(ctx, input, (size_t)n) < 0 ||
            write_pending(hose, ctx) < 0)
            break;
    }
    telnetty_destroy(ctx);

done:
    suspenders_hose_close(hose);
    memento_thread_heap_free(memento_thread_heap_get(), hose,
                             sizeof(*hose));
}

static void accept_clients(void *arg) {
    suspenders_hose_t *listener = (suspenders_hose_t *)arg;
    while (running) {
        memento_thread_heap_t *heap = memento_thread_heap_get();
        suspenders_hose_t *client = (suspenders_hose_t *)
            memento_thread_heap_alloc(heap, sizeof(*client));
        if (!client) break;
        suspenders_hose_init(client, NULL);
        if (!suspenders_hose_accept_dl(listener, client,
                suspenders_now_ns() + 100000000ULL)) {
            memento_thread_heap_free(heap, client, sizeof(*client));
            if (suspenders_errno == SUSPENDERS_TIMEDOUT) continue;
            if (!running) break;
            continue;
        }
        if (!suspenders_spawn(serve_client, client, SUSPENDERS_QOS_NORMAL)) {
            suspenders_hose_close(client);
            memento_thread_heap_free(heap, client, sizeof(*client));
        }
    }
}

int main(void) {
    suspenders_hose_t listener;
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    if (suspenders_init(1, 256) != SUSPENDERS_OK) {
        fprintf(stderr, "could not initialize Suspenders\n");
        return 1;
    }
    suspenders_hose_init(&listener, NULL);
    if (!suspenders_hose_listen(&listener, LISTEN_URI)) {
        fprintf(stderr, "could not listen on %s\n", LISTEN_URI);
        suspenders_shutdown();
        return 1;
    }
    printf("Telnet echo server listening on %s (Ctrl-C to stop)\n", LISTEN_URI);
    (void)suspenders_go(accept_clients, &listener);
    suspenders_run();
    suspenders_hose_close(&listener);
    suspenders_shutdown();
    return 0;
}
