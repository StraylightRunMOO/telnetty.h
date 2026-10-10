/**
 * @file telnetty_compression.h
 * @brief Compression protocol implementations for TELNET
 * 
 * This header provides implementations for MUD Client Compression Protocol
 * versions 2 and 3 (MCCP2/MCCP3), which allow for data compression in
 * TELNETTY connections to reduce bandwidth usage.
 * 
 * Features:
 * - MCCP2 (MUD Client Compression Protocol v2)
 * - MCCP3 (MUD Client Compression Protocol v3)
 * - zlib integration
 * - Compression state management
 * 
 * @author Damus <damus@straylightrun.org>
 * @version 1.2.0
 * 
 */

#ifndef TELNETTY_COMPRESSION_H
#define TELNETTY_COMPRESSION_H

#include "telnetty_core.h"
#include "telnetty_events.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mark functions as potentially unused (header-only library) */
#ifndef TELNETTY_UNUSED
#ifdef __GNUC__
#define TELNETTY_UNUSED __attribute__((unused))
#else
#define TELNETTY_UNUSED
#endif
#endif


/* ============================================================================
 * Configuration Options
 * ============================================================================ */

#ifndef TELNETTY_COMPRESSION_LEVEL
#define TELNETTY_COMPRESSION_LEVEL 6  /* zlib compression level (1-9) */
#endif

#ifndef TELNETTY_COMPRESSION_BUFFER_SIZE
#define TELNETTY_COMPRESSION_BUFFER_SIZE (8 * 1024)  /* 8KB compression buffer */
#endif

#ifndef TELNETTY_COMPRESSION_MAX_RATIO
#define TELNETTY_COMPRESSION_MAX_RATIO 100  /* Maximum compression ratio */
#endif

/* ============================================================================
 * MCCP2 Protocol (MUD Client Compression Protocol v2)
 * ============================================================================ */

/* MCCP1 (RFC-ish, MUD-tradition) option code — TELOPT 85. Legacy; the
 * on-the-wire "flip to compressed" sentinel is IAC SB 85 WILL SE IAC SE,
 * a weird sequence chosen so a naive TELNET stack wouldn't choke. We
 * accept it but forward the inflate to the same zlib pipe MCCP2 uses. */
#define TELNETTY_TELOPT_MCCP1 85

/* MCCP2 option code */
#define TELNETTY_TELOPT_MCCP2 86

/* MCCP2 constants */
#define TELNETTY_MCCP2_COMPRESS 85
#define TCCP2_MAXINPUT 4096

/* MCCP2 data structure */
typedef struct {
    bool enabled;                       /**< MCCP2 is enabled */
    bool compressing;                   /**< Currently compressing */
    void* zlib_stream;                  /**< zlib stream (opaque) */
    uint8_t* input_buffer;              /**< Input buffer */
    uint8_t* output_buffer;             /**< Output buffer */
    size_t input_size;                  /**< Input buffer size */
    size_t output_size;                 /**< Output buffer size */
    size_t compressed_bytes;            /**< Total compressed bytes */
    size_t uncompressed_bytes;          /**< Total uncompressed bytes */
    double compression_ratio;           /**< Compression ratio */
    int level;                          /**< zlib level, applied on DO */
    bool inflating;                     /**< Client side: inflating the
                                         *   server's stream after its
                                         *   IAC SB MCCP2 IAC SE marker. */
    void* inflate_stream;               /**< zlib inflate stream (opaque) */
} telnetty_mccp2_data_t;

#ifdef TELNETTY_COMPRESSION_IMPLEMENTATION
/**
 * MCCP2 option handler
 * 
 * @param ctx TELNETTY context
 * @param option Option code (should be TELNETTY_TELOPT_MCCP2)
 * @param command Command received
 * @param user_data User data (should be telnetty_mccp2_data_t*)
 */
static TELNETTY_UNUSED void telnetty_mccp2_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Enable MCCP2 compression
 * 
 * @param ctx TELNETTY context
 * @param level Compression level (1-9, 0 for default)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_mccp2(
    telnetty_context_t* ctx,
    int level
);

/**
 * Client side: accept MCCP2 from the server. Answers the server's
 * WILL MCCP2 with DO; when the server's IAC SB MCCP2 IAC SE marker
 * arrives, everything after it (including the rest of the same read) is
 * inflated before parsing. On the server's Z_STREAM_END parsing returns to
 * plain bytes. A corrupt stream fires EVENT_ERROR (TELNETTY_ERR_COMPRESSION)
 * and drops back to plain parsing.
 *
 * @return 0 on success, -1 on failure (or no zlib)
 */
static TELNETTY_UNUSED int telnetty_accept_mccp2(telnetty_context_t* ctx);

/**
 * Disable MCCP2 compression. Server side: sends WONT (inside the stream)
 * and then ends the zlib stream with Z_FINISH so the client resumes plain
 * parsing at the right byte. Client side: sends DONT; the inflater keeps
 * running until the server ends its stream.
 * 
 * @param ctx TELNETTY context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_mccp2(telnetty_context_t* ctx);

/**
 * Check if MCCP2 is enabled
 * 
 * @param ctx TELNETTY context
 * @return true if MCCP2 is enabled, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_is_mccp2_enabled(telnetty_context_t* ctx);

/**
 * Send compressed data via MCCP2
 * 
 * @param ctx TELNETTY context
 * @param data Data to compress and send
 * @param length Data length
 * @return Number of bytes compressed or -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mccp2_send(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
);

/**
 * Get MCCP2 compression statistics
 * 
 * @param ctx TELNETTY context
 * @param compressed_bytes Output compressed bytes (optional)
 * @param uncompressed_bytes Output uncompressed bytes (optional)
 * @param ratio Output compression ratio (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mccp2_get_stats(
    telnetty_context_t* ctx,
    size_t* compressed_bytes,
    size_t* uncompressed_bytes,
    double* ratio
);
#endif /* TELNETTY_COMPRESSION_IMPLEMENTATION */

/* ============================================================================
 * MCCP3 Protocol (MUD Client Compression Protocol v3)
 * ============================================================================ */

/* MCCP3 option code */
#define TELNETTY_TELOPT_MCCP3 87

