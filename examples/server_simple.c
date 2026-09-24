/* Suppress warnings for extended event types in switch statements */
#ifdef __clang__
#pragma clang diagnostic ignored "-Wswitch"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wswitch"
#endif

/**
 * @file example_server_simple.c
 * @brief Simple single-threaded Telnetty server example
 * 
 * This example demonstrates a basic Telnetty server that uses the unified
 * Telnetty library to handle multiple clients with various Telnetty options.
 * 
 * Features demonstrated:
 * - Event-driven architecture
 * - Telnetty option negotiation
 * - MUD protocol support (MSDP, GMCP)
 * - Color support
 * - Compression (MCCP2/3)
 * - Multiple client handling
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.0.0
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
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

#define SERVER_PORT 4000
#define MAX_CLIENTS 100
#define BUFFER_SIZE 4096
#define CLIENT_TIMEOUT 300  /* seconds */

/* ============================================================================
 * Client Management
 * ============================================================================ */

typedef struct {
    int fd;                             /* Client socket */
    struct sockaddr_in addr;            /* Client address */
    time_t last_activity;               /* Last activity timestamp */
    telnetty_context_t* telnetty_ctx;       /* Telnetty context */
    char* buffer;                       /* I/O buffer */
    size_t buffer_size;                 /* Buffer size */
    char* name;                         /* Client name */
    bool authenticated;                 /* Client authenticated */
    uint64_t mtts_flags;                /* MTTS capability flags */
    uint16_t window_width;              /* Terminal width */
    uint16_t window_height;             /* Terminal height */
} client_t;

static client_t* clients[MAX_CLIENTS] = {0};
static int server_fd = -1;
static bool running = true;

/* ============================================================================
 * Event Handlers
 * ============================================================================ */

/**
 * Telnetty event handler - processes all Telnetty events
 */
