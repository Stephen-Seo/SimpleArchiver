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
// `hash_map.c` is the source for a hash map implementation.

#include "hash_map.h"

#include <stdlib.h>
#include <string.h>

#include "../algorithms/linear_congruential_gen.h"

typedef struct SDArchiverHashMapData {
  void *value;
  void *key;
  size_t key_size;
  void (*value_cleanup_fn)(void *);
  void (*key_cleanup_fn)(void *);
} SDArchiverHashMapData;

typedef struct SDArchiverHashMapKeyData {
  void *key;
  size_t key_size;
} SDArchiverHashMapKeyData;

typedef struct SDArchiverInternalIterContext {
  int (*iter_check_fn)(const void *, size_t, const void *, void *);
  int ret;
  void *user_data;
} SDArchiverInternalIterContext;

void simple_archiver_hash_map_internal_cleanup_data(void *data) {
  SDArchiverHashMapData *hash_map_data = data;
  if (hash_map_data->value) {
    if (hash_map_data->value_cleanup_fn) {
      hash_map_data->value_cleanup_fn(hash_map_data->value);
    } else {
      free(hash_map_data->value);
    }
  }

  if (hash_map_data->key) {
    if (hash_map_data->key_cleanup_fn) {
      hash_map_data->key_cleanup_fn(hash_map_data->key);
    } else {
      free(hash_map_data->key);
    }
  }

  free(data);
}

int simple_archiver_hash_map_internal_pick_in_list(void *data, void *ud) {
  SDArchiverHashMapData *hash_map_data = data;
  SDArchiverHashMapKeyData *key_data = ud;

  return hash_map_data->key_size == key_data->key_size &&
                 memcmp(hash_map_data->key, key_data->key,
                        key_data->key_size) == 0
             ? 1
             : 0;
}

uint64_t simple_archiver_hash_default_fn(const void *key, size_t key_size) {
  uint64_t seed = 0;
  uint64_t temp;
  for (size_t idx = 0; idx < key_size; ++idx) {
    temp = (uint64_t)(((uint8_t*)key)[idx]) + seed;
    if (idx % 3 == 0) {
      temp ^= 0xA5A538A5A9B5A5A5;
    } else if (idx % 3 == 1) {
      temp ^= 0xD7A58BD7A58BD7AA;
    } else {
      temp ^= 0x8B7A8B8B87CB8B84;
    }
    seed += simple_archiver_algo_lcg_defaults(temp);
  }

  return simple_archiver_algo_lcg_defaults(seed);
}

void simple_archiver_hash_map_internal_no_free_fn(
    __attribute__((unused)) void *unused) {
  return;
}

/// Returns 0 on success.
int simple_archiver_hash_map_internal_rehash(SDArchiverHashMap *hash_map) {
  if (!hash_map) {
    return 1;
  }
  SDArchiverHashMap new_hash_map;
  new_hash_map.hash_fn = hash_map->hash_fn;
  new_hash_map.buckets_size = (hash_map->buckets_size - 1) * 2 + 1;
  // Pointers have the same size (at least on the same machine), so
  // sizeof(void*) should be ok.
  new_hash_map.buckets = malloc(sizeof(void *) * new_hash_map.buckets_size);
  for (size_t idx = 0; idx < new_hash_map.buckets_size; ++idx) {
    new_hash_map.buckets[idx] = simple_archiver_list_init();
  }
  new_hash_map.count = 0;

  // Iterate through the old hash map to populate the new hash map.
  for (size_t bucket_idx = 0; bucket_idx < hash_map->buckets_size;
       ++bucket_idx) {
    SDArchiverLLNode *node = hash_map->buckets[bucket_idx]->head;
    while (node) {
      node = node->next;
      if (node && node != hash_map->buckets[bucket_idx]->tail && node->data) {
        SDArchiverHashMapData *data = node->data;
        simple_archiver_hash_map_insert(&new_hash_map, data->value, data->key,
                                        data->key_size, data->value_cleanup_fn,
                                        data->key_cleanup_fn);
        data->key_cleanup_fn = simple_archiver_hash_map_internal_no_free_fn;
        data->value_cleanup_fn = simple_archiver_hash_map_internal_no_free_fn;
      }
    }
  }

  // Free the buckets in the old hash_map.
  for (size_t idx = 0; idx < hash_map->buckets_size; ++idx) {
    SDArchiverLinkedList **linked_list = hash_map->buckets + idx;
    simple_archiver_list_free(linked_list);
  }
  free(hash_map->buckets);

  // Move the new buckets and related data into the old hash_map.
  *hash_map = new_hash_map;

  return 0;
}

SDArchiverHashMap *simple_archiver_hash_map_init(void) {
  return simple_archiver_hash_map_init_custom_hasher(
      simple_archiver_hash_default_fn);
}

