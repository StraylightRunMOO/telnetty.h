# Changelog

All notable changes to Telnetty are documented in this file.

## [1.2.0] — 2026-10-10

Focus: more MUD protocols, working compression in both directions, safer
byte handling, headers that build cleanly in other people's projects, and
tests for the awkward cases.

### Upgrading from 1.1

Most changes add APIs. These change behavior on the wire or at build time:

- `TELNETTY_TELOPT_MCP` moved from 200 to 207. 200 is ATCP.
- MCCP3 is now real. A server that calls `telnetty_enable_mccp3` inflates
  client input inside `telnetty_process()`. Callers of
  `telnetty_process_inbound()` need no change.
- MCP 2.1 follows the spec's handshake and ignores messages without the
  session key. The placeholder builtin packages are inactive.
- GMCP `Core.Hello` is sent by the client side only.
- NEW-ENVIRON frames start with the RFC 1572 `IS` byte.
- A file that includes a header without its implementation macro now sees
  types and constants only. Calling a function from such a file was never
  linkable; it is now a compile-time error.

### Added — protocol surface

- **MXP** (MUD eXtended Protocol, TELOPT 91): bounded incremental inline
  tag tokenizer with quoted-attribute handling, tag callback, negotiated
  dispatch, and caller-controlled OPEN/SECURE/LOCKED mode state. MXP tags
  are reported to the application; they are not rendered or treated as safe.
- **ZMP** (Zenith MUD Protocol, TELOPT 93): NUL-delimited command and
  argument parsing plus a bounded sender. Dispatch requires negotiated
  option state.

- **ATCP** (Achaea Telnet Client Protocol, TELOPT 200):
  `telnetty_enable_atcp(ctx, cb, user_data)` and
  `telnetty_atcp_send(ctx, package, message, payload, len)`. Frames are
  `<package>.<message> <payload>` and dispatch through
  `telnetty_atcp_callback_t` (`telnetty_mud.h`).
- **MSP** (MUD Sound Protocol, TELOPT 90): `telnetty_enable_msp` scans
  every inbound DATA frame for `!!SOUND(...)` / `!!MUSIC(...)` triggers
  and parses the V/L/P/C/T/U arguments into a
  `telnetty_msp_trigger_t`. The MSP scanner runs on a HIGH-priority
  extended-event handler so app DATA callbacks still see the full,
  untouched text stream.
- **MCCP1** (TELOPT 85): `telnetty_enable_mccp1` — legacy compression
  protocol responds cleanly to peer offers rather than being silent.
  Negotiation only; telnetty never compresses or inflates with MCCP1.
- **MNES** (MUD NEW-ENVIRON Standard) canonical variable names and
  `telnetty_send_mnes(ctx, client_name, version, term, charset, mtts,
  screen_reader)` on top of the existing RFC 1572 NEW-ENVIRON.
- **StartTLS** (TELOPT 46, draft-altman): `telnetty_enable_starttls` +
  `telnetty_starttls_send_follows`. A peer `FOLLOWS` SB fires
  `TELNETTY_STARTTLS_EVENT_ID` (custom event) so the app knows to
  hand its socket to a TLS layer. The library performs no crypto.
- **OSC 8 hyperlinks**: `telnetty_osc8_link(ctx, params, url, text)`
  emits `\e]8;<p>;<url>\e\\<text>\e]8;;\e\\` into the output buffer.
- **OSC 4 / 10 / 11 palette queries**:
  `telnetty_osc_query_color(ctx, code, index)` — terminal responds
  through the ordinary DATA path.
- **MTTS canonical bit constants**: added `MNES` (512) and `MSLP`
  (1024) to `telnetty_mtts_flag_t`; verified existing bits 1..256 match
  the MTTS 3.0 standard.
- **LINEMODE deep-dive** (`telnetty_options.h`): full RFC 1184 MODE-ACK
  round-trip and SLC triple parsing into a per-function table
  (`telnetty_get_slc(ctx, func, &flags, &value)`), with defensive
  partial-triple handling. `telnetty_linemode_send_mode(ctx, mode)` for
  initiating a MODE offer.

### Changed — TELOPT 200 collision fix

