/*
 * Copyright 2024 Stephen Seo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * `priority_heap.h` is the header for a priority heap implementation.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_PRIORITY_HEAP_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_PRIORITY_HEAP_H_

#define SC_SA_DS_PRIORITY_HEAP_START_SIZE 32

typedef struct SDArchiverPHNode {
  long long priority;
  void *data;
  void (*data_cleanup_fn)(void *);
  /// Is non-zero if valid.
  int is_valid;
} SDArchiverPHNode;

typedef struct SDArchiverPHeap {
  SDArchiverPHNode *nodes;
  unsigned long long capacity;
  unsigned long long size;
  int (*less_fn)(long long, long long);
} SDArchiverPHeap;

/// Default "less" function to determine if a has higher priority than b.
/// Returns non-zero if "less".
int simple_archiver_priority_heap_default_less(long long a, long long b);

SDArchiverPHeap *simple_archiver_priority_heap_init(void);
SDArchiverPHeap *simple_archiver_priority_heap_init_less_fn(
    int (*less_fn)(long long, long long));
void simple_archiver_priority_heap_free(SDArchiverPHeap **priority_heap);

/// If data_cleanup_fn is NULL, then "free()" is used on data when freed.
void simple_archiver_priority_heap_insert(SDArchiverPHeap **priority_heap,
                                          long long priority, void *data,
                                          void (*data_cleanup_fn)(void *));

/// Returns NULL if empty or if priority_heap is NULL.
void *simple_archiver_priority_heap_top(SDArchiverPHeap *priority_heap);

/// Returns NULL if empty or if priority_heap is NULL.
/// When data is popped, the data_cleanup_fn is ignored and the user must take
/// ownership of the returned data pointer.
void *simple_archiver_priority_heap_pop(SDArchiverPHeap *priority_heap);

#endif
