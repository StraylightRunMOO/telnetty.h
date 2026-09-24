/**
 * @file example_client_simple.c
 * @brief Simple single-threaded Telnetty client example
 * 
 * This example demonstrates a basic Telnetty client that connects to a server
 * and demonstrates various Telnetty features including color support and
 * protocol negotiation.
 * 
 * Features demonstrated:
 * - Telnetty connection establishment
 * - Option negotiation
 * - Color support
 * - Interactive I/O handling
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.0.0
 * 
 */

/* Suppress warnings for extended event types in switch statements */
#ifdef __clang__
#pragma clang diagnostic ignored "-Wswitch"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wswitch"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>

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

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define BUFFER_SIZE 4096
#define MAX_LINE_LENGTH 1024

/* ============================================================================
 * Client State
 * ============================================================================ */

typedef struct {
    int fd;                             /* Server socket */
    telnetty_context_t* telnetty_ctx;       /* Telnetty context */
    char* buffer;                       /* I/O buffer */
    size_t buffer_size;                 /* Buffer size */
    char* server_name;                  /* Server name */
    bool connected;                     /* Connected to server */
    bool echo_enabled;                  /* Local echo enabled */
    uint16_t window_width;              /* Terminal width */
    uint16_t window_height;             /* Terminal height */
    struct termios saved_termios;       /* Saved terminal settings */
} client_t;

static client_t client = {0};

/* ============================================================================
 * Terminal Management
 * ============================================================================ */

/**
 * Set terminal to raw mode for interactive input
 */
static void set_raw_mode(void) {
    struct termios raw;
    
    /* Get current terminal settings */
    tcgetattr(STDIN_FILENO, &client.saved_termios);
    
    /* Copy and modify */
    raw = client.saved_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    /* Apply new settings */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/**
 * Restore terminal settings
 */
static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &client.saved_termios);
}

/**
 * Get terminal window size
 */
static void get_window_size(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        client.window_width = w.ws_col;
        client.window_height = w.ws_row;
    } else {
        client.window_width = 80;
        client.window_height = 24;
    }
}

/* ============================================================================
 * Event Handlers
 * ============================================================================ */

/**
 * Telnetty event handler
 */
