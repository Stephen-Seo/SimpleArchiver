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
 * `linked_list.c` is the source for a linked list data structure.
 */

#include "linked_list.h"

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

void simple_archiver_list_free(SDArchiverLinkedList **list) {
  if (list && *list) {
    SDArchiverLLNode *node = (*list)->head;
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

    free(*list);
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

int simple_archiver_list_remove(SDArchiverLinkedList *list,
                                int (*data_check_fn)(void *, void *),
                                void *user_data) {
  if (!list) {
    return 0;
  }

  int removed_count = 0;

  SDArchiverLLNode *node = list->head;
  int iter_removed = 0;
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
