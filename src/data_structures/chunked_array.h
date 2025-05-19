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

typedef struct SDArchiverChunkedArr {
    uint64_t chunk_count;
    uint32_t last_size;
    uint32_t elem_size;
    void (*elem_cleanup_fn)(void*);
    void **array;
} SDArchiverChunkedArr;

/// Use a no-op or NULL elem_cleanup_fn if the element is primitive data like an
/// integer. If the element has pointers within it, use the cleanup fn to
/// cleanup the pointers but not the element itself.
SDArchiverChunkedArr simple_archiver_chunked_array_init(
  void (*elem_cleanup_fn)(void*), uint32_t elem_size);

void simple_archiver_chunked_array_cleanup(SDArchiverChunkedArr *);

/// Returns non-void ptr to element on success.
void *simple_archiver_chunked_array_at(SDArchiverChunkedArr *, uint64_t idx);

/// Returns non-void ptr to element on success.
const void *simple_archiver_chunked_array_at_const(const SDArchiverChunkedArr *,
                                                   uint64_t idx);

/// Returns 0 on success.
int simple_archiver_chunked_array_push(SDArchiverChunkedArr *, void *to_copy);

/// Returns non-null on success.
/// Returned ptr is newly allocated and must be free'd.
/// If "no_cleanup" is non-zero, then the cleanup function will not be run on
/// the element before a newly allocated copy of it is returned.
void *simple_archiver_chunked_array_pop(SDArchiverChunkedArr *, int no_cleanup);

/// Returns non-zero if an element was removed.
int simple_archiver_chunked_array_pop_no_ret(SDArchiverChunkedArr *);

/// Clears the chunked array so that it is as if it was newly initialized.
void simple_archiver_chunked_array_clear(SDArchiverChunkedArr *);

/// Returns the number of elements in the chunked array.
/// This will return 0 if the chunked_array is invalid.
uint64_t simple_archiver_chunked_array_size(const SDArchiverChunkedArr *);

/// Returns non-null if not empty. Returned ptr is NOT newly allocated and is
/// still owned by the list array. Value returned by "top" is the next value
/// that will be popped if "pop" is called. (index (size-1))
void *simple_archiver_chunked_array_top(SDArchiverChunkedArr *);

const void *simple_archiver_chunked_array_top_const(
  const SDArchiverChunkedArr *);

/// Returns non-null if not empty. Returned ptr is NOT newly allocated and is
/// still owned by the list array. Value returned by "bottom" is the last
/// value that will be popped if "pop" is repeatedly called. (index 0)
void *simple_archiver_chunked_array_bottom(SDArchiverChunkedArr *);

const void *simple_archiver_chunked_array_bottom_const(
  const SDArchiverChunkedArr *);

#endif
