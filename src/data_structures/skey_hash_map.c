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
// `skey_hash_map.c` is the implementation of a string-keyed hash map.

#include "skey_hash_map.h"

// Standard library includes
#include <stdlib.h>
#include <string.h>

// Local includes
#include "hash_map.h"

void simple_archiver_skey_hash_map_internal_rehash(SDArchiverSKeyHashMap *map) {
  SDArchiverSKeyHashMap *new_map = malloc(sizeof(SDArchiverSKeyHashMap));
  SDArchiverSKeyHashMap *old_map = map;

  new_map->buckets_size = (old_map->buckets_size - 1) * 2 + 1;
  new_map->count = 0;
  new_map->hash_fn = old_map->hash_fn;
  new_map->buckets = malloc(sizeof(void *) * new_map->buckets_size);

  for (size_t idx = 0; idx < new_map->buckets_size; ++idx) {
    new_map->buckets[idx] = malloc(sizeof(SDASKeyHMNode));
    new_map->buckets[idx]->next = NULL;
    new_map->buckets[idx]->data = NULL;
    new_map->buckets[idx]->data_cleanup_fn = NULL;
    new_map->buckets[idx]->str_len = 0;
  }

  for (size_t idx = 0; idx < old_map->buckets_size; ++idx) {
    SDASKeyHMNode *current = old_map->buckets[idx];
    current = current->next;
    SDASKeyHMNode *next = current ? current->next : NULL;

    while (current) {
      simple_archiver_skey_hash_map_insert(
          new_map,
          current->data,
          ((char*)current) + sizeof(SDASKeyHMNode),
          current->data_cleanup_fn);

      free(current);

      current = next;
      if (current) {
        next = current->next;
      }
    }
    free(old_map->buckets[idx]);
  }

  free(old_map->buckets);

  *map = *new_map;

  free(new_map);
}

SDArchiverSKeyHashMap *simple_archiver_skey_hash_map_init(void) {
  return simple_archiver_skey_hash_map_init_custom_hasher(
      simple_archiver_hash_default_fn);
}

SDArchiverSKeyHashMap *simple_archiver_skey_hash_map_init_custom_hasher(
    uint64_t (*hash_fn)(const void *, size_t)) {
  SDArchiverSKeyHashMap *map = malloc(sizeof(SDArchiverSKeyHashMap));

  map->buckets_size = SC_SA_DS_HASH_MAP_START_BUCKET_SIZE + 1;
  map->count = 0;
  map->hash_fn = hash_fn;

  map->buckets = malloc(sizeof(void *) * map->buckets_size);

  for (size_t idx = 0; idx < map->buckets_size; ++idx) {
    map->buckets[idx] = malloc(sizeof(SDASKeyHMNode));
    map->buckets[idx]->next = NULL;
    map->buckets[idx]->data = NULL;
    map->buckets[idx]->data_cleanup_fn = NULL;
    map->buckets[idx]->str_len = 0;
  }

  return map;
}

void simple_archiver_skey_hash_map_free_single_ptr(SDArchiverSKeyHashMap *map) {
  for (size_t idx = 0; idx < map->buckets_size; ++idx) {
    SDASKeyHMNode *current = map->buckets[idx]->next;
    SDASKeyHMNode *next = current ? current->next : NULL;
    while (current) {
      if (current->data_cleanup_fn) {
        current->data_cleanup_fn(current->data);
      } else {
        free(current->data);
      }
      free(current);

      current = next;
      if (current) {
        next = current->next;
      }
    }
    free(map->buckets[idx]);
  }

  free(map->buckets);
  free(map);
}

void simple_archiver_skey_hash_map_free(SDArchiverSKeyHashMap **map) {
  if (map && *map) {
    simple_archiver_skey_hash_map_free_single_ptr(*map);
    *map = NULL;
  }
}

