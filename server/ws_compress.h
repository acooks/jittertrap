/*
 * ws_compress.h - WebSocket message compression using zlib
 *
 * Provides deflate compression with a preset dictionary optimized for
 * JitterTrap JSON messages. The dictionary contains common JSON keys,
 * message types, and field values to improve compression ratio,
 * especially for smaller messages.
 *
 * Format: [0x01][deflate-compressed-data]
 * Uncompressed messages remain as text frames (no change).
 */

#ifndef WS_COMPRESS_H
#define WS_COMPRESS_H

#include <stddef.h>

/* Compression threshold - only compress messages larger than this */
#define WS_COMPRESS_THRESHOLD 128

/* Maximum input size we'll attempt to compress */
#define WS_COMPRESS_MAX_INPUT (64 * 1024)

/* Header byte to indicate compressed data */
#define WS_COMPRESS_HEADER 0x01

/*
 * Initialize compression module.
 * Returns 0 on success, -1 on failure.
 */
int ws_compress_init(void);

/*
 * Compress data using gzip.
 *
 * in: input data
 * in_len: input length
 * out: output buffer (caller must free if return > 0)
 * out_len: output length (set on success)
 *
 * Returns: 0 on success with compressed data in out,
 *          1 if compression not beneficial (use original),
 *         -1 on error
 */
int ws_compress(const char *in, size_t in_len, unsigned char **out,
                size_t *out_len);

/*
 * Check if a message should be compressed based on size.
 */
int ws_should_compress(size_t msg_len);

/*
 * Get the preset dictionary for client-side decompression.
 * Returns pointer to dictionary string and sets len to its length.
 */
const char *ws_compress_get_dictionary(size_t *len);

#endif /* WS_COMPRESS_H */