- `TELNETTY_TELOPT_MCP` moved from **200 to 207** (via a new alias
  `TELNETTY_MCP_STATE_TELOPT`). MCP 2.1 is an in-band `#$#` protocol
  and has no TELNET option; the old 200 slot collided with ATCP.
  `telnetty_enable_mcp` no longer sends `IAC WILL 200` on the wire —
  it only attaches state so the freer path runs on destroy. Source
  callers using the `TELNETTY_TELOPT_MCP` macro are unaffected; the
  literal `200` now correctly points at ATCP.

### Fixed — input and output bounds

- `telnetty_buffer_append` now checks size arithmetic before growing a buffer
  and refuses lengths that cannot fit its legacy `int` return value.
- `telnetty_send_environ` no longer copies uncounted string terminators into
  the NEW-ENVIRON frame. It checks payload size and escapes reserved marker
  bytes from RFC 1572.
- `telnetty_gmcp_send_supports` now quotes module names as JSON strings,
  including quotes, backslashes, and control characters.
- MCP message construction writes the measured bytes directly, so a caller's
  exact-size buffer is enough; it also checks size arithmetic.
- Color-buffer capacity checks now use subtraction after validating the
  current position, avoiding wraparound in the size calculation.
- The simple socket examples include the POSIX `select` declarations and use
  the current `hostent` address-list field.

### Fixed — compression and stream integrity

- **MCCP3 now inflates client input.** The roles were reversed (the server
  set up a deflate stream on DO) and nothing handled the client's
  `IAC SB MCCP3 IAC SE` start marker, so a client such as TinTin++ had its
  compressed bytes parsed as keystrokes. The server now inflates from the
  marker on, including bytes after the marker in the same read. On the
  client's `Z_STREAM_END` it goes back to plain parsing. A corrupt stream
  fires `EVENT_ERROR` (`TELNETTY_ERR_COMPRESSION`), sends `WONT MCCP3`, and
  is never parsed as text. `telnetty_mccp3_get_stats` reports real numbers.
- **`telnetty_process()` honors the input hook.** Existing callers get
  decompression without code changes, and `telnetty_process_inbound()` is now
  an alias. If a handler installs a hook partway through a buffer, the rest
  of that buffer goes to the hook. Nested calls from inside a hook parse
  directly.
- **Color and OSC helpers no longer corrupt MCCP2 streams.**
  `telnetty__color_emit`, `telnetty_osc8_link` and `telnetty_osc_query_color`
  appended straight to `out_buffer`, bypassing the output hook. They now go
  through `telnetty_send`, which also IAC-escapes 0xFF bytes in OSC 8 URLs
  and text.
- MCCP2 output with the hook installed but no live stream is now an error,
  not a raw append into the compressed stream. Disabling MCCP2 (or a peer's
  DONT) ends the stream with `Z_FINISH`, so the client resumes plain parsing
  at the right byte. Calling enable twice no longer frees live state.
- Define `TELNETTY_DEBUG_OUT_HOOK` to abort when bytes reach `out_buffer`
  around an installed output hook. The extended test suite enables it.

### Added — compression client side

- `telnetty_accept_mccp2(ctx)`: inflate the server's MCCP2 stream.
- `telnetty_accept_mccp3(ctx, level)`: answer the server's MCCP3 offer,
  send the start marker, and compress our output.
- `telnetty_compression_cleanup` now ends running streams and removes the
  hooks. `telnetty_compression_get_stats` zero-fills the fields it doesn't
  track.

### Fixed — protocol correctness

- **MCP 2.1 handshake follows the spec.** In server mode, telnetty now stores
  the key from the client's `#$#mcp authentication-key:` reply instead of
  answering with an auth-key line of its own. In client mode, it chooses a
  key when the server's opening line arrives. Every later message must carry
  the session key, and messages without it are ignored. `mcp-negotiate` is
  advertised as MCP 2.1 requires, packages are advertised at their own
  versions, and `mcp-negotiate-can` records `peer_supports`.
- The placeholder MCP builtin packages (`mcp-core`, `simple-edit`, `redirect`,
  `dns`) have empty handlers and are now registered inactive. Before, `dns`
  prefix-matched every real `dns-org-mud-moo-*` message and dropped it before
  it could reach the default handler.