/* MCCP3 data structure */
typedef struct {
    bool enabled;                       /**< MCCP3 is enabled */
    bool client_mode;                   /**< Client-side compression */
    bool server_mode;                   /**< Server-side compression */
    void* zlib_stream;                  /**< zlib stream (opaque) */
    uint8_t* input_buffer;              /**< Input buffer */
    uint8_t* output_buffer;             /**< Output buffer */
    size_t input_size;                  /**< Input buffer size */
    size_t output_size;                 /**< Output buffer size */
    size_t compressed_bytes;            /**< Total compressed bytes */
    size_t uncompressed_bytes;          /**< Total uncompressed bytes */
    double compression_ratio;           /**< Compression ratio */
    int level;
    bool compressing;                   /**< Client side: deflating our
                                         *   output after sending the
                                         *   IAC SB MCCP3 IAC SE marker. */
    bool inflating;                     /**< Server side: inflating the
                                         *   client's stream after its
                                         *   marker arrived. */
    void* inflate_stream;               /**< zlib inflate stream (opaque) */
} telnetty_mccp3_data_t;

#ifdef TELNETTY_COMPRESSION_IMPLEMENTATION
/**
 * MCCP3 option handler
 * 
 * @param ctx TELNETTY context
 * @param option Option code (should be TELNETTY_TELOPT_MCCP3)
 * @param command Command received
 * @param user_data User data (should be telnetty_mccp3_data_t*)
 */
static TELNETTY_UNUSED void telnetty_mccp3_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
);

/**
 * Server side: offer MCCP3 (sends WILL MCCP3). After the client answers DO
 * and sends IAC SB MCCP3 IAC SE, every byte it sends is zlib data;
 * telnetty_process() inflates it before parsing, including bytes that
 * follow the marker in the same read. The client's Z_STREAM_END returns to
 * plain parsing; a corrupt stream fires EVENT_ERROR
 * (TELNETTY_ERR_COMPRESSION), sends WONT MCCP3 and returns -1.
 * 
 * @param ctx TELNETTY context
 * @param level Compression level (1-9, 0 for default). Unused on the
 *              server side; kept for symmetry with MCCP2.
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_enable_mccp3(
    telnetty_context_t* ctx,
    int level
);

/**
 * Client side: accept MCCP3 from the server. On the server's WILL MCCP3
 * we answer DO, send IAC SB MCCP3 IAC SE and compress everything sent
 * afterwards.
 *
 * @param ctx TELNETTY context
 * @param level Compression level (1-9, 0 for default)
 * @return 0 on success, -1 on failure (or no zlib)
 */
static TELNETTY_UNUSED int telnetty_accept_mccp3(telnetty_context_t* ctx, int level);

/**
 * Disable MCCP3 compression. Client side: sends DONT and ends our zlib
 * stream with Z_FINISH. Server side: sends WONT; the inflater keeps
 * running until the client ends its stream.
 * 
 * @param ctx TELNETTY context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_disable_mccp3(telnetty_context_t* ctx);

/**
 * Check if MCCP3 is enabled
 * 
 * @param ctx TELNETTY context
 * @return true if MCCP3 is enabled, false otherwise
 */
static TELNETTY_UNUSED bool telnetty_is_mccp3_enabled(telnetty_context_t* ctx);

/**
 * Send data via MCCP3. Same as telnetty_send(): once the client-side
 * stream is running the output hook compresses it.
 * 
 * @param ctx TELNETTY context
 * @param data Data to compress and send
 * @param length Data length
 * @return Number of bytes queued or -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mccp3_send(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
);

/**
 * Get MCCP3 compression statistics
 * 
 * @param ctx TELNETTY context
 * @param compressed_bytes Output compressed bytes (optional)
 * @param uncompressed_bytes Output uncompressed bytes (optional)
 * @param ratio Output compression ratio (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_mccp3_get_stats(
    telnetty_context_t* ctx,
    size_t* compressed_bytes,
    size_t* uncompressed_bytes,
    double* ratio
);

/* ============================================================================
 * Compression Utilities
 * ============================================================================ */

/**
 * Initialize compression system
 * 
 * @param ctx TELNETTY context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_compression_init(telnetty_context_t* ctx);
#endif /* TELNETTY_COMPRESSION_IMPLEMENTATION */

/* ============================================================================
 * Statistics Structures
 * ============================================================================ */

/* Compression statistics */
typedef struct {
    size_t compressed_bytes;            /**< Total compressed bytes */
    size_t uncompressed_bytes;          /**< Total uncompressed bytes */
    double compression_ratio;           /**< Overall compression ratio */
    size_t packets_compressed;          /**< Number of packets compressed */
    size_t packets_sent;                /**< Number of packets sent */
    double average_packet_size;         /**< Average packet size */
    time_t start_time;                  /**< Compression start time */
    time_t last_activity;               /**< Last compression activity */
} telnetty_compression_stats_t;

#ifdef TELNETTY_COMPRESSION_IMPLEMENTATION
/**
 * Stop all running compression streams on ctx: outbound streams are ended
 * with Z_FINISH (so the peer stays in sync) and the output/input hooks are
 * removed. Negotiated option state and per-option data are left alone;
 * telnetty_destroy() frees them.
 *
 * @param ctx TELNETTY context
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_compression_cleanup(telnetty_context_t* ctx);

/**
 * Get overall compression statistics
 * 
 * @param ctx TELNETTY context
 * @param mccp2_stats Output MCCP2 statistics (optional)
 * @param mccp3_stats Output MCCP3 statistics (optional)
 * @return 0 on success, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_compression_get_stats(
    telnetty_context_t* ctx,
    telnetty_compression_stats_t* mccp2_stats,
    telnetty_compression_stats_t* mccp3_stats
);

/**
 * Server side: offer both MCCP2 and MCCP3. They compress opposite
 * directions, so there is nothing to choose between.
 * 
 * @param ctx TELNETTY context
 * @param prefer_mccp3 Ignored; kept for source compatibility
 * @return 0 if at least one was offered, -1 on failure
 */
static TELNETTY_UNUSED int telnetty_compression_auto_negotiate(
    telnetty_context_t* ctx,
    bool prefer_mccp3
);
#endif /* TELNETTY_COMPRESSION_IMPLEMENTATION */

/* ============================================================================
 * Implementation
 * ============================================================================ */

#ifdef TELNETTY_COMPRESSION_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* zlib includes - optional */
#ifdef TELNETTY_HAVE_ZLIB
#include <zlib.h>
#endif

/* Memory allocation wrappers */
#ifndef TELNETTY_COMPRESSION_MALLOC
#define TELNETTY_COMPRESSION_MALLOC(size) malloc(size)
#endif

