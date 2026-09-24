#!/usr/bin/env python3
"""MANIAC: Markov Algorithm for Nuking Interpret As Command.

Reads tests/MANIAC.json (a telnetlib3 pytest run log annotated with wire
bytes) and emits a compact C header tests/maniac_matrices.h containing:

  - Per-direction event alphabets (short symbolic strings + byte templates)
  - First-order transition tables as flat {from_id, to_id, weight} triples

The C fuzz harness (tests/test_maniac.c) draws a token from the transition
table, emits its byte template into telnetty_process, and asserts the
context stays alive and reports no non-whitelisted errors. See the header
for the on-disk schema.

Run:  python3 tests/maniac_gen.py
"""

import collections
import json
import os
import sys

INPUT = os.path.join(os.path.dirname(__file__), "MANIAC.json")
OUTPUT = os.path.join(os.path.dirname(__file__), "maniac_matrices.h")

# Sub-corpora to emit as their own matrices, in addition to the aggregate.
# Each name is a test-file stem (without .py). Frames from tests outside
# this list still contribute to the aggregate but not to a per-feature
# corpus. Groups too small to build a k=2 chain are dropped at emit time.
SUB_CORPORA = [
    "test_core",
    "test_charset",
    "test_encoding",
    "test_environ",
    "test_linemode",
    "test_mccp",
    "test_naws",
    "test_reader",
    "test_shell",
    "test_sync",
    "test_timeout",
    "test_tspeed",
    "test_ttype",
    "test_xdisploc",
]

# TELNET command bytes
IAC = 0xFF
SB  = 0xFA
SE  = 0xF0
CMD_NAMES = {
    0xF0: "SE", 0xF1: "NOP", 0xF2: "DM", 0xF3: "BREAK", 0xF4: "IP",
    0xF5: "AO", 0xF6: "AYT", 0xF7: "EC", 0xF8: "EL", 0xF9: "GA",
    0xFB: "WILL", 0xFC: "WONT", 0xFD: "DO", 0xFE: "DONT",
}


def tokenize(hex_str):
    """Return a list of (symbol, byte_template) tuples for one wire frame.

    Symbols are strings like 'WILL:24', 'SB:39', 'DATA', 'NOP'. The
    byte template is the exact byte sequence that reproduces the token
    on the wire so the fuzz harness can replay it verbatim."""
    b = bytes.fromhex(hex_str)
    tokens = []
    i = 0
    n = len(b)
    while i < n:
        if b[i] != IAC:
            j = i
            while j < n and b[j] != IAC:
                j += 1
            # Cap DATA runs to keep templates bounded.
            take = min(j - i, 32)
            tokens.append(("DATA", bytes(b[i:i + take])))
            i = j
            continue
        # IAC ...
        if i + 1 >= n:
            i += 1
            continue
        c = b[i + 1]
        if c == IAC:
            tokens.append(("DATA", b"\xff"))
            i += 2
        elif c in (0xFB, 0xFC, 0xFD, 0xFE):
            if i + 2 >= n:
                i += 2
                continue
            opt = b[i + 2]
            tokens.append((f"{CMD_NAMES[c]}:{opt}", bytes([IAC, c, opt])))
            i += 3
        elif c == SB:
            if i + 2 >= n:
                i += 2
                continue
            opt = b[i + 2]
            j = i + 3
            while j + 1 < n and not (b[j] == IAC and b[j + 1] == SE):
                if b[j] == IAC and j + 1 < n and b[j + 1] == IAC:
                    j += 2
                else:
                    j += 1
            payload = bytes(b[i + 3:j])
            frame = bytes([IAC, SB, opt]) + payload + bytes([IAC, SE])
            # Cap total SB template to keep header manageable.
            if len(frame) > 96:
                frame = frame[:94] + bytes([IAC, SE])
            tokens.append((f"SB:{opt}", frame))
            i = j + 2
        else:
            tokens.append((CMD_NAMES.get(c, f"CMD:{c}"), bytes([IAC, c])))
            i += 2
    return tokens


