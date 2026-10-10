/**
 * @file server_simple.c
 * @brief A small multi-user TELNET chat server built on poll()
 *
 * Shows the whole integration loop in one file:
 *
 * - one telnetty context per connection
 * - socket reads fed to telnetty_process() (MCCP3 input is inflated there)
 * - DATA events assembled into lines, with backspace handling
 * - everything telnetty queues (text, negotiation, MCCP2-compressed bytes)
 *   moved to a per-client pending buffer and written as the socket allows,
 *   so partial writes never lose or reorder output
 * - telnetty_tick() for negotiation timeouts
 * - NAWS, TTYPE, MCCP2/MCCP3 and ANSI color
 *
 * Commands: /who, /me <action>, /quit. Anything else is said to the room.
 *
 * Usage: server_simple [port]   (default 4000)
 * Try:   telnet localhost 4000   or   tt++ / Mudlet / any MUD client
 */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION
#define TELNETTY_COLOR_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_options.h"
#include "telnetty_compression.h"
#include "telnetty_color.h"

#define DEFAULT_PORT         4000
#define MAX_CLIENTS          64
#define MAX_LINE             512
#define IDLE_TIMEOUT_SEC     (15 * 60)
#define NEGOTIATION_TIMEOUT  5000

typedef struct {
    int fd;
    telnetty_context_t* telnet;
    char name[32];
    char line[MAX_LINE];
    size_t line_len;
    uint8_t* pending;          /* bytes telnetty produced, not yet written */
    size_t pending_len;
    int quitting;              /* close once pending output is written */
    time_t last_activity;
} client_t;

static client_t* clients[MAX_CLIENTS];
static volatile sig_atomic_t running = 1;
static unsigned next_guest = 1;

/* ------------------------------------------------------------------------ */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void say(client_t* c, const char* text) {
    telnetty_send(c->telnet, (const uint8_t*)text, strlen(text));
}

static void sayf(client_t* c, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    say(c, buf);
}

static void broadcast(client_t* from, const char* text) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t* c = clients[i];
        if (!c || c == from || c->quitting) continue;
        telnetty_color_set_foreground(c->telnet, TELNETTY_COLOR_CYAN);
        say(c, text);
        telnetty_color_reset(c->telnet);
        say(c, "\r\n");
    }
}

/* Move telnetty's queued output into the pending buffer, then write as much
 * as the socket accepts. telnetty_flush() only after the bytes are copied:
 * the output buffer is reused for the next batch. */
static int flush_client(client_t* c) {
    telnetty_buffer_t* out = telnetty_get_output_buffer(c->telnet);
    if (out && out->length > 0) {
        uint8_t* grown = (uint8_t*)realloc(c->pending, c->pending_len + out->length);
        if (!grown) return -1;
        memcpy(grown + c->pending_len, out->data, out->length);
        c->pending = grown;
        c->pending_len += out->length;
        telnetty_flush(c->telnet);
    }
    while (c->pending_len > 0) {
        ssize_t n = write(c->fd, c->pending, c->pending_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            if (errno == EINTR) continue;
            return -1;
        }
        memmove(c->pending, c->pending + n, c->pending_len - (size_t)n);
        c->pending_len -= (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */

static void who(client_t* c) {
    say(c, "Connected:\r\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t* o = clients[i];
        if (!o) continue;
        uint16_t w = 0, h = 0;
        const char* term = telnetty_get_terminal_type(o->telnet);
        int have_size = telnetty_get_window_size(o->telnet, &w, &h) == 0;
        size_t zc = 0, zu = 0;
        telnetty_mccp2_get_stats(o->telnet, &zc, &zu, NULL);
        char size[24] = "?";
        if (have_size) snprintf(size, sizeof(size), "%ux%u", (unsigned)w, (unsigned)h);
        sayf(c, "  %-12s term=%s size=%s mccp2=%s mccp3=%s",
             o->name, term ? term : "?", size,
             telnetty_is_mccp2_enabled(o->telnet) ? "on" : "off",
             telnetty_is_mccp3_enabled(o->telnet) ? "on" : "off");
        if (zu > 0)
            sayf(c, " (%zu -> %zu bytes)", zu, zc);
        say(c, "\r\n");
    }
}

static void handle_line(client_t* c, const char* line) {
    char msg[MAX_LINE + 64];
    if (line[0] == '\0') return;
    if (strcmp(line, "/quit") == 0) {
        say(c, "Goodbye.\r\n");
        snprintf(msg, sizeof(msg), "%s has left.", c->name);
        broadcast(c, msg);
        telnetty_compression_cleanup(c->telnet);   /* end MCCP2 cleanly */
        c->quitting = 1;
    } else if (strcmp(line, "/who") == 0) {
        who(c);
    } else if (strncmp(line, "/me ", 4) == 0) {
        snprintf(msg, sizeof(msg), "* %s %s", c->name, line + 4);
        broadcast(c, msg);
        sayf(c, "%s\r\n", msg);
    } else if (line[0] == '/') {
        say(c, "Commands: /who, /me <action>, /quit\r\n");
    } else {
        snprintf(msg, sizeof(msg), "%s says: %s", c->name, line);
        broadcast(c, msg);
        sayf(c, "You say: %s\r\n", line);
    }
}

/* Assemble DATA bytes into lines. TELNET ends lines with CR LF or CR NUL;
 * bare LF from sloppy clients is accepted too. */
static void handle_input(client_t* c, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && !c->quitting; i++) {
        uint8_t ch = data[i];
        if (ch == '\r' || ch == '\n') {
            if (ch == '\n' && c->line_len == 0 && i > 0 && data[i - 1] == '\r')
                continue;
            c->line[c->line_len] = '\0';
            handle_line(c, c->line);
            c->line_len = 0;
        } else if (ch == 8 || ch == 127) {
            if (c->line_len > 0) c->line_len--;
        } else if (ch >= 32 && c->line_len + 1 < sizeof(c->line)) {
            c->line[c->line_len++] = (char)ch;
        }
    }
}

static void on_telnet_event(telnetty_context_t* ctx, telnetty_event_type_t event,
                            const telnetty_event_data_union_t* data, void* user_data) {
    (void)ctx;
    client_t* c = (client_t*)user_data;
    if (event == TELNETTY_EVENT_DATA) {
        handle_input(c, data->data.data, data->data.length);
    } else if (event == TELNETTY_EVENT_ERROR) {
        fprintf(stderr, "[%s] telnet error %d: %s\n", c->name, data->error.code,
                data->error.message ? data->error.message : "");
    }
}

/* ------------------------------------------------------------------------ */

static void drop_client(int i) {
    client_t* c = clients[i];
    printf("[%s] disconnected\n", c->name);
    close(c->fd);
    telnetty_destroy(c->telnet);
    free(c->pending);
    free(c);
    clients[i] = NULL;
}

static void accept_client(int listener) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd = accept(listener, (struct sockaddr*)&addr, &len);
    if (fd < 0) return;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) if (!clients[i]) { slot = i; break; }
    client_t* c = slot >= 0 ? (client_t*)calloc(1, sizeof(*c)) : NULL;
    if (!c) {
        static const char full[] = "Server full.\r\n";
        (void)!write(fd, full, sizeof(full) - 1);
        close(fd);
        return;
    }
    c->telnet = telnetty_create(on_telnet_event, c);
    if (!c->telnet) { free(c); close(fd); return; }
    c->fd = fd;
    c->last_activity = time(NULL);
    snprintf(c->name, sizeof(c->name), "Guest%u", next_guest++);
    clients[slot] = c;

    /* Start the clock before offering options so negotiation timeouts are
     * measured from now. */
    telnetty_tick(c->telnet, now_ms());
    telnetty_set_negotiation_timeout(c->telnet, NEGOTIATION_TIMEOUT);
    telnetty_enable_sga(c->telnet);
    telnetty_enable_naws(c->telnet);
    telnetty_enable_ttype(c->telnet, 0);
    telnetty_compression_auto_negotiate(c->telnet, true);   /* MCCP2 + MCCP3 */
    telnetty_color_init(c->telnet, false);

    telnetty_color_set_attribute(c->telnet, TELNETTY_ATTR_BOLD);
    say(c, "Welcome to the telnetty chat server.");
    telnetty_color_reset(c->telnet);
    sayf(c, "\r\nYou are %s. Type /who, /me <action> or /quit.\r\n", c->name);

    char msg[64];
    snprintf(msg, sizeof(msg), "%s has arrived.", c->name);
    broadcast(c, msg);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    printf("[%s] connected from %s:%u\n", c->name, ip, (unsigned)ntohs(addr.sin_port));
}