int simple_archiver_skey_hash_map_insert(SDArchiverSKeyHashMap *map,
                                         void *value,
                                         const char *key,
                                         void (*value_cleanup_fn)(void *)) {
  if (!map || !value || !key) {
    return 1;
  }

  if (map->buckets_size <= map->count) {
    simple_archiver_skey_hash_map_internal_rehash(map);
  }

  uint64_t hash = map->hash_fn(key, strlen(key) + 1);
  size_t bucket_idx = hash % (uint64_t)map->buckets_size;

  SDASKeyHMNode *bucket = map->buckets[bucket_idx];

  SDASKeyHMNode *last = bucket;
  while (last->next != NULL) {
    last = last->next;
  }

  size_t key_len = strlen(key);

  SDASKeyHMNode *new_node = malloc(sizeof(SDASKeyHMNode) + key_len + 1);

  last->next = new_node;
  new_node->data = value;
  new_node->data_cleanup_fn = value_cleanup_fn;
  new_node->next = NULL;
  new_node->str_len = key_len;

  memcpy(((char*)new_node) + sizeof(SDASKeyHMNode), key, key_len + 1);

  ++map->count;

  return 0;
}

void *simple_archiver_skey_hash_map_get(const SDArchiverSKeyHashMap *map,
                                        const char *key) {
  if (!map || !key) {
    return NULL;
  }

  uint64_t hash = map->hash_fn(key, strlen(key) + 1);
  size_t bucket_idx = hash % (uint64_t)map->buckets_size;

  SDASKeyHMNode *current = map->buckets[bucket_idx];

  if (!current->next) {
    return NULL;
  }

  current = current->next;
  SDASKeyHMNode *next = current ? current->next : NULL;

  while (current) {
    const char *current_key = ((char*)current) + sizeof(SDASKeyHMNode);
    if (current->str_len == strlen(key) && strcmp(current_key, key) == 0) {
      return current->data;
    }

    current = next;
    if (current) {
      next = current->next;
    }
  }

  return NULL;
}

int simple_archiver_skey_hash_map_remove(SDArchiverSKeyHashMap *map,
                                         const char *key) {
  if (!map || !key) {
    return -1;
  }

  uint64_t hash = map->hash_fn(key, strlen(key) + 1);
  size_t bucket_idx = hash % (uint64_t)map->buckets_size;

  SDASKeyHMNode *current = map->buckets[bucket_idx];

  if (!current->next) {
    return -1;
  }

  SDASKeyHMNode *prev = current;
  current = current->next;
  SDASKeyHMNode *next = current ? current->next : NULL;

  uint64_t removed_count = 0;

  while (current) {
    const char *current_key = ((char*)current) + sizeof(SDASKeyHMNode);
    if (current->str_len == strlen(key) && strcmp(current_key, key) == 0) {
      if (current->data_cleanup_fn) {
        current->data_cleanup_fn(current->data);
      } else {
        free(current->data);
      }
      prev->next = next;
      free(current);
      current = prev->next;
      if (current) {
        next = current->next;
      }
      ++removed_count;
      continue;
    }

    prev = current;
    current = next;
    if (current) {
      next = current->next;
    }
  }

  map->count -= removed_count;

  if (removed_count > 1) {
    return 1;
  } else if (removed_count == 1) {
    return 0;
  } else {
    return -1;
  }
}

int simple_archiver_skey_hash_map_iter(const SDArchiverSKeyHashMap *map,
                                       int (*iter_check_fn)(const char *,
                                                            const void *,
                                                            void *),
                                       void *user_data) {
  if (!map || !iter_check_fn) {
    return -1;
  }

  for (size_t b_idx = 0; b_idx < map->buckets_size; ++b_idx) {
    SDASKeyHMNode *bucket = map->buckets[b_idx];
    SDASKeyHMNode *current = bucket->next;
    SDASKeyHMNode *next = current ? current->next : NULL;
    while (current) {
      int ret = iter_check_fn(((char*)current) + sizeof(SDASKeyHMNode),
                              current->data,
                              user_data);
      if (ret != 0) {
        return ret;
      }

      current = next;
      if (current) {
        next = current->next;
      }
    }
  }

  return 0;
}
