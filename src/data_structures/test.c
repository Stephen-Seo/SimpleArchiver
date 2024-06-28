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

#include "hash_map.h"
#include "linked_list.h"

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

  printf("Checks checked: %u\n", checks_checked);
  printf("Checks passed:  %u\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
