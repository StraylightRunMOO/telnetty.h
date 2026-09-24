/**
 * @file test_maniac.c
 * @brief MANIAC — Markov Algorithm for Nuking Interpret As Command.
 *
 * A deterministic fuzz harness that walks a first-order Markov chain built
 * from real telnetlib3 wire traces (tests/MANIAC.json → maniac_matrices.h),
 * feeding chunks of each transition's byte template into telnetty_process
 * across a range of chunkings (1..8 byte splits + one-shot).
 *
 * The invariants asserted per iteration:
 *   - telnetty_process never returns < 0 (parser doesn't self-abort)
 *   - the context stays parseable across the full walk
 *   - no error surfaces other than TELNETTY_ERR_PROTOCOL / _SB_OVERFLOW
 *     (both expected for adversarial input; anything else is a bug)
 *
 * We do NOT assert on specific out_buffer contents — the point is that no
 * corpus-derived transition, in any chunking, blows up the parser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_EVENTS_IMPLEMENTATION
#define TELNETTY_BUFFER_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_events.h"
#include "telnetty_buffer.h"
#include "telnetty_options.h"

#include "maniac_matrices.h"

/* --- deterministic PRNG (xorshift64) so failures reproduce from a seed --- */

static uint64_t rng_state = 0;
static uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    rng_state = x;
    return x;
}
static uint32_t rng_range(uint32_t bound) {
    return (uint32_t)(rng_next() % (uint64_t)bound);
}

/* --- rolled-up transition index per source symbol.
 *     For each `from`, cumulative weights over its outgoing edges so a
 *     single random draw picks a `to` proportional to weight. --- */

typedef struct {
    int   first_edge;   /* index into edges[] */
    int   edge_count;
    unsigned int total_weight;
} maniac_row_t;

static void build_rows(
    int n_syms,
    const void* edges_v, int n_edges, size_t edge_stride,
    maniac_row_t* rows
) {
    for (int i = 0; i < n_syms; i++) {
        rows[i].first_edge  = -1;
        rows[i].edge_count  = 0;
        rows[i].total_weight = 0;
    }
    const unsigned char* base = (const unsigned char*)edges_v;
    for (int e = 0; e < n_edges; e++) {
        const unsigned char* row = base + (size_t)e * edge_stride;
        unsigned short from   = *(const unsigned short*)(row + 0);
        unsigned int   weight = *(const unsigned int  *)(row + 4);
        if (rows[from].first_edge < 0) rows[from].first_edge = e;
        rows[from].edge_count++;
        rows[from].total_weight += weight;
    }
}

/* Draw the next symbol given the current one. Returns -1 if the row is
 * empty (dangling terminal — fuzzer will pick a fresh start). */
static int draw_next(
    const void* edges_v, size_t edge_stride,
    const maniac_row_t* rows, int from
) {
    if (rows[from].edge_count == 0 || rows[from].total_weight == 0) return -1;
    unsigned int pick = rng_range(rows[from].total_weight);
    const unsigned char* base = (const unsigned char*)edges_v;
    for (int i = 0; i < rows[from].edge_count; i++) {
        const unsigned char* row = base +
            (size_t)(rows[from].first_edge + i) * edge_stride;
        unsigned short to     = *(const unsigned short*)(row + 2);
        unsigned int   weight = *(const unsigned int  *)(row + 4);
        if (pick < weight) return to;
        pick -= weight;
    }
    return -1;
}

/* k=2 lookup. The edge2 array is sorted by (prev, cur), so scan forward
 * from the first index whose (prev, cur) matches and reservoir-sum the
 * weights. Miss -> -1, caller falls back to k=1. Linear scan is fine —
 * 270 s2c / 146 c2s edges total. */