- `telnetty_disable_mcp` affects only its own context. It used to clear the
  data hook for every connection in the process.
- `telnetty_mcp_get_packages` and `telnetty_mcp_generate_message_id` are
  implemented. The message ID counter is per context, not process-wide.
- GMCP: `Core.Hello` is sent by the client side (the peer's WILL), not by a
  server answering DO. Payloads over 1 KiB reach the callback whole instead
  of truncated. `telnetty_gmcp_register_module` / `_unregister_module`
  maintain the module list and send `Core.Supports.Add` / `.Remove` once
  GMCP is up.
- MSDP: `telnetty_msdp_get`, `_set_flags`, `_clear_flags` and `_remove` are
  backed by a real variable table (scalar values received or sent).
  `telnetty_msdp_request` sends `VAR "SEND" VAL <name>` as the MSDP spec
  requires.
- NEW-ENVIRON: frames now start with the RFC 1572 `IS` command byte
  (`telnetty_send_environ_info` sends `INFO`). Peer `IS` / `INFO` replies are
  parsed and stored for `telnetty_get_environ`. `telnetty_enable_environ` no
  longer leaks an unregistered tracker.
- TTYPE and NAWS: `telnetty_get_terminal_type` and `telnetty_get_window_size`
  return what the peer sent. The TTYPE state no longer leaks.
- `telnetty_color_clear_attribute(BOLD)` emits SGR 22, not 21 (double
  underline), and `NO_*` values are emitted as-is.

### Fixed — build

- The extended-event convenience wrappers are only defined with
  `TELNETTY_EVENTS_IMPLEMENTATION`, which clears `-Wundefined-internal` in
  translation units that include the header without the implementation.
- The core hook pointers are `static`, so two translation units can both
  define `TELNETTY_IMPLEMENTATION` without duplicate-symbol link errors.

### Changed — headers are clean for downstream builds

- A program that includes the headers with `-Wall -Wextra -Wpedantic` gets
  no warnings. Every header-defined function is marked `TELNETTY_UNUSED`;
  before, a typical program saw about 80 unused-function warnings.
- The headers compile as strict ISO C99/C11/C17 with no POSIX feature
  macros. Internal `strdup` / `strndup` calls became allocator-aware
  helpers, so strings are now allocated with the same overridable
  `*_MALLOC` macro that frees them. `posix_memalign` was replaced too (see
  below).
- The headers build as C++11 and later (four implicit `void*` / enum
  conversions fixed).
- Function prototypes are declared only when the module's implementation
  macro is defined. A file that includes a header without the macro sees
  types and constants only, and no longer gets a "declared static but never
  defined" warning for every function.

### Fixed — 1.2.0 polish

- Negotiation timeouts for options offered before the first
  `telnetty_tick()` (the usual connection setup) were measured from 0, so
  the first tick with a real monotonic clock timed every one of them out.
  Such options now start their timeout at the first tick.
- MSP and MXP need the extended-event implementation. Without
  `TELNETTY_EVENTS_IMPLEMENTATION`, any program using them failed to link,
  and every translation unit got a never-defined warning.
  `TELNETTY_MUD_IMPLEMENTATION` now turns the events implementation on.
  If `telnetty_events.h` was already included without it, the enable calls
  return `-1`. MSP also no longer ignores a failed handler registration.
- `TELNETTY_BUFFER_ALLOC_ALIGNED` on Windows released `_aligned_malloc`
  memory with `free()`. Aligned buffers now use C11 `aligned_alloc` where
  available and `malloc` otherwise; both are released with `free()`. The
  unused `<pthread.h>` / `<windows.h>` includes are gone.
- `telnetty_msdp_send_variable` was declared but never defined; the
  declaration is removed.
- `telnetty_process_events` and `telnetty_event_create_connect` document
  what they actually do (a compatibility no-op; the port is not carried).

### Changed — build and packaging

- The version is read from `telnetty_core.h`, so CMake and the header
  always agree.
- `telnetty::telnetty` now carries `TELNETTY_HAVE_ZLIB` and links
  `ZLIB::ZLIB`. Before, consumers got the zlib link but not the define, so
  compression silently compiled out of their code.
- `find_package(telnetty)` works: the install now ships
  `telnetty-config.cmake` and a version file. `add_subdirectory` and
  FetchContent work too, and examples build only when Telnetty is the
  top-level project.
- Builds no longer force `-O3 -DNDEBUG` or define `_POSIX_C_SOURCE` and
  `-Wno-unused-function` globally. The default build type is Release, and
  warnings and POSIX macros are set per target.
- The Doxygen target is behind `BUILD_DOCS` and no longer needs the missing
  `Doxyfile.in`. Before, configure failed on any machine with doxygen
  installed.
- GitHub Actions CI: gcc and clang, zlib on and off, ASan/UBSan, macOS.

### Changed — examples

- `server_simple`, `client_simple`, and `server_libuv` are rewritten. None
  of them used to write Telnetty's output to the socket, so clients received
  nothing. The libuv server also touched handles across threads and could
  crash on shutdown.
  - `server_simple`: a `poll()` chat server showing line input, NAWS/TTYPE,
    MCCP2/MCCP3, color, partial writes, and timeouts.
  - `client_simple`: a line-mode client that answers TTYPE/NAWS and accepts
    MCCP2/MCCP3.
  - `server_libuv`: a correct single-loop echo server.
- The new `tests/e2e_examples.py` runs the examples over real sockets
  against a scripted client that negotiates MCCP2 and MCCP3 the way
  TinTin++ does.

### Added — test infrastructure

- **MANIAC** (Markov Algorithm for Nuking Interpret As Command). A
  corpus-driven fuzz harness that walks per-direction transition
  matrices built from real telnetlib3 wire traces (`tests/MANIAC.json`
  → `tests/maniac_matrices.h`, generated by `tests/maniac_gen.py`).
  Second-order (k=2) draws with k=1 and uniform fallbacks; 22 corpora
  (aggregate + 20 per-feature sub-corpora); iterated across four chunk
  cadences and a spread of seeds. Whitelist errors: `PROTOCOL`,
  `SB_OVERFLOW`, `CHARSET_UNSUPPORTED`. Default: 4 seeds × 200 steps
  per corpus; `--long`: 16 × 5000 steps (~7M draws).
- **Split-input torture** in `test_extended`: IAC across chunks, IAC
  escape, SB with IAC escape, byte-at-a-time binary transparency.
  Every chunking must produce identical event streams.
- Test file renamed: `test_polish.c` → `test_extended.c`. Scope had
  broadened past 1.1 polish.
- Added boundary coverage for buffer capacity, NEW-ENVIRON escaping, GMCP
  JSON escaping, MXP split tags and repeated enable, and ZMP round trips.
- Reworked the README and project guide around the actual build, threading,
  transport, and protocol boundaries; added the repository's MIT license.

## [1.1.0] — 2026-09-22

Focus: production polish. Stability, protocol correctness, and hot-path
performance. All existing public function signatures are preserved — this is a
minor release with additive-only API changes.

### Fixed — crash / undefined-behavior

- **Buffer pool pre-alloc** (`telnetty_buffer.h`): a failed
  `telnetty_buffer_create_ex` in `telnetty_buffer_pool_create` used to leave
  NULL slots in the middle of the dense array. `pool_get`'s LIFO index would
  then return NULL from the middle and crash on next use. The pool now packs
  successful allocations densely and tears down cleanly on any failure.
- **`buffer_chain_split` tail aliasing** (`telnetty_buffer.h`): the old code
  read `chain->tail` *after* reassigning it, so `new_chain->tail` pointed at
  the wrong end. The within-buffer branch also never actually split a node —
  it just moved whole buffers. Both are fixed; the within-buffer branch now
  copies the tail bytes into a fresh dynamic buffer and re-links.
- **Zero-size buffer grow loops** (`telnetty_core.h`, `telnetty_buffer.h`):
  `while (new_size < needed) new_size *= 2` would spin at 0 forever if a
  reference-buffer view came through. Clamped to `TELNETTY_INITIAL_BUFFER_SIZE`
  before the doubling loop.
- **`fire_custom_event` strlen on binary data** (`telnetty_events.h`): the
  legacy signal-only helper ran `strlen` on the caller's `void*`, which is UB
  for non-string payloads. Added `telnetty_fire_custom_event_bin(ctx, id,
  data, length, prio)` that takes an explicit length; the legacy variant now
  routes through it with `length = 0`.
- **`register_option`/`register_sb_handler` user_data clobber**
  (`telnetty_core.h`): both entrypoints used to overwrite `opt->user_data`
  without invoking the freer, silently leaking any owned payload installed
  through `telnetty_option_set_user_data`. They now leave a non-NULL
  `user_data` untouched. Callers that want to replace ownership must go
  through `telnetty_option_set_user_data`, which honors the freer.
- **SB payload silent truncation** (`telnetty_core.h`): the 256-byte cap was
  smaller than routine MSSP / GMCP / MCCP frames, and overflow silently
  dropped bytes. The parser now caps at `TELNETTY_MAX_SB_LENGTH` (64 KiB by
  default) and fires `TELNETTY_EVENT_ERROR` with
  `TELNETTY_ERR_SB_OVERFLOW` on overflow before returning to `STATE_DATA`.
- **`IAC + non-SE/non-IAC` inside SB** (`telnetty_core.h`): previously
  smuggled the stray bytes into the payload; now fires
  `TELNETTY_EVENT_ERROR` with `TELNETTY_ERR_PROTOCOL` and resumes in the
  SUBNEG state, which matches RFC 855's "undefined behavior" text more
  defensively.
- **Empty SB event payload NULL pointer** (`telnetty_core.h`): when a peer
  sent `IAC SB opt IAC SE` with no bytes, `sb_buffer->data` could be NULL
  and downstream consumers would deref. The event now uses an empty-string
  sentinel with `length == 0`.
- **Color emitter bounds** (`telnetty_color.h`): every `snprintf`
  accumulator (`telnetty_color_string_ansi/256/true`) used
  `buffer_size - offset` after `offset` had already crossed `buffer_size`,
  underflowing `size_t` and writing far past the caller's buffer. New
  internal `telnetty__color_appendf` / `_appendc` helpers clamp on every
  step and short-circuit to `-1` on truncation. `_appendc` bounds the bare
  `buffer[offset++]` writes too.

### Fixed — protocol correctness

- **RFC 1143 Q-method WANTNO_OP transient** (`telnetty_core.h`): the
  transient re-request branches (peer WONT with `him=Q_WANTNO_OP`; peer
  DONT with `us=Q_WANTNO_OP`) previously fired `EVENT_DO`/`EVENT_WILL` and
  called the option handler with `hcmd=DO/WILL` *before* the negotiation
  settled. That caused spurious `mccp2_begin`, `gmcp_send_hello`, and
  `charset_send_request`. The re-request is still issued on the wire, but
  no user-facing settled event fires until the peer's confirming reply
  lands.
- **`is_mccp3_enabled` hardcoded false** (`telnetty_compression.h`): now
  reads the real user_data state.
- **MCCP3 direction preset** (`telnetty_compression.h`): `enable_mccp3`
  used to set `server_mode = true` before negotiation, so a peer's WILL
  would (incorrectly) init a deflate side. The preset is removed;
  `mccp3_handler` sets the direction from the ACK.
- **`mccp2_out_hook` sizing + retry** (`telnetty_compression.h`): the
  `len + 64` heuristic under-allocated for tiny inputs whose zlib overhead
  exceeded 64 bytes. Now uses `deflateBound(strm, len) + 16` and retries
  on short-write (`avail_in > 0`) with a doubled sink buffer.
- **`mccp3_decompress` distinguishes zlib returns**
  (`telnetty_compression.h`): `Z_STREAM_END` now triggers `inflateReset`
  so post-stream bytes decode cleanly; `Z_BUF_ERROR` is treated as
  progress; `Z_DATA_ERROR`/`Z_MEM_ERROR`/`Z_NEED_DICT` return -1.
- **CHARSET ACCEPTED validation** (`telnetty_options.h`): the ACCEPTED
  handler used to store any string the peer sent into
  `selected_charset` — a peer could smuggle arbitrary bytes into what
  `telnetty_get_charset` returns. Now the accepted charset must match one
  of the offered `supported_charsets`; on mismatch the ACCEPTED is
  refused via `TELNETTY_EVENT_ERROR` with
  `TELNETTY_ERR_CHARSET_UNSUPPORTED`.
- **CHARSET TTABLE reject** (`telnetty_options.h`): TTABLE-IS,
  TTABLE-REJECTED, TTABLE-ACK, TTABLE-NAK used to fall through silently;
  now reply with TTABLE-REJECTED (translation tables not implemented) so
  peers don't retry indefinitely.

### Added — performance

- **SB payload fast path** (`telnetty_core.h`): once the SB option byte is
  consumed, `telnetty_process` drains a run of non-IAC bytes with a
  single `memchr` scan and one `telnetty_buffer_append`. Removes the
  per-byte callback + capacity-check overhead that dominated MSSP / GMCP
  frames.
- **`buffer_find` complexity** (`telnetty_buffer.h`): replaced the naive
  O(nm) nested scan with a `memchr(first_byte)` + `memcmp` loop.
  Roughly 5-10× on typical MSSP/GMCP framing sizes.
- **Branch hints + `restrict`** (`telnetty_core.h`): added
  `TELNETTY_LIKELY/UNLIKELY` macros (GCC/Clang; no-op elsewhere), tagged
  the IAC branch as unlikely in the DATA hot path, and marked
  `telnetty_process` / `telnetty_send`'s `data` pointer `restrict`.
- **Pool exhaustion falls back to a fresh allocation**
  (`telnetty_buffer.h`): `telnetty_buffer_pool_get` no longer returns
  NULL when the pool is empty; it allocates a fresh buffer of the pool's
  configured size. The miss is still counted in `pool_misses`.

### Added — public API (all additive; no signatures renamed or removed)

- `TELNETTY_VERSION_MAJOR`, `TELNETTY_VERSION_MINOR`,
  `TELNETTY_VERSION_PATCH`, `TELNETTY_VERSION_STRING`,
  `TELNETTY_VERSION_HEX` — compile-time version macros.
- `telnetty_error_t` enum with `TELNETTY_OK` and error codes for the
  common failure modes (`ALLOC`, `SB_OVERFLOW`, `PROTOCOL`, `TIMEOUT`,
  `RATE_LIMIT`, `INVAL`, `NOT_IMPLEMENTED`, `CHARSET_UNSUPPORTED`,
  `COMPRESSION`). Consumed by:
- `telnetty_last_error(ctx)` — read the most recent error surfaced
  through `EVENT_ERROR`.
- `telnetty_set_input_hook(ctx, hook, user_data)` and
  `telnetty_process_inbound(ctx, data, length)` — companion of the
  existing `telnetty_set_out_hook`, so MCCP-inflate on the client side
  can plug in without every caller writing its own trampoline.
- `telnetty_default_option_policy(ctx, allow, n_allow, deny, n_deny)` —
  replace the WILL/DO switch boilerplate in every example / consumer.
- `telnetty_fire_custom_event_bin(ctx, id, data, length, prio)` —
  binary-safe replacement for `telnetty_fire_custom_event`.
- **Structured logging**: `telnetty_log_level_t` (TRACE/DEBUG/INFO/WARN/
  ERROR), `telnetty_log_callback_t`,
  `telnetty_set_log_callback(ctx, level, cb, user_data)`, and
  `telnetty_log(ctx, level, category, fmt, ...)`. `report_error`
  automatically routes at ERROR level. NULL sink = silent (no stderr
  fallback).
- **Negotiation timeout**:
  `telnetty_set_negotiation_timeout(ctx, ms)` records a per-option
  deadline whenever `telnetty_send_option` pushes an option into any
  WANT* state. `telnetty_tick(ctx, now_ms)` fires `EVENT_ERROR` with
  `TELNETTY_ERR_TIMEOUT` on expiry and resets the pending side to
  `Q_NO` so the caller can retry. Deadlines auto-clear when both sides
  settle to `Q_NO`/`Q_YES`.
- **Rate limiting**:
  `telnetty_set_rate_limit(ctx, bytes_per_sec, iac_per_sec)` installs
  token buckets that refill on each `telnetty_tick`. Zero either bucket
  to disable it. Trips fire `EVENT_ERROR` with `TELNETTY_ERR_RATE_LIMIT`
  and drop the offending batch. Requires `telnetty_tick` to be driven
  by the caller; unticked contexts skip enforcement.
- **Structured MSDP**: `telnetty_msdp_recursive_callback_t` +
  `telnetty_msdp_set_recursive_callback(ctx, cb, user_data)` surface
  `EV_VAR`/`EV_VAL`/`EV_TABLE_OPEN`/`EV_TABLE_CLOSE`/`EV_ARRAY_OPEN`/
  `EV_ARRAY_CLOSE` with depth + dot-joined parent path. Runs
  side-by-side with the flat `telnetty_enable_msdp` handler for
  backward compatibility. Bounded at `TELNETTY_MSDP_MAX_DEPTH` (16).
- **Extended event system** (`telnetty_events.h`): the previously-stub
  `telnetty_register_event_handler` / `telnetty_unregister_event_handler`
  / `telnetty_fire_event_ex` are now a real priority-ordered linked
  list on `ctx->event_handlers`. HIGH fires before NORMAL fires before
  LOW regardless of registration order.
- **MTTS three-cycle handshake** (`telnetty_mud.h`): full cycle-1
  client name, cycle-2 terminal type, cycle-3 `MTTS <flags>` state
  machine driven by `telnetty_mtts_sb_handler`. Server-side captures
  peer name/term/flags; client-side responds with configurable
  `local_flags` (defaults ANSI | UTF8 | 256COLORS).
- **Color state attachment** (`telnetty_color.h`):
  `telnetty_color_init` / `_attach` install a `telnetty_color_state_t`
  on the context. Every `set_foreground` / `set_background` /
  `set_attribute` / `reset*` now actually emits SGR into
  `ctx->out_buffer` using bounds-checked helpers. Added 256-color and
  24-bit RGB variants + `rgb_to_256` using the correct xterm 6×6×6
  cube stops.
- **MCP 2.1 in-band wire format** (`telnetty_mcp.h`): rewritten to
  emit `#$#package-message <auth-key> key: value ...` in-band (per
  MCP 2.1) instead of the previous IAC SB 200 wrapper. Real tokenizer
  with quoted-arg support (`\"` and `\\` escapes), `_data-tag` cord
  tracking (bounded at 32 open cords), and the `mcp` / `mcp-negotiate-can` /
  `mcp-negotiate-end` handshake. Auth key generation prefers
  `/dev/urandom` when available with a `time(NULL) ^ (uintptr_t)ctx`
  fallback. Line length capped at `TELNETTY_MCP_MAX_LINE` (64 KiB).

### Notes

- Added an optional `server_suspenders` example showing a Telnetty echo
  server inside Suspenders, with Memento-backed connection records. It is
  C11-only and opt-in so the core library remains C99 and dependency-free.
- MANIAC now compares parser framing state with an independent model after
  every feed chunk and reads generated transition structs without
  alignment-sensitive casts. The generated corpus still models incoming
  wire symbols; computed-goto action dispatch was left out to keep this C99
  harness portable and because it does not yet have outbound action records.
- Test builds now handle zlib being disabled: compression-specific cases
  skip while the rest of the suite still runs. CMake also ignores stale
  cached zlib discovery when `ENABLE_ZLIB=OFF`.

- The prior `TELNETTY_MAX_OPTION_LENGTH` (256) is retained as a legacy
  alias; the parser uses `TELNETTY_MAX_SB_LENGTH` (64 KiB default).
- **Thread safety**: one `telnetty_context_t` per thread. The
  `TELNETTY_POOL_FLAG_THREAD_SAFE` / `TELNETTY_QUEUE_FLAG_THREAD_SAFE`
  flags remain declared for source-compatibility but have no effect and
  should be considered deprecated. Rate-limit and negotiation-timeout
  state, in particular, is not synchronized — driving `telnetty_tick`
  concurrently with `telnetty_process` from a second thread is
  undefined.
- SB payload footprint: `telnetty_context_t` already owns a persistent
  256-byte `sb_buffer` that is reset (not freed) between frames, so
  small SB payloads (NAWS, TTYPE, short MTTS) are already zero-alloc
  per frame after context creation. The planned `sb_inline[64]`
  optimization is redundant and was not added.