static void read_client(int i) {
    client_t* c = clients[i];
    uint8_t buf[4096];
    ssize_t n = read(c->fd, buf, sizeof(buf));
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s has left.", c->name);
        broadcast(c, msg);
        drop_client(i);
        return;
    }
    if (n < 0) return;
    c->last_activity = time(NULL);
    if (telnetty_process(c->telnet, buf, (size_t)n) < 0) {
        /* Corrupt MCCP3 stream or similar; already reported via EVENT_ERROR. */
        say(c, "Protocol error.\r\n");
        c->quitting = 1;
    }
}

static int open_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char** argv) {
    int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int listener = open_listener(port);
    if (listener < 0) {
        perror("listen");
        return 1;
    }
    printf("telnetty %s chat server on port %d (Ctrl+C to stop)\n",
           TELNETTY_VERSION_STRING, port);
    fflush(stdout);

    while (running) {
        struct pollfd fds[MAX_CLIENTS + 1];
        int idx[MAX_CLIENTS + 1];
        nfds_t n = 0;
        fds[n].fd = listener; fds[n].events = POLLIN; idx[n++] = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) continue;
            fds[n].fd = clients[i]->fd;
            fds[n].events = (short)((clients[i]->quitting ? 0 : POLLIN) |
                                    (clients[i]->pending_len ? POLLOUT : 0));
            idx[n++] = i;
        }

        if (poll(fds, n, 250) < 0 && errno != EINTR) {
            perror("poll");
            break;
        }

        for (nfds_t k = 0; k < n; k++) {
            if (!fds[k].revents) continue;
            if (idx[k] < 0) accept_client(listener);
            else if (clients[idx[k]] && (fds[k].revents & (POLLIN | POLLHUP | POLLERR)))
                read_client(idx[k]);
        }

        /* Tick, flush everyone (broadcasts queue output on other clients),
         * and retire finished or idle connections. */
        uint64_t ms = now_ms();
        time_t now = time(NULL);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_t* c = clients[i];
            if (!c) continue;
            telnetty_tick(c->telnet, ms);
            if (!c->quitting && now - c->last_activity > IDLE_TIMEOUT_SEC) {
                say(c, "Idle timeout.\r\n");
                telnetty_compression_cleanup(c->telnet);
                c->quitting = 1;
            }
            if (flush_client(c) < 0 || (c->quitting && c->pending_len == 0))
                drop_client(i);
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i]) continue;
        say(clients[i], "\r\nServer shutting down.\r\n");
        telnetty_compression_cleanup(clients[i]->telnet);
        (void)flush_client(clients[i]);
        drop_client(i);
    }
    close(listener);
    return 0;
}
