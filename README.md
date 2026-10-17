# Telnetty

A C99 header-only TELNET library for people who don't want to step on rakes while wrangling state machines and parsers. It handles the framing, the RFC 1143 Q-method, and the usual MUD extensions. Sockets, event loops, and rationalizations for why a frog drops a codpiece when you grease him remain the responsibility of the library consumer. If you wanted a full TCP stack, TLS, or a reason to feel good about still using TELNET, this is not it.

Modules are optional in the ordinary C sense: include the headers you call and define the corresponding `*_IMPLEMENTATION` macro before those includes. The headers compile clean under `-std=c99 -Wall -Wextra -Wpedantic -Werror` (also C11/C17, and from C++11 onward) with no POSIX feature macros required. Every function is `static`, so each translation unit that actually calls Telnetty gets its own private copy and they link without fighting. We have all been burned by the alternative.

## Quickstart

The shortest path that does something useful. One context, echo, NAWS, and a willingness to compress if the peer asks nicely. It doesn't try to be clever. It will also not segfault on the first IAC.

```c
#define TELNETTY_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_options.h"
#include "telnetty_compression.h"

static void on_event(telnetty_context_t *ctx,
                     telnetty_event_type_t event,
                     const telnetty_event_data_union_t *data,
                     void *user_data) {
    (void)user_data;
    if (event == TELNETTY_EVENT_DATA) {
        /* Borrowed. Copy it if you need it later. */
        telnetty_send(ctx, data->data.data, data->data.length);
    }
}

int main(void) {
    telnetty_context_t *ctx = telnetty_create(on_event, NULL);
    if (!ctx) return 1;

    telnetty_tick(ctx, 0);                    /* start the clock */
    telnetty_set_negotiation_timeout(ctx, 5000);
    telnetty_enable_echo(ctx);
    telnetty_enable_naws(ctx);
    telnetty_compression_auto_negotiate(ctx, 1);

    /* In your read loop: */
    /* uint8_t buf[4096]; ssize_t n = read(fd, buf, sizeof buf); */
    /* if (n > 0) telnetty_process(ctx, buf, (size_t)n); */

    /* Then drain the output buffer to the socket. */
    /* Do not call telnetty_flush until every queued byte has left. */

    telnetty_destroy(ctx);
    return 0;
}
```

That is the entire happy path. Everything else is either more options or the same pattern with different callbacks. We have not invented a new way to feed bytes to a state machine.

## What it supports

### Standard TELNET options

BINARY, ECHO, SUPPRESS-GO-AHEAD, STATUS, TIMING-MARK, TERMINAL-TYPE, NAWS, TERMINAL-SPEED, LINEMODE, REMOTE-FLOW-CONTROL, NEW-ENVIRON, CHARSET, and END-OF-RECORD. The Q-method state machine (RFC 1143) helps avoid a lot of footguns while negotiating them.

### MUD and terminal protocols

| Protocol | What it does | Notes |
| --- | --- | --- |
| MSDP | Structured variable updates | Flat and nested callbacks |
| GMCP | JSON package messages | You parse the JSON; we just deliver the text |
| MTTS | Terminal capability reporting | Includes the three-cycle TTYPE exchange |
| MSSP | Server status values | |
| MXP | Inline tag tokenization | Reports tags; does not render or decide which ones are safe |
| ZMP | NUL-delimited commands and arguments | Subnegotiation payloads |
| MSP | Inline sound and music triggers | Reports them; does not play them |
| ATCP | Package and message subnegotiation | Option 200 |
| MCCP2 / MCCP3 | MUD compression, both directions | Server and client sides; requires zlib |
| MCCP1 | Legacy option negotiation | Negotiation only; no built-in inflate path |
| MCP 2.1 | In-band line messages | `#$#` framing; not a TELNET option |
| MNES | Standard MUD environment variables | Sent through NEW-ENVIRON |

ANSI, 256-color, true-color, and selected OSC helpers live in the color module. StartTLS only signals the protocol transition; TLS itself belongs to your TLS library and your socket code. WebSocket framing is not here and is not coming.

## Build and tests

