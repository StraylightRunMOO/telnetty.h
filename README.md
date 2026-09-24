# Telnetty

Telnetty is a C99, header-only TELNET library for MUD clients, MUD servers,
and other programs that still need TELNET to behave itself. It handles the
protocol parser and option negotiation. Your application handles sockets,
event loops, and deciding what to do with the data.

The code is modular, but every module is optional only in the ordinary C
sense: include the headers you use and define each module's implementation
macro in exactly one C file.

## What it supports

### TELNET options

The standard-options module covers BINARY, ECHO, SUPPRESS-GO-AHEAD, STATUS,
TIMING-MARK, TERMINAL-TYPE, NAWS, TERMINAL-SPEED, LINEMODE, REMOTE-FLOW-
CONTROL, NEW-ENVIRON, CHARSET, and END-OF-RECORD.

### MUD and terminal protocols

| Protocol | What it does | Notes |
| --- | --- | --- |
| MSDP | Structured variable updates | Flat and nested callbacks |
| GMCP | JSON package messages | Payload JSON is the caller's responsibility |
| MTTS | Terminal capability reporting | Includes the three-cycle TTYPE exchange |
| MSSP | Server status values | |
| MXP | Inline tag tokenization | Reports tags; does not render or sanitize them |
| ZMP | NUL-delimited commands and arguments | Subnegotiation payloads |
| MSP | Inline sound and music triggers | Reports triggers to the application |
| ATCP | Package and message subnegotiation | Uses TELNET option 200 |
| MCCP2 / MCCP3 | MUD compression | Requires zlib for compression support |
| MCCP1 | Legacy option negotiation | Negotiation only; no built-in inflate path |
| MCP 2.1 | In-band line messages | `#$#` framing; it is not a TELNET option |
| MNES | Standard MUD environment variables | Sent through NEW-ENVIRON |

The library also provides ANSI, 256-color, true-color, and selected OSC
helpers. StartTLS support only signals the protocol transition; TLS itself
belongs to your TLS library and socket code. WebSocket framing is not part of
Telnetty.

## Build and run the tests

You need a C99 compiler. CMake 3.10 or newer builds the project. zlib enables
compression; libuv is only needed for the libuv server example. The optional
Suspenders example needs a C11 compiler and the two extra headers described
below.

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Useful switches:

| CMake option | Default | Effect |
| --- | --- | --- |
| `BUILD_TESTS` | `OFF` | Build and register the test programs |
| `BUILD_EXAMPLES` | `ON` | Build examples that have their dependencies |
| `ENABLE_ZLIB` | `ON` | Look for zlib and enable its compression support when found |
| `ENABLE_DEBUG` | `OFF` | Use debug build settings and memory tracking |
| `BUILD_SUSPENDERS_EXAMPLE` | `OFF` | Build `server_suspenders` (also set both dependency include paths) |

To build without zlib, configure with `-DENABLE_ZLIB=OFF`. The library still
builds; compression calls that require zlib will not be available. CMake also
exports an interface target named `telnetty::telnetty` for downstream use.

