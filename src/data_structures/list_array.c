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
// `list_array.c` is the source for a list-of-arrays implementation.

#include "list_array.h"
#include "linked_list.h"

#include <string.h>
#include <stdlib.h>

typedef struct SDArchiverListArrNode {
    void *data;
    uint64_t elem_size;
    uint64_t arr_count;
    void (*elem_cleanup_fn)(void*);
} SDArchiverListArrNode;

void internal_sa_la_node_free(void *data) {
  SDArchiverListArrNode *node = data;

  if (node->elem_cleanup_fn) {
    for (uint64_t idx = 0; idx < node->arr_count; ++idx) {
      node->elem_cleanup_fn(((char*)node->data) + idx * node->elem_size);
    }
  }

  free(node->data);
  free(node);
}

SDArchiverListArr simple_archiver_list_array_init(
    void (*elem_cleanup_fn)(void*), uint32_t elem_size) {
  SDArchiverListArr la =
    (SDArchiverListArr){.list = simple_archiver_list_init(),
                        .elem_cleanup_fn = elem_cleanup_fn,
                        .elem_size = elem_size};

  return la;
}

void simple_archiver_list_array_cleanup(SDArchiverListArr *la) {
  if (la && la->list) {
    simple_archiver_list_free(&la->list);
    memset(la, 0, sizeof(SDArchiverListArr));
  }
}

void *simple_archiver_list_array_at(SDArchiverListArr *la, uint64_t idx) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *last_node = la->list->tail->prev->data;
  uint64_t temp = (la->list->count - 1) * SD_SA_DS_LIST_ARR_DEFAULT_SIZE;

  if (idx >= temp + last_node->arr_count) {
    return NULL;
  }

  SDArchiverLLNode *lnode;
  for (lnode = la->list->head->next;
       idx >= SD_SA_DS_LIST_ARR_DEFAULT_SIZE;
       idx -= SD_SA_DS_LIST_ARR_DEFAULT_SIZE, lnode = lnode->next) {}

  SDArchiverListArrNode *target_node = lnode->data;
  if (!target_node || idx >= SD_SA_DS_LIST_ARR_DEFAULT_SIZE) {
    return NULL;
  }

  char *target = target_node->data;
  return target + target_node->elem_size * idx;
}

const void *simple_archiver_list_array_at_const(const SDArchiverListArr *la,
                                                uint64_t idx) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *last_node = la->list->tail->prev->data;
  uint64_t temp = (la->list->count - 1) * SD_SA_DS_LIST_ARR_DEFAULT_SIZE;

  if (idx >= temp + last_node->arr_count) {
    return NULL;
  }

  SDArchiverLLNode *lnode;
  for (lnode = la->list->head->next;
       idx >= SD_SA_DS_LIST_ARR_DEFAULT_SIZE;
       idx -= SD_SA_DS_LIST_ARR_DEFAULT_SIZE, lnode = lnode->next) {}

  SDArchiverListArrNode *target_node = lnode->data;
  if (!target_node || idx >= SD_SA_DS_LIST_ARR_DEFAULT_SIZE) {
    return NULL;
  }

  char *target = target_node->data;
  return target + target_node->elem_size * idx;
}

int simple_archiver_list_array_push(SDArchiverListArr *la, void *to_copy) {
  if (!la || !la->list) {
    return 1;
  }

  if (la->list->count == 0) {
    SDArchiverListArrNode *new_node = malloc(sizeof(SDArchiverListArrNode));
    new_node->data = malloc(la->elem_size * SD_SA_DS_LIST_ARR_DEFAULT_SIZE);
    new_node->elem_size = la->elem_size;
    new_node->arr_count = 1;
    new_node->elem_cleanup_fn = la->elem_cleanup_fn;
    memcpy(new_node->data, to_copy, la->elem_size);
    simple_archiver_list_add(la->list, new_node, internal_sa_la_node_free);
    return 0;
  }

  SDArchiverListArrNode *last_node = la->list->tail->prev->data;

  if (last_node->arr_count == SD_SA_DS_LIST_ARR_DEFAULT_SIZE) {
    SDArchiverListArrNode *new_node = malloc(sizeof(SDArchiverListArrNode));
    new_node->data = malloc(la->elem_size * SD_SA_DS_LIST_ARR_DEFAULT_SIZE);
    new_node->elem_size = la->elem_size;
    new_node->arr_count = 1;
    new_node->elem_cleanup_fn = la->elem_cleanup_fn;
    memcpy(new_node->data, to_copy, la->elem_size);
    simple_archiver_list_add(la->list, new_node, internal_sa_la_node_free);
    return 0;
  }

  char *buf = last_node->data;
  memcpy(buf + last_node->elem_size * last_node->arr_count,
         to_copy,
         last_node->elem_size);
  ++last_node->arr_count;
  return 0;
}