static void handle_telnetty_event(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data,
    void* user_data
) {
    (void)user_data;  /* Unused */
    switch (event) {
        case TELNETTY_EVENT_DATA: {
            /* Display received data */
            const char* text = (const char*)data->data.data;
            size_t length = data->data.length;
            
            /* Write to stdout */
            write(STDOUT_FILENO, text, length);
            fflush(stdout);
            break;
        }
        
        case TELNETTY_EVENT_WILL: {
            uint8_t option = data->option.option;
            printf("\r\n[DEBUG] Server WILL option %d\r\n", option);
            
            /* Handle server WILL requests */
            switch (option) {
                case TELNETTY_TELOPT_ECHO:
                    /* Server wants to echo - send DO */
                    telnetty_send_option(ctx, TELNETTY_DO, option);
                    client.echo_enabled = true;
                    break;
                    
                case TELNETTY_TELOPT_SGA:
                    /* Server wants to suppress go ahead - send DO */
                    telnetty_send_option(ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_TTYPE:
                    /* Server wants terminal type - send DO */
                    telnetty_send_option(ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_NAWS:
                    /* Server wants window size - send DO and our size */
                    telnetty_send_option(ctx, TELNETTY_DO, option);
                    telnetty_send_window_size(ctx, client.window_width, 
                                          client.window_height);
                    break;
                    
                default:
                    /* For unknown options, send DONT */
                    telnetty_send_option(ctx, TELNETTY_DONT, option);
                    break;
            }
            break;
        }
        
        case TELNETTY_EVENT_DO: {
            uint8_t option = data->option.option;
            printf("\r\n[DEBUG] Server DO option %d\r\n", option);
            
            /* Handle server DO requests */
            switch (option) {
                case TELNETTY_TELOPT_ECHO:
                    /* Server wants us to echo - send WILL */
                    telnetty_send_option(ctx, TELNETTY_WILL, option);
                    /* Disable local echo */
                    client.echo_enabled = false;
                    break;
                    
                case TELNETTY_TELOPT_SGA:
                    /* Server wants us to suppress go ahead - send WILL */
                    telnetty_send_option(ctx, TELNETTY_WILL, option);
                    break;
                    
                case TELNETTY_TELOPT_MCCP2:
                    /* Server wants compression - enable it */
                    telnetty_send_option(ctx, TELNETTY_WILL, option);
                    break;
                    
                case TELNETTY_TELOPT_MCCP3:
                    /* Server wants compression - enable it */
                    telnetty_send_option(ctx, TELNETTY_WILL, option);
                    break;
                    
                default:
                    /* For unknown options, send WONT */
                    telnetty_send_option(ctx, TELNETTY_WONT, option);
                    break;
            }
            break;
        }
        
        case TELNETTY_EVENT_SB: {
            /* Handle subnegotiation */
            uint8_t option = data->sub.option;
            const uint8_t* sb_data = data->sub.data;
            size_t sb_length = data->sub.length;
            
            printf("\r\n[DEBUG] Subnegotiation for option %d (%zu bytes)\r\n",
                   option, sb_length);
            
            switch (option) {
                case TELNETTY_TELOPT_TTYPE: {
                    /* Terminal type request */
                    if (sb_length > 1 && sb_data[0] == 1) { /* SEND */
                        /* Send our terminal type */
                        const char* term_type = getenv("TERM");
                        if (!term_type) term_type = "xterm-256color";
                        
                        uint8_t response[64];
                        response[0] = 0; /* IS */
                        strcpy((char*)response + 1, term_type);
                        telnetty_send_subnegotiation(ctx, option, 
                            response, 1 + strlen(term_type));
                    }
                    break;
                }
                
                case TELNETTY_TELOPT_NAWS: {
                    /* Window size request - already sent */
                    break;
                }
                
                case TELNETTY_TELOPT_NEW_ENVIRON: {
                    /* Environment variables */
                    printf("\r\n[DEBUG] Environment negotiation\r\n");
                    break;
                }
            }
            break;
        }
        
        case TELNETTY_EVENT_ERROR: {
            printf("\r\n[ERROR] Telnetty error: %s\r\n", data->error.message);
            break;
        }
        
        case (telnetty_event_type_t)TELNETTY_EVENT_CONNECT: {
            printf("\r\n[INFO] Connected to server\r\n");
            client.connected = true;
            
            /* Send initial negotiation */
            /* Request echo */
            telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_ECHO);
            
            /* Request suppress go ahead */
            telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_SGA);
            
            /* Request terminal type */
            telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_TTYPE);
            
            /* Request window size */
            telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_NAWS);
            
            /* Request compression */
            telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MCCP2);
            telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MCCP3);
            
            break;
        }
        
        case (telnetty_event_type_t)TELNETTY_EVENT_DISCONNECT: {
            printf("\r\n[INFO] Disconnected from server\r\n");
            client.connected = false;
            break;
        }
        
    }
}

/* ============================================================================
 * Connection Management
 * ============================================================================ */

/**
 * Connect to Telnetty server
 */
static int connect_to_server(const char* host, int port) {
    /* Create socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Resolve hostname */
    struct hostent* host_info = gethostbyname(host);
    if (!host_info) {
        fprintf(stderr, "Unknown host: %s\n", host);
        close(fd);
        return -1;
    }
    
    /* Set up server address */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host_info->h_addr_list[0], host_info->h_length);
    
    /* Connect to server */
    printf("Connecting to %s:%d...\n", host, port);
    if (connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    
    printf("Connected!\n");
    
    /* Set socket to non-blocking */
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
    return fd;
}

/**
 * Disconnect from server
 */
static void disconnect_from_server(void) {
    if (client.fd >= 0) {
        close(client.fd);
        client.fd = -1;
    }
    
    client.connected = false;
}

/* ============================================================================
 * I/O Handling
 * ============================================================================ */

/**
 * Handle keyboard input
 */
static void handle_keyboard_input(void) {
    char buffer[MAX_LINE_LENGTH];
    ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
    
    if (bytes_read > 0) {
        /* Send to server */
        if (client.connected) {
            telnetty_send(client.telnetty_ctx, (uint8_t*)buffer, bytes_read);
        }
        
        /* Local echo if enabled */
        if (client.echo_enabled) {
            write(STDOUT_FILENO, buffer, bytes_read);
        }
    }
}