#ifndef TELNETTY_COMPRESSION_FREE
#define TELNETTY_COMPRESSION_FREE(ptr) free(ptr)
#endif

#ifndef TELNETTY_COMPRESSION_REALLOC
#define TELNETTY_COMPRESSION_REALLOC(ptr, size) realloc(ptr, size)
#endif

/* Forward declarations for internal functions */
static TELNETTY_UNUSED int telnetty_mccp2_init(telnetty_context_t* ctx, telnetty_mccp2_data_t* data, int level);
static TELNETTY_UNUSED int telnetty_mccp2_cleanup(telnetty_mccp2_data_t* data);
static TELNETTY_UNUSED int telnetty_mccp3_init(telnetty_context_t* ctx, telnetty_mccp3_data_t* data, int level);
static TELNETTY_UNUSED int telnetty_mccp3_cleanup(telnetty_mccp3_data_t* data);

/* ============================================================================
 * Shared zlib plumbing
 * ============================================================================ */

/* telnetty__inflate_feed results. */
#define TELNETTY__INFLATE_MORE    0   /* all input consumed, stream open */
#define TELNETTY__INFLATE_END     1   /* Z_STREAM_END; *rest = trailing raw bytes */
#define TELNETTY__INFLATE_GONE    2   /* a handler replaced the stream under us */
#define TELNETTY__INFLATE_ZERR   -1   /* corrupt stream / zlib failure */
#define TELNETTY__INFLATE_PERR   -2   /* telnetty_process failed on the output */

static TELNETTY_UNUSED void telnetty__compression_ratio(size_t compressed, size_t uncompressed, double* ratio) {
    if (uncompressed > 0) *ratio = (double)compressed / (double)uncompressed;
}

#ifdef TELNETTY_HAVE_ZLIB

static TELNETTY_UNUSED void* telnetty__deflate_new(int level) {
    z_stream* strm = (z_stream*)TELNETTY_COMPRESSION_MALLOC(sizeof(z_stream));
    if (!strm) return NULL;
    memset(strm, 0, sizeof(*strm));
    if (deflateInit(strm, level > 0 ? level : TELNETTY_COMPRESSION_LEVEL) != Z_OK) {
        TELNETTY_COMPRESSION_FREE(strm);
        return NULL;
    }
    return strm;
}

/* zlib format (not raw deflate): both MCCP2 and MCCP3 carry a zlib header. */
static TELNETTY_UNUSED void* telnetty__inflate_new(void) {
    z_stream* strm = (z_stream*)TELNETTY_COMPRESSION_MALLOC(sizeof(z_stream));
    if (!strm) return NULL;
    memset(strm, 0, sizeof(*strm));
    if (inflateInit(strm) != Z_OK) {
        TELNETTY_COMPRESSION_FREE(strm);
        return NULL;
    }
    return strm;
}

static TELNETTY_UNUSED void telnetty__deflate_free(void** slot) {
    if (!*slot) return;
    deflateEnd((z_stream*)*slot);
    TELNETTY_COMPRESSION_FREE(*slot);
    *slot = NULL;
}

static TELNETTY_UNUSED void telnetty__inflate_free(void** slot) {
    if (!*slot) return;
    inflateEnd((z_stream*)*slot);
    TELNETTY_COMPRESSION_FREE(*slot);
    *slot = NULL;
}

/* Deflate `len` bytes with `flush` (Z_SYNC_FLUSH per write, Z_FINISH to end
 * the stream) and append the result straight to out_buffer. Only output
 * hooks and stream-end paths call this; everything else goes through
 * telnetty_out_write so it reaches the hook. */
static TELNETTY_UNUSED int telnetty__deflate_out(
    telnetty_context_t* ctx,
    void* stream,
    const uint8_t* data,
    size_t len,
    int flush,
    size_t* produced
) {
    z_stream* strm = (z_stream*)stream;
    uint8_t chunk[TELNETTY_COMPRESSION_BUFFER_SIZE];
    strm->next_in = (Bytef*)data;
    strm->avail_in = (uInt)len;
    do {
        strm->next_out = chunk;
        strm->avail_out = (uInt)sizeof(chunk);
        if (deflate(strm, flush) == Z_STREAM_ERROR) return -1;
        size_t n = sizeof(chunk) - strm->avail_out;
        if (n && telnetty_buffer_append(ctx->out_buffer, chunk, n) < 0) return -1;
        *produced += n;
    } while (strm->avail_out == 0);
    return 0;
}

/* Inflate wire bytes and feed the output to the parser. The output lives in
 * a stack chunk, so a handler that frees the protocol state mid-parse can't
 * pull the buffer out from under telnetty_process. `current` re-reads the
 * live stream after each parse; if it changed we stop touching `stream`. */
static TELNETTY_UNUSED int telnetty__inflate_feed(
    telnetty_context_t* ctx,
    void* stream,
    void* (*current)(telnetty_context_t*),
    const uint8_t* data,
    size_t len,
    size_t* compressed,
    size_t* uncompressed,
    const uint8_t** rest,
    size_t* rest_len
) {
    z_stream* strm = (z_stream*)stream;
    uint8_t chunk[TELNETTY_COMPRESSION_BUFFER_SIZE];
    const uint8_t* next = data;
    size_t left = len;

    for (;;) {
        strm->next_in = (Bytef*)next;
        strm->avail_in = (uInt)left;
        strm->next_out = chunk;
        strm->avail_out = (uInt)sizeof(chunk);
        int rc = inflate(strm, Z_SYNC_FLUSH);
        size_t used = left - strm->avail_in;
        size_t n = sizeof(chunk) - strm->avail_out;
        bool full = strm->avail_out == 0;
        next += used;
        left -= used;
        *compressed += used;
        *uncompressed += n;

        if (rc == Z_BUF_ERROR && used == 0 && n == 0) {
            if (left == 0) return TELNETTY__INFLATE_MORE;  /* needs more input */
            return TELNETTY__INFLATE_ZERR;
        }
        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR)
            return TELNETTY__INFLATE_ZERR;

        if (n) {
            if (telnetty_process(ctx, chunk, n) < 0) return TELNETTY__INFLATE_PERR;
            if (current(ctx) != stream) {
                *rest = next;
                *rest_len = left;
                return TELNETTY__INFLATE_GONE;
            }
        }
        if (rc == Z_STREAM_END) {
            *rest = next;
            *rest_len = left;
            return TELNETTY__INFLATE_END;
        }
        if (left == 0 && !full) return TELNETTY__INFLATE_MORE;
    }
}

