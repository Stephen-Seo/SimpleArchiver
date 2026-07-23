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
// `data_structures/string_list.c` is the source for string-list data structure.

#include "string_list.h"

#include <stdlib.h>
#include <string.h>

SDArchiverStringList *simple_archiver_slist_init(void) {
  SDArchiverStringList *slist = malloc(sizeof(SDArchiverStringList));

  slist->count = 0;

  slist->head = malloc(sizeof(SDArchiverSLNode));
  slist->tail = malloc(sizeof(SDArchiverSLNode));

  slist->head->prev = NULL;
  slist->head->next = slist->tail;
  slist->head->size = 0;

  slist->tail->prev = slist->head;
  slist->tail->next = NULL;
  slist->tail->size = 0;

  return slist;
}

void simple_archiver_slist_free_single_ptr(SDArchiverStringList *slist) {
  if (!slist) {
    return;
  }

  SDArchiverSLNode *node = slist->head->next;
  while (node != slist->tail) {
    SDArchiverSLNode *next = node->next;
    free(node);
    node = next;
  }

  free(slist->head);
  free(slist->tail);

  free(slist);
}

void simple_archiver_slist_free(SDArchiverStringList **slist) {
  if (!slist) {
    return;
  }

  simple_archiver_slist_free_single_ptr(*slist);

  *slist = NULL;
}

int simple_archiver_slist_add(SDArchiverStringList *slist, const char *str) {
  if (!slist) {
    return 1;
  }

  uint64_t size = strlen(str);

  SDArchiverSLNode *new_node = malloc(sizeof(SDArchiverSLNode) + size + 1);
  new_node->next = slist->tail;
  new_node->prev = slist->tail->prev;
  slist->tail->prev->next = new_node;
  slist->tail->prev = new_node;

  new_node->size = size;

  memcpy(((char*)new_node) + sizeof(SDArchiverSLNode),
         str,
         size + 1);

  ++(slist->count);

  return 0;
}

int simple_archiver_slist_add_front(SDArchiverStringList *slist,
                                    const char *str) {
  if (!slist) {
    return 1;
  }

  uint64_t size = strlen(str);

  SDArchiverSLNode *new_node = malloc(sizeof(SDArchiverSLNode) + size + 1);
  new_node->next = slist->head->next;
  new_node->prev = slist->head;
  slist->head->next->prev = new_node;
  slist->head->next = new_node;

  new_node->size = size;

  memcpy(((char*)new_node) + sizeof(SDArchiverSLNode),
         str,
         size + 1);

  ++(slist->count);

  return 0;
}

uint64_t simple_archiver_slist_remove(
    SDArchiverStringList *slist,
    int (*data_check_fn)(const char *, void *),
    void *user_data) {
  if (!slist) {
    return 0;
  }

  uint64_t remove_count = 0;

  SDArchiverSLNode *node = slist->head->next;
  while (node != slist->tail) {
    SDArchiverSLNode *next = node->next;

    if (data_check_fn(((char*)node) + sizeof(SDArchiverSLNode), user_data)) {
      // remove
      node->prev->next = node->next;
      node->next->prev = node->prev;
      free(node);

      ++remove_count;
      --(slist->count);
    }

    node = next;
  }

  return remove_count;
}

int simple_archiver_slist_remove_once(
    SDArchiverStringList *slist,
    int (*data_check_fn)(const char *, void *),
    void *user_data) {
  if (!slist) {
    return 0;
  }

  SDArchiverSLNode *node = slist->head->next;
  while (node != slist->tail) {
    SDArchiverSLNode *next = node->next;

    if (data_check_fn(((char*)node) + sizeof(SDArchiverSLNode), user_data)) {
      // remove
      node->prev->next = node->next;
      node->next->prev = node->prev;
      free(node);

      --(slist->count);

      return 1;
    }

    node = next;
  }

  return 0;
}

const char *simple_archiver_slist_get(
    const SDArchiverStringList *slist,
    int (*data_check_fn)(const char *, void*),
    void *user_data) {
  if (!slist) {
    return NULL;
  }

  SDArchiverSLNode *node = slist->head->next;
  while (node != slist->tail) {
    const char *str = ((char*)node) + sizeof(SDArchiverSLNode);
    if (data_check_fn(str, user_data)) {
      return str;
    }

    node = node->next;
  }

  return NULL;
}
