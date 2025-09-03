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
#include "chunked_array.h"
#include "list_array.h"
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
    //printf("Got data %" PRIu32 "\n", *uint_data);
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

typedef struct TestStruct {
  int *first;
  int *second;
} TestStruct;

void cleanup_test_struct_fn(void *ptr) {
  TestStruct *t = ptr;
  if (t->first) {
    free(t->first);
  }
  if (t->second) {
    free(t->second);
  }
}

void *internal_pheap_clone_uint32_t(void *data) {
  uint32_t *data_u32 = data;
  uint32_t *u32 = malloc(4);

  *u32 = *data_u32;

  return u32;
}

int internal_pheap_less_generic(void *left, void *right) {
  return *((int*)left) < *((int*)right);
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

  // Test ChunkedArray.
  {
    // Test cleanup immediately after init.
    SDArchiverChunkedArr chunked_array =
      simple_archiver_chunked_array_init(no_free_fn,
                                         sizeof(int));

    simple_archiver_chunked_array_cleanup(&chunked_array);

    // Test cleanup after pushing some values.
    chunked_array =
      simple_archiver_chunked_array_init(no_free_fn,
                                         sizeof(int));

    int value = 1;
    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

    value = 20;
    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

    value = 300;
    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

    int *int_ptr = simple_archiver_chunked_array_at(&chunked_array, 0);
    CHECK_TRUE(int_ptr);
    CHECK_TRUE(*int_ptr == 1);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 1);
    CHECK_TRUE(int_ptr);
    CHECK_TRUE(*int_ptr == 20);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 2);
    CHECK_TRUE(int_ptr);
    CHECK_TRUE(*int_ptr == 300);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 3);
    CHECK_FALSE(int_ptr);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 4);
    CHECK_FALSE(int_ptr);

    simple_archiver_chunked_array_cleanup(&chunked_array);

    // Same test but with NULL free/cleanup function.
    chunked_array = simple_archiver_chunked_array_init(0, sizeof(int));

    value = 1;
    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

    value = 20;
    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

    value = 300;
    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 0);
    CHECK_TRUE(int_ptr);
    CHECK_TRUE(*int_ptr == 1);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 1);
    CHECK_TRUE(int_ptr);
    CHECK_TRUE(*int_ptr == 20);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 2);
    CHECK_TRUE(int_ptr);
    CHECK_TRUE(*int_ptr == 300);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 3);
    CHECK_FALSE(int_ptr);

    int_ptr = simple_archiver_chunked_array_at(&chunked_array, 4);
    CHECK_FALSE(int_ptr);

    simple_archiver_chunked_array_cleanup(&chunked_array);

    // Test arbitrary data.
    chunked_array =
      simple_archiver_chunked_array_init(cleanup_test_struct_fn,
                                         sizeof(TestStruct));
    TestStruct t = (TestStruct){.first=malloc(sizeof(int)),
                                .second=malloc(sizeof(int))};
    *t.first = 100;
    *t.second = 200;

    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &t) == 0);

    t.first = malloc(sizeof(int));
    *t.first = 3000;

    t.second = malloc(sizeof(int));
    *t.second = 4444;

    CHECK_TRUE(simple_archiver_chunked_array_push(&chunked_array, &t) == 0);

    TestStruct *t_ptr = simple_archiver_chunked_array_at(&chunked_array, 0);

    CHECK_TRUE(t_ptr);
    CHECK_TRUE(*t_ptr->first == 100);
    CHECK_TRUE(*t_ptr->second == 200);

    t_ptr = simple_archiver_chunked_array_at(&chunked_array, 1);

    CHECK_TRUE(t_ptr);
    CHECK_TRUE(*t_ptr->first == 3000);
    CHECK_TRUE(*t_ptr->second == 4444);

    CHECK_FALSE(simple_archiver_chunked_array_at(&chunked_array, 2));
    CHECK_FALSE(simple_archiver_chunked_array_at(&chunked_array, 3));
    CHECK_FALSE(simple_archiver_chunked_array_at(&chunked_array, 3090));

    // Check clearn, push, and pop with arbitrary data.
    simple_archiver_chunked_array_clear(&chunked_array);

    t_ptr = simple_archiver_chunked_array_top(&chunked_array);
    CHECK_FALSE(t_ptr);
    t_ptr = simple_archiver_chunked_array_bottom(&chunked_array);
    CHECK_FALSE(t_ptr);

    for (int idx = 0; idx < 100; ++idx) {
      t = (TestStruct){.first=malloc(sizeof(int)), .second=malloc(sizeof(int))};
      *t.first = idx;
      *t.second = idx * 1000;
      simple_archiver_chunked_array_push(&chunked_array, &t);
      t_ptr = simple_archiver_chunked_array_top(&chunked_array);
      CHECK_TRUE(t_ptr);
      if (t_ptr) {
        CHECK_TRUE(*t_ptr->first == idx);
        CHECK_TRUE(*t_ptr->second == idx * 1000);
      }
      t_ptr = simple_archiver_chunked_array_bottom(&chunked_array);
      CHECK_TRUE(t_ptr);
      if (t_ptr) {
        CHECK_TRUE(*t_ptr->first == 0);
        CHECK_TRUE(*t_ptr->second == 0);
      }
    }

    // Check size.
    CHECK_TRUE(simple_archiver_chunked_array_size(&chunked_array) == 100);

    for (int idx = 100; idx-- > 0;) {
      t_ptr = simple_archiver_chunked_array_top(&chunked_array);
      CHECK_TRUE(t_ptr);
      if (t_ptr) {
        CHECK_TRUE(*t_ptr->first == idx);
        CHECK_TRUE(*t_ptr->second == idx * 1000);
      }
      t_ptr = simple_archiver_chunked_array_bottom(&chunked_array);
      CHECK_TRUE(t_ptr);
      if (t_ptr) {
        CHECK_TRUE(*t_ptr->first == 0);
        CHECK_TRUE(*t_ptr->second == 0);
      }

      if (idx > 50) {
        CHECK_TRUE(
          simple_archiver_chunked_array_pop_no_ret(&chunked_array) != 0);
      } else if (idx > 70) {
        t_ptr = simple_archiver_chunked_array_pop(&chunked_array, 0);
        CHECK_TRUE(t_ptr);
        if (t_ptr) {
          free(t_ptr);
        }
      } else {
        t_ptr = simple_archiver_chunked_array_pop(&chunked_array, 1);
        CHECK_TRUE(t_ptr);
        if (t_ptr) {
          cleanup_test_struct_fn(t_ptr);
          free(t_ptr);
        }
      }
    }

    t_ptr = simple_archiver_chunked_array_top(&chunked_array);
    CHECK_FALSE(t_ptr);
    t_ptr = simple_archiver_chunked_array_bottom(&chunked_array);
    CHECK_FALSE(t_ptr);

    // Check size.
    CHECK_TRUE(simple_archiver_chunked_array_size(&chunked_array) == 0);

    simple_archiver_chunked_array_cleanup(&chunked_array);

    // Test push more than 32 elements.
    chunked_array =
      simple_archiver_chunked_array_init(no_free_fn,
                                         sizeof(int));

    const int *const_int_ptr =
      simple_archiver_chunked_array_top_const(&chunked_array);
    CHECK_FALSE(const_int_ptr);
    const_int_ptr = simple_archiver_chunked_array_bottom_const(&chunked_array);
    CHECK_FALSE(const_int_ptr);

    for (int idx = 0; idx < 100; ++idx) {
      value = idx;
      CHECK_TRUE(
        simple_archiver_chunked_array_push(&chunked_array, &value) == 0);

      const_int_ptr = simple_archiver_chunked_array_top_const(&chunked_array);
      CHECK_TRUE(const_int_ptr);
      if (const_int_ptr) {
        CHECK_TRUE(*const_int_ptr == idx);
      }
      const_int_ptr =
        simple_archiver_chunked_array_bottom_const(&chunked_array);
      CHECK_TRUE(const_int_ptr);
      if (const_int_ptr) {
        CHECK_TRUE(*const_int_ptr == 0);
      }
    }

    for (int idx = 0; idx < 110; ++idx) {
      int_ptr = simple_archiver_chunked_array_at(&chunked_array, idx);
      if (idx < 100) {
        CHECK_TRUE(int_ptr);
        CHECK_TRUE(*int_ptr == idx);
      } else {
        CHECK_FALSE(int_ptr);
      }
    }

    for (int idx = 100; idx-- > 0;) {
      const_int_ptr = simple_archiver_chunked_array_top_const(&chunked_array);
      CHECK_TRUE(const_int_ptr);
      if (const_int_ptr) {
        CHECK_TRUE(*const_int_ptr == idx);
      }
      const_int_ptr =
        simple_archiver_chunked_array_bottom_const(&chunked_array);
      CHECK_TRUE(const_int_ptr);
      if (const_int_ptr) {
        CHECK_TRUE(*const_int_ptr == 0);
      }

      int_ptr = simple_archiver_chunked_array_pop(&chunked_array, 0);
      CHECK_TRUE(int_ptr);
      CHECK_TRUE(*int_ptr == idx);
      free(int_ptr);
    }

    for (int idx = 0; idx < 10; ++idx) {
      int_ptr = simple_archiver_chunked_array_pop(&chunked_array, 0);
      CHECK_FALSE(int_ptr);
    }

    const_int_ptr =
      simple_archiver_chunked_array_top_const(&chunked_array);
    CHECK_FALSE(const_int_ptr);
    const_int_ptr = simple_archiver_chunked_array_bottom_const(&chunked_array);
    CHECK_FALSE(const_int_ptr);

    simple_archiver_chunked_array_cleanup(&chunked_array);

    // Repeat test but use "clear" at end.
    chunked_array =
      simple_archiver_chunked_array_init(no_free_fn,
                                         sizeof(int));

    for (int idx = 0; idx < 100; ++idx) {
      value = idx;
      CHECK_TRUE(
        simple_archiver_chunked_array_push(&chunked_array, &value) == 0);
    }

    for (int idx = 0; idx < 110; ++idx) {
      int_ptr = simple_archiver_chunked_array_at(&chunked_array, idx);
      if (idx < 100) {
        CHECK_TRUE(int_ptr);
        CHECK_TRUE(*int_ptr == idx);
      } else {
        CHECK_FALSE(int_ptr);
      }
    }

    simple_archiver_chunked_array_clear(&chunked_array);
    simple_archiver_chunked_array_cleanup(&chunked_array);
  }

  // Test ListArray
  {
    SDArchiverListArr la =
      simple_archiver_list_array_init(cleanup_test_struct_fn,
                                      sizeof(TestStruct));
    simple_archiver_list_array_cleanup(&la);

    // Test cleanup after pushing some values.
    la = simple_archiver_list_array_init(cleanup_test_struct_fn,
                                         sizeof(TestStruct));

    TestStruct test_struct;
    TestStruct *test_struct_ptr;
    const TestStruct *ctest_struct_ptr;

    test_struct_ptr = simple_archiver_list_array_top(&la);
    CHECK_FALSE(test_struct_ptr);
    test_struct_ptr = simple_archiver_list_array_bottom(&la);
    CHECK_FALSE(test_struct_ptr);

    ctest_struct_ptr = simple_archiver_list_array_top_const(&la);
    CHECK_FALSE(ctest_struct_ptr);
    ctest_struct_ptr = simple_archiver_list_array_bottom_const(&la);
    CHECK_FALSE(ctest_struct_ptr);

    for (int idx = 0; idx < 128; ++idx) {
      test_struct.first = malloc(sizeof(int));
      *test_struct.first = 1 + idx * 2;
      test_struct.second = malloc(sizeof(int));
      *test_struct.second = 2 + idx * 2;
      CHECK_TRUE(simple_archiver_list_array_push(&la, &test_struct) == 0);
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx + 1);

      test_struct_ptr = simple_archiver_list_array_top(&la);
      CHECK_TRUE(test_struct_ptr);
      if (test_struct_ptr) {
        CHECK_TRUE(*test_struct_ptr->first == 1 + idx * 2);
        CHECK_TRUE(*test_struct_ptr->second == 2 + idx * 2);
      }
      test_struct_ptr = simple_archiver_list_array_bottom(&la);
      CHECK_TRUE(test_struct_ptr);
      if (test_struct_ptr) {
        CHECK_TRUE(*test_struct_ptr->first == 1);
        CHECK_TRUE(*test_struct_ptr->second == 2);
      }

      ctest_struct_ptr = simple_archiver_list_array_top_const(&la);
      CHECK_TRUE(ctest_struct_ptr);
      if (ctest_struct_ptr) {
        CHECK_TRUE(*ctest_struct_ptr->first == 1 + idx * 2);
        CHECK_TRUE(*ctest_struct_ptr->second == 2 + idx * 2);
      }
      ctest_struct_ptr = simple_archiver_list_array_bottom_const(&la);
      CHECK_TRUE(ctest_struct_ptr);
      if (ctest_struct_ptr) {
        CHECK_TRUE(*ctest_struct_ptr->first == 1);
        CHECK_TRUE(*ctest_struct_ptr->second == 2);
      }
    }
    for (int idx = 0; idx < 128; ++idx) {
      test_struct_ptr = simple_archiver_list_array_at(&la, idx);
      CHECK_TRUE(*test_struct_ptr->first == 1 + idx * 2);
      CHECK_TRUE(*test_struct_ptr->second == 2 + idx * 2);
    }
    simple_archiver_list_array_cleanup(&la);

    // Test cleanup after pushing integers.
    la = simple_archiver_list_array_init(no_free_fn, sizeof(int));

    for (int idx = 0; idx < 128; ++idx) {
      CHECK_TRUE(simple_archiver_list_array_push(&la, &idx) == 0);
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx + 1);
    }
    for (int idx = 0; idx < 128; ++idx) {
      const int *int_ptr = simple_archiver_list_array_at_const(&la, idx);
      CHECK_TRUE(*int_ptr == idx);
    }

    simple_archiver_list_array_cleanup(&la);

    // Test cleanup after pushing integers with NULL free fn.
    la = simple_archiver_list_array_init(0, sizeof(int));

    for (int idx = 0; idx < 128; ++idx) {
      CHECK_TRUE(simple_archiver_list_array_push(&la, &idx) == 0);
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx + 1);
    }
    for (int idx = 0; idx < 128; ++idx) {
      const int *int_ptr = simple_archiver_list_array_at_const(&la, idx);
      CHECK_TRUE(*int_ptr == idx);
    }

    simple_archiver_list_array_cleanup(&la);

    // Test pop.
    la = simple_archiver_list_array_init(cleanup_test_struct_fn,
                                         sizeof(TestStruct));

    for (int idx = 0; idx < 128; ++idx) {
      test_struct.first = malloc(sizeof(int));
      *test_struct.first = 1 + idx * 2;
      test_struct.second = malloc(sizeof(int));
      *test_struct.second = 2 + idx * 2;
      CHECK_TRUE(simple_archiver_list_array_push(&la, &test_struct) == 0);
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx + 1);
    }

    for (int idx = 128; idx-- > 0;) {
      test_struct_ptr = simple_archiver_list_array_top(&la);
      CHECK_TRUE(test_struct_ptr);
      if (test_struct_ptr) {
        CHECK_TRUE(*test_struct_ptr->first == 1 + idx * 2);
        CHECK_TRUE(*test_struct_ptr->second == 2 + idx * 2);
      }
      test_struct_ptr = simple_archiver_list_array_bottom(&la);
      CHECK_TRUE(test_struct_ptr);
      if (test_struct_ptr) {
        CHECK_TRUE(*test_struct_ptr->first == 1);
        CHECK_TRUE(*test_struct_ptr->second == 2);
      }

      ctest_struct_ptr = simple_archiver_list_array_top_const(&la);
      CHECK_TRUE(ctest_struct_ptr);
      if (ctest_struct_ptr) {
        CHECK_TRUE(*ctest_struct_ptr->first == 1 + idx * 2);
        CHECK_TRUE(*ctest_struct_ptr->second == 2 + idx * 2);
      }
      ctest_struct_ptr = simple_archiver_list_array_bottom_const(&la);
      CHECK_TRUE(ctest_struct_ptr);
      if (ctest_struct_ptr) {
        CHECK_TRUE(*ctest_struct_ptr->first == 1);
        CHECK_TRUE(*ctest_struct_ptr->second == 2);
      }

      if (idx % 3 == 0) {
        test_struct_ptr = simple_archiver_list_array_pop(&la, 1);
        CHECK_TRUE(test_struct_ptr);
        if (test_struct_ptr) {
          CHECK_TRUE(*test_struct_ptr->first == 1 + idx * 2);
          CHECK_TRUE(*test_struct_ptr->second == 2 + idx * 2);
          cleanup_test_struct_fn(test_struct_ptr);
          free(test_struct_ptr);
        }
      } else if (idx % 3 == 1) {
        test_struct_ptr = simple_archiver_list_array_pop(&la, 0);
        CHECK_TRUE(test_struct_ptr);
        if (test_struct_ptr) {
          free(test_struct_ptr);
        }
      } else {
        CHECK_TRUE(simple_archiver_list_array_pop_no_ret(&la) != 0);
      }
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx);
    }

    test_struct_ptr = simple_archiver_list_array_top(&la);
    CHECK_FALSE(test_struct_ptr);
    test_struct_ptr = simple_archiver_list_array_bottom(&la);
    CHECK_FALSE(test_struct_ptr);

    ctest_struct_ptr = simple_archiver_list_array_top_const(&la);
    CHECK_FALSE(ctest_struct_ptr);
    ctest_struct_ptr = simple_archiver_list_array_bottom_const(&la);
    CHECK_FALSE(ctest_struct_ptr);

    simple_archiver_list_array_cleanup(&la);

    // Test list array clear.
    la = simple_archiver_list_array_init(cleanup_test_struct_fn,
                                         sizeof(TestStruct));

    for (int idx = 0; idx < 128; ++idx) {
      test_struct.first = malloc(sizeof(int));
      *test_struct.first = 1 + idx * 2;
      test_struct.second = malloc(sizeof(int));
      *test_struct.second = 2 + idx * 2;
      CHECK_TRUE(simple_archiver_list_array_push(&la, &test_struct) == 0);
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx + 1);
    }

    simple_archiver_list_array_clear(&la);
    CHECK_TRUE(simple_archiver_list_array_size(&la) == 0);

    for (int idx = 0; idx < 128; ++idx) {
      test_struct.first = malloc(sizeof(int));
      *test_struct.first = 1 + idx * 2;
      test_struct.second = malloc(sizeof(int));
      *test_struct.second = 2 + idx * 2;
      CHECK_TRUE(simple_archiver_list_array_push(&la, &test_struct) == 0);
      CHECK_TRUE(simple_archiver_list_array_size(&la) == (uint64_t)idx + 1);
    }

    simple_archiver_list_array_cleanup(&la);
  }

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
      //printf("Begin iteration of 15 elements:\n");

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

    // Test "shallow clone".
    priority_heap = simple_archiver_priority_heap_init();

    for (uint32_t idx = 0; idx < 50; ++idx) {
      uint32_t *data = malloc(4);
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, idx, data, NULL);
    }

    {
      // Create "shallow clone"
      __attribute__((cleanup(simple_archiver_priority_heap_free)))
      SDArchiverPHeap *shallow_clone =
        simple_archiver_priority_heap_clone(priority_heap, NULL);
    }

    {
      // Create "shallow clone" and pop its contents.
      __attribute__((cleanup(simple_archiver_priority_heap_free)))
      SDArchiverPHeap *shallow_clone =
        simple_archiver_priority_heap_clone(priority_heap, NULL);

      uint32_t idx = 0;
      while (simple_archiver_priority_heap_size(shallow_clone) != 0) {
        uint32_t *data = simple_archiver_priority_heap_pop(shallow_clone);
        CHECK_TRUE(*data == idx++);
      }
    }

    {
      // Create proper clone of pheap.
      __attribute__((cleanup(simple_archiver_priority_heap_free)))
      SDArchiverPHeap *pheap_clone =
        simple_archiver_priority_heap_clone(priority_heap,
                                            internal_pheap_clone_uint32_t);
    }

    {
      // Create proper clone of pheap and pop its contents.
      __attribute__((cleanup(simple_archiver_priority_heap_free)))
      SDArchiverPHeap *pheap_clone =
        simple_archiver_priority_heap_clone(priority_heap,
                                            internal_pheap_clone_uint32_t);
      uint32_t idx = 0;
      while (simple_archiver_priority_heap_size(pheap_clone) != 0) {
        uint32_t *data = simple_archiver_priority_heap_pop(pheap_clone);
        CHECK_TRUE(*data == idx++);
        free(data);
      }
    }

    simple_archiver_priority_heap_free(&priority_heap);

    priority_heap = simple_archiver_priority_heap_init_less_generic_fn(internal_pheap_less_generic);

    for (int idx = 0; idx < 10; idx += 2) {
      int *data = malloc(sizeof(int));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, 0, data, NULL);
    }

    for (int idx = 1; idx < 10; idx += 2) {
      int *data = malloc(sizeof(int));
      *data = idx;
      simple_archiver_priority_heap_insert(priority_heap, 0, data, NULL);
    }

    for (int idx = 0; idx < 10; ++idx) {
      int *data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(data);
      if (data) {
        CHECK_TRUE(*data == idx);
        free(data);
      }
    }

    simple_archiver_priority_heap_free(&priority_heap);
  }

  printf("Checks checked: %" PRId32 "\n", checks_checked);
  printf("Checks passed:  %" PRId32 "\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
