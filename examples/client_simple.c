/**
 * @file client_simple.c
 * @brief A minimal line-mode TELNET client
 *
 * The client side of the integration loop:
 *
 * - declares which options it accepts before any server bytes arrive
 * - answers TTYPE with $TERM and NAWS with the terminal size
 * - inflates the server's MCCP2 stream and compresses its own input with
 *   MCCP3 when the server offers them (telnetty does both inside
 *   telnetty_process / telnetty_send)
 * - prints DATA to stdout, sends typed lines with CR LF line endings
 *
 * The terminal stays in normal line mode, so when the server turns on ECHO
 * your typing is shown twice; a full client would switch the tty to raw
 * mode on the server's WILL ECHO.
 *
 * Usage: client_simple [host] [port]   (default localhost 4000)
 */

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_options.h"
#include "telnetty_compression.h"

static int sock = -1;

/* Write everything telnetty queued, then reset its buffer. */
static int flush_output(telnetty_context_t* ctx) {
    telnetty_buffer_t* out = telnetty_get_output_buffer(ctx);
    size_t off = 0;
    while (out && off < out->length) {
        ssize_t n = write(sock, out->data + off, out->length - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return telnetty_flush(ctx);
}

static void send_window_size(telnetty_context_t* ctx) {
    struct winsize ws;
    uint16_t cols = 80, rows = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
    telnetty_send_window_size(ctx, cols, rows);
}

static void on_telnet_event(telnetty_context_t* ctx, telnetty_event_type_t event,
                            const telnetty_event_data_union_t* data, void* user_data) {
    (void)user_data;
    switch (event) {
    case TELNETTY_EVENT_DATA:
        fwrite(data->data.data, 1, data->data.length, stdout);
        fflush(stdout);
        break;
    case TELNETTY_EVENT_DO:
        /* We agreed to NAWS: report the size right away. */
        if (data->option.option == TELNETTY_TELOPT_NAWS)
            send_window_size(ctx);
        break;
    case TELNETTY_EVENT_SB:
        /* IAC SB TTYPE SEND IAC SE -> IAC SB TTYPE IS <name> IAC SE */
        if (data->sub.option == TELNETTY_TELOPT_TTYPE &&
            data->sub.length >= 1 && data->sub.data[0] == 1) {
            const char* term = getenv("TERM");
            if (!term || !*term) term = "unknown";
            uint8_t reply[64];
            size_t n = strlen(term);
            if (n > sizeof(reply) - 1) n = sizeof(reply) - 1;
            reply[0] = 0;   /* IS */
            memcpy(reply + 1, term, n);
            telnetty_send_subnegotiation(ctx, TELNETTY_TELOPT_TTYPE, reply, n + 1);
        }
        break;
    case TELNETTY_EVENT_ERROR:
        fprintf(stderr, "\n[telnet error %d: %s]\n", data->error.code,
                data->error.message ? data->error.message : "");
        break;
    default:
        break;
    }
}

static int connect_to(const char* host, const char* port) {
    struct addrinfo hints, *res, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", host, gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "localhost";
    const char* port = argc > 2 ? argv[2] : "4000";

    sock = connect_to(host, port);
    if (sock < 0) {
        fprintf(stderr, "could not connect to %s:%s\n", host, port);
        return 1;
    }

    telnetty_context_t* ctx = telnetty_create(on_telnet_event, NULL);
    if (!ctx) return 1;

    /* What we accept, declared before the server's offers arrive. The
     * server may ECHO and SGA; we may send TTYPE and NAWS. Anything else
     * is refused by telnetty's RFC 1143 negotiation. */
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_ECHO, 0, 1);
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_SGA, 1, 1);
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_TTYPE, 1, 0);
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_NAWS, 1, 0);
    telnetty_accept_mccp2(ctx);      /* return -1 when built without zlib */
    telnetty_accept_mccp3(ctx, 0);

    fprintf(stderr, "Connected to %s:%s. Ctrl+D to quit.\n", host, port);

    int stdin_open = 1;
    for (;;) {
        struct pollfd fds[2] = {
            { sock, POLLIN, 0 },
            { STDIN_FILENO, POLLIN, 0 },
        };
        if (poll(fds, stdin_open ? 2 : 1, -1) < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            uint8_t buf[4096];
            ssize_t n = read(sock, buf, sizeof(buf));
            if (n <= 0) {
                fprintf(stderr, "\nConnection closed.\n");
                break;
            }
            if (telnetty_process(ctx, buf, (size_t)n) < 0) {
                fprintf(stderr, "\nProtocol error.\n");
                break;
            }
        }

        if (stdin_open && (fds[1].revents & (POLLIN | POLLHUP))) {
            char line[1024];
            if (!fgets(line, sizeof(line), stdin)) {
                /* End of input: finish our MCCP3 stream, then hang up. */
                stdin_open = 0;
                telnetty_compression_cleanup(ctx);
                flush_output(ctx);
                shutdown(sock, SHUT_WR);
            } else {
                size_t n = strcspn(line, "\r\n");
                telnetty_send(ctx, (const uint8_t*)line, n);
                telnetty_send(ctx, (const uint8_t*)"\r\n", 2);
            }
        }

        if (flush_output(ctx) < 0) {
            perror("write");
            break;
        }
    }

    telnetty_destroy(ctx);
    close(sock);
    return 0;
}
