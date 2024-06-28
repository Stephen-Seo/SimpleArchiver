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
 * `linked_list.h` is the header for a linked list data structure.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_LINKED_LIST_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_LINKED_LIST_H_

typedef struct SDArchiverLLNode {
  struct SDArchiverLLNode *next;
  struct SDArchiverLLNode *prev;
  void *data;
  void (*data_free_fn)(void *);
} SDArchiverLLNode;

typedef struct SDArchiverLinkedList {
  SDArchiverLLNode *head;
  SDArchiverLLNode *tail;
  unsigned int count;
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
void *simple_archiver_list_get(SDArchiverLinkedList *list,
                               int (*data_check_fn)(void *, void *),
                               void *user_data);

#endif
