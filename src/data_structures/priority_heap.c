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
 * `priority_heap.c` is the source for a priority heap implementation.
 */

#include "priority_heap.h"

#include <stdlib.h>

void simple_archiver_priority_heap_internal_realloc(
    SDArchiverPHeap **priority_heap) {
  SDArchiverPHeap *new_priority_heap = malloc(sizeof(SDArchiverPHeap));

  new_priority_heap->capacity = (*priority_heap)->capacity * 2;
  new_priority_heap->size = 0;

  new_priority_heap->nodes =
      calloc(new_priority_heap->capacity, sizeof(SDArchiverPHNode));

  for (unsigned int idx = 1; idx < (*priority_heap)->size + 1; ++idx) {
    if ((*priority_heap)->nodes[idx].is_valid != 0) {
      simple_archiver_priority_heap_insert(
          &new_priority_heap, (*priority_heap)->nodes[idx].priority,
          (*priority_heap)->nodes[idx].data,
          (*priority_heap)->nodes[idx].data_cleanup_fn);
      (*priority_heap)->nodes[idx].is_valid = 0;
    }
  }

  simple_archiver_priority_heap_free(priority_heap);

  *priority_heap = new_priority_heap;
}

SDArchiverPHeap *simple_archiver_priority_heap_init(void) {
  SDArchiverPHeap *priority_heap = malloc(sizeof(SDArchiverPHeap));

  priority_heap->capacity = SC_SA_DS_PRIORITY_HEAP_START_SIZE;
  priority_heap->size = 0;

  priority_heap->nodes =
      calloc(priority_heap->capacity, sizeof(SDArchiverPHNode));

  return priority_heap;
}

void simple_archiver_priority_heap_free(SDArchiverPHeap **priority_heap) {
  if (priority_heap && *priority_heap) {
    for (unsigned int idx = 1; idx < (*priority_heap)->size + 1; ++idx) {
      if ((*priority_heap)->nodes[idx].is_valid != 0) {
        if ((*priority_heap)->nodes[idx].data_cleanup_fn) {
          (*priority_heap)
              ->nodes[idx]
              .data_cleanup_fn((*priority_heap)->nodes[idx].data);
        } else {
          free((*priority_heap)->nodes[idx].data);
        }
        (*priority_heap)->nodes[idx].is_valid = 0;
      }
    }

    free((*priority_heap)->nodes);
    free(*priority_heap);
    *priority_heap = NULL;
  }
}

void simple_archiver_priority_heap_insert(SDArchiverPHeap **priority_heap,
                                          long long priority, void *data,
                                          void (*data_cleanup_fn)(void *)) {
  if (!priority_heap || !*priority_heap) {
    return;
  }

  if ((*priority_heap)->size + 1 >= (*priority_heap)->capacity) {
    simple_archiver_priority_heap_internal_realloc(priority_heap);
  }

  unsigned int hole = (*priority_heap)->size + 1;

  while (hole > 1 && priority < (*priority_heap)->nodes[hole / 2].priority) {
    (*priority_heap)->nodes[hole] = (*priority_heap)->nodes[hole / 2];
    hole /= 2;
  }

  (*priority_heap)->nodes[hole].priority = priority;
  (*priority_heap)->nodes[hole].data = data;
  (*priority_heap)->nodes[hole].data_cleanup_fn = data_cleanup_fn;
  (*priority_heap)->nodes[hole].is_valid = 1;

  ++(*priority_heap)->size;
}

void *simple_archiver_priority_heap_top(SDArchiverPHeap *priority_heap) {
  if (priority_heap && priority_heap->size != 0) {
    return priority_heap->nodes[1].data;
  }

  return NULL;
}

void *simple_archiver_priority_heap_pop(SDArchiverPHeap *priority_heap) {
  if (!priority_heap || priority_heap->size == 0) {
    return NULL;
  }

  void *data = priority_heap->nodes[1].data;
  priority_heap->nodes[1].is_valid = 0;

  SDArchiverPHNode end = priority_heap->nodes[priority_heap->size];
  priority_heap->nodes[priority_heap->size].is_valid = 0;

  unsigned int hole = 1;
  while (hole * 2 + 1 <= priority_heap->size) {
    if (priority_heap->nodes[hole * 2].is_valid != 0 &&
        priority_heap->nodes[hole * 2 + 1].is_valid != 0) {
      if (end.priority < priority_heap->nodes[hole * 2].priority &&
          end.priority < priority_heap->nodes[hole * 2 + 1].priority) {
        break;
      }
      if (priority_heap->nodes[hole * 2].priority <
          priority_heap->nodes[hole * 2 + 1].priority) {
        priority_heap->nodes[hole] = priority_heap->nodes[hole * 2];
        hole = hole * 2;
      } else {
        priority_heap->nodes[hole] = priority_heap->nodes[hole * 2 + 1];
        hole = hole * 2 + 1;
      }
    } else if (priority_heap->nodes[hole * 2].is_valid != 0) {
      if (end.priority < priority_heap->nodes[hole * 2].priority) {
        break;
      }
      priority_heap->nodes[hole] = priority_heap->nodes[hole * 2];
      hole = hole * 2;
      break;
    } else {
      break;
    }
  }

  priority_heap->nodes[hole] = end;

  --priority_heap->size;

  return data;
}