You need a C99 compiler. CMake 3.10 or newer builds the project. zlib enables compression; libuv is only needed for the libuv server example. The optional Suspenders example needs a C11 compiler and the two external headers described below. None of this is exotic. If it fails to build, the problem is probably local.

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Besides the unit tests, the suite compiles every header on its own and together as strict C99 and occasionally as C++ with warnings as errors. When `python3` is available it also runs the example servers over real sockets against a scripted client that negotiates MCCP2 and MCCP3. There is also MANIAC (**M**arkov **A**lgorithm for **N**uking **I**nterpret **A**s **C**ommand), a deterministic Markov fuzz harness built from real wire traces, because hoping the parser survives adversarial input is not a strategy.

| CMake option | Default | Effect |
| --- | --- | --- |
| `BUILD_TESTS` | OFF | Build and register the test programs |
| `BUILD_EXAMPLES` | ON | Build examples that have their dependencies |
| `ENABLE_ZLIB` | ON | Look for zlib and enable compression when found |
| `ENABLE_DEBUG` | OFF | Define `TELNETTY_DEBUG_MEMORY` |
| `BUILD_DOCS` | OFF | Add a Doxygen target |
| `BUILD_SUSPENDERS_EXAMPLE` | OFF | Build the coroutine example (also set both dependency include paths) |

To build without zlib, configure with `-DENABLE_ZLIB=OFF`. The library still builds; the MCCP enable/accept calls return `-1`. You will notice.

### Use it from CMake

The `telnetty::telnetty` target carries the include path and, when zlib was found, the `TELNETTY_HAVE_ZLIB` definition and the zlib link.

```cmake
# Installed copy
find_package(telnetty 1.2 REQUIRED)

# Vendored checkout
add_subdirectory(third_party/telnetty.h)

# Fetched at configure time
include(FetchContent)
FetchContent_Declare(telnetty
    GIT_REPOSITORY https://github.com/StraylightRunMOO/telnetty.h.git
    GIT_TAG v1.2.0)
FetchContent_MakeAvailable(telnetty)

target_link_libraries(my_mud PRIVATE telnetty::telnetty)
```

Examples are only built when Telnetty is the top-level project. Without CMake, add `include/` to your include path and, for compression, define `TELNETTY_HAVE_ZLIB` and link `-lz`. The old ways still work.

### Examples

The examples live in `examples/`. Each is one file you can read top to bottom. They are not production daemons. They are starting points that happen to compile.

| Program | What it shows |
| --- | --- |
| `server_simple [port]` | `poll()` chat server: line input, NAWS/TTYPE, MCCP2/MCCP3, color, partial writes, timeouts |
| `client_simple [host] [port]` | Line-mode client: answers TTYPE/NAWS, accepts MCCP2/MCCP3 |
| `server_libuv [port]` | Echo server on a libuv loop; built only when libuv is installed |
| `server_suspenders` | Optional C11 coroutine server |