The examples live in `examples/`. `server_simple` and `client_simple` are the
small socket examples. `server_libuv` is built only when libuv is installed.
`server_suspenders` is an optional C11 echo server using
[Suspenders](https://github.com/StraylightRunMOO/suspenders.h) and
[Memento](https://github.com/StraylightRunMOO/memento); it is useful when you
want to see Telnetty inside a coroutine based TCP loop. Suspenders currently
expects Memento 3.x. Build it by supplying both checkout include directories:

```sh
cmake -S . -B build-suspenders \
  -DBUILD_SUSPENDERS_EXAMPLE=ON \
  -DMEMENTO_INCLUDE_DIR=/path/to/memento/include \
  -DSUSPENDERS_INCLUDE_DIR=/path/to/suspenders/include
cmake --build build-suspenders --target server_suspenders
./build-suspenders/server_suspenders
```

It listens on port 4001. The example deliberately starts one worker because
each Telnetty context belongs to one thread. That is a useful constraint to
see in a small example, before it turns up in a production incident.
They are starting points, not production daemons; in particular, they do not
replace your deployment's socket lifecycle or error handling.

## Use it in a C program

Choose the modules you need. This example includes the common set. Put the
implementation defines in one source file only; defining them in every file
is a reliable way to meet the linker twice.

```c
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
```

The implementation macros must appear before the includes. If you do not use
a module, leave out its macro and header. The headers are under `include/`.

Create one context per connection, pass received bytes to
`telnetty_process()`, and write queued output to your socket:

```c
static void on_telnet_event(telnetty_context_t* ctx,
                            telnetty_event_type_t event,
                            const telnetty_event_data_union_t* data,
                            void* user_data) {
    (void)user_data;

    if (event == TELNETTY_EVENT_DATA) {
        /* The event data is borrowed. Use it during this callback or copy it. */
        (void)telnetty_send(ctx, data->data.data, data->data.length);
    }
}

/* In your connection setup: */
telnetty_context_t* ctx = telnetty_create(on_telnet_event, NULL);
if (!ctx) {
    /* Handle allocation failure. */
}

telnetty_enable_echo(ctx);
telnetty_enable_naws(ctx);
telnetty_color_init(ctx, true);

/* For each socket read: */
/* telnetty_process(ctx, bytes, byte_count); */

/* After processing and callbacks, write ctx's queued output to the socket.
 * Call telnetty_flush(ctx) only after every queued byte has been written. */

/* When the connection closes: */
telnetty_destroy(ctx);
```

`telnetty_send()` queues application data and doubles any TELNET IAC bytes as
required by the protocol. `telnetty_get_output_buffer()` gives you the queued
bytes. Your socket layer must handle partial writes; do not reset the output
buffer until those bytes have all been sent.

Option and protocol helpers usually start negotiation when enabled. Check
their return value and handle `-1` as a setup or send failure. Negotiation is
not complete just because your call returned successfully; wait for the
peer's response or configure a timeout and drive `telnetty_tick()`.

## A few operational rules

### One context, one thread

Contexts are not thread-safe. Keep each `telnetty_context_t` on one thread and
use a separate context for each connection. Do not call `telnetty_process()`
and `telnetty_tick()` concurrently on the same context. The old pool and
queue “thread-safe” flags remain for source compatibility and do not add
locking.

### Drive timeouts and rate limits

`telnetty_set_negotiation_timeout()` and `telnetty_set_rate_limit()` rely on
your event loop calling `telnetty_tick(ctx, now_ms)`. Use a monotonic clock
for `now_ms`. If nothing drives the clock, neither timeout nor rate limit
will magically expire. Computers are stubborn that way.

### Treat peer content as input

TELNET framing is parsed and bounded, but protocol payloads still come from
the network. Validate values in your callbacks. In particular, MXP callbacks
report markup; they do not decide which tags are safe to execute or render.
GMCP gives your application JSON text, not a JSON parser.

### Compression and TLS have boundaries

Link zlib when enabling the compression module. MCCP1 currently negotiates
the legacy option but does not inflate its stream. The StartTLS helpers only
signal `FOLLOWS`; your application must switch to a TLS implementation and
manage the socket transition. Telnetty does not provide TLS or WebSocket
framing.

## Where to look next

- `include/telnetty_core.h` — parser, context, negotiation, send and receive
- `include/telnetty_options.h` — standard TELNET options
- `include/telnetty_mud.h` — MUD protocols and their callback APIs
- `include/telnetty_compression.h` — zlib-backed compression helpers
- `include/telnetty_events.h` — extended events and priorities
- `include/telnetty_buffer.h` — buffers, chains, pools, and queues
- `include/telnetty_color.h` — terminal colors and OSC helpers
- `include/telnetty_mcp.h` — MCP 2.1 in-band messages
- `CHANGELOG.md` — release history and API changes
- `PLAN.md` — remaining protocol and release work

The public declarations and comments in each header are the most complete API
reference. The examples show how the pieces fit together without requiring a
tour through every RFC first.

## License

Telnetty is distributed under the [MIT License](LICENSE). Keep the license
notice with redistributed copies; the code is more useful when nobody has to
guess what they are allowed to do with it.
