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
 * `hash_map.c` is the source for a hash map implementation.
 */

#include "hash_map.h"

#include <stdlib.h>
#include <string.h>

#include "../algorithms/linear_congruential_gen.h"

typedef struct SDArchiverHashMapData {
  void *value;
  void *key;
  unsigned int key_size;
  void (*value_cleanup_fn)(void *);
  void (*key_cleanup_fn)(void *);
} SDArchiverHashMapData;

typedef struct SDArchiverHashMapKeyData {
  void *key;
  unsigned int key_size;
} SDArchiverHashMapKeyData;

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

unsigned long long simple_archiver_hash_map_internal_key_to_hash(
    void *key, unsigned int key_size) {
  unsigned long long seed = 0;
  unsigned long long temp = 0;
  unsigned int count = 0;
  for (unsigned int idx = 0; idx < key_size; ++idx) {
    temp |= *((unsigned char *)key + count++);
    if (count >= 8) {
      count = 0;
      seed += temp;
      temp = 0;
    }
  }
  if (temp != 0) {
    seed += temp;
  }

  return simple_archiver_algo_lcg(seed, SC_ALGO_LCG_DEFAULT_A,
                                  SC_ALGO_LCG_DEFAULT_C);
}

/// Returns 0 on success.
int simple_archiver_hash_map_internal_rehash(SDArchiverHashMap **hash_map) {
  if (!hash_map || !*hash_map) {
    return 1;
  }
  SDArchiverHashMap *new_hash_map = malloc(sizeof(SDArchiverHashMap));
  new_hash_map->buckets_size = (*hash_map)->buckets_size * 2;
  // Pointers have the same size (at least on the same machine), so
  // sizeof(void*) should be ok.
  new_hash_map->buckets = malloc(sizeof(void *) * new_hash_map->buckets_size);
  for (unsigned int idx = 0; idx < new_hash_map->buckets_size; ++idx) {
    new_hash_map->buckets[idx] = simple_archiver_list_init();
  }
  new_hash_map->count = 0;

  // Iterate through the old hash map to populate the new hash map.
  for (unsigned int bucket_idx = 0; bucket_idx < (*hash_map)->buckets_size;
       ++bucket_idx) {
    SDArchiverLLNode *node = (*hash_map)->buckets[bucket_idx]->head;
    while (node) {
      node = node->next;
      if (node && node != (*hash_map)->buckets[bucket_idx]->tail &&
          node->data) {
        SDArchiverHashMapData *data = node->data;
        simple_archiver_hash_map_insert(&new_hash_map, data->value, data->key,
                                        data->key_size, data->value_cleanup_fn,
                                        data->key_cleanup_fn);
        node->data = NULL;
      }
    }
  }

  simple_archiver_hash_map_free(hash_map);
  *hash_map = new_hash_map;

  return 0;
}

SDArchiverHashMap *simple_archiver_hash_map_init(void) {
  SDArchiverHashMap *hash_map = malloc(sizeof(SDArchiverHashMap));
  hash_map->buckets_size = SC_SA_DS_HASH_MAP_START_BUCKET_SIZE;
  // Pointers have the same size (at least on the same machine), so
  // sizeof(void*) should be ok.
  hash_map->buckets = malloc(sizeof(void *) * hash_map->buckets_size);
  for (unsigned int idx = 0; idx < hash_map->buckets_size; ++idx) {
    hash_map->buckets[idx] = simple_archiver_list_init();
  }
  hash_map->count = 0;

  return hash_map;
}

void simple_archiver_hash_map_free(SDArchiverHashMap **hash_map) {
  if (hash_map && *hash_map) {
    for (unsigned int idx = 0; idx < (*hash_map)->buckets_size; ++idx) {
      SDArchiverLinkedList **linked_list = (*hash_map)->buckets + idx;
      simple_archiver_list_free(linked_list);
    }

    free((*hash_map)->buckets);
    free(*hash_map);

    *hash_map = NULL;
  }
}

int simple_archiver_hash_map_insert(SDArchiverHashMap **hash_map, void *value,
                                    void *key, unsigned int key_size,
                                    void (*value_cleanup_fn)(void *),
                                    void (*key_cleanup_fn)(void *)) {
  if ((*hash_map)->buckets_size <= (*hash_map)->count) {
    simple_archiver_hash_map_internal_rehash(hash_map);
  }

  SDArchiverHashMapData *data = malloc(sizeof(SDArchiverHashMapData));
  data->value = value;
  data->key = key;
  data->key_size = key_size;
  data->value_cleanup_fn = value_cleanup_fn;
  data->key_cleanup_fn = key_cleanup_fn;

  unsigned long long hash =
      simple_archiver_hash_map_internal_key_to_hash(key, key_size) %
      (*hash_map)->buckets_size;
  int result = simple_archiver_list_add_front(
      (*hash_map)->buckets[hash], data,
      simple_archiver_hash_map_internal_cleanup_data);

  if (result == 0) {
    ++(*hash_map)->count;
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

void *simple_archiver_hash_map_get(SDArchiverHashMap *hash_map, void *key,
                                   unsigned int key_size) {
  unsigned long long hash =
      simple_archiver_hash_map_internal_key_to_hash(key, key_size) %
      hash_map->buckets_size;

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
                                    unsigned int key_size) {
  unsigned long long hash =
      simple_archiver_hash_map_internal_key_to_hash(key, key_size) %
      hash_map->buckets_size;

  SDArchiverHashMapKeyData key_data;
  key_data.key = key;
  key_data.key_size = key_size;

  int result = simple_archiver_list_remove(
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
