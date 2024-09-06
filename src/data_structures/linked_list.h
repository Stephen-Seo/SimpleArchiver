// ISC License
//
// Copyright (c) 2024 Stephen Seo
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
// `linked_list.h` is the header for a linked list data structure.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_LINKED_LIST_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_LINKED_LIST_H_

#include <stddef.h>

typedef struct SDArchiverLLNode {
  struct SDArchiverLLNode *next;
  struct SDArchiverLLNode *prev;
  void *data;
  void (*data_free_fn)(void *);
} SDArchiverLLNode;

typedef struct SDArchiverLinkedList {
  SDArchiverLLNode *head;
  SDArchiverLLNode *tail;
  size_t count;
} SDArchiverLinkedList;

SDArchiverLinkedList *simple_archiver_list_init(void);
void simple_archiver_list_free(SDArchiverLinkedList **list);

/// Returns 0 on success. Puts data at the end of the list
/// If data_free_fn is NULL, then "free" is used instead.
int simple_archiver_list_add(SDArchiverLinkedList *list, void *data,
                             void (*data_free_fn)(void *));

/// Returns 0 on success. Puts data at the front of the list
/// If data_free_fn is NULL, then "free" is used instead.
int simple_archiver_list_add_front(SDArchiverLinkedList *list, void *data,
                                   void (*data_free_fn)(void *));

/// Returns number of removed items.
/// data_check_fn must return non-zero if the data passed to it is to be
/// removed.
int simple_archiver_list_remove(SDArchiverLinkedList *list,
                                int (*data_check_fn)(void *, void *),
                                void *user_data);

/// Returns 1 on removed, 0 if not removed.
/// data_check_fn must return non-zero if the data passed to it is to be
/// removed.
int simple_archiver_list_remove_once(SDArchiverLinkedList *list,
                                     int (*data_check_fn)(void *, void *),
                                     void *user_data);

/// Returns non-null on success.
/// data_check_fn must return non-zero if the data passed to it is to be
/// returned.
void *simple_archiver_list_get(const SDArchiverLinkedList *list,
                               int (*data_check_fn)(void *, void *),
                               void *user_data);

#endif
