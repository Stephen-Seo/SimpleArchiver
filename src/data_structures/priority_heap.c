// ISC License
//
// Copyright (c) 2024-2026 Stephen Seo
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
// `priority_heap.c` is the source for a priority heap implementation.

#include "priority_heap.h"

// Standard library includes.
#include <stdlib.h>
#include <string.h>

// Local includes.
#include "chunked_array.h"

#ifndef NDEBUG
# include <stdio.h>
# include <stdint.h>
# include <inttypes.h>
#endif

void internal_simple_archiver_cleanup_priority_heap_node(void *p) {
  SDArchiverPHNode *node = p;

  if (node && node->is_valid && node->data) {
    if (node->data_cleanup_fn) {
      node->data_cleanup_fn(node->data);
    } else {
      free(node->data);
    }
    node->data = 0;
  }
}

void internal_simple_archiver_cleanup_nop(__attribute__((unused)) void *unused)
{}

int simple_archiver_priority_heap_default_less(int64_t a, int64_t b) {
  return a < b ? 1 : 0;
}

SDArchiverPHeap *simple_archiver_priority_heap_init(void) {
  SDArchiverPHeap *priority_heap = malloc(sizeof(SDArchiverPHeap));

  priority_heap->less_fn = simple_archiver_priority_heap_default_less;
  priority_heap->gen_less_fn = NULL;
  priority_heap->gen_less_fn_ud = NULL;
  priority_heap->ud = NULL;

  priority_heap->node_array =
    simple_archiver_chunked_array_init(
      internal_simple_archiver_cleanup_priority_heap_node,
      sizeof(SDArchiverPHNode));

  // Priorty-heap expects an "unused" first element at idx 0.
  SDArchiverPHNode *hole_node = malloc(sizeof(SDArchiverPHNode));
  memset(hole_node, 0, sizeof(SDArchiverPHNode));

  simple_archiver_chunked_array_push(&priority_heap->node_array, hole_node);
  free(hole_node);

  return priority_heap;
}

SDArchiverPHeap *simple_archiver_priority_heap_init_less_fn(
    int (*less_fn)(int64_t, int64_t)) {
  SDArchiverPHeap *priority_heap = malloc(sizeof(SDArchiverPHeap));

  priority_heap->less_fn = less_fn;
  priority_heap->gen_less_fn = NULL;
  priority_heap->gen_less_fn_ud = NULL;
  priority_heap->ud = NULL;

  priority_heap->node_array =
    simple_archiver_chunked_array_init(
      internal_simple_archiver_cleanup_priority_heap_node,
      sizeof(SDArchiverPHNode));

  // Priorty-heap expects an "unused" first element at idx 0.
  SDArchiverPHNode *hole_node = malloc(sizeof(SDArchiverPHNode));
  memset(hole_node, 0, sizeof(SDArchiverPHNode));

  simple_archiver_chunked_array_push(&priority_heap->node_array, hole_node);
  free(hole_node);

  return priority_heap;
}

SDArchiverPHeap *simple_archiver_priority_heap_init_less_generic_fn(
    int (*less_fn)(void*, void*)) {
  SDArchiverPHeap *priority_heap = malloc(sizeof(SDArchiverPHeap));

  priority_heap->less_fn = NULL;
  priority_heap->gen_less_fn = less_fn;
  priority_heap->gen_less_fn_ud = NULL;
  priority_heap->ud = NULL;

  priority_heap->node_array =
    simple_archiver_chunked_array_init(
      internal_simple_archiver_cleanup_priority_heap_node,
      sizeof(SDArchiverPHNode));

  // Priorty-heap expects an "unused" first element at idx 0.
  SDArchiverPHNode *hole_node = malloc(sizeof(SDArchiverPHNode));
  memset(hole_node, 0, sizeof(SDArchiverPHNode));

  simple_archiver_chunked_array_push(&priority_heap->node_array, hole_node);
  free(hole_node);

  return priority_heap;
}

SDArchiverPHeap *simple_archiver_priority_heap_init_less_generic_fn_ud(
    int (*less_fn)(void*, void*, void*), void *ud) {
  SDArchiverPHeap *priority_heap = malloc(sizeof(SDArchiverPHeap));

  priority_heap->less_fn = NULL;
  priority_heap->gen_less_fn = NULL;
  priority_heap->gen_less_fn_ud = less_fn;
  priority_heap->ud = ud;

  priority_heap->node_array =
    simple_archiver_chunked_array_init(
      internal_simple_archiver_cleanup_priority_heap_node,
      sizeof(SDArchiverPHNode));

  // Priorty-heap expects an "unused" first element at idx 0.
  SDArchiverPHNode *hole_node = malloc(sizeof(SDArchiverPHNode));
  memset(hole_node, 0, sizeof(SDArchiverPHNode));

  simple_archiver_chunked_array_push(&priority_heap->node_array, hole_node);
  free(hole_node);

  return priority_heap;
}

