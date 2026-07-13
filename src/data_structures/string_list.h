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
// `data_structures/string_list.h` is the header for string-list data structure.

#ifndef COM_SEODISPARATE_SIMPLE_ARCHIVER_DATA_STRUCTURE_STRING_LIST_H_
#define COM_SEODISPARATE_SIMPLE_ARCHIVER_DATA_STRUCTURE_STRING_LIST_H_

#include <stdint.h>

typedef struct SDArchiverSLNode {
  struct SDArchiverSLNode *next;
  struct SDArchiverSLNode *prev;
  // size does not include NULL terminator.
  uint64_t size;
} SDArchiverSLNode;

typedef struct SDArchiverStringList {
  SDArchiverSLNode *head;
  SDArchiverSLNode *tail;
  uint64_t count;
} SDArchiverStringList;

SDArchiverStringList *simple_archiver_slist_init(void);

void simple_archiver_slist_free_single_ptr(SDArchiverStringList *slist);

void simple_archiver_slist_free(SDArchiverStringList **slist);

/// Returns 0 on success.
int simple_archiver_slist_add(SDArchiverStringList *slist, const char *str);

/// Returns 0 on success.
int simple_archiver_slist_add_front(SDArchiverStringList *slist,
                                    const char *str);

/// Returns number of removed strings.
/// data_check_fn must return non-zero if the string passed to it is to be
/// removed.
uint64_t simple_archiver_slist_remove(
    SDArchiverStringList *slist,
    int (*data_check_fn)(const char *, void *),
    void *user_data);

/// Returns 1 on removed, 0 if not removed.
/// data_check_fn must return non-zero if the string passed to it is to be
/// removed.
int simple_archiver_slist_remove_once(
    SDArchiverStringList *slist,
    int (*data_check_fn)(const char *, void *),
    void *user_data);

/// Returns non-null on success.
/// data_check_fn must return non-zero if the string passed to it is to be
/// returned.
const char *simple_archiver_slist_get(
    const SDArchiverStringList *slist,
    int (*data_check_fn)(const char *, void*),
    void *user_data);

#endif