static void handle_telnetty_event(
    telnetty_context_t* ctx,
    telnetty_event_type_t event,
    const telnetty_event_data_union_t* data,
    void* user_data
) {
    (void)ctx; /* Unused parameter */
    client_t* client = (client_t*)user_data;
    
    switch (event) {
        case TELNETTY_EVENT_DATA: {
            /* Process received data */
            const char* text = (const char*)data->data.data;
            size_t length = data->data.length;
            
            printf("Client %s sent %zu bytes: %.*s\n", 
                   client->name ? client->name : "unknown",
                   length, (int)length, text);
            
            /* Echo back with some processing */
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), 
                    "Server received: %.*s\r\n", (int)length, text);
            
            /* Send colored response */
            telnetty_color_send(client->telnetty_ctx, response, 
                            TELNETTY_COLOR_CYAN, TELNETTY_COLOR_BLACK, 0);
            
            /* Update last activity */
            client->last_activity = time(NULL);
            break;
        }
        
        case TELNETTY_EVENT_WILL: {
            uint8_t option = data->option.option;
            printf("Client %s WILL option %d\n", 
                   client->name ? client->name : "unknown", option);
            
            /* Handle option negotiation */
            switch (option) {
                case TELNETTY_TELOPT_ECHO:
                    /* Client wants to echo - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_SGA:
                    /* Client wants to suppress go ahead - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_TTYPE:
                    /* Client wants to send terminal type - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    /* Request terminal type */
                    telnetty_query_terminal_type(client->telnetty_ctx);
                    break;
                    
                case TELNETTY_TELOPT_NAWS:
                    /* Client wants to send window size - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_MSDP:
                    /* Client wants MSDP - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_GMCP:
                    /* Client wants GMCP - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    /* Send GMCP hello */
                    telnetty_gmcp_send_hello(client->telnetty_ctx, "Simple Server", "1.0.0");
                    break;
                    
                case TELNETTY_TELOPT_MCCP2:
                    /* Client wants MCCP2 - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    break;
                    
                case TELNETTY_TELOPT_MCCP3:
                    /* Client wants MCCP3 - send DO */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_DO, option);
                    break;
                    
                default:
                    /* For unknown options, send WONT */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_WONT, option);
                    break;
            }
            break;
        }
        
        case TELNETTY_EVENT_DO: {
            uint8_t option = data->option.option;
            printf("Client %s DO option %d\n", 
                   client->name ? client->name : "unknown", option);
            
            /* Handle DO requests */
            switch (option) {
                case TELNETTY_TELOPT_ECHO:
                    /* Client wants us to echo - send WILL */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_WILL, option);
                    break;
                    
                case TELNETTY_TELOPT_SGA:
                    /* Client wants us to suppress go ahead - send WILL */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_WILL, option);
                    break;
                    
                case TELNETTY_TELOPT_MCCP2:
                    /* Client wants compression - enable it */
                    telnetty_enable_mccp2(client->telnetty_ctx, 6);
                    break;
                    
                case TELNETTY_TELOPT_MCCP3:
                    /* Client wants compression - enable it */
                    telnetty_enable_mccp3(client->telnetty_ctx, 6);
                    break;
                    
                default:
                    /* For unknown options, send WONT */
                    telnetty_send_option(client->telnetty_ctx, TELNETTY_WONT, option);
                    break;
            }
            break;
        }
        
        case TELNETTY_EVENT_SB: {
            /* Handle subnegotiation */
            uint8_t option = data->sub.option;
            const uint8_t* sb_data = data->sub.data;
            size_t sb_length = data->sub.length;
            
            printf("Client %s subnegotiation for option %d (%zu bytes)\n",
                   client->name ? client->name : "unknown", 
                   option, sb_length);
            
            switch (option) {
                case TELNETTY_TELOPT_TTYPE: {
                    /* Terminal type information */
                    if (sb_length > 1 && sb_data[0] == 0) { /* IS */
                        char* term_type = strndup((const char*)sb_data + 1, sb_length - 1);
                        if (client->name) {
                            free(client->name);
                        }
                        client->name = term_type;
                        
                        /* Detect color capabilities */
                        telnetty_color_cap_t caps = telnetty_color_detect_capabilities(
                            client->telnetty_ctx, term_type);
                        telnetty_color_set_capabilities(client->telnetty_ctx, caps);
                        
                        printf("Client terminal type: %s\n", term_type);
                    }
                    break;
                }
                
                case TELNETTY_TELOPT_NAWS: {
                    /* Window size information */
                    if (sb_length >= 4) {
                        client->window_width = (sb_data[0] << 8) | sb_data[1];
                        client->window_height = (sb_data[2] << 8) | sb_data[3];
                        
                        printf("Client window size: %dx%d\n", 
                               client->window_width, client->window_height);
                    }
                    break;
                }
                
                case TELNETTY_TELOPT_MSDP: {
                    /* MSDP data - handle it */
                    /* This would parse MSDP variables and values */
                    printf("Received MSDP data (%zu bytes)\n", sb_length);
                    break;
                }
                
                case TELNETTY_TELOPT_GMCP: {
                    /* GMCP data - handle it */
                    /* This would parse GMCP messages */
                    printf("Received GMCP data (%zu bytes)\n", sb_length);
                    break;
                }
            }
            break;
        }
        
        case TELNETTY_EVENT_ERROR: {
            printf("Telnetty error for client %s: %s\n",
                   client->name ? client->name : "unknown",
                   data->error.message);
            break;
        }
        
        case (telnetty_event_type_t)TELNETTY_EVENT_CONNECT: {
            telnetty_event_connect_t* conn = (telnetty_event_connect_t*)&data->ext.data;
            printf("Client %s connected from %s:%d\n",
                   client->name ? client->name : "unknown",
                   conn->address, conn->port);
            
            /* Send welcome message */
            const char* welcome = 
                "\r\n"
                "Welcome to the Unified Telnetty Library Demo Server!\r\n"
                "==============================================\r\n"
                "\r\n"
                "This server demonstrates:\r\n"
                "- Telnetty protocol compliance\r\n"
                "- MUD protocol support (MSDP, GMCP)\r\n"
                "- Color support\r\n"
                "- Compression (MCCP2/MCCP3)\r\n"
                "\r\n"
                "Type anything and it will be echoed back in color!\r\n"
                "\r\n";
            
            telnetty_color_send(client->telnetty_ctx, welcome, 
                            TELNETTY_COLOR_GREEN, TELNETTY_COLOR_BLACK, 0);
            
            /* Enable all Telnetty options */
            telnetty_enable_all_options(client->telnetty_ctx);
            break;
        }
        
        case (telnetty_event_type_t)TELNETTY_EVENT_DISCONNECT: {
            printf("Client %s disconnected\n",
                   client->name ? client->name : "unknown");
            break;
        }
        
        default:
            /* Ignore other events */
            break;
    }
}