SDArchiverHashMap *simple_archiver_hash_map_init_custom_hasher(
    uint64_t (*hash_fn)(const void *, size_t)) {
  SDArchiverHashMap *hash_map = malloc(sizeof(SDArchiverHashMap));
  hash_map->hash_fn = hash_fn;
  hash_map->buckets_size = SC_SA_DS_HASH_MAP_START_BUCKET_SIZE + 1;
  // Pointers have the same size (at least on the same machine), so
  // sizeof(void*) should be ok.
  hash_map->buckets = malloc(sizeof(void *) * hash_map->buckets_size);
  for (size_t idx = 0; idx < hash_map->buckets_size; ++idx) {
    hash_map->buckets[idx] = simple_archiver_list_init();
  }
  hash_map->count = 0;

  return hash_map;
}

void simple_archiver_hash_map_free_single_ptr(SDArchiverHashMap *hash_map) {
  if (hash_map) {
    for (size_t idx = 0; idx < hash_map->buckets_size; ++idx) {
      SDArchiverLinkedList **linked_list = hash_map->buckets + idx;
      simple_archiver_list_free(linked_list);
    }

    free(hash_map->buckets);
    free(hash_map);
  }
}

void simple_archiver_hash_map_free(SDArchiverHashMap **hash_map) {
  if (hash_map && *hash_map) {
    simple_archiver_hash_map_free_single_ptr(*hash_map);
    *hash_map = NULL;
  }
}

int simple_archiver_hash_map_insert(SDArchiverHashMap *hash_map, void *value,
                                    void *key, size_t key_size,
                                    void (*value_cleanup_fn)(void *),
                                    void (*key_cleanup_fn)(void *)) {
  if (hash_map->buckets_size <= hash_map->count) {
    simple_archiver_hash_map_internal_rehash(hash_map);
  }

  SDArchiverHashMapData *data = malloc(sizeof(SDArchiverHashMapData));
  data->value = value;
  data->key = key;
  data->key_size = key_size;
  data->value_cleanup_fn = value_cleanup_fn;
  data->key_cleanup_fn = key_cleanup_fn;

  uint64_t hash = hash_map->hash_fn(key, key_size) % hash_map->buckets_size;
  int result = simple_archiver_list_add_front(
      hash_map->buckets[hash], data,
      simple_archiver_hash_map_internal_cleanup_data);

  if (result == 0) {
    ++hash_map->count;
    return 0;
  } else {
    if (value) {
      if (value_cleanup_fn) {
        value_cleanup_fn(value);
      } else {
        free(value);
      }
    }
    if (key) {
      if (key_cleanup_fn) {
        key_cleanup_fn(key);
      } else {
        free(key);
      }
    }

    free(data);
    return 1;
  }
}

void *simple_archiver_hash_map_get(const SDArchiverHashMap *hash_map,
                                   const void *key, size_t key_size) {
  uint64_t hash = hash_map->hash_fn(key, key_size) % hash_map->buckets_size;

  SDArchiverLLNode *node = hash_map->buckets[hash]->head;
  while (node) {
    node = node->next;
    if (node && node != hash_map->buckets[hash]->tail && node->data) {
      SDArchiverHashMapData *data = node->data;
      if (key_size == data->key_size && memcmp(data->key, key, key_size) == 0) {
        return data->value;
      }
    }
  }

  return NULL;
}

int simple_archiver_hash_map_remove(SDArchiverHashMap *hash_map, void *key,
                                    size_t key_size) {
  uint64_t hash = hash_map->hash_fn(key, key_size) % hash_map->buckets_size;

  SDArchiverHashMapKeyData key_data;
  key_data.key = key;
  key_data.key_size = key_size;

  uint64_t result = simple_archiver_list_remove(
      hash_map->buckets[hash], simple_archiver_hash_map_internal_pick_in_list,
      &key_data);
  if (result == 1) {
    return 0;
  } else if (result > 1) {
    return 1;
  } else {
    return 2;
  }
}

int simple_archiver_internal_hash_map_bucket_iter_fn(void *data, void *ud) {
  SDArchiverHashMapData *hash_map_data = data;
  SDArchiverInternalIterContext *ctx = ud;

  ctx->ret = ctx->iter_check_fn(hash_map_data->key, hash_map_data->key_size,
                                hash_map_data->value, ctx->user_data);
  if (ctx->ret != 0) {
    return 1;
  } else {
    return 0;
  }
}

int simple_archiver_hash_map_iter(const SDArchiverHashMap *hash_map,
                                  int (*iter_check_fn)(const void *, size_t,
                                                       const void *, void *),
                                  void *user_data) {
  SDArchiverInternalIterContext ctx;
  ctx.iter_check_fn = iter_check_fn;
  ctx.ret = 0;
  ctx.user_data = user_data;
  for (size_t idx = 0; idx < hash_map->buckets_size; ++idx) {
    if (simple_archiver_list_get(
            hash_map->buckets[idx],
            simple_archiver_internal_hash_map_bucket_iter_fn, &ctx) != 0) {
      return ctx.ret;
    }
  }
  return ctx.ret;
}