#endif /* TELNETTY_HAVE_ZLIB */

/* ============================================================================
 * MCCP2 Implementation
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_mccp2_data_t* telnetty_mccp2_data_create(void) {
    telnetty_mccp2_data_t* data = (telnetty_mccp2_data_t*)TELNETTY_COMPRESSION_MALLOC(sizeof(telnetty_mccp2_data_t));
    if (!data) return NULL;

    memset(data, 0, sizeof(telnetty_mccp2_data_t));
    data->input_size = TELNETTY_COMPRESSION_BUFFER_SIZE;
    data->output_size = TELNETTY_COMPRESSION_BUFFER_SIZE;

    return data;
}

static TELNETTY_UNUSED void telnetty_mccp2_data_destroy(telnetty_mccp2_data_t* data) {
    if (!data) return;

    if (data->input_buffer) {
        TELNETTY_COMPRESSION_FREE(data->input_buffer);
    }
    if (data->output_buffer) {
        TELNETTY_COMPRESSION_FREE(data->output_buffer);
    }

    TELNETTY_COMPRESSION_FREE(data);
}

static TELNETTY_UNUSED void telnetty_mccp2_data_free(void* p) {
    telnetty_mccp2_data_t* d = (telnetty_mccp2_data_t*)p;
    if (!d) return;
    telnetty_mccp2_cleanup(d);
    telnetty_mccp2_data_destroy(d);
}

static TELNETTY_UNUSED telnetty_mccp2_data_t* telnetty__mccp2_of(telnetty_context_t* ctx) {
    telnetty_option_t* opt = ctx ? ctx->options[TELNETTY_TELOPT_MCCP2] : NULL;
    return (opt && opt->user_data) ? (telnetty_mccp2_data_t*)opt->user_data : NULL;
}

static TELNETTY_UNUSED void* telnetty__mccp2_inflate_of(telnetty_context_t* ctx) {
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    return d ? d->inflate_stream : NULL;
}

static TELNETTY_UNUSED int telnetty_mccp2_out_hook(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    /* The hook only goes in after the start marker, so with no live stream
     * a raw append would corrupt the client's inflater. Refuse instead. */
    if (!d || !d->compressing || !d->zlib_stream) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP2 output hook active without a deflate stream");
        return -1;
    }
#ifdef TELNETTY_HAVE_ZLIB
    size_t produced = 0;
    if (telnetty__deflate_out(ctx, d->zlib_stream, data, len, Z_SYNC_FLUSH, &produced) < 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP2 deflate failed");
        return -1;
    }
    d->uncompressed_bytes += len;
    d->compressed_bytes += produced;
    telnetty__compression_ratio(d->compressed_bytes, d->uncompressed_bytes, &d->compression_ratio);
    return (int)len;
#else
    (void)data; (void)len;
    return -1;
#endif
}

static TELNETTY_UNUSED void telnetty_mccp2_begin(telnetty_context_t* ctx, telnetty_mccp2_data_t* d) {
    if (!ctx || !d || d->compressing) return;
    if (telnetty_mccp2_init(ctx, d, d->level) != 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP2 deflateInit failed");
        return;
    }
    /* COMPRESS2 start marker goes out uncompressed, ahead of the hook;
     * everything after it is zlib. */
    uint8_t start[5] = { TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_MCCP2, TELNETTY_IAC, TELNETTY_SE };
    if (telnetty_buffer_append(ctx->out_buffer, start, 5) < 0) {
        telnetty_mccp2_cleanup(d);
        telnetty_report_error(ctx, TELNETTY_ERR_ALLOC, "MCCP2 start marker append failed");
        return;
    }
    ctx->stats.bytes_sent += 5;
    d->enabled = true;
    telnetty_set_out_hook(ctx, telnetty_mccp2_out_hook);
}

/* End our outbound stream with Z_FINISH so the client sees Z_STREAM_END
 * and resumes plain parsing exactly here, then drop the hook. */
static TELNETTY_UNUSED void telnetty__mccp2_end(telnetty_context_t* ctx, telnetty_mccp2_data_t* d) {
    if (!ctx || !d || !d->compressing) return;
#ifdef TELNETTY_HAVE_ZLIB
    size_t produced = 0;
    if (d->zlib_stream &&
        telnetty__deflate_out(ctx, d->zlib_stream, NULL, 0, Z_FINISH, &produced) < 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP2 stream finish failed");
    }
    d->compressed_bytes += produced;
    telnetty__deflate_free(&d->zlib_stream);
#endif
    d->compressing = false;
    if (ctx->out_hook == telnetty_mccp2_out_hook)
        telnetty_set_out_hook(ctx, NULL);
}

static TELNETTY_UNUSED void telnetty__mccp2_stop_inflate(telnetty_context_t* ctx, telnetty_mccp2_data_t* d) {
    if (!ctx || !d) return;
#ifdef TELNETTY_HAVE_ZLIB
    telnetty__inflate_free(&d->inflate_stream);
#endif
    d->inflating = false;
}

#ifdef TELNETTY_HAVE_ZLIB
static TELNETTY_UNUSED int telnetty__mccp2_in_hook(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    if (!d || !d->inflating || !d->inflate_stream) {
        telnetty_set_input_hook(ctx, NULL, NULL);
        return telnetty_process(ctx, data, len);
    }
    const uint8_t* rest = NULL;
    size_t rest_len = 0;
    int rc = telnetty__inflate_feed(ctx, d->inflate_stream, telnetty__mccp2_inflate_of,
                                    data, len, &d->compressed_bytes,
                                    &d->uncompressed_bytes, &rest, &rest_len);
    d = telnetty__mccp2_of(ctx);
    if (d) telnetty__compression_ratio(d->compressed_bytes, d->uncompressed_bytes,
                                       &d->compression_ratio);
    switch (rc) {
    case TELNETTY__INFLATE_MORE:
    case TELNETTY__INFLATE_GONE:
        return (int)len;
    case TELNETTY__INFLATE_END:
        /* Server ended compression; trailing bytes are plain again. */
        telnetty__mccp2_stop_inflate(ctx, d);
        telnetty_set_input_hook(ctx, NULL, NULL);
        if (rest_len && telnetty_process(ctx, rest, rest_len) < 0) return -1;
        return (int)len;
    case TELNETTY__INFLATE_PERR:
        return -1;
    default:
        telnetty__mccp2_stop_inflate(ctx, d);
        telnetty_set_input_hook(ctx, NULL, NULL);
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP2 inflate failed; compression disabled");
        return -1;
    }
}
#endif