static int draw_next_k2(
    const void* edges2_v, size_t stride, int n_edges2,
    int prev, int cur
) {
    const unsigned char* base = (const unsigned char*)edges2_v;
    /* First pass: total weight for (prev, cur). */
    unsigned int total = 0;
    for (int e = 0; e < n_edges2; e++) {
        const unsigned char* row = base + (size_t)e * stride;
        unsigned short p = *(const unsigned short*)(row + 0);
        unsigned short c = *(const unsigned short*)(row + 2);
        if (p == prev && c == cur) {
            total += *(const unsigned int*)(row + 8);
        } else if (p > prev || (p == prev && c > cur)) {
            break;  /* sorted — past the window */
        }
    }
    if (!total) return -1;
    unsigned int pick = rng_range(total);
    /* Second pass: pick weighted target. */
    for (int e = 0; e < n_edges2; e++) {
        const unsigned char* row = base + (size_t)e * stride;
        unsigned short p = *(const unsigned short*)(row + 0);
        unsigned short c = *(const unsigned short*)(row + 2);
        if (p != prev || c != cur) {
            if (p > prev || (p == prev && c > cur)) break;
            continue;
        }
        unsigned int w = *(const unsigned int*)(row + 8);
        if (pick < w) return *(const unsigned short*)(row + 4);
        pick -= w;
    }
    return -1;
}

/* --- error tracking --- */

static int    error_hits = 0;
static int    unexpected_errors = 0;
static int    last_error_code = 0;

static void err_cb(telnetty_context_t* ctx, telnetty_event_type_t ev,
                   const telnetty_event_data_union_t* d, void* ud) {
    (void)ctx; (void)ud;
    if (ev == TELNETTY_EVENT_ERROR && d) {
        error_hits++;
        last_error_code = d->error.code;
        /* Whitelist: adversarial input may legitimately hit PROTOCOL or
         * SB_OVERFLOW. Anything else — ALLOC, INVAL, CHARSET_UNSUPPORTED
         * etc. — is a real bug for this harness. */
        if (d->error.code != TELNETTY_ERR_PROTOCOL &&
            d->error.code != TELNETTY_ERR_SB_OVERFLOW &&
            d->error.code != TELNETTY_ERR_CHARSET_UNSUPPORTED) {
            unexpected_errors++;
        }
    }
}

/* --- one walk --- */

/* Counters so we can see the k=2/k=1/uniform mix in the run summary. */
static uint64_t draws_k2 = 0;
static uint64_t draws_k1 = 0;
static uint64_t draws_uniform = 0;

static int walk(
    const maniac_symbol_t* alpha, int n_syms,
    const void* edges_v, int n_edges,
    size_t edge_stride,
    const maniac_row_t* rows,
    const void* edges2_v, int n_edges2, size_t edge2_stride,
    int steps, size_t chunk_sz
) {
    (void)n_edges;
    telnetty_context_t* ctx = telnetty_create(err_cb, NULL);
    if (!ctx) return -1;

    /* Register wide option support so negotiations settle (otherwise every
     * WILL/DO takes the reject path and we exercise less of the parser). */
    for (int o = 0; o < 64; o++) {
        telnetty_set_option_support(ctx, (uint8_t)o, 1, 1);
    }

    int prev = -1;
    int cur = (int)rng_range((uint32_t)n_syms);
    for (int s = 0; s < steps; s++) {
        const unsigned char* bytes = alpha[cur].bytes;
        size_t len = alpha[cur].length;

        if (chunk_sz == 0 || chunk_sz >= len) {
            if (telnetty_process(ctx, bytes, len) < 0) {
                telnetty_destroy(ctx);
                return -1;
            }
        } else {
            for (size_t i = 0; i < len; i += chunk_sz) {
                size_t take = len - i;
                if (take > chunk_sz) take = chunk_sz;
                if (telnetty_process(ctx, bytes + i, take) < 0) {
                    telnetty_destroy(ctx);
                    return -1;
                }
            }
        }

        /* Try k=2 first for context sensitivity; fall back to k=1 on miss,
         * then uniform if the first-order row is also empty. */
        int nxt = -1;
        if (prev >= 0) {
            nxt = draw_next_k2(edges2_v, edge2_stride, n_edges2, prev, cur);
            if (nxt >= 0) draws_k2++;
        }
        if (nxt < 0) {
            nxt = draw_next(edges_v, edge_stride, rows, cur);
            if (nxt >= 0) draws_k1++;
        }
        if (nxt < 0) {
            nxt = (int)rng_range((uint32_t)n_syms);
            draws_uniform++;
        }
        prev = cur;
        cur = nxt;
    }

    telnetty_destroy(ctx);
    return 0;
}

/* --- entry --- */