`server_suspenders` is an optional C11 echo server using [Suspenders](https://github.com/StraylightRunMOO/suspenders.h) and [Memento](https://github.com/StraylightRunMOO/memento). Suspenders currently expects Memento 3.x. It is useful when you want to see a Telnetty context inside a coroutine-based TCP loop and have already accepted the additional dependencies.

```sh
cmake -S . -B build-suspenders \
  -DBUILD_SUSPENDERS_EXAMPLE=ON \
  -DMEMENTO_INCLUDE_DIR=/path/to/memento/include \
  -DSUSPENDERS_INCLUDE_DIR=/path/to/suspenders/include
cmake --build build-suspenders --target server_suspenders
./build-suspenders/server_suspenders
```

It listens on port 4001. The example deliberately starts one worker because each Telnetty context belongs to one thread. That is a useful constraint to see in a small example, before it turns up in a production incident. You probably don't need threading for a game nobody's going to play anyway. But we admire the optimism.

## Using it in a C program

Choose the modules you need. Macros before includes. Leave out anything you do not call. Everything needs the core. `TELNETTY_MUD_IMPLEMENTATION` turns on events for MSP and MXP, but only if the macros come before every include. Include the events header without its macro and `telnetty_enable_msp` / `telnetty_enable_mxp` return `-1`. This is not a design we are proud of. It is the design that works.

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

A file that includes the headers without the macros sees only the types and constants. That is handy for passing `telnetty_context_t*` around without dragging the whole implementation into every translation unit.

Create one context per connection. Feed received bytes to `telnetty_process()`. Write the queued output to the socket. Call `telnetty_tick()` from your event loop with a monotonic clock if you care about timeouts or rate limits.

```c
static void on_telnet_event(telnetty_context_t *ctx,
                            telnetty_event_type_t event,
                            const telnetty_event_data_union_t *data,
                            void *user_data) {
    (void)user_data;

    switch (event) {
    case TELNETTY_EVENT_DATA:
        /* Borrowed pointer. Use it now or copy it. */
        telnetty_send(ctx, data->data.data, data->data.length);
        break;
    case TELNETTY_EVENT_ERROR:
        /* data->error.code is one of the TELNETTY_ERR_* values */
        break;
    default:
        break;
    }
}

telnetty_context_t *ctx = telnetty_create(on_telnet_event, NULL);
if (!ctx) { /* allocation failed */ }

telnetty_tick(ctx, monotonic_ms());
telnetty_set_negotiation_timeout(ctx, 5000);
telnetty_set_rate_limit(ctx, 64 * 1024, 100);   /* bytes/s, IAC frames/s */

telnetty_enable_echo(ctx);
telnetty_enable_naws(ctx);
telnetty_enable_ttype(ctx);
telnetty_compression_auto_negotiate(ctx, 1);
telnetty_color_init(ctx, 1);

/* each read: */
int n = telnetty_process(ctx, bytes, byte_count);
if (n < 0) { /* fatal; check last error and close */ }

/* then write the queued output. */
/* telnetty_get_output_buffer(ctx) gives the buffer. */
/* Call telnetty_flush(ctx) only after every queued byte has been written. */

telnetty_destroy(ctx);
```

`telnetty_send()` doubles any embedded IAC bytes as required. Option and protocol helpers usually start negotiation when enabled. Check their return value; `-1` means setup or send failure. A successful return does not mean negotiation is finished. Wait for the peer or drive `telnetty_tick()` against a timeout. You can drive the clock at whatever speed you like. That means our knuckle-dragging Diku friends can play too.

## Operational rules

**One context, one thread.** Contexts are not thread-safe. Keep each `telnetty_context_t` on one thread and use a separate context for each connection. Do not call `telnetty_process()` and `telnetty_tick()` concurrently on the same context. 

**Drive the clock.** `telnetty_set_negotiation_timeout()` and `telnetty_set_rate_limit()` only work if your event loop calls `telnetty_tick(ctx, now_ms)` with a monotonic clock. If nothing drives the clock, neither timeout nor rate limit will magically expire. Computers are stubborn that way.

**Treat peer content as input.** Framing is parsed and bounded, but protocol payloads still come from the network. Validate values in your callbacks. MXP reports markup; it does not decide which tags are safe to execute or render. GMCP gives your application JSON text, not a JSON parser.

**Compression.** Once negotiated it is transparent:

- **Server:** `telnetty_compression_auto_negotiate()` (or the individual enable calls) offers both. MCCP2 output is compressed inside `telnetty_send()` and every other helper. MCCP3 input is inflated inside `telnetty_process()`, including bytes that arrive in the same read as the client's start marker.
- **Client:** `telnetty_accept_mccp2()` and `telnetty_accept_mccp3()` accept the server's offers. Inflating and compressing happen in the same places.
- **Closing:** call `telnetty_compression_cleanup()` before your last flush. It ends outbound streams with `Z_FINISH` so the peer sees a clean end of stream.
- **Errors:** a corrupt inbound stream fires `EVENT_ERROR` with `TELNETTY_ERR_COMPRESSION`, and `telnetty_process()` returns `-1`. Garbage is never delivered as text.

Write output only through Telnetty (`telnetty_send` and friends). A byte appended to the output buffer directly, after MCCP2 starts, corrupts the client's stream. Define `TELNETTY_DEBUG_OUT_HOOK` in a debug build to abort at the next write if that happens. It is a blunt instrument. It works.

MCCP1 negotiates the legacy option but does not inflate its stream. We implemented the negotiation because someone asked. We did not implement the inflate because nobody has asked yet.

**TLS has a boundary.** The StartTLS helpers only signal `FOLLOWS`. Your application must switch to a TLS implementation and manage the socket transition. Telnetty does not provide TLS or WebSocket framing.

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

The public declarations and comments in each header are the most complete API reference. The examples show how the pieces fit together without requiring a tour through every RFC first. We read the RFCs so you do not have to. You are welcome.

## License

Telnetty is distributed under the [MIT License](LICENSE). Keep the license notice with redistributed copies; the code is more useful when nobody has to guess what they are allowed to do with it. If you improve it, send the patch. If you do not, that is also fine.