void simple_archiver_priority_heap_free_single_ptr(
    SDArchiverPHeap *priority_heap) {
  if (priority_heap) {
    simple_archiver_chunked_array_cleanup(&priority_heap->node_array);
    free(priority_heap);
  }
}

void simple_archiver_priority_heap_free(SDArchiverPHeap **priority_heap) {
  if (priority_heap && *priority_heap) {
    simple_archiver_priority_heap_free_single_ptr(*priority_heap);
    *priority_heap = NULL;
  }
}

void simple_archiver_priority_heap_insert(SDArchiverPHeap *priority_heap,
                                          int64_t priority, void *data,
                                          void (*data_cleanup_fn)(void *)) {
  if (!priority_heap ||
      (!priority_heap->less_fn
       && !priority_heap->gen_less_fn
       && (!priority_heap->gen_less_fn_ud || !priority_heap->ud))) {
    return;
  }

  uint64_t hole =
    simple_archiver_chunked_array_size(&priority_heap->node_array);

  SDArchiverPHNode node;
  memset(&node, 0, sizeof(SDArchiverPHNode));

  simple_archiver_chunked_array_push(&priority_heap->node_array, &node);

  while (hole > 1 &&
      ((priority_heap->less_fn &&
        priority_heap->less_fn(
          priority,
          ((SDArchiverPHNode *)
            (simple_archiver_chunked_array_at(
              &priority_heap->node_array, hole / 2)))
          ->priority) != 0)
        ||
        (priority_heap->gen_less_fn &&
          priority_heap->gen_less_fn(
            data,
            ((SDArchiverPHNode *)
            (simple_archiver_chunked_array_at(
            &priority_heap->node_array, hole / 2)))
              ->data) != 0)
        ||
        (priority_heap->gen_less_fn_ud &&
          priority_heap->gen_less_fn_ud(
            data,
            ((SDArchiverPHNode *)
            (simple_archiver_chunked_array_at(
            &priority_heap->node_array, hole / 2)))
              ->data,
            priority_heap->ud
          ) != 0)
       )) {
    SDArchiverPHNode *to =
      simple_archiver_chunked_array_at(&priority_heap->node_array, hole);
    SDArchiverPHNode *from =
      simple_archiver_chunked_array_at(&priority_heap->node_array, hole / 2);

    memcpy(to, from, sizeof(SDArchiverPHNode));
    hole /= 2;
  }

  SDArchiverPHNode *hole_node =
    simple_archiver_chunked_array_at(&priority_heap->node_array, hole);

  hole_node->priority = priority;
  hole_node->data = data;
  hole_node->data_cleanup_fn = data_cleanup_fn;
  hole_node->is_valid = 1;
}

void *simple_archiver_priority_heap_top(SDArchiverPHeap *priority_heap) {
  if (!priority_heap) {
    return NULL;
  }

  uint64_t count =
    simple_archiver_chunked_array_size(&priority_heap->node_array);

  if (priority_heap && count > 1) {
    SDArchiverPHNode *node =
      simple_archiver_chunked_array_at(&priority_heap->node_array, 1);
    if (node->is_valid) {
      return node->data;
    }
  }

  return NULL;
}