/* IAC SB MCCP2 IAC SE from the server: switch inbound to inflate. Only the
 * side that agreed to the server's WILL (him == YES) acts on it. */
static TELNETTY_UNUSED void telnetty__mccp2_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    (void)option; (void)data; (void)length; (void)user_data;
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MCCP2];
    if (!d || !opt || opt->him != TELNETTY_Q_YES || d->inflating) return;
#ifdef TELNETTY_HAVE_ZLIB
    if (ctx->in_hook && ctx->in_hook != telnetty__mccp2_in_hook) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP2 start: another input hook is installed");
        return;
    }
    d->inflate_stream = telnetty__inflate_new();
    if (!d->inflate_stream) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP2 inflateInit failed");
        return;
    }
    d->inflating = true;
    telnetty_set_input_hook(ctx, telnetty__mccp2_in_hook, NULL);
#endif
}

static TELNETTY_UNUSED void telnetty_mccp2_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (!ctx || option != TELNETTY_TELOPT_MCCP2) return;
    telnetty_mccp2_data_t* mccp2_data = (telnetty_mccp2_data_t*)user_data;
    if (!mccp2_data) return;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MCCP2];
    switch (command) {
    case TELNETTY_DO:
        telnetty_mccp2_begin(ctx, mccp2_data);
        break;
    case TELNETTY_WILL:
        mccp2_data->enabled = true;
        break;
    case TELNETTY_DONT:
    case TELNETTY_WONT:
        /* WONT is ambiguous here (our WONT settling, or the peer's), so go
         * by the option state. Our outbound side is off: finish the stream.
         * An inbound stream keeps inflating until the server's
         * Z_STREAM_END, which is what actually marks the switch. */
        if (!opt || opt->us != TELNETTY_Q_YES)
            telnetty__mccp2_end(ctx, mccp2_data);
        mccp2_data->enabled = opt && (opt->us == TELNETTY_Q_YES ||
                                      opt->him == TELNETTY_Q_YES);
        break;
    }
}

/* Find or attach the MCCP2 state and handlers on ctx. Idempotent, so
 * calling enable twice (or enable + accept) never frees live state. */
static TELNETTY_UNUSED telnetty_mccp2_data_t* telnetty__mccp2_ensure(telnetty_context_t* ctx) {
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    if (d) return d;
    d = telnetty_mccp2_data_create();
    if (!d) return NULL;
    d->level = TELNETTY_COMPRESSION_LEVEL;
    if (telnetty_register_option(ctx, TELNETTY_TELOPT_MCCP2, telnetty_mccp2_handler, NULL) < 0 ||
        telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_MCCP2, telnetty__mccp2_sb_handler, NULL) < 0) {
        telnetty_mccp2_data_destroy(d);
        return NULL;
    }
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_MCCP2, d, telnetty_mccp2_data_free);
    return d;
}

static TELNETTY_UNUSED int telnetty_enable_mccp2(telnetty_context_t* ctx, int level) {
    if (!ctx) return -1;
#ifndef TELNETTY_HAVE_ZLIB
    (void)level;
    return -1;
#else
    telnetty_mccp2_data_t* mccp2_data = telnetty__mccp2_ensure(ctx);
    if (!mccp2_data) return -1;
    mccp2_data->level = level > 0 ? level : TELNETTY_COMPRESSION_LEVEL;
    ctx->options[TELNETTY_TELOPT_MCCP2]->support_us = 1;
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MCCP2);
#endif
}

static TELNETTY_UNUSED int telnetty_accept_mccp2(telnetty_context_t* ctx) {
    if (!ctx) return -1;
#ifndef TELNETTY_HAVE_ZLIB
    return -1;
#else
    if (!telnetty__mccp2_ensure(ctx)) return -1;
    ctx->options[TELNETTY_TELOPT_MCCP2]->support_him = 1;
    return 0;
#endif
}

static TELNETTY_UNUSED int telnetty_disable_mccp2(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MCCP2];
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    if (opt && opt->him == TELNETTY_Q_YES)
        telnetty_send_option(ctx, TELNETTY_DONT, TELNETTY_TELOPT_MCCP2);

    /* WONT travels inside the compressed stream, then Z_FINISH ends it. */
    int rc = telnetty_send_option(ctx, TELNETTY_WONT, TELNETTY_TELOPT_MCCP2);
    telnetty__mccp2_end(ctx, d);
    return rc;
}

/* MCCP1 handler: legacy protocol still seen on some servers. MCCP1 uses
 * the same zlib inflate pipe as MCCP2 but signals the switch-to-compressed
 * transition with a bizarre `IAC SB 85 WILL SE` sentinel. We negotiate the
 * option code so a peer that offers it gets a definitive WILL/WONT reply
 * instead of silence, but never compress or inflate with it: MCCP2 is
 * preferred by every server built in the last two decades. */
static TELNETTY_UNUSED void telnetty_mccp1_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    (void)ctx; (void)option; (void)command; (void)user_data;
    /* Empty: negotiation only. This mirrors libtelnet's minimal MCCP1
     * stance. */
}

static TELNETTY_UNUSED int telnetty_enable_mccp1(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_set_option_support(ctx, TELNETTY_TELOPT_MCCP1, 1, 1);
    telnetty_register_option(ctx, TELNETTY_TELOPT_MCCP1,
                             telnetty_mccp1_handler, NULL);
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MCCP1);
}

static TELNETTY_UNUSED bool telnetty_is_mccp2_enabled(telnetty_context_t* ctx) {
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    return d && (d->compressing || d->inflating);
}

static TELNETTY_UNUSED int telnetty_mccp2_send(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
) {
    /* telnetty_send already passes through the MCCP hook once DO landed. */
    return telnetty_send(ctx, data, length);
}

