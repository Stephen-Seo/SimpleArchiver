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
 * `test.c` is the source for testing code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser_internal.h"

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

int main(void) {
  // Test parser.
  {
    unsigned int idx = simple_archiver_parser_internal_filename_idx("test");
    CHECK_TRUE(idx == 0);

    idx = simple_archiver_parser_internal_filename_idx("./test");
    CHECK_TRUE(idx == 2);

    idx = simple_archiver_parser_internal_filename_idx("././test");
    CHECK_TRUE(idx == 4);

    idx = simple_archiver_parser_internal_filename_idx("././//././//./test");
    CHECK_TRUE(idx == 14);

    idx = simple_archiver_parser_internal_filename_idx("/././//././//./test");
    CHECK_TRUE(idx == 0);

    idx = simple_archiver_parser_internal_filename_idx(".derp/.//././//./test");
    CHECK_TRUE(idx == 0);

    idx = simple_archiver_parser_internal_filename_idx("././/.derp/.///./test");
    CHECK_TRUE(idx == 5);

    idx = simple_archiver_parser_internal_filename_idx("././/.//.//./");
    CHECK_TRUE(idx == 11);

    idx = simple_archiver_parser_internal_filename_idx("././/.//.//.");
    CHECK_TRUE(idx == 11);

    idx = simple_archiver_parser_internal_filename_idx("././/.//.//");
    CHECK_TRUE(idx == 8);

    SDArchiverParsed parsed = simple_archiver_create_parsed();
    simple_archiver_parse_args(
        4,
        (const char *[]){"parser", "--", "././/././//./derp", "./doop", NULL},
        &parsed);

    CHECK_TRUE(strcmp("derp", parsed.working_files[0]) == 0);
    CHECK_TRUE(strcmp("doop", parsed.working_files[1]) == 0);
    CHECK_TRUE(parsed.working_files[2] == NULL);
    CHECK_TRUE(parsed.filename == NULL);
    CHECK_TRUE(parsed.flags == 0);

    simple_archiver_free_parsed(&parsed);

    parsed = simple_archiver_create_parsed();
    simple_archiver_parse_args(
        7,
        (const char *[]){"parser", "-x", "-f", "the_filename",
                         "././/././//./.derp", "././//./_doop",
                         "./../../.prev_dir_file", NULL},
        &parsed);

    CHECK_TRUE(strcmp(".derp", parsed.working_files[0]) == 0);
    CHECK_TRUE(strcmp("_doop", parsed.working_files[1]) == 0);
    CHECK_TRUE(strcmp("../../.prev_dir_file", parsed.working_files[2]) == 0);
    CHECK_TRUE(parsed.working_files[3] == NULL);
    CHECK_TRUE(strcmp("the_filename", parsed.filename) == 0);
    CHECK_TRUE(parsed.flags == 1);

    simple_archiver_free_parsed(&parsed);
  }

  printf("Checks checked: %u\n", checks_checked);
  printf("Checks passed:  %u\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
