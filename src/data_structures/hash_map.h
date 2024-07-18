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
 * `hash_map.h` is the header for a hash map implementation.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_HASH_MAP_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_HASH_MAP_H_

#define SC_SA_DS_HASH_MAP_START_BUCKET_SIZE 32

#include "linked_list.h"

typedef struct SDArchiverHashMap {
  SDArchiverLinkedList **buckets;
  unsigned int buckets_size;
  unsigned int count;
} SDArchiverHashMap;

SDArchiverHashMap *simple_archiver_hash_map_init(void);
void simple_archiver_hash_map_free(SDArchiverHashMap **hash_map);

/// Returns zero on success.
/// On failure, frees the value and key using the given functions.
/// key must remain valid for the lifetime of its entry in the hash map.
/// If value_cleanup_fn is NULL, then "free" is used instead.
/// If key_cleanup_fn is NULL, then "free" is used instead.
/// NOTICE: You must not pass NULL to value, otherwise all "get" checks will
/// fail for the inserted key.
int simple_archiver_hash_map_insert(SDArchiverHashMap **hash_map, void *value,
                                    void *key, unsigned int key_size,
                                    void (*value_cleanup_fn)(void *),
                                    void (*key_cleanup_fn)(void *));

/// Returns NULL if not found.
void *simple_archiver_hash_map_get(SDArchiverHashMap *hash_map, void *key,
                                   unsigned int key_size);

/// Returns zero on success. Returns one if more than one entry was removed.
/// Otherwise returns non-zero and non-one value on error.
int simple_archiver_hash_map_remove(SDArchiverHashMap *hash_map, void *key,
                                    unsigned int key_size);

#endif
