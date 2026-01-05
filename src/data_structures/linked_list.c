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
// `linked_list.c` is the source for a linked list data structure.

#include "linked_list.h"

// Standard library includes.
#include <stdint.h>
#include <stdlib.h>

SDArchiverLinkedList *simple_archiver_list_init(void) {
  SDArchiverLinkedList *list = malloc(sizeof(SDArchiverLinkedList));

  list->head = malloc(sizeof(SDArchiverLLNode));
  list->tail = malloc(sizeof(SDArchiverLLNode));

  list->head->next = list->tail;
  list->head->prev = NULL;
  list->head->data = NULL;
  list->head->data_free_fn = NULL;

  list->tail->next = NULL;
  list->tail->prev = list->head;
  list->tail->data = NULL;
  list->tail->data_free_fn = NULL;

  list->count = 0;

  return list;
}

void simple_archiver_list_free_single_ptr(SDArchiverLinkedList *list) {
  if (list) {
    SDArchiverLLNode *node = list->head;
    SDArchiverLLNode *prev;
    while (node) {
      prev = node;
      node = node->next;
      free(prev);
      if (node && node->data) {
        if (node->data_free_fn) {
          node->data_free_fn(node->data);
        } else {
          free(node->data);
        }
      }
    }

    free(list);
  }
}

void simple_archiver_list_free(SDArchiverLinkedList **list) {
  if (list && *list) {
    simple_archiver_list_free_single_ptr(*list);
    *list = NULL;
  }
}

int simple_archiver_list_add(SDArchiverLinkedList *list, void *data,
                             void (*data_free_fn)(void *)) {
  if (!list) {
    return 1;
  }

  SDArchiverLLNode *new_node = malloc(sizeof(SDArchiverLLNode));
  new_node->data = data;
  new_node->data_free_fn = data_free_fn;

  new_node->next = list->tail;
  new_node->prev = list->tail->prev;

  list->tail->prev->next = new_node;
  list->tail->prev = new_node;

  ++list->count;

  return 0;
}

int simple_archiver_list_add_front(SDArchiverLinkedList *list, void *data,
                                   void (*data_free_fn)(void *)) {
  if (!list) {
    return 1;
  }

  SDArchiverLLNode *new_node = malloc(sizeof(SDArchiverLLNode));
  new_node->data = data;
  new_node->data_free_fn = data_free_fn;

  new_node->next = list->head->next;
  new_node->prev = list->head;

  list->head->next->prev = new_node;
  list->head->next = new_node;

  ++list->count;

  return 0;
}

uint64_t simple_archiver_list_remove(SDArchiverLinkedList *list,
                                     int (*data_check_fn)(void *, void *),
                                     void *user_data) {
  if (!list) {
    return 0;
  }

  uint64_t removed_count = 0;

  SDArchiverLLNode *node = list->head;
  uint64_t iter_removed = 0;
  while (node) {
    if (iter_removed == 0) {
      node = node->next;
    }
    iter_removed = 0;
    if (node && node != list->tail) {
      if (data_check_fn(node->data, user_data) != 0) {
        SDArchiverLLNode *temp = node->next;

        if (node->data_free_fn) {
          node->data_free_fn(node->data);
        } else {
          free(node->data);
        }

        node->prev->next = node->next;
        node->next->prev = node->prev;
        free(node);

        node = temp;
        iter_removed = 1;
        ++removed_count;
        --list->count;
      }
    }
  }

  return removed_count;
}

int simple_archiver_list_remove_once(SDArchiverLinkedList *list,
                                     int (*data_check_fn)(void *, void *),
                                     void *user_data) {
  if (!list) {
    return 0;
  }

  SDArchiverLLNode *node = list->head;
  while (node) {
    node = node->next;
    if (node && node != list->tail) {
      if (data_check_fn(node->data, user_data) != 0) {
        if (node->data_free_fn) {
          node->data_free_fn(node->data);
        } else {
          free(node->data);
        }

        node->prev->next = node->next;
        node->next->prev = node->prev;
        free(node);

        --list->count;

        return 1;
      }
    }
  }

  return 0;
}

void *simple_archiver_list_get(const SDArchiverLinkedList *list,
                               int (*data_check_fn)(void *, void *),
                               void *user_data) {
  if (!list) {
    return NULL;
  }

  SDArchiverLLNode *node = list->head;
  while (node) {
    node = node->next;
    if (node && node != list->tail) {
      if (data_check_fn(node->data, user_data) != 0) {
        return node->data;
      }
    }
  }

  return NULL;
}