int main(int argc, char** argv) {
    uint64_t seed = 0xC0FFEEULL;
    int steps = 200;
    int seeds = 4;
    int verbose = 0;

    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--long")) { steps = 5000; seeds = 16; }
        else if (!strcmp(argv[a], "--verbose")) { verbose = 1; }
        else if (!strcmp(argv[a], "--seed") && a + 1 < argc) {
            seed = strtoull(argv[++a], NULL, 0);
        }
    }

    printf("MANIAC: %d corpora, %d seeds x %d steps x {chunk 1,3,7,0}...\n",
           MANIAC_CORPORA_N, seeds, steps);

    int total_walks = 0;
    int total_errors = 0;
    int total_unexpected = 0;

    const size_t chunk_sizes[] = { 1, 3, 7, 0 };
    const int NCHUNKS = (int)(sizeof(chunk_sizes) / sizeof(chunk_sizes[0]));

    for (int ci = 0; ci < MANIAC_CORPORA_N; ci++) {
        const maniac_corpus_t* corpus = &maniac_corpora[ci];

        /* Per-corpus row table (variable length; a VLA keeps this compact). */
        maniac_row_t rows[corpus->n_syms];
        build_rows(corpus->n_syms,
                   corpus->edges, corpus->n_edges,
                   sizeof(corpus->edges[0]), rows);

        uint64_t corpus_walks = 0, corpus_errors = 0;
        uint64_t k2_before = draws_k2, k1_before = draws_k1,
                 un_before = draws_uniform;

        for (int s = 0; s < seeds; s++) {
            for (int c = 0; c < NCHUNKS; c++) {
                rng_state = seed
                    ^ ((uint64_t)ci * 0x94D049BB133111EBULL)
                    ^ ((uint64_t)s  * 0x9E3779B97F4A7C15ULL)
                    ^ ((uint64_t)c  * 0xBF58476D1CE4E5B9ULL);
                if (rng_state == 0) rng_state = 1;

                error_hits = 0;
                unexpected_errors = 0;

                int r = walk(corpus->alphabet, corpus->n_syms,
                             corpus->edges, corpus->n_edges,
                             sizeof(corpus->edges[0]), rows,
                             corpus->edges2, corpus->n_edges2,
                             sizeof(corpus->edges2[0]),
                             steps, chunk_sizes[c]);
                if (r < 0) {
                    fprintf(stderr,
                            "FAIL: corpus=%s seed=%d chunk=%zu "
                            "walk returned -1 (last code=%d)\n",
                            corpus->label, s, chunk_sizes[c], last_error_code);
                    return 1;
                }
                total_walks++;
                corpus_walks++;
                total_errors += error_hits;
                corpus_errors += error_hits;
                total_unexpected += unexpected_errors;
                if (unexpected_errors > 0) {
                    fprintf(stderr,
                            "FAIL: corpus=%s seed=%d chunk=%zu "
                            "unexpected_errors=%d (last code=%d)\n",
                            corpus->label, s, chunk_sizes[c],
                            unexpected_errors, last_error_code);
                    return 1;
                }
            }
        }

        if (verbose) {
            uint64_t k2 = draws_k2 - k2_before;
            uint64_t k1 = draws_k1 - k1_before;
            uint64_t un = draws_uniform - un_before;
            uint64_t d  = k2 + k1 + un;
            printf("  %-28s %2llu syms / %3llu k=2 edges: "
                   "%llu walks, %llu whitelisted err, "
                   "k=2 %.0f%% k=1 %.0f%% U %.0f%%\n",
                   corpus->label,
                   (unsigned long long)corpus->n_syms,
                   (unsigned long long)corpus->n_edges2,
                   (unsigned long long)corpus_walks,
                   (unsigned long long)corpus_errors,
                   d ? 100.0 * (double)k2 / (double)d : 0.0,
                   d ? 100.0 * (double)k1 / (double)d : 0.0,
                   d ? 100.0 * (double)un / (double)d : 0.0);
        }
    }

    uint64_t total_draws = draws_k2 + draws_k1 + draws_uniform;
    printf("MANIAC: %d walks OK, %d whitelisted errors, %d unexpected.\n",
           total_walks, total_errors, total_unexpected);
    if (total_draws > 0) {
        printf("Draws: k=2 %.1f%%  k=1 %.1f%%  uniform %.1f%%  (n=%llu)\n",
               100.0 * (double)draws_k2 / (double)total_draws,
               100.0 * (double)draws_k1 / (double)total_draws,
               100.0 * (double)draws_uniform / (double)total_draws,
               (unsigned long long)total_draws);
    }
    return total_unexpected == 0 ? 0 : 1;
}
