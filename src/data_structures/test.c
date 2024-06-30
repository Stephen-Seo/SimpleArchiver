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
 * `data_structures/test.c` is the source for testing data structure code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../algorithms/linear_congruential_gen.h"
#include "hash_map.h"
#include "linked_list.h"
#include "priority_heap.h"

static int checks_checked = 0;
static int checks_passed = 0;

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

int more_fn(long long a, long long b) { return a > b ? 1 : 0; }

int main(void) {
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
      int *value, *key;

      for (unsigned int idx = 0; idx < 20; ++idx) {
        value = malloc(sizeof(int));
        key = malloc(sizeof(int));
        *value = idx;
        *key = idx;
        simple_archiver_hash_map_insert(&hash_map, value, key, sizeof(int),
                                        NULL, NULL);
      }
    }

    int value, key;
    void *value_ptr;

    for (value = 0, key = 0; value < 20 && key < 20; ++value, ++key) {
      value_ptr = simple_archiver_hash_map_get(hash_map, &key, sizeof(int));
      CHECK_TRUE(value_ptr != NULL);
      CHECK_TRUE(memcmp(value_ptr, &value, sizeof(int)) == 0);
    }

    key = 5;
    simple_archiver_hash_map_remove(hash_map, &key, sizeof(int));
    key = 15;
    simple_archiver_hash_map_remove(hash_map, &key, sizeof(int));

    for (value = 0, key = 0; value < 20 && key < 20; ++value, ++key) {
      value_ptr = simple_archiver_hash_map_get(hash_map, &key, sizeof(int));
      if (key != 5 && key != 15) {
        CHECK_TRUE(value_ptr != NULL);
        CHECK_TRUE(memcmp(value_ptr, &value, sizeof(int)) == 0);
      } else {
        CHECK_TRUE(value_ptr == NULL);
      }
    }

    simple_archiver_hash_map_free(&hash_map);
  }

  // Test PriorityHeap.
  {
    SDArchiverPHeap *priority_heap = simple_archiver_priority_heap_init();
    simple_archiver_priority_heap_free(&priority_heap);

    priority_heap = simple_archiver_priority_heap_init();

    // Just 3 elements.
    for (unsigned int idx = 0; idx < 3; ++idx) {
      unsigned int *data = malloc(sizeof(unsigned int));
      *data = idx;
      simple_archiver_priority_heap_insert(&priority_heap, idx, data, NULL);
    }
    for (unsigned int idx = 0; idx < 3; ++idx) {
      unsigned int *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %u, data is %u\n", idx, *data);
      }
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %u, data is %u\n", idx, *data);
      }
      free(data);
    }

    // 100 elements.
    unsigned int max = 100;

    for (unsigned int idx = 0; idx < max; ++idx) {
      unsigned int *data = malloc(sizeof(unsigned int));
      *data = idx;
      simple_archiver_priority_heap_insert(&priority_heap, idx, data, NULL);
    }

    for (unsigned int idx = 0; idx < max; ++idx) {
      unsigned int *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      free(data);
    }

    // Insert in reverse order.
    for (unsigned int idx = max; idx-- > 0;) {
      unsigned int *data = malloc(sizeof(unsigned int));
      *data = idx;
      simple_archiver_priority_heap_insert(&priority_heap, idx, data, NULL);
    }

    for (unsigned int idx = 0; idx < max; ++idx) {
      unsigned int *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      free(data);
    }

    // Insert in random order.
    unsigned int *array = malloc(sizeof(unsigned int) * max);
    for (unsigned int idx = 0; idx < max; ++idx) {
      array[idx] = idx;
    }

    // Deterministic randomization.
    for (unsigned int idx = max - 1; idx-- > 0;) {
      unsigned int other_idx = simple_archiver_algo_lcg_defaults(idx) %
                               (unsigned long long)(idx + 1);
      if (max - 1 != other_idx) {
        unsigned int temp = array[max - 1];
        array[max - 1] = array[other_idx];
        array[other_idx] = temp;
      }
    }

    // Insert the deterministically randomized array.
    for (unsigned int idx = 0; idx < max; ++idx) {
      simple_archiver_priority_heap_insert(&priority_heap, array[idx],
                                           array + idx, no_free_fn);
    }

    for (unsigned int idx = 0; idx < max; ++idx) {
      unsigned int *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %u, data is %u\n", idx, *data);
      }
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %u, data is %u\n", idx, *data);
      }
    }
    free(array);

    simple_archiver_priority_heap_free(&priority_heap);

    // Insert, don't pop, do free, for memcheck.
    priority_heap = simple_archiver_priority_heap_init();
    for (unsigned int idx = 0; idx < max; ++idx) {
      unsigned int *data = malloc(sizeof(unsigned int));
      *data = idx;
      simple_archiver_priority_heap_insert(&priority_heap, idx, data, NULL);
    }
    simple_archiver_priority_heap_free(&priority_heap);

    // Reverse priority.
    priority_heap = simple_archiver_priority_heap_init_less_fn(more_fn);

    for (unsigned int idx = 0; idx < max; ++idx) {
      unsigned int *data = malloc(sizeof(unsigned int));
      *data = idx;
      simple_archiver_priority_heap_insert(&priority_heap, idx, data, NULL);
    }

    for (unsigned int idx = max; idx-- > 0;) {
      unsigned int *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      free(data);
    }

    simple_archiver_priority_heap_free(&priority_heap);

    // Insert in random order with reverse-priority-heap.
    priority_heap = simple_archiver_priority_heap_init_less_fn(more_fn);
    array = malloc(sizeof(unsigned int) * max);
    for (unsigned int idx = 0; idx < max; ++idx) {
      array[idx] = idx;
    }

    // Deterministic randomization.
    for (unsigned int idx = max - 1; idx-- > 0;) {
      unsigned int other_idx = simple_archiver_algo_lcg_defaults(idx) %
                               (unsigned long long)(idx + 1);
      if (max - 1 != other_idx) {
        unsigned int temp = array[max - 1];
        array[max - 1] = array[other_idx];
        array[other_idx] = temp;
      }
    }

    // Insert the deterministically randomized array.
    for (unsigned int idx = 0; idx < max; ++idx) {
      simple_archiver_priority_heap_insert(&priority_heap, array[idx],
                                           array + idx, no_free_fn);
    }

    for (unsigned int idx = max; idx-- > 0;) {
      unsigned int *data = simple_archiver_priority_heap_top(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %u, data is %u\n", idx, *data);
      }
      data = simple_archiver_priority_heap_pop(priority_heap);
      CHECK_TRUE(*data == idx);
      if (*data != idx) {
        printf("idx is %u, data is %u\n", idx, *data);
      }
    }
    free(array);

    simple_archiver_priority_heap_free(&priority_heap);
  }

  printf("Checks checked: %u\n", checks_checked);
  printf("Checks passed:  %u\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