/* ============================================================================
 * Client Management Functions
 * ============================================================================ */

/**
 * Create a new client
 */
static client_t* client_create(int fd, struct sockaddr_in* addr) {
    client_t* client = (client_t*)malloc(sizeof(client_t));
    if (!client) return NULL;
    
    memset(client, 0, sizeof(client_t));
    
    client->fd = fd;
    client->addr = *addr;
    client->last_activity = time(NULL);
    client->buffer_size = BUFFER_SIZE;
    client->buffer = (char*)malloc(client->buffer_size);
    
    if (!client->buffer) {
        free(client);
        return NULL;
    }
    
    /* Create Telnetty context */
    client->telnetty_ctx = telnetty_create(handle_telnetty_event, client);
    if (!client->telnetty_ctx) {
        free(client->buffer);
        free(client);
        return NULL;
    }
    
    /* Initialize color support */
    telnetty_color_init(client->telnetty_ctx, true);
    
    /* Enable compression auto-negotiation */
    telnetty_compression_auto_negotiate(client->telnetty_ctx, true);
    
    /* Set client name to IP address initially */
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
    client->name = strdup(ip_str);
    
    return client;
}

/**
 * Destroy a client
 */
static void client_destroy(client_t* client) {
    if (!client) return;
    
    /* Cleanup Telnetty context */
    if (client->telnetty_ctx) {
        telnetty_color_cleanup(client->telnetty_ctx);
        telnetty_destroy(client->telnetty_ctx);
    }
    
    /* Close socket */
    if (client->fd >= 0) {
        close(client->fd);
    }
    
    /* Free resources */
    if (client->buffer) {
        free(client->buffer);
    }
    if (client->name) {
        free(client->name);
    }
    
    free(client);
}

/**
 * Find a free client slot
 */
static int find_free_client_slot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == NULL) {
            return i;
        }
    }
    return -1;
}

/**
 * Remove a client
 */
static void remove_client(int index) {
    if (index >= 0 && index < MAX_CLIENTS && clients[index]) {
        client_destroy(clients[index]);
        clients[index] = NULL;
    }
}

/**
 * Check for timed out clients
 */
static void check_client_timeouts(void) {
    time_t now = time(NULL);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && (now - clients[i]->last_activity) > CLIENT_TIMEOUT) {
            printf("Client %s timed out\n", 
                   clients[i]->name ? clients[i]->name : "unknown");
            remove_client(i);
        }
    }
}

/* ============================================================================
 * Server Functions
 * ============================================================================ */

/**
 * Create server socket
 */
static int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set socket options */
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }
    
    /* Set non-blocking */
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
    /* Bind to port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    
    /* Listen for connections */
    if (listen(fd, 10) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    
    return fd;
}

/**
 * Accept a new client connection
 */
static void accept_client(void) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept");
        }
        return;
    }
    
    /* Find free slot */
    int slot = find_free_client_slot();
    if (slot < 0) {
        printf("Server full, rejecting connection from %s:%d\n",
               inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        close(client_fd);
        return;
    }
    
    /* Create client */
    client_t* client = client_create(client_fd, &addr);
    if (!client) {
        printf("Failed to create client for %s:%d\n",
               inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        close(client_fd);
        return;
    }
    
    /* Add to clients array */
    clients[slot] = client;
    
    printf("Accepted connection from %s:%d (slot %d)\n",
           inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), slot);
    
    /* Fire connection event */
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    
    telnetty_event_data_union_t event_data;
    telnetty_event_connect_t* conn = (telnetty_event_connect_t*)&event_data.ext.data;
    conn->address = ip_str;
    conn->port = ntohs(addr.sin_port);
    conn->timestamp = time(NULL);
    
    telnetty_fire_event_ex(client->telnetty_ctx, (telnetty_event_type_t)TELNETTY_EVENT_CONNECT, 
                        TELNETTY_PRIORITY_NORMAL, &event_data);
}

/**
 * Handle client I/O
 */