def build(records, direction):
    """Return (alphabet, k1_transitions, k2_transitions) for one direction.

    k1_transitions:  (from, to)         -> count   — first-order fallback
    k2_transitions:  (prev, cur, to)    -> count   — second-order primary
    """
    alphabet = {}
    k1 = collections.Counter()
    k2 = collections.Counter()
    for hex_str in records:
        toks = tokenize(hex_str)
        for sym, tmpl in toks:
            if sym not in alphabet:
                alphabet[sym] = tmpl
        # k=1
        for a, b in zip(toks, toks[1:]):
            k1[(a[0], b[0])] += 1
        # k=2: needs 3-token windows
        for a, b, c in zip(toks, toks[1:], toks[2:]):
            k2[(a[0], b[0], c[0])] += 1
    return alphabet, k1, k2


def _stem_of(record):
    t = record.get("test") or ""
    base = t.split("::")[0]
    return os.path.basename(base).replace(".py", "")


def load_records():
    """Returns:
        aggregate: {"s2c": [hex, ...], "c2s": [hex, ...]}
        per_test:  {"test_foo": {"s2c": [...], "c2s": [...]}, ...}
    """
    aggregate = {"s2c": [], "c2s": []}
    per_test = {name: {"s2c": [], "c2s": []} for name in SUB_CORPORA}
    with open(INPUT) as f:
        for line in f:
            r = json.loads(line)
            if r.get("event") != "data":
                continue
            hex_str = r.get("hex")
            if not hex_str:
                continue
            d = r["dir"]
            if d not in aggregate:
                continue
            aggregate[d].append(hex_str)
            stem = _stem_of(r)
            if stem in per_test:
                per_test[stem][d].append(hex_str)
    return aggregate, per_test


def emit_bytes(name, data):
    out = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        out.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    out.append("};")
    return "\n".join(out)


def _dump_dir(prefix, alphabet, k1, k2):
    """Emit the C source for one (corpus, direction) matrix. `prefix` is
    the corpus-and-direction-scoped C identifier prefix, e.g.
    `maniac_s2c` for aggregate or `maniac_test_naws_s2c` for a sub-corpus."""
    symbols = sorted(alphabet.keys())
    idx = {s: i for i, s in enumerate(symbols)}

    lines = []
    lines.append(f"/* --- {prefix}: {len(symbols)} symbols, "
                 f"{len(k1)} k=1 edges, {len(k2)} k=2 edges --- */")

    for i, s in enumerate(symbols):
        lines.append(emit_bytes(f"{prefix}_tpl_{i}", alphabet[s]))

    lines.append(f"static const maniac_symbol_t {prefix}_alphabet[] = {{")
    for i, s in enumerate(symbols):
        lines.append(f'    {{ "{s}", '
                     f'{prefix}_tpl_{i}, {len(alphabet[s])} }},')
    lines.append("};")
    lines.append(f"#define {prefix.upper()}_N {len(symbols)}")

    lines.append(f"static const maniac_edge_t {prefix}_edges[] = {{")
    for (a, b), w in sorted(k1.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        lines.append(f"    {{ {idx[a]}, {idx[b]}, {w} }},")
    lines.append("};")
    lines.append(f"#define {prefix.upper()}_EDGES {len(k1)}")

    lines.append(f"static const maniac_edge2_t {prefix}_edges2[] = {{")
    for (a, b, c), w in sorted(
            k2.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2])):
        lines.append(f"    {{ {idx[a]}, {idx[b]}, {idx[c]}, {w} }},")
    lines.append("};")
    lines.append(f"#define {prefix.upper()}_EDGES2 {len(k2)}")

    return "\n".join(lines)


# Minimum k=2 edges a sub-corpus must have to be worth emitting: below
# this the chain collapses too often to be an interesting fuzz signal.
# k=1 fallback still catches short chains, so this floor is intentionally
# low — the goal is "does this feature's own traffic shape parse cleanly."
MIN_K2_EDGES = 4


