# Changelog

All notable changes to Telnetty are documented in this file.

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