static void handle_client_io(int index) {
    client_t* client = clients[index];
    if (!client) return;
    
    /* Read data from client */
    ssize_t bytes_read = recv(client->fd, client->buffer, client->buffer_size, 0);
    
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
            remove_client(index);
        }
        return;
    } else if (bytes_read == 0) {
        /* Client disconnected */
        printf("Client %s disconnected\n", 
               client->name ? client->name : "unknown");
        
        /* Fire disconnect event */
        telnetty_event_data_union_t event_data;
        telnetty_event_disconnect_t* disc = (telnetty_event_disconnect_t*)&event_data.ext.data;
        disc->reason = 0;
        disc->message = "Client disconnected";
        
        telnetty_fire_event_ex(client->telnetty_ctx, (telnetty_event_type_t)TELNETTY_EVENT_DISCONNECT,
                            TELNETTY_PRIORITY_HIGH, &event_data);
        
        remove_client(index);
        return;
    }
    
    /* Process Telnetty data */
    int processed = telnetty_process(client->telnetty_ctx, 
                                  (uint8_t*)client->buffer, 
                                  bytes_read);
    
    if (processed < 0) {
        printf("Telnetty processing error for client %s\n",
               client->name ? client->name : "unknown");
    }
    
    /* Update last activity */
    client->last_activity = time(NULL);
}

/**
 * Send data to all connected clients
 */
static void broadcast_message(const char* message, telnetty_ansi_color_t fg, 
                             telnetty_ansi_color_t bg, uint32_t attrs) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            telnetty_color_send(clients[i]->telnetty_ctx, message, fg, bg, attrs);
        }
    }
}

/**
 * Server main loop
 */
static void server_loop(void) {
    fd_set read_fds, write_fds;
    struct timeval timeout;
    int max_fd;
    
    while (running) {
        /* Set up file descriptor sets */
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        
        /* Add server socket */
        FD_SET(server_fd, &read_fds);
        max_fd = server_fd;
        
        /* Add client sockets */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i]) {
                FD_SET(clients[i]->fd, &read_fds);
                if (clients[i]->fd > max_fd) {
                    max_fd = clients[i]->fd;
                }
            }
        }
        
        /* Set timeout */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        /* Wait for activity */
        int activity = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                perror("select");
            }
            continue;
        }
        
        /* Check for new connections */
        if (FD_ISSET(server_fd, &read_fds)) {
            accept_client();
        }
        
        /* Check for client I/O */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && FD_ISSET(clients[i]->fd, &read_fds)) {
                handle_client_io(i);
            }
        }
        
        /* Check for timeouts */
        check_client_timeouts();
        
        /* Periodically send server status */
        static time_t last_status = 0;
        time_t now = time(NULL);
        if (now - last_status >= 30) { /* Every 30 seconds */
            char status[256];
            int active_clients = 0;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i]) active_clients++;
            }
            
            snprintf(status, sizeof(status), 
                    "\r\n[Server Status] Active clients: %d, Uptime: %ld seconds\r\n",
                    active_clients, now - last_status);
            
            broadcast_message(status, TELNETTY_COLOR_YELLOW, TELNETTY_COLOR_BLACK, 
                            TELNETTY_ATTR_BOLD);
            
            last_status = now;
        }
    }
}

/**
 * Signal handler
 */
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nShutting down server...\n");
        running = false;
    }
}

/**
 * Print server banner
 */
static void print_banner(void) {
    printf("\n");
    printf("==============================================\n");
    printf("Unified Telnetty Library Demo Server\n");
    printf("==============================================\n");
    printf("Port: %d\n", SERVER_PORT);
    printf("Max clients: %d\n", MAX_CLIENTS);
    printf("Client timeout: %d seconds\n", CLIENT_TIMEOUT);
    printf("==============================================\n");
    printf("Press Ctrl+C to stop the server\n");
    printf("==============================================\n\n");
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
    int port = SERVER_PORT;
    
    /* Parse command line arguments */
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }
    
    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create server socket */
    server_fd = create_server_socket(port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to create server socket\n");
        return 1;
    }
    
    /* Print banner */
    print_banner();
    printf("Server listening on port %d...\n", port);
    
    /* Main server loop */
    server_loop();
    
    /* Cleanup */
    close(server_fd);
    
    /* Clean up all clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            remove_client(i);
        }
    }
    
    printf("Server shutdown complete.\n");
    
    return 0;
}