def write_header(aggregate, per_test, path):
    with open(path, "w") as f:
        f.write("/* AUTO-GENERATED by tests/maniac_gen.py — do not edit. */\n")
        f.write("#ifndef TELNETTY_MANIAC_MATRICES_H\n")
        f.write("#define TELNETTY_MANIAC_MATRICES_H\n")
        f.write("#include <stddef.h>\n\n")

        # Shared type declarations — one definition, many corpora.
        f.write("typedef struct {\n"
                "    const char*        name;\n"
                "    const unsigned char* bytes;\n"
                "    size_t             length;\n"
                "} maniac_symbol_t;\n"
                "typedef struct {\n"
                "    unsigned short from;\n"
                "    unsigned short to;\n"
                "    unsigned int   weight;\n"
                "} maniac_edge_t;\n"
                "typedef struct {\n"
                "    unsigned short prev;\n"
                "    unsigned short cur;\n"
                "    unsigned short to;\n"
                "    unsigned int   weight;\n"
                "} maniac_edge2_t;\n\n")

        # Aggregate: kept under the same identifiers the harness historically
        # used (maniac_s2c_*, maniac_c2s_*), so a fresh regen is a drop-in.
        for d in ("s2c", "c2s"):
            a, k1, k2 = build(aggregate[d], d)
            f.write(_dump_dir(f"maniac_{d}", a, k1, k2))
            f.write("\n\n")

        # Sub-corpora. Each gets its own prefix. Corpora with too few edges
        # to sustain a k=2 walk are dropped.
        emitted = []
        for name in SUB_CORPORA:
            for d in ("s2c", "c2s"):
                records = per_test[name][d]
                if not records:
                    continue
                a, k1, k2 = build(records, d)
                if len(k2) < MIN_K2_EDGES:
                    continue
                prefix = f"maniac_{name}_{d}"
                f.write(_dump_dir(prefix, a, k1, k2))
                f.write("\n\n")
                emitted.append((prefix, len(a), len(k1), len(k2)))

        # Registry: harness iterates this. Aggregate + sub-corpora.
        f.write("typedef struct {\n"
                "    const char*             label;\n"
                "    const maniac_symbol_t*  alphabet;\n"
                "    int                     n_syms;\n"
                "    const maniac_edge_t*    edges;\n"
                "    int                     n_edges;\n"
                "    const maniac_edge2_t*   edges2;\n"
                "    int                     n_edges2;\n"
                "} maniac_corpus_t;\n\n")

        f.write("static const maniac_corpus_t maniac_corpora[] = {\n")
        for d in ("s2c", "c2s"):
            f.write(f'    {{ "aggregate/{d}", '
                    f'maniac_{d}_alphabet, MANIAC_{d.upper()}_N, '
                    f'maniac_{d}_edges,    MANIAC_{d.upper()}_EDGES, '
                    f'maniac_{d}_edges2,   MANIAC_{d.upper()}_EDGES2 }},\n')
        for prefix, _, _, _ in emitted:
            # Label is the trailing feature/dir chunk after `maniac_`.
            label = prefix.replace("maniac_", "", 1)
            f.write(f'    {{ "{label}", '
                    f'{prefix}_alphabet, {prefix.upper()}_N, '
                    f'{prefix}_edges,    {prefix.upper()}_EDGES, '
                    f'{prefix}_edges2,   {prefix.upper()}_EDGES2 }},\n')
        f.write("};\n")
        f.write(f"#define MANIAC_CORPORA_N {2 + len(emitted)}\n\n")

        f.write("#endif /* TELNETTY_MANIAC_MATRICES_H */\n")

    return emitted


def main():
    aggregate, per_test = load_records()
    emitted = write_header(aggregate, per_test, OUTPUT)
    print(f"wrote {OUTPUT}")
    for d in ("s2c", "c2s"):
        a, k1, k2 = build(aggregate[d], d)
        print(f"  aggregate/{d}: {len(a)} symbols, "
              f"{len(k1)} k=1 edges, {len(k2)} k=2 edges")
    for prefix, n_syms, n_k1, n_k2 in emitted:
        label = prefix.replace("maniac_", "", 1)
        print(f"  {label}: {n_syms} symbols, "
              f"{n_k1} k=1 edges, {n_k2} k=2 edges")


if __name__ == "__main__":
    sys.exit(main())