/**
 * Handle server data
 */
static void handle_server_data(void) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client.fd, buffer, sizeof(buffer), 0);
    
    if (bytes_read > 0) {
        /* Process Telnetty data */
        int processed = telnetty_process(client.telnetty_ctx, 
                                      (uint8_t*)buffer, 
                                      bytes_read);
        
        if (processed < 0) {
            fprintf(stderr, "Telnetty processing error\n");
        }
    } else if (bytes_read == 0) {
        /* Server disconnected */
        printf("\r\nServer disconnected\r\n");
        client.connected = false;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("recv");
        client.connected = false;
    }
}

/* ============================================================================
 * Main Functions
 * ============================================================================ */

/**
 * Print usage information
 */
static void print_usage(const char* program_name) {
    printf("Usage: %s [host] [port]\n", program_name);
    printf("\n");
    printf("Connects to a Telnetty server and provides an interactive session.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  host    Server hostname or IP address (default: localhost)\n");
    printf("  port    Server port (default: 4000)\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s localhost 4000\n", program_name);
    printf("  %s mud.example.com 23\n", program_name);
}

/**
 * Print client banner
 */
static void print_banner(void) {
    printf("\n");
    printf("==============================================\n");
    printf("Unified Telnetty Library Demo Client\n");
    printf("==============================================\n");
    printf("Features:\n");
    printf("- Telnetty protocol compliance\n");
    printf("- Option negotiation\n");
    printf("- Color support\n");
    printf("- Compression (MCCP2/MCCP3)\n");
    printf("- MUD protocols (MSDP, GMCP)\n");
    printf("==============================================\n");
    printf("Press Ctrl+D to quit\n");
    printf("==============================================\n\n");
}

/**
 * Main client loop
 */
static void client_loop(void) {
    fd_set read_fds;
    struct timeval timeout;
    int max_fd;
    
    while (client.connected) {
        /* Set up file descriptor set */
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(client.fd, &read_fds);
        max_fd = (STDIN_FILENO > client.fd) ? STDIN_FILENO : client.fd;
        
        /* Set timeout */
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; /* 100ms */
        
        /* Wait for input */
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                perror("select");
            }
            continue;
        }
        
        /* Check for keyboard input */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            handle_keyboard_input();
        }
        
        /* Check for server data */
        if (FD_ISSET(client.fd, &read_fds)) {
            handle_server_data();
        }
    }
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
    const char* host = "localhost";
    int port = 4000;
    
    /* Parse command line arguments */
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        host = argv[1];
    }
    
    if (argc > 2) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return 1;
        }
    }
    
    /* Get terminal information */
    get_window_size();
    
    /* Initialize client */
    client.buffer_size = BUFFER_SIZE;
    client.buffer = (char*)malloc(client.buffer_size);
    if (!client.buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return 1;
    }
    
    /* Create Telnetty context */
    client.telnetty_ctx = telnetty_create(handle_telnetty_event, &client);
    if (!client.telnetty_ctx) {
        fprintf(stderr, "Failed to create Telnetty context\n");
        free(client.buffer);
        return 1;
    }
    
    /* Initialize color support */
    telnetty_color_init(client.telnetty_ctx, true);
    
    /* Set up terminal for raw mode */
    set_raw_mode();
    atexit(restore_terminal);
    
    /* Print banner */
    print_banner();
    printf("Connecting to %s:%d...\n", host, port);
    
    /* Connect to server */
    client.fd = connect_to_server(host, port);
    if (client.fd < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        telnetty_destroy(client.telnetty_ctx);
        free(client.buffer);
        return 1;
    }
    
    printf("Connected! Type 'quit' to exit.\n");
    
    /* Set up file descriptor for non-blocking I/O */
    fcntl(client.fd, F_SETFL, O_NONBLOCK);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    
    /* Main client loop */
    client_loop();
    
    /* Cleanup */
    disconnect_from_server();
    telnetty_destroy(client.telnetty_ctx);
    free(client.buffer);
    
    printf("\nDisconnected. Goodbye!\n");
    
    return 0;
}