static TELNETTY_UNUSED int telnetty_mccp2_get_stats(
    telnetty_context_t* ctx,
    size_t* compressed_bytes,
    size_t* uncompressed_bytes,
    double* ratio
) {
    if (!ctx) return -1;
    telnetty_mccp2_data_t* d = telnetty__mccp2_of(ctx);
    if (compressed_bytes) *compressed_bytes = d ? d->compressed_bytes : 0;
    if (uncompressed_bytes) *uncompressed_bytes = d ? d->uncompressed_bytes : 0;
    if (ratio) *ratio = d ? d->compression_ratio : 0.0;
    return 0;
}

/* ============================================================================
 * MCCP3 Implementation
 *
 * MCCP3 compresses client -> server. The server offers WILL MCCP3; the
 * client answers DO, sends IAC SB MCCP3 IAC SE and compresses everything
 * after it. The server inflates from that marker on. Either side ends it:
 * the client by finishing its zlib stream, the server by WONT (after which
 * the client is expected to finish its stream).
 * ============================================================================ */

static TELNETTY_UNUSED telnetty_mccp3_data_t* telnetty_mccp3_data_create(void) {
    telnetty_mccp3_data_t* data = (telnetty_mccp3_data_t*)TELNETTY_COMPRESSION_MALLOC(sizeof(telnetty_mccp3_data_t));
    if (!data) return NULL;

    memset(data, 0, sizeof(telnetty_mccp3_data_t));
    data->input_size = TELNETTY_COMPRESSION_BUFFER_SIZE;
    data->output_size = TELNETTY_COMPRESSION_BUFFER_SIZE;

    return data;
}

static TELNETTY_UNUSED void telnetty_mccp3_data_destroy(telnetty_mccp3_data_t* data) {
    if (!data) return;

    if (data->input_buffer) {
        TELNETTY_COMPRESSION_FREE(data->input_buffer);
    }
    if (data->output_buffer) {
        TELNETTY_COMPRESSION_FREE(data->output_buffer);
    }

    TELNETTY_COMPRESSION_FREE(data);
}

static TELNETTY_UNUSED void telnetty_mccp3_data_free(void* p) {
    telnetty_mccp3_data_t* d = (telnetty_mccp3_data_t*)p;
    if (!d) return;
    telnetty_mccp3_cleanup(d);
    telnetty_mccp3_data_destroy(d);
}

static TELNETTY_UNUSED telnetty_mccp3_data_t* telnetty__mccp3_of(telnetty_context_t* ctx) {
    telnetty_option_t* opt = ctx ? ctx->options[TELNETTY_TELOPT_MCCP3] : NULL;
    return (opt && opt->user_data) ? (telnetty_mccp3_data_t*)opt->user_data : NULL;
}

static TELNETTY_UNUSED void* telnetty__mccp3_inflate_of(telnetty_context_t* ctx) {
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    return d ? d->inflate_stream : NULL;
}

/* Client side: compress everything we send after the start marker. */
static TELNETTY_UNUSED int telnetty__mccp3_out_hook(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    if (!d || !d->compressing || !d->zlib_stream) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP3 output hook active without a deflate stream");
        return -1;
    }
#ifdef TELNETTY_HAVE_ZLIB
    size_t produced = 0;
    if (telnetty__deflate_out(ctx, d->zlib_stream, data, len, Z_SYNC_FLUSH, &produced) < 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP3 deflate failed");
        return -1;
    }
    d->uncompressed_bytes += len;
    d->compressed_bytes += produced;
    telnetty__compression_ratio(d->compressed_bytes, d->uncompressed_bytes, &d->compression_ratio);
    return (int)len;
#else
    (void)data; (void)len;
    return -1;
#endif
}

static TELNETTY_UNUSED void telnetty__mccp3_begin(telnetty_context_t* ctx, telnetty_mccp3_data_t* d) {
    if (!ctx || !d || d->compressing) return;
    if (ctx->out_hook) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP3 start: another output hook is installed");
        return;
    }
    if (telnetty_mccp3_init(ctx, d, d->level) != 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP3 deflateInit failed");
        return;
    }
    /* The marker itself is the last uncompressed thing we send. */
    uint8_t start[5] = { TELNETTY_IAC, TELNETTY_SB, TELNETTY_TELOPT_MCCP3, TELNETTY_IAC, TELNETTY_SE };
    if (telnetty_out_write(ctx, start, 5) < 0) {
        telnetty_mccp3_cleanup(d);
        telnetty_report_error(ctx, TELNETTY_ERR_ALLOC, "MCCP3 start marker append failed");
        return;
    }
    ctx->stats.bytes_sent += 5;
    d->compressing = true;
    telnetty_set_out_hook(ctx, telnetty__mccp3_out_hook);
}

static TELNETTY_UNUSED void telnetty__mccp3_end(telnetty_context_t* ctx, telnetty_mccp3_data_t* d) {
    if (!ctx || !d || !d->compressing) return;
#ifdef TELNETTY_HAVE_ZLIB
    size_t produced = 0;
    if (d->zlib_stream &&
        telnetty__deflate_out(ctx, d->zlib_stream, NULL, 0, Z_FINISH, &produced) < 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP3 stream finish failed");
    }
    d->compressed_bytes += produced;
    telnetty__deflate_free(&d->zlib_stream);
#endif
    d->compressing = false;
    if (ctx->out_hook == telnetty__mccp3_out_hook)
        telnetty_set_out_hook(ctx, NULL);
}

static TELNETTY_UNUSED void telnetty__mccp3_stop_inflate(telnetty_context_t* ctx, telnetty_mccp3_data_t* d) {
    if (!ctx || !d) return;
#ifdef TELNETTY_HAVE_ZLIB
    telnetty__inflate_free(&d->inflate_stream);
#endif
    d->inflating = false;
}