void *simple_archiver_list_array_pop(SDArchiverListArr *la, int no_cleanup) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *last_node = la->list->tail->prev->data;
  char *buf = malloc(last_node->elem_size);
  char *target = last_node->data;
  target += last_node->elem_size * (last_node->arr_count - 1);
  memcpy(buf, target, last_node->elem_size);

  if (no_cleanup == 0 && last_node->elem_cleanup_fn) {
    last_node->elem_cleanup_fn(target);
  }

  --last_node->arr_count;

  if (last_node->arr_count == 0) {
    SDArchiverLLNode *list_last = la->list->tail->prev;
    internal_sa_la_node_free(list_last->data);
    list_last->next->prev = list_last->prev;
    list_last->prev->next = list_last->next;
    free(list_last);
    --la->list->count;
  }

  return buf;
}

int simple_archiver_list_array_pop_no_ret(SDArchiverListArr *la) {
  if (!la || !la->list || la->list->count == 0) {
    return 0;
  }

  SDArchiverListArrNode *last_node = la->list->tail->prev->data;
  char *target = last_node->data;
  target += last_node->elem_size * (last_node->arr_count - 1);
  if (last_node->elem_cleanup_fn) {
    last_node->elem_cleanup_fn(target);
  }
  --last_node->arr_count;

  if (last_node->arr_count == 0) {
    SDArchiverLLNode *list_last = la->list->tail->prev;
    internal_sa_la_node_free(list_last->data);
    list_last->next->prev = list_last->prev;
    list_last->prev->next = list_last->next;
    free(list_last);
    --la->list->count;
  }

  return 1;
}

void simple_archiver_list_array_clear(SDArchiverListArr *la) {
  if (!la || !la->list) {
    return;
  }
  simple_archiver_list_free(&la->list);
  la->list = simple_archiver_list_init();
}

uint64_t simple_archiver_list_array_size(const SDArchiverListArr *la) {
  if (!la || !la->list) {
    return 0;
  }
  uint64_t size = 0;

  for (SDArchiverLLNode *node = la->list->head->next;
       node != la->list->tail;
       node = node->next) {
    SDArchiverListArrNode *la_node = node->data;
    size += la_node->arr_count;
  }

  return size;
}

void *simple_archiver_list_array_top(SDArchiverListArr *la) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *node = la->list->tail->prev->data;
  if (node->arr_count == 0) {
    return NULL;
  }

  char *ptr = node->data;
  ptr += (node->arr_count - 1) * node->elem_size;

  return ptr;
}

const void *simple_archiver_list_array_top_const(const SDArchiverListArr *la) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *node = la->list->tail->prev->data;
  if (node->arr_count == 0) {
    return NULL;
  }

  char *ptr = node->data;
  ptr += (node->arr_count - 1) * node->elem_size;

  return ptr;
}

void *simple_archiver_list_array_bottom(SDArchiverListArr *la) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *node = la->list->head->next->data;
  if (node->arr_count == 0) {
    return NULL;
  }

  // data ptr points to first (index 0) element.
  return node->data;
}

const void *simple_archiver_list_array_bottom_const(
    const SDArchiverListArr *la) {
  if (!la || !la->list || la->list->count == 0) {
    return NULL;
  }

  SDArchiverListArrNode *node = la->list->head->next->data;
  if (node->arr_count == 0) {
    return NULL;
  }

  // data ptr points to first (index 0) element.
  return node->data;
}
