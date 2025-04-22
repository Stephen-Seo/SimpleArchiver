// ISC License
//
// Copyright (c) 2024-2025 Stephen Seo
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
// OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.
//
// `chunked_array.h` is the header for a chunked-array implementation.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_CHUNKED_ARRAY_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_CHUNKED_ARRAY_H_

// Standard library includes
#include <stdint.h>
#include <stddef.h>

#define SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE 32
#define SD_SA_DS_CHUNKED_ARR_DEFAULT_CAPACITY 4

typedef struct SDArchiverChunkedArr {
    uint64_t chunk_count;
    uint32_t last_size;
    uint32_t elem_size;
    void (*elem_cleanup_fn)(void*);
    void **array;
} SDArchiverChunkedArr;

SDArchiverChunkedArr simple_archiver_chunked_array_init(
  void (*elem_cleanup_fn)(void*), uint32_t elem_size);

void simple_archiver_chunked_array_cleanup(SDArchiverChunkedArr *);

/// Returns non-void ptr to element on success.
void *simple_archiver_chunked_array_at(SDArchiverChunkedArr *, uint64_t idx);

/// Returns 0 on success.
int simple_archiver_chunked_array_push(SDArchiverChunkedArr *, void *to_copy);

/// Returns non-null on success.
/// Returned ptr is newly allocated and must be free'd.
void *simple_archiver_chunked_array_pop(SDArchiverChunkedArr *);

/// Clears the chunked array so that it is as if it was newly initialized.
void simple_archiver_chunked_array_clear(SDArchiverChunkedArr *);

#endif
