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
// `priority_heap.h` is the header for a priority heap implementation.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_PRIORITY_HEAP_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_PRIORITY_HEAP_H_

// Standard library includes.
#include <stdint.h>

#define SC_SA_DS_PRIORITY_HEAP_START_SIZE 32

typedef struct SDArchiverPHNode {
  int64_t priority;
  void *data;
  void (*data_cleanup_fn)(void *);
  /// Is non-zero if valid.
  int is_valid;
} SDArchiverPHNode;

typedef struct SDArchiverPHeap {
  SDArchiverPHNode *nodes;
  uint64_t capacity;
  uint64_t size;
  int (*less_fn)(int64_t, int64_t);
} SDArchiverPHeap;

/// Default "less" function to determine if a has higher priority than b.
/// Returns non-zero if "less".
int simple_archiver_priority_heap_default_less(int64_t a, int64_t b);

SDArchiverPHeap *simple_archiver_priority_heap_init(void);
SDArchiverPHeap *simple_archiver_priority_heap_init_less_fn(
    int (*less_fn)(int64_t, int64_t));

/// It is recommended to use the double-pointer version of priority-heap free
/// as that will ensure the variable holding the pointer will end up pointing
/// to NULL after free.
void simple_archiver_priority_heap_free_single_ptr(
    SDArchiverPHeap *priority_heap);
void simple_archiver_priority_heap_free(SDArchiverPHeap **priority_heap);

/// If data_cleanup_fn is NULL, then "free()" is used on data when freed.
void simple_archiver_priority_heap_insert(SDArchiverPHeap *priority_heap,
                                          int64_t priority, void *data,
                                          void (*data_cleanup_fn)(void *));

/// Returns NULL if empty or if priority_heap is NULL.
void *simple_archiver_priority_heap_top(SDArchiverPHeap *priority_heap);

/// Returns NULL if empty or if priority_heap is NULL.
/// When data is popped, the data_cleanup_fn is ignored and the user must take
/// ownership of the returned data pointer.
void *simple_archiver_priority_heap_pop(SDArchiverPHeap *priority_heap);

/// Iterates through all items in the priority heap (breadth-width search not
/// depth-first search).
void simple_archiver_priority_heap_iter(SDArchiverPHeap *priority_heap,
                                        void(*iter_fn)(void*, void*),
                                        void *user_data);

#endif
