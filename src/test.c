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
// `test.c` is the source for testing code.

// Standard library includes.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Local includes.
#include "archiver.h"
#include "data_structures/hash_map.h"
#include "helpers.h"
#include "parser.h"
#include "parser_internal.h"

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
#define CHECK_STREQ(a, b)                                                    \
  do {                                                                       \
    ++checks_checked;                                                        \
    if (strcmp((a), (b)) == 0) {                                             \
      ++checks_passed;                                                       \
    } else {                                                                 \
      printf("CHECK_STREQ at line %u failed: %s != %s\n", __LINE__, #a, #b); \
    }                                                                        \
  } while (0);

int main(void) {
  puts("Begin unit test.");
  fflush(stdout);

  // Test parser.
  {
    size_t idx =
        simple_archiver_parser_internal_get_first_non_current_idx("test");
    CHECK_TRUE(idx == 0);

    idx = simple_archiver_parser_internal_get_first_non_current_idx("./test");
    CHECK_TRUE(idx == 2);

    idx = simple_archiver_parser_internal_get_first_non_current_idx("././test");
    CHECK_TRUE(idx == 4);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        "././//././//./test");
    CHECK_TRUE(idx == 14);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        "/././//././//./test");
    CHECK_TRUE(idx == 0);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        ".derp/.//././//./test");
    CHECK_TRUE(idx == 0);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        "././/.derp/.///./test");
    CHECK_TRUE(idx == 5);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        "././/.//.//./");
    CHECK_TRUE(idx == 11);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        "././/.//.//.");
    CHECK_TRUE(idx == 11);

    idx = simple_archiver_parser_internal_get_first_non_current_idx(
        "././/.//.//");
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
    CHECK_TRUE(parsed.flags == 0x40);

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
    CHECK_TRUE(parsed.flags == 0x41);

    simple_archiver_free_parsed(&parsed);

    // Test mappings.
    parsed = simple_archiver_create_parsed();
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1000:1001",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) == 0);
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1002:user0",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) == 0);
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "user1:1003",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) == 0);
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "user2:user3",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) == 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1000:1091",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1000:other",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1002:user00",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1002:100",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "user1:1033",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "user1:user10",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "user2:us3r3",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    fprintf(stderr, "Expecting ERROR output on next line:\n");
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "user2:3",
      parsed.mappings.UidToUname,
      parsed.mappings.UnameToUid,
      parsed.mappings.UidToUid,
      parsed.mappings.UnameToUname) != 0);
    uint32_t id_check = 1000;
    uint32_t *id_get;

    id_get = simple_archiver_hash_map_get(parsed.mappings.UidToUid,
                                          &id_check,
                                          sizeof(uint32_t));
    CHECK_TRUE(id_get);
    CHECK_TRUE(*id_get == 1001);
    id_check = 1001;
    id_get = simple_archiver_hash_map_get(parsed.mappings.UidToUid,
                                          &id_check,
                                          sizeof(uint32_t));
    CHECK_FALSE(id_get);

    id_check = 1002;
    char *name_get;

    name_get = simple_archiver_hash_map_get(parsed.mappings.UidToUname,
                                            &id_check,
                                            sizeof(uint32_t));
    CHECK_TRUE(name_get);
    CHECK_STREQ(name_get, "user0");
    id_check = 1010;
    name_get = simple_archiver_hash_map_get(parsed.mappings.UidToUname,
                                            &id_check,
                                            sizeof(uint32_t));
    CHECK_FALSE(name_get);

    id_get = simple_archiver_hash_map_get(parsed.mappings.UnameToUid,
                                          "user1",
                                          6);
    CHECK_TRUE(id_get);
    CHECK_TRUE(*id_get == 1003);
    id_get = simple_archiver_hash_map_get(parsed.mappings.UnameToUid,
                                          "user9",
                                          6);
    CHECK_FALSE(id_get);

    name_get = simple_archiver_hash_map_get(parsed.mappings.UnameToUname,
                                            "user2",
                                            6);
    CHECK_TRUE(name_get);
    CHECK_STREQ(name_get, "user3");
    name_get = simple_archiver_hash_map_get(parsed.mappings.UnameToUname,
                                            "user3",
                                            6);
    CHECK_FALSE(name_get);

    simple_archiver_free_parsed(&parsed);
  }

  // Test helpers.
  {
    // Only if system is little-endian.
    if (simple_archiver_helper_is_big_endian() == 0) {
      uint16_t u16 = 0x0102;
      CHECK_TRUE(((uint8_t *)&u16)[0] == 2);
      CHECK_TRUE(((uint8_t *)&u16)[1] == 1);
      simple_archiver_helper_16_bit_be(&u16);
      CHECK_TRUE(((uint8_t *)&u16)[0] == 1);
      CHECK_TRUE(((uint8_t *)&u16)[1] == 2);
      simple_archiver_helper_16_bit_be(&u16);
      CHECK_TRUE(((uint8_t *)&u16)[0] == 2);
      CHECK_TRUE(((uint8_t *)&u16)[1] == 1);

      uint32_t u32 = 0x01020304;
      CHECK_TRUE(((uint8_t *)&u32)[0] == 4);
      CHECK_TRUE(((uint8_t *)&u32)[1] == 3);
      CHECK_TRUE(((uint8_t *)&u32)[2] == 2);
      CHECK_TRUE(((uint8_t *)&u32)[3] == 1);
      simple_archiver_helper_32_bit_be(&u32);
      CHECK_TRUE(((uint8_t *)&u32)[0] == 1);
      CHECK_TRUE(((uint8_t *)&u32)[1] == 2);
      CHECK_TRUE(((uint8_t *)&u32)[2] == 3);
      CHECK_TRUE(((uint8_t *)&u32)[3] == 4);
      simple_archiver_helper_32_bit_be(&u32);
      CHECK_TRUE(((uint8_t *)&u32)[0] == 4);
      CHECK_TRUE(((uint8_t *)&u32)[1] == 3);
      CHECK_TRUE(((uint8_t *)&u32)[2] == 2);
      CHECK_TRUE(((uint8_t *)&u32)[3] == 1);

      uint64_t u64 = 0x010203040a0b0c0d;
      CHECK_TRUE(((uint8_t *)&u64)[0] == 0xd);
      CHECK_TRUE(((uint8_t *)&u64)[1] == 0xc);
      CHECK_TRUE(((uint8_t *)&u64)[2] == 0xb);
      CHECK_TRUE(((uint8_t *)&u64)[3] == 0xa);
      CHECK_TRUE(((uint8_t *)&u64)[4] == 0x4);
      CHECK_TRUE(((uint8_t *)&u64)[5] == 0x3);
      CHECK_TRUE(((uint8_t *)&u64)[6] == 0x2);
      CHECK_TRUE(((uint8_t *)&u64)[7] == 0x1);
      simple_archiver_helper_64_bit_be(&u64);
      CHECK_TRUE(((uint8_t *)&u64)[0] == 0x1);
      CHECK_TRUE(((uint8_t *)&u64)[1] == 0x2);
      CHECK_TRUE(((uint8_t *)&u64)[2] == 0x3);
      CHECK_TRUE(((uint8_t *)&u64)[3] == 0x4);
      CHECK_TRUE(((uint8_t *)&u64)[4] == 0xa);
      CHECK_TRUE(((uint8_t *)&u64)[5] == 0xb);
      CHECK_TRUE(((uint8_t *)&u64)[6] == 0xc);
      CHECK_TRUE(((uint8_t *)&u64)[7] == 0xd);
      simple_archiver_helper_64_bit_be(&u64);
      CHECK_TRUE(((uint8_t *)&u64)[0] == 0xd);
      CHECK_TRUE(((uint8_t *)&u64)[1] == 0xc);
      CHECK_TRUE(((uint8_t *)&u64)[2] == 0xb);
      CHECK_TRUE(((uint8_t *)&u64)[3] == 0xa);
      CHECK_TRUE(((uint8_t *)&u64)[4] == 0x4);
      CHECK_TRUE(((uint8_t *)&u64)[5] == 0x3);
      CHECK_TRUE(((uint8_t *)&u64)[6] == 0x2);
      CHECK_TRUE(((uint8_t *)&u64)[7] == 0x1);
    }
  }

  // Test helpers cmd string to argv.
  do {
    const char *cmd = "zstd  --compress --ultra\n -20  derp_file";
    char **result_argv = simple_archiver_helper_cmd_string_to_argv(cmd);
    CHECK_TRUE(result_argv);
    if (!result_argv) {
      break;
    }
    CHECK_STREQ("zstd", result_argv[0]);
    CHECK_STREQ("--compress", result_argv[1]);
    CHECK_STREQ("--ultra", result_argv[2]);
    CHECK_STREQ("-20", result_argv[3]);
    CHECK_STREQ("derp_file", result_argv[4]);
    CHECK_TRUE(result_argv[5] == NULL);

    simple_archiver_helper_cmd_string_argv_free(result_argv);
  } while (0);

  // Test helpers cut substr.
  {
    const char *s = "one two three.";
    uint32_t s_len = strlen(s);
    // Invalid range.
    char *out = simple_archiver_helper_cut_substr(s, 1, 0);
    CHECK_FALSE(out);
    // First idx out of range.
    out = simple_archiver_helper_cut_substr(s, s_len, s_len + 1);
    CHECK_FALSE(out);
    // Second idx out of range.
    out = simple_archiver_helper_cut_substr(s, 1, s_len + 1);
    CHECK_FALSE(out);
    // Invalid cut of full string.
    out = simple_archiver_helper_cut_substr(s, 0, s_len);
    CHECK_FALSE(out);
    // Cut end of string.
    out = simple_archiver_helper_cut_substr(s, 2, s_len);
    CHECK_TRUE(out);
    CHECK_STREQ(out, "on");
    free(out);
    // Cut start of string.
    out = simple_archiver_helper_cut_substr(s, 0, s_len - 3);
    CHECK_TRUE(out);
    CHECK_STREQ(out, "ee.");
    free(out);
    // Cut inside string.
    out = simple_archiver_helper_cut_substr(s, 4, 8);
    CHECK_TRUE(out);
    CHECK_STREQ(out, "one three.");
    free(out);
  }

  // Test archiver.
  {
    __attribute__((
        cleanup(simple_archiver_helper_cleanup_c_string))) char *rel_path =
        simple_archiver_filenames_to_relative_path(
            "/one/two/three/four/five", "/one/two/branch/other/path");
    CHECK_STREQ(rel_path, "../../branch/other/path");
    simple_archiver_helper_cleanup_c_string(&rel_path);

    rel_path = simple_archiver_filenames_to_relative_path(
        "/one/two/three/four/five", "/one/two/three/other/dir/");
    CHECK_STREQ(rel_path, "../other/dir/");
    simple_archiver_helper_cleanup_c_string(&rel_path);

    rel_path = simple_archiver_filenames_to_relative_path(
        "/one/two/three/", "/one/two/three/four");
    CHECK_STREQ(rel_path, "four");
    simple_archiver_helper_cleanup_c_string(&rel_path);

    CHECK_FALSE(simple_archiver_validate_file_path("Local/Path"));
    CHECK_TRUE(simple_archiver_validate_file_path("/Abs/Path"));
    CHECK_TRUE(simple_archiver_validate_file_path("Local/../../not/really"));
    CHECK_TRUE(simple_archiver_validate_file_path("./../almost"));
    CHECK_TRUE(simple_archiver_validate_file_path("strange/.."));
  }

  printf("Checks checked: %" PRId32 "\n", checks_checked);
  printf("Checks passed:  %" PRId32 "\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
