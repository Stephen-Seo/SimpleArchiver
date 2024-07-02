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

#include "helpers.h"
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

  // Test helpers.
  {
    // Only if system is little-endian.
    if (simple_archiver_helper_is_big_endian() == 0) {
      unsigned short u16 = 0x0102;
      CHECK_TRUE(((unsigned char *)&u16)[0] == 2);
      CHECK_TRUE(((unsigned char *)&u16)[1] == 1);
      simple_archiver_helper_16_bit_be(&u16);
      CHECK_TRUE(((unsigned char *)&u16)[0] == 1);
      CHECK_TRUE(((unsigned char *)&u16)[1] == 2);
      simple_archiver_helper_16_bit_be(&u16);
      CHECK_TRUE(((unsigned char *)&u16)[0] == 2);
      CHECK_TRUE(((unsigned char *)&u16)[1] == 1);

      unsigned int u32 = 0x01020304;
      CHECK_TRUE(((unsigned char *)&u32)[0] == 4);
      CHECK_TRUE(((unsigned char *)&u32)[1] == 3);
      CHECK_TRUE(((unsigned char *)&u32)[2] == 2);
      CHECK_TRUE(((unsigned char *)&u32)[3] == 1);
      simple_archiver_helper_32_bit_be(&u32);
      CHECK_TRUE(((unsigned char *)&u32)[0] == 1);
      CHECK_TRUE(((unsigned char *)&u32)[1] == 2);
      CHECK_TRUE(((unsigned char *)&u32)[2] == 3);
      CHECK_TRUE(((unsigned char *)&u32)[3] == 4);
      simple_archiver_helper_32_bit_be(&u32);
      CHECK_TRUE(((unsigned char *)&u32)[0] == 4);
      CHECK_TRUE(((unsigned char *)&u32)[1] == 3);
      CHECK_TRUE(((unsigned char *)&u32)[2] == 2);
      CHECK_TRUE(((unsigned char *)&u32)[3] == 1);

      unsigned long long u64 = 0x010203040a0b0c0d;
      CHECK_TRUE(((unsigned char *)&u64)[0] == 0xd);
      CHECK_TRUE(((unsigned char *)&u64)[1] == 0xc);
      CHECK_TRUE(((unsigned char *)&u64)[2] == 0xb);
      CHECK_TRUE(((unsigned char *)&u64)[3] == 0xa);
      CHECK_TRUE(((unsigned char *)&u64)[4] == 0x4);
      CHECK_TRUE(((unsigned char *)&u64)[5] == 0x3);
      CHECK_TRUE(((unsigned char *)&u64)[6] == 0x2);
      CHECK_TRUE(((unsigned char *)&u64)[7] == 0x1);
      simple_archiver_helper_64_bit_be(&u64);
      CHECK_TRUE(((unsigned char *)&u64)[0] == 0x1);
      CHECK_TRUE(((unsigned char *)&u64)[1] == 0x2);
      CHECK_TRUE(((unsigned char *)&u64)[2] == 0x3);
      CHECK_TRUE(((unsigned char *)&u64)[3] == 0x4);
      CHECK_TRUE(((unsigned char *)&u64)[4] == 0xa);
      CHECK_TRUE(((unsigned char *)&u64)[5] == 0xb);
      CHECK_TRUE(((unsigned char *)&u64)[6] == 0xc);
      CHECK_TRUE(((unsigned char *)&u64)[7] == 0xd);
      simple_archiver_helper_64_bit_be(&u64);
      CHECK_TRUE(((unsigned char *)&u64)[0] == 0xd);
      CHECK_TRUE(((unsigned char *)&u64)[1] == 0xc);
      CHECK_TRUE(((unsigned char *)&u64)[2] == 0xb);
      CHECK_TRUE(((unsigned char *)&u64)[3] == 0xa);
      CHECK_TRUE(((unsigned char *)&u64)[4] == 0x4);
      CHECK_TRUE(((unsigned char *)&u64)[5] == 0x3);
      CHECK_TRUE(((unsigned char *)&u64)[6] == 0x2);
      CHECK_TRUE(((unsigned char *)&u64)[7] == 0x1);
    }
  }

  printf("Checks checked: %u\n", checks_checked);
  printf("Checks passed:  %u\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
