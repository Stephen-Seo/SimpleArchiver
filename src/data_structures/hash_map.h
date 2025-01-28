// ISC License
//
// Copyright (c) 2024-2025 Stephen Seo
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
// `hash_map.h` is the header for a hash map implementation.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_HASH_MAP_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_DATA_STRUCTURE_HASH_MAP_H_

#define SC_SA_DS_HASH_MAP_START_BUCKET_SIZE 32

// Standard library includes.
#include <stddef.h>
#include <stdint.h>

// Local includes.
#include "linked_list.h"

typedef struct SDArchiverHashMap {
  SDArchiverLinkedList **buckets;
  size_t buckets_size;
  size_t count;
  uint64_t (*hash_fn)(const void *, size_t);
} SDArchiverHashMap;

uint64_t simple_archiver_hash_default_fn(const void *key, size_t key_size);

SDArchiverHashMap *simple_archiver_hash_map_init(void);

/// Creates a hash map that will use the custom hash function instead of the
/// default. Note that the hash function must return a 64-bit unsigned integer
/// as specified by the function's api. The first parameter of hash_fn is a
/// pointer to the key to be hashed, and the second parameter is the size of
/// the key in bytes.
SDArchiverHashMap *simple_archiver_hash_map_init_custom_hasher(
    uint64_t (*hash_fn)(const void *, size_t));

/// It is recommended to use the double-pointer version of hash-map free as
/// that will ensure the variable holding the pointer will end up pointing to
/// NULL after free.
void simple_archiver_hash_map_free_single_ptr(SDArchiverHashMap *hash_map);
void simple_archiver_hash_map_free(SDArchiverHashMap **hash_map);

/// Returns zero on success.
/// On failure, frees the value and key using the given functions.
/// key must remain valid for the lifetime of its entry in the hash map.
/// If value_cleanup_fn is NULL, then "free" is used instead.
/// If key_cleanup_fn is NULL, then "free" is used instead.
/// NOTICE: You must not pass NULL to value, otherwise all "get" checks will
/// fail for the inserted key.
int simple_archiver_hash_map_insert(SDArchiverHashMap *hash_map, void *value,
                                    void *key, size_t key_size,
                                    void (*value_cleanup_fn)(void *),
                                    void (*key_cleanup_fn)(void *));

/// Returns NULL if not found.
void *simple_archiver_hash_map_get(const SDArchiverHashMap *hash_map,
                                   const void *key, size_t key_size);

/// Returns zero on success. Returns one if more than one entry was removed.
/// Otherwise returns non-zero and non-one value on error.
int simple_archiver_hash_map_remove(SDArchiverHashMap *hash_map, void *key,
                                    size_t key_size);

/// Iterates through the hash map with the "iter_check_fn", which is passed the
/// key, key-size, value, and user_data. This function will call "iter_check_fn"
/// on every entry in the given hash_map. If "iter_check_fn" returns non-zero,
/// iteration will halt and this function will return the same value. If
/// "iter_check_fn" returns zero for every call, then this function will return
/// zero after having iterated through every key-value pair.
int simple_archiver_hash_map_iter(const SDArchiverHashMap *hash_map,
                                  int (*iter_check_fn)(const void *, size_t,
                                                       const void *, void *),
                                  void *user_data);

#endif
