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
// `list_array.h` is the header for a list-of-arrays implementation.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_LIST_ARRAY_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_LIST_ARRAY_H_

#define SD_SA_DS_LIST_ARR_DEFAULT_SIZE 32

#include "linked_list.h"

typedef struct SDArchiverListArr {
    SDArchiverLinkedList *list;
    void (*elem_cleanup_fn)(void*);
    uint32_t elem_size;
} SDArchiverListArr;

/// Use a no-op or NULL elem_cleanup_fn if the element is primitive data like an
/// integer. If the element has pointers within it, use the cleanup fn to
/// cleanup the pointers but not the element itself.
SDArchiverListArr simple_archiver_list_array_init(
  void (*elem_cleanup_fn)(void*), uint32_t elem_size);

void simple_archiver_list_array_cleanup(SDArchiverListArr *);

/// Returns non-void ptr to element on success.
void *simple_archiver_list_array_at(SDArchiverListArr *, uint64_t idx);

/// Returns non-void ptr to element on success.
const void *simple_archiver_list_array_at_const(const SDArchiverListArr *,
                                                uint64_t idx);

/// Returns 0 on success.
int simple_archiver_list_array_push(SDArchiverListArr *, void *to_copy);

/// Returns non-null on success.
/// Returned ptr is newly allocated and must be free'd.
/// If "no_cleanup" is non-zero, then the cleanup function will not be run on
/// the element before a newly allocated copy of it is returned.
void *simple_archiver_list_array_pop(SDArchiverListArr *, int no_cleanup);

/// Returns non-zero if an element was removed.
int simple_archiver_list_array_pop_no_ret(SDArchiverListArr *);

/// Clears the list array so that it is as if it was newly initialized.
void simple_archiver_list_array_clear(SDArchiverListArr *);

/// Returns the number of elements in the list array.
/// This will return 0 if the list_array is invalid.
uint64_t simple_archiver_list_array_size(const SDArchiverListArr *);

#endif