void *simple_archiver_priority_heap_pop(SDArchiverPHeap *priority_heap) {
  if (!priority_heap
      || simple_archiver_chunked_array_size(&priority_heap->node_array) <= 1
      || (!priority_heap->less_fn
        && !priority_heap->gen_less_fn
        && (!priority_heap->gen_less_fn_ud || !priority_heap->ud))) {
    return NULL;
  }

  // Get first (top) node's data and mark first node as invalid.
  SDArchiverPHNode *node =
    simple_archiver_chunked_array_at(&priority_heap->node_array, 1);
  void *data = node->data;
  node->is_valid = 0;

  // Get last node.
  SDArchiverPHNode *end = simple_archiver_chunked_array_at(
    &priority_heap->node_array,
    simple_archiver_chunked_array_size(&priority_heap->node_array) - 1);

  // "trickle" down left/right nodes until "end" finds matching spot while
  // pushing nodes up.
  uint64_t hole = 1;
  SDArchiverPHNode *hole_node;
  const uint64_t size =
    simple_archiver_chunked_array_size(&priority_heap->node_array);
  while (hole * 2 + 1 <= size) {
    SDArchiverPHNode *left =
      simple_archiver_chunked_array_at(&priority_heap->node_array, hole * 2);
    SDArchiverPHNode *right =
      simple_archiver_chunked_array_at(
        &priority_heap->node_array, hole * 2 + 1);

    if (left && left->is_valid != 0 && right && right->is_valid != 0) {
      if ((priority_heap->less_fn &&
          priority_heap->less_fn(end->priority, left->priority) != 0 &&
          priority_heap->less_fn(end->priority, right->priority) != 0)
          || (priority_heap->gen_less_fn &&
              priority_heap->gen_less_fn(end->data, left->data) != 0 &&
              priority_heap->gen_less_fn(end->data, right->data) != 0)
          || (priority_heap->gen_less_fn_ud &&
              priority_heap->gen_less_fn_ud(end->data,
                                            left->data,
                                            priority_heap->ud) != 0 &&
              priority_heap->gen_less_fn_ud(end->data,
                                            right->data,
                                            priority_heap->ud) != 0)
          ) {
        break;
      }
      if ((priority_heap->less_fn &&
           priority_heap->less_fn(left->priority, right->priority) != 0)
          || (priority_heap->gen_less_fn &&
              priority_heap->gen_less_fn(left->data, right->data) != 0)
          || (priority_heap->gen_less_fn_ud &&
              priority_heap->gen_less_fn_ud(left->data,
                                            right->data,
                                            priority_heap->ud) != 0)) {
        hole_node =
          simple_archiver_chunked_array_at(&priority_heap->node_array, hole);
        memcpy(hole_node, left, sizeof(SDArchiverPHNode));
        hole = hole * 2;
      } else {
        hole_node =
          simple_archiver_chunked_array_at(&priority_heap->node_array, hole);
        memcpy(hole_node, right, sizeof(SDArchiverPHNode));
        hole = hole * 2 + 1;
      }
    } else if (left && left->is_valid != 0) {
      if ((priority_heap->less_fn &&
           priority_heap->less_fn(end->priority, left->priority) != 0)
          || (priority_heap->gen_less_fn &&
              priority_heap->gen_less_fn(end->data, left->data) != 0)
          || (priority_heap->gen_less_fn_ud &&
              priority_heap->gen_less_fn_ud(end->data,
                                            left->data,
                                            priority_heap->ud) != 0)) {
        break;
      }
      hole_node =
        simple_archiver_chunked_array_at(&priority_heap->node_array, hole);
      memcpy(hole_node, left, sizeof(SDArchiverPHNode));
      hole = hole * 2;
      break;
    } else {
      break;
    }
  }

  // Fill in "hole" with "end".
  hole_node =
    simple_archiver_chunked_array_at(&priority_heap->node_array, hole);
  memcpy(hole_node, end, sizeof(SDArchiverPHNode));

  // Zero out "end" to prevent erronous free's.
  memset(end, 0, sizeof(SDArchiverPHNode));

  simple_archiver_chunked_array_pop_no_ret(&priority_heap->node_array);

  return data;
}

void simple_archiver_priority_heap_iter(SDArchiverPHeap *priority_heap,
                                        void(*iter_fn)(void*, void*),
                                        void *user_data) {
  if (!priority_heap) {
    return;
  }

  const uint64_t size =
    simple_archiver_chunked_array_size(&priority_heap->node_array);
  if (size <= 1) {
    return;
  }

  for (uint64_t idx = 1; idx < size; ++idx) {
    SDArchiverPHNode *node =
      simple_archiver_chunked_array_at(&priority_heap->node_array, idx);
    if (node && node->is_valid) {
      iter_fn(node->data, user_data);
    }
  }
}

uint64_t simple_archiver_priority_heap_size(SDArchiverPHeap *priority_heap) {
  if (!priority_heap) {
    return 0;
  }

  uint64_t size =
    simple_archiver_chunked_array_size(&priority_heap->node_array);
  if (size != 0) {
    return size - 1;
  } else {
    return 0;
  }
}

SDArchiverPHeap *simple_archiver_priority_heap_clone(
    const SDArchiverPHeap *prev_heap,
    void*(*clone_fn)(void*)) {
  if (!prev_heap
      || (!prev_heap->less_fn
        && !prev_heap->gen_less_fn
        && (!prev_heap->gen_less_fn_ud || !prev_heap->ud))) {
    return NULL;
  }

  const uint64_t size =
    simple_archiver_chunked_array_size(&prev_heap->node_array);
  if (size <= 1) {
    return NULL;
  }

  SDArchiverPHeap *cloned_heap;
  if (prev_heap->less_fn) {
    cloned_heap =
      simple_archiver_priority_heap_init_less_fn(prev_heap->less_fn);
  } else if (prev_heap->gen_less_fn) {
    cloned_heap = simple_archiver_priority_heap_init_less_generic_fn(
        prev_heap->gen_less_fn);
  } else if (prev_heap->gen_less_fn_ud && prev_heap->ud) {
    cloned_heap = simple_archiver_priority_heap_init_less_generic_fn_ud(
        prev_heap->gen_less_fn_ud, prev_heap->ud);
  } else {
    return NULL;
  }

  for (uint64_t idx = 1; idx < size; ++idx) {
    const SDArchiverPHNode *node =
      simple_archiver_chunked_array_at_const(&prev_heap->node_array, idx);
    if (clone_fn) {
      simple_archiver_priority_heap_insert(cloned_heap,
                                           node->priority,
                                           clone_fn(node->data),
                                           node->data_cleanup_fn);
    } else {
      simple_archiver_priority_heap_insert(
        cloned_heap,
        node->priority,
        node->data,
        internal_simple_archiver_cleanup_nop);
    }
  }

  return cloned_heap;
}