#ifdef TELNETTY_HAVE_ZLIB
static TELNETTY_UNUSED int telnetty__mccp3_in_hook(telnetty_context_t* ctx, const uint8_t* data, size_t len) {
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    if (!d || !d->inflating || !d->inflate_stream) {
        telnetty_set_input_hook(ctx, NULL, NULL);
        return telnetty_process(ctx, data, len);
    }
    const uint8_t* rest = NULL;
    size_t rest_len = 0;
    int rc = telnetty__inflate_feed(ctx, d->inflate_stream, telnetty__mccp3_inflate_of,
                                    data, len, &d->compressed_bytes,
                                    &d->uncompressed_bytes, &rest, &rest_len);
    d = telnetty__mccp3_of(ctx);
    if (d) telnetty__compression_ratio(d->compressed_bytes, d->uncompressed_bytes,
                                       &d->compression_ratio);
    switch (rc) {
    case TELNETTY__INFLATE_MORE:
    case TELNETTY__INFLATE_GONE:
        return (int)len;
    case TELNETTY__INFLATE_END:
        /* Client finished its stream; whatever follows is plain. */
        telnetty__mccp3_stop_inflate(ctx, d);
        telnetty_set_input_hook(ctx, NULL, NULL);
        if (rest_len && telnetty_process(ctx, rest, rest_len) < 0) return -1;
        return (int)len;
    case TELNETTY__INFLATE_PERR:
        return -1;
    default:
        /* Don't parse garbage: drop the rest of this read, tell the client
         * to stop compressing, and surface the failure. */
        telnetty__mccp3_stop_inflate(ctx, d);
        telnetty_set_input_hook(ctx, NULL, NULL);
        if (d) d->enabled = false;
        telnetty_send_option(ctx, TELNETTY_WONT, TELNETTY_TELOPT_MCCP3);
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP3 inflate failed; compression disabled");
        return -1;
    }
}
#endif

/* IAC SB MCCP3 IAC SE from the client: every byte after it is zlib. The
 * parser hands the remainder of the current read to the hook installed
 * here (see the in_hook handoff in telnetty__parse). */
static TELNETTY_UNUSED void telnetty__mccp3_sb_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    const uint8_t* data,
    size_t length,
    void* user_data
) {
    (void)option; (void)data; (void)length; (void)user_data;
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MCCP3];
    if (!d || !opt || opt->us != TELNETTY_Q_YES || d->inflating) return;
    d->server_mode = true;
    if (telnetty_mccp3_init(ctx, d, d->level) != 0) {
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION, "MCCP3 inflateInit failed");
        return;
    }
#ifdef TELNETTY_HAVE_ZLIB
    if (ctx->in_hook && ctx->in_hook != telnetty__mccp3_in_hook) {
        telnetty__mccp3_stop_inflate(ctx, d);
        telnetty_report_error(ctx, TELNETTY_ERR_COMPRESSION,
                              "MCCP3 start: another input hook is installed");
        return;
    }
    d->inflating = true;
    telnetty_set_input_hook(ctx, telnetty__mccp3_in_hook, NULL);
#endif
}

static TELNETTY_UNUSED void telnetty_mccp3_handler(
    telnetty_context_t* ctx,
    uint8_t option,
    uint8_t command,
    void* user_data
) {
    if (!ctx || option != TELNETTY_TELOPT_MCCP3) return;

    telnetty_mccp3_data_t* mccp3_data = (telnetty_mccp3_data_t*)user_data;
    if (!mccp3_data) return;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MCCP3];

    switch (command) {
        case TELNETTY_DO:
            /* Server: the client agreed. Inflation starts at its marker. */
            mccp3_data->enabled = true;
            mccp3_data->server_mode = true;
            break;

        case TELNETTY_WILL:
            /* Client: the server offered and we answered DO. Send the
             * marker and compress from here on. */
            mccp3_data->enabled = true;
            mccp3_data->client_mode = true;
            telnetty__mccp3_begin(ctx, mccp3_data);
            break;

        case TELNETTY_WONT:
        case TELNETTY_DONT:
            /* Our outbound stream (client side) ends with Z_FINISH. An
             * inbound stream (server side) runs until the client finishes
             * it, which is what marks the switch back to plain bytes. */
            if (!opt || opt->him != TELNETTY_Q_YES)
                telnetty__mccp3_end(ctx, mccp3_data);
            mccp3_data->enabled = opt && (opt->us == TELNETTY_Q_YES ||
                                          opt->him == TELNETTY_Q_YES);
            break;
    }
}

static TELNETTY_UNUSED telnetty_mccp3_data_t* telnetty__mccp3_ensure(telnetty_context_t* ctx) {
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    if (d) return d;
    d = telnetty_mccp3_data_create();
    if (!d) return NULL;
    d->level = TELNETTY_COMPRESSION_LEVEL;
    if (telnetty_register_option(ctx, TELNETTY_TELOPT_MCCP3, telnetty_mccp3_handler, NULL) < 0 ||
        telnetty_register_sb_handler(ctx, TELNETTY_TELOPT_MCCP3, telnetty__mccp3_sb_handler, NULL) < 0) {
        telnetty_mccp3_data_destroy(d);
        return NULL;
    }
    telnetty_option_set_user_data(ctx, TELNETTY_TELOPT_MCCP3, d, telnetty_mccp3_data_free);
    return d;
}

static TELNETTY_UNUSED int telnetty_enable_mccp3(telnetty_context_t* ctx, int level) {
    if (!ctx) return -1;
#ifndef TELNETTY_HAVE_ZLIB
    (void)level;
    return -1;
#else
    telnetty_mccp3_data_t* mccp3_data = telnetty__mccp3_ensure(ctx);
    if (!mccp3_data) return -1;
    mccp3_data->level = level > 0 ? level : TELNETTY_COMPRESSION_LEVEL;
    ctx->options[TELNETTY_TELOPT_MCCP3]->support_us = 1;
    return telnetty_send_option(ctx, TELNETTY_WILL, TELNETTY_TELOPT_MCCP3);
#endif
}

static TELNETTY_UNUSED int telnetty_accept_mccp3(telnetty_context_t* ctx, int level) {
    if (!ctx) return -1;
#ifndef TELNETTY_HAVE_ZLIB
    (void)level;
    return -1;
#else
    telnetty_mccp3_data_t* mccp3_data = telnetty__mccp3_ensure(ctx);
    if (!mccp3_data) return -1;
    mccp3_data->level = level > 0 ? level : TELNETTY_COMPRESSION_LEVEL;
    ctx->options[TELNETTY_TELOPT_MCCP3]->support_him = 1;
    return 0;
#endif
}

static TELNETTY_UNUSED int telnetty_disable_mccp3(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    telnetty_option_t* opt = ctx->options[TELNETTY_TELOPT_MCCP3];
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    int rc = 0;
    if (opt && opt->him == TELNETTY_Q_YES) {
        /* Client: DONT goes inside our stream, then Z_FINISH ends it. */
        rc = telnetty_send_option(ctx, TELNETTY_DONT, TELNETTY_TELOPT_MCCP3);
        telnetty__mccp3_end(ctx, d);
        return rc;
    }
    return telnetty_send_option(ctx, TELNETTY_WONT, TELNETTY_TELOPT_MCCP3);
}

