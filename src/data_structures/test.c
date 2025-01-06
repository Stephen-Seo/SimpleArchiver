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
// `data_structures/test.c` is the source for testing data structure code.

// Standard library includes.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Local includes.
#include "../algorithms/linear_congruential_gen.h"
#include "hash_map.h"
#include "linked_list.h"
#include "priority_heap.h"

#define SDARCHIVER_DS_TEST_HASH_MAP_ITER_SIZE 100

static int32_t checks_checked = 0;
static int32_t checks_passed = 0;

#define CHECK_TRUE(x)                                             \
  do {                                                            \
    ++checks_checked;                                             \
    if (!(x)) {                                                   \
      printf("CHECK_TRUE at line %u failed: %s\n", __LINE__, #x); \
    } else {                                                      \
      ++checks_passed;                                            \
    }                                                             \
  } while (0);
#define CHECK_FALSE(x)                                             \
  do {                                                             \
    ++checks_checked;                                              \
    if (x) {                                                       \
      printf("CHECK_FALSE at line %u failed: %s\n", __LINE__, #x); \
    } else {                                                       \
      ++checks_passed;                                             \
    }                                                              \
  } while (0);

void no_free_fn(__attribute__((unused)) void *unused) { return; }

int get_one_fn(void *data, __attribute__((unused)) void *ud) {
  return strcmp(data, "one") == 0 ? 1 : 0;
}

int get_two_fn(void *data, __attribute__((unused)) void *ud) {
  return strcmp(data, "two") == 0 ? 1 : 0;
}

int get_three_fn(void *data, __attribute__((unused)) void *ud) {
  return strcmp(data, "three") == 0 ? 1 : 0;
}

int more_fn(int64_t a, int64_t b) { return a > b ? 1 : 0; }

int hash_map_iter_check_fn(__attribute__((unused)) const void *key,
                           __attribute__((unused)) size_t key_size,
                           const void *value, void *ud) {
  char *found_buf = ud;
  const size_t real_value = (const size_t)value;
  if (real_value < SDARCHIVER_DS_TEST_HASH_MAP_ITER_SIZE) {
    found_buf[real_value] += 1;
    return 0;
  } else {
    return 1;
  }
}

int hash_map_iter_check_fn2(__attribute__((unused)) const void *key,
                            __attribute__((unused)) size_t key_size,
                            __attribute__((unused)) const void *value,
                            __attribute__((unused)) void *ud) {
  return 2;
}

void test_iter_fn_priority_heap(void *data, void *user_data) {
  void **ptrs = user_data;
  char *elems = ptrs[0];
  const uint32_t *size = ptrs[1];

  uint32_t *uint_data = data;
  if (uint_data) {
    printf("Got data %" PRIu32 "\n", *uint_data);
    if (*uint_data < *size) {
      elems[*uint_data] = 1;
    } else {
      printf(
        "WARNING: data in priorty heap is invalid: \"%" PRIu32 "\"!\n",
        *uint_data);
    }
  } else {
    printf("WARNING: data in priority heap is NULL!\n");
  }
}

int main(void) {
  puts("Begin data-structures unit test.");
  fflush(stdout);

  // Test LinkedList.
  {
    SDArchiverLinkedList *list = simple_archiver_list_init();

    CHECK_TRUE(list->count == 0);

    const char *one = "one";
    const char *two = "two";
    const char *three = "three";

    simple_archiver_list_add(list, (void *)one, no_free_fn);

    CHECK_TRUE(list->count == 1);

    simple_archiver_list_add(list, (void *)two, no_free_fn);

    CHECK_TRUE(list->count == 2);

    simple_archiver_list_add(list, (void *)three, no_free_fn);

    CHECK_TRUE(list->count == 3);

    void *ptr = simple_archiver_list_get(list, get_one_fn, NULL);
    CHECK_TRUE(ptr == one);

    ptr = simple_archiver_list_get(list, get_two_fn, NULL);
    CHECK_TRUE(ptr == two);

    ptr = simple_archiver_list_get(list, get_three_fn, NULL);
    CHECK_TRUE(ptr == three);

    CHECK_TRUE(simple_archiver_list_remove(list, get_two_fn, NULL) == 1);
    CHECK_TRUE(list->count == 2);
    CHECK_TRUE(simple_archiver_list_get(list, get_two_fn, NULL) == NULL);

    CHECK_TRUE(simple_archiver_list_remove_once(list, get_one_fn, NULL) == 1);
    CHECK_TRUE(list->count == 1);
    CHECK_TRUE(simple_archiver_list_get(list, get_one_fn, NULL) == NULL);

    simple_archiver_list_free(&list);

    CHECK_TRUE(list == NULL);
  }

  // Test HashMap.
  {
    SDArchiverHashMap *hash_map = simple_archiver_hash_map_init();
    simple_archiver_hash_map_free(&hash_map);

    hash_map = simple_archiver_hash_map_init();

    {
      int32_t *value, *key;

      for (uint32_t idx = 0; idx < 20; ++idx) {
        value = malloc(sizeof(int32_t));
        key = malloc(sizeof(int32_t));
        *value = idx;
        *key = idx;
        simple_archiver_hash_map_insert(hash_map, value, key, sizeof(int32_t),
                                        NULL, NULL);
      }
    }

    int32_t value, key;
    void *value_ptr;

    for (value = 0, key = 0; value < 20 && key < 20; ++value, ++key) {
      value_ptr = simple_archiver_hash_map_get(hash_map, &key, sizeof(int32_t));
      CHECK_TRUE(value_ptr != NULL);
      CHECK_TRUE(memcmp(value_ptr, &value, sizeof(int32_t)) == 0);
    }

    key = 5;
    simple_archiver_hash_map_remove(hash_map, &key, sizeof(int32_t));
    key = 15;
    simple_archiver_hash_map_remove(hash_map, &key, sizeof(int32_t));

    for (value = 0, key = 0; value < 20 && key < 20; ++value, ++key) {
      value_ptr = simple_archiver_hash_map_get(hash_map, &key, sizeof(int32_t));
      if (key != 5 && key != 15) {
        CHECK_TRUE(value_ptr != NULL);
        CHECK_TRUE(memcmp(value_ptr, &value, sizeof(int32_t)) == 0);
      } else {
        CHECK_TRUE(value_ptr == NULL);
      }
    }

    simple_archiver_hash_map_free(&hash_map);

    // Rehash test for Memcheck.
    hash_map = simple_archiver_hash_map_init();
    for (uint32_t idx = 0; idx < SC_SA_DS_HASH_MAP_START_BUCKET_SIZE + 1;
         ++idx) {
      uint32_t *copy_value = malloc(sizeof(uint32_t));
      *copy_value = idx;
      uint32_t *copy_key = malloc(sizeof(uint32_t));
      *copy_key = idx;
      simple_archiver_hash_map_insert(hash_map, copy_value, copy_key,
                                      sizeof(uint32_t), NULL, NULL);
    }
    simple_archiver_hash_map_free(&hash_map);

    // Hash map iter test.
    hash_map = simple_archiver_hash_map_init();

    for (size_t idx = 0; idx < SDARCHIVER_DS_TEST_HASH_MAP_ITER_SIZE; ++idx) {
      simple_archiver_hash_map_insert(hash_map, (void *)idx, &idx,
                                      sizeof(size_t), no_free_fn, no_free_fn);
    }

    char found[SDARCHIVER_DS_TEST_HASH_MAP_ITER_SIZE] = {0};

    CHECK_TRUE(simple_archiver_hash_map_iter(hash_map, hash_map_iter_check_fn,
                                             found) == 0);

    for (uint32_t idx = 0; idx < SDARCHIVER_DS_TEST_HASH_MAP_ITER_SIZE; ++idx) {
      CHECK_TRUE(found[idx] == 1);
    }

    CHECK_TRUE(simple_archiver_hash_map_iter(hash_map, hash_map_iter_check_fn2,
                                             found) == 2);

    for (uint32_t idx = 0; idx < SDARCHIVER_DS_TEST_HASH_MAP_ITER_SIZE; ++idx) {
      CHECK_TRUE(found[idx] == 1);
    }

    simple_archiver_hash_map_free(&hash_map);
  }

  // Test hashing.
  //{
  //  printf("Distribution of 13 over 33...\n");
  //  unsigned int counts[33];
  //  memset(counts, 0, sizeof(unsigned int) * 33);

  //  uint64_t hash;

  //  hash = simple_archiver_hash_default_fn("/", 2);
  //  printf("%s in bucket %lu (%lu)\n", "/", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/faq", 5);
  //  printf("%s in bucket %lu (%lu)\n", "/faq", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/FAQ", 5);
  //  printf("%s in bucket %lu (%lu)\n", "/FAQ", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/url", 5);
  //  printf("%s in bucket %lu (%lu)\n", "/url", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/home", 6);
  //  printf("%s in bucket %lu (%lu)\n", "/home", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/blog", 6);
  //  printf("%s in bucket %lu (%lu)\n", "/blog", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/test", 6);
  //  printf("%s in bucket %lu (%lu)\n", "/test", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/menu", 6);
  //  printf("%s in bucket %lu (%lu)\n", "/menu", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/posts", 7);
  //  printf("%s in bucket %lu (%lu)\n", "/posts", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/about", 7);
  //  printf("%s in bucket %lu (%lu)\n", "/about", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/media", 7);
  //  printf("%s in bucket %lu (%lu)\n", "/media", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/social", 8);
  //  printf("%s in bucket %lu (%lu)\n", "/social", hash % 33, hash);
  //  counts[hash % 33] += 1;
  //  hash = simple_archiver_hash_default_fn("/projects", 10);
  //  printf("%s in bucket %lu (%lu)\n", "/projects", hash % 33, hash);
  //  counts[hash % 33] += 1;

  //  for (unsigned int idx = 0; idx < 33; ++idx) {
  //    printf("Bucket %u: %u\n", idx, counts[idx]);
  //  }
  //}

  // Test PriorityHeap.
  {
    SDArchiverPHeap *priority_heap = simple_archiver_priority_heap_init();
    simple_archiver_priority_heap_free(&priority_heap);

    priority_heap = simple_archiver_priority_heap_init();

    // Just 3 elements.
    for (uint32_t idx = 0; idx < 3; ++idx) {
      uint32_t *data = malloc(sizeof(uint32_t));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
    }
    for (uint32_t idx = 0; idx < 3; ++idx) {
      uint32_t *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %" PRIu32 ", data is %" PRIu32 "\n", idx, *data);
      }
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %" PRIu32 ", data is %" PRIu32 "\n", idx, *data);
      }
      free(data);
    }

    // 100 elements.
    uint32_t max = 100;

    for (uint32_t idx = 0; idx < max; ++idx) {
      uint32_t *data = malloc(sizeof(uint32_t));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
    }

    for (uint32_t idx = 0; idx < max; ++idx) {
      uint32_t *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      free(data);
    }

    // Insert in reverse order.
    for (uint32_t idx = max; idx-- > 0;) {
      uint32_t *data = malloc(sizeof(uint32_t));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
    }

    for (uint32_t idx = 0; idx < max; ++idx) {
      uint32_t *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      free(data);
    }

    // Insert in random order.
    uint32_t *array = malloc(sizeof(uint32_t) * max);
    for (uint32_t idx = 0; idx < max; ++idx) {
      array[idx] = idx;
    }

    // Deterministic randomization.
    for (uint32_t idx = max - 1; idx-- > 0;) {
      uint32_t other_idx =
          simple_archiver_algo_lcg_defaults(idx) % (uint64_t)(idx + 1);
      if (max - 1 != other_idx) {
        uint32_t temp = array[max - 1];
        array[max - 1] = array[other_idx];
        array[other_idx] = temp;
      }
    }

    // Insert the deterministically randomized array.
    for (uint32_t idx = 0; idx < max; ++idx) {
      simple_archiver_priority_heap_insert(priority_heap, array[idx],
                                           array + idx, no_free_fn);
    }

    for (uint32_t idx = 0; idx < max; ++idx) {
      uint32_t *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %" PRIu32 ", data is %" PRIu32 "\n", idx, *data);
      }
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %" PRIu32 ", data is %" PRIu32 "\n", idx, *data);
      }
    }
    free(array);

    simple_archiver_priority_heap_free(&priority_heap);

    // Insert, don't pop, do free, for memcheck.
    priority_heap = simple_archiver_priority_heap_init();
    for (uint32_t idx = 0; idx < max; ++idx) {
      uint32_t *data = malloc(sizeof(uint32_t));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
    }
    simple_archiver_priority_heap_free(&priority_heap);

    // Reverse priority.
    priority_heap = simple_archiver_priority_heap_init_less_fn(more_fn);

    for (uint32_t idx = 0; idx < max; ++idx) {
      uint32_t *data = malloc(sizeof(uint32_t));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
    }

    for (uint32_t idx = max; idx-- > 0;) {
      uint32_t *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      free(data);
    }

    simple_archiver_priority_heap_free(&priority_heap);

    // Insert in random order with reverse-priority-heap.
    priority_heap = simple_archiver_priority_heap_init_less_fn(more_fn);
    array = malloc(sizeof(uint32_t) * max);
    for (uint32_t idx = 0; idx < max; ++idx) {
      array[idx] = idx;
    }

    // Deterministic randomization.
    for (uint32_t idx = max - 1; idx-- > 0;) {
      uint32_t other_idx =
          simple_archiver_algo_lcg_defaults(idx) % (uint64_t)(idx + 1);
      if (max - 1 != other_idx) {
        uint32_t temp = array[max - 1];
        array[max - 1] = array[other_idx];
        array[other_idx] = temp;
      }
    }

    // Insert the deterministically randomized array.
    for (uint32_t idx = 0; idx < max; ++idx) {
      simple_archiver_priority_heap_insert(priority_heap, array[idx],
                                           array + idx, no_free_fn);
    }

    for (uint32_t idx = max; idx-- > 0;) {
      uint32_t *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %" PRIu32 ", data is %" PRIu32 "\n", idx, *data);
      }
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %" PRIu32 ", data is %" PRIu32 "\n", idx, *data);
      }
    }
    free(array);

    simple_archiver_priority_heap_free(&priority_heap);

    priority_heap = simple_archiver_priority_heap_init();

    {
      const uint32_t size = 15;
      char elems[15];

      for (uint32_t idx = 0; idx < size; ++idx) {
        uint32_t *data = malloc(sizeof(uint32_t));
        *data = idx;
        simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
        elems[idx] = 0;
      }
      printf("Begin iteration of 15 elements:\n");

      void **ptrs = malloc(sizeof(void*) * 2);
      ptrs[0] = elems;
      ptrs[1] = (void*)&size;
      simple_archiver_priority_heap_iter(priority_heap,
                                         test_iter_fn_priority_heap,
                                         ptrs);
      free(ptrs);

      for (uint32_t idx = 0; idx < size; ++idx) {
        CHECK_TRUE(elems[idx]);
      }
    }
    simple_archiver_priority_heap_free(&priority_heap);
  }

  printf("Checks checked: %" PRId32 "\n", checks_checked);
  printf("Checks passed:  %" PRId32 "\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