static TELNETTY_UNUSED bool telnetty_is_mccp3_enabled(telnetty_context_t* ctx) {
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    return d && d->enabled;
}

static TELNETTY_UNUSED int telnetty_mccp3_send(
    telnetty_context_t* ctx,
    const uint8_t* data,
    size_t length
) {
    /* Once the client-side stream runs, telnetty_send goes through the
     * MCCP3 output hook. */
    return telnetty_send(ctx, data, length);
}

static TELNETTY_UNUSED int telnetty_mccp3_get_stats(
    telnetty_context_t* ctx,
    size_t* compressed_bytes,
    size_t* uncompressed_bytes,
    double* ratio
) {
    if (!ctx) return -1;
    telnetty_mccp3_data_t* d = telnetty__mccp3_of(ctx);
    if (compressed_bytes) *compressed_bytes = d ? d->compressed_bytes : 0;
    if (uncompressed_bytes) *uncompressed_bytes = d ? d->uncompressed_bytes : 0;
    if (ratio) *ratio = d ? d->compression_ratio : 0.0;
    return 0;
}

/* ============================================================================
 * Internal Implementation Functions
 * ============================================================================ */

/* Start the MCCP2 deflate stream (server side). */
static TELNETTY_UNUSED int telnetty_mccp2_init(telnetty_context_t* ctx, telnetty_mccp2_data_t* data, int level) {
    if (!ctx || !data) return -1;
#ifdef TELNETTY_HAVE_ZLIB
    if (!data->zlib_stream) {
        data->zlib_stream = telnetty__deflate_new(level);
        if (!data->zlib_stream) return -1;
    }
    data->compressing = true;
    return 0;
#else
    (void)level;
    return -1;
#endif
}

/* Release every zlib stream without emitting anything (destroy path). */
static TELNETTY_UNUSED int telnetty_mccp2_cleanup(telnetty_mccp2_data_t* data) {
    if (!data) return -1;
#ifdef TELNETTY_HAVE_ZLIB
    telnetty__deflate_free(&data->zlib_stream);
    telnetty__inflate_free(&data->inflate_stream);
#endif
    data->compressing = false;
    data->inflating = false;
    return 0;
}

/* Server mode: inflate stream for the client's data. Client mode: deflate
 * stream for ours. */
static TELNETTY_UNUSED int telnetty_mccp3_init(telnetty_context_t* ctx, telnetty_mccp3_data_t* data, int level) {
    if (!ctx || !data) return -1;
#ifdef TELNETTY_HAVE_ZLIB
    if (data->server_mode) {
        if (!data->inflate_stream) data->inflate_stream = telnetty__inflate_new();
        return data->inflate_stream ? 0 : -1;
    }
    if (data->client_mode) {
        if (!data->zlib_stream) data->zlib_stream = telnetty__deflate_new(level);
        return data->zlib_stream ? 0 : -1;
    }
    return -1;
#else
    (void)level;
    return -1;
#endif
}

static TELNETTY_UNUSED int telnetty_mccp3_cleanup(telnetty_mccp3_data_t* data) {
    if (!data) return -1;
#ifdef TELNETTY_HAVE_ZLIB
    telnetty__deflate_free(&data->zlib_stream);
    telnetty__inflate_free(&data->inflate_stream);
#endif
    data->compressing = false;
    data->inflating = false;
    return 0;
}

/* ============================================================================
 * Utility Functions Implementation
 * ============================================================================ */

static TELNETTY_UNUSED int telnetty_compression_init(telnetty_context_t* ctx) {
    if (!ctx) return -1;
    
    /* Initialize compression system */
    /* This would set up the overall compression management */
    
    return 0;
}

static TELNETTY_UNUSED int telnetty_compression_cleanup(telnetty_context_t* ctx) {
    if (!ctx) return -1;

    telnetty_mccp2_data_t* d2 = telnetty__mccp2_of(ctx);
    if (d2) {
        telnetty__mccp2_end(ctx, d2);
        telnetty__mccp2_stop_inflate(ctx, d2);
    }
    telnetty_mccp3_data_t* d3 = telnetty__mccp3_of(ctx);
    if (d3) {
        telnetty__mccp3_end(ctx, d3);
        telnetty__mccp3_stop_inflate(ctx, d3);
    }
#ifdef TELNETTY_HAVE_ZLIB
    if (ctx->in_hook == telnetty__mccp2_in_hook || ctx->in_hook == telnetty__mccp3_in_hook)
        telnetty_set_input_hook(ctx, NULL, NULL);
#endif
    return 0;
}

static TELNETTY_UNUSED void telnetty__compression_fill_stats(
    telnetty_compression_stats_t* out,
    size_t compressed,
    size_t uncompressed,
    double ratio
) {
    memset(out, 0, sizeof(*out));
    out->compressed_bytes = compressed;
    out->uncompressed_bytes = uncompressed;
    out->compression_ratio = ratio;
}

static TELNETTY_UNUSED int telnetty_compression_get_stats(
    telnetty_context_t* ctx,
    telnetty_compression_stats_t* mccp2_stats,
    telnetty_compression_stats_t* mccp3_stats
) {
    if (!ctx) return -1;

    size_t c = 0, u = 0;
    double r = 0.0;
    if (mccp2_stats) {
        telnetty_mccp2_get_stats(ctx, &c, &u, &r);
        telnetty__compression_fill_stats(mccp2_stats, c, u, r);
    }
    if (mccp3_stats) {
        telnetty_mccp3_get_stats(ctx, &c, &u, &r);
        telnetty__compression_fill_stats(mccp3_stats, c, u, r);
    }
    return 0;
}

static TELNETTY_UNUSED int telnetty_compression_auto_negotiate(
    telnetty_context_t* ctx,
    bool prefer_mccp3
) {
    (void)prefer_mccp3;
    if (!ctx) return -1;
    
    /* Enable both compression protocols for automatic negotiation */
    int result1 = telnetty_enable_mccp2(ctx, TELNETTY_COMPRESSION_LEVEL);
    int result2 = telnetty_enable_mccp3(ctx, TELNETTY_COMPRESSION_LEVEL);
    
    /* Return success if at least one protocol was enabled */
    return (result1 == 0 || result2 == 0) ? 0 : -1;
}

#endif /* TELNETTY_COMPRESSION_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* TELNETTY_COMPRESSION_H */
