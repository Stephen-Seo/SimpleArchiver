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
// `test.c` is the source for testing code.

// Standard library includes.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

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
    const char **args = (const char *[]){"parser",
                                         "--",
                                         "././/././//./derp",
                                         "./doop",
                                         NULL};
    simple_archiver_parse_args(
        4,
        args,
        &parsed);

    CHECK_TRUE(simple_archiver_hash_map_get(parsed.just_w_files, "derp", 5));
    CHECK_TRUE(simple_archiver_hash_map_get(parsed.just_w_files, "doop", 5));
    CHECK_TRUE(parsed.filename == NULL);
    CHECK_TRUE(parsed.flags == 0x40);

    simple_archiver_free_parsed(&parsed);

    parsed = simple_archiver_create_parsed();
    const char **args2 = (const char *[]){"parser", "-x", "-f", "the_filename",
                         "././/././//./.derp", "././//./_doop",
                         "./../../.prev_dir_file", NULL};
    simple_archiver_parse_args(
        7,
        args2,
        &parsed);

    CHECK_TRUE(simple_archiver_hash_map_get(parsed.just_w_files, ".derp", 6));
    CHECK_TRUE(simple_archiver_hash_map_get(parsed.just_w_files, "_doop", 6));
    CHECK_TRUE(simple_archiver_hash_map_get(parsed.just_w_files,
                                            "../../.prev_dir_file",
                                            21));
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

    // Test get mappings.

    // Prepare users_infos.
    simple_archiver_users_free_users_infos(&parsed.users_infos);
    parsed.users_infos.UidToUname = simple_archiver_hash_map_init();
    parsed.users_infos.UnameToUid = simple_archiver_hash_map_init();
    parsed.users_infos.GidToGname = simple_archiver_hash_map_init();
    parsed.users_infos.GnameToGid = simple_archiver_hash_map_init();

    uint32_t *temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1000;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user1000",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1000;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user1000",
      9,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1001;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user1001",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1001;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user1001",
      9,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1002;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user1002",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1002;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user1002",
      9,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1100;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user0",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1100;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user0",
      6,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1101;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user1",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1101;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user1",
      6,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1003;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user1003",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1003;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user1003",
      9,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1102;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user2",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1102;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user2",
      6,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1103;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UidToUname,
      "user3",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1103;
    simple_archiver_hash_map_insert(
      parsed.users_infos.UnameToUid,
      temp_id,
      "user3",
      6,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    // Mappings checks.
    uint32_t out_id;
    const char *out_name = NULL;
    CHECK_TRUE(simple_archiver_get_uid_mapping(parsed.mappings,
                                               parsed.users_infos,
                                               1000,
                                               &out_id,
                                               &out_name) == 0);
    CHECK_TRUE(out_id == 1001);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "user1001");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_uid_mapping(parsed.mappings,
                                               parsed.users_infos,
                                               1002,
                                               &out_id,
                                               &out_name) == 0);
    CHECK_TRUE(out_id == 1100);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "user0");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_uid_mapping(parsed.mappings,
                                               parsed.users_infos,
                                               2000,
                                               &out_id,
                                               &out_name) != 0);
    CHECK_FALSE(out_name);

    CHECK_TRUE(simple_archiver_get_user_mapping(parsed.mappings,
                                                parsed.users_infos,
                                                "user1",
                                                &out_id,
                                                &out_name) == 0);
    CHECK_TRUE(out_id == 1003);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "user1003");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_user_mapping(parsed.mappings,
                                                parsed.users_infos,
                                                "user2",
                                                &out_id,
                                                &out_name) == 0);
    CHECK_TRUE(out_id == 1103);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "user3");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_user_mapping(parsed.mappings,
                                                parsed.users_infos,
                                                "invalid_user",
                                                &out_id,
                                                &out_name) != 0);
    CHECK_FALSE(out_name);

    // Test group mappings.
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1000:1",
      parsed.mappings.GidToGname,
      parsed.mappings.GnameToGid,
      parsed.mappings.GidToGid,
      parsed.mappings.GnameToGname) == 0);
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "1001:root",
      parsed.mappings.GidToGname,
      parsed.mappings.GnameToGid,
      parsed.mappings.GidToGid,
      parsed.mappings.GnameToGname) == 0);
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "audio:200",
      parsed.mappings.GidToGname,
      parsed.mappings.GnameToGid,
      parsed.mappings.GidToGid,
      parsed.mappings.GnameToGname) == 0);
    CHECK_TRUE(simple_archiver_handle_map_user_or_group(
      "lp:realtime",
      parsed.mappings.GidToGname,
      parsed.mappings.GnameToGid,
      parsed.mappings.GidToGid,
      parsed.mappings.GnameToGname) == 0);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1000;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "group1000",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1000;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "group1000",
      10,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "one",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "one",
      4,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1001;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "group1001",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 1001;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "group1001",
      10,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 0;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "root",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 0;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "root",
      5,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 50;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "audio",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 50;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "audio",
      6,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 200;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "group200",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 200;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "group200",
      9,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 300;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "lp",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 300;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "lp",
      3,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 10;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GidToGname,
      "realtime",
      temp_id,
      sizeof(uint32_t),
      simple_archiver_helper_datastructure_cleanup_nop,
      NULL);
    temp_id = malloc(sizeof(uint32_t));
    *temp_id = 10;
    simple_archiver_hash_map_insert(
      parsed.users_infos.GnameToGid,
      temp_id,
      "realtime",
      9,
      NULL,
      simple_archiver_helper_datastructure_cleanup_nop);

    // Mappings checks for groups.
    CHECK_TRUE(simple_archiver_get_gid_mapping(parsed.mappings,
                                               parsed.users_infos,
                                               1000,
                                               &out_id,
                                               &out_name) == 0);
    CHECK_TRUE(out_id == 1);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "one");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_gid_mapping(parsed.mappings,
                                               parsed.users_infos,
                                               1001,
                                               &out_id,
                                               &out_name) == 0);
    CHECK_TRUE(out_id == 0);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "root");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_gid_mapping(parsed.mappings,
                                               parsed.users_infos,
                                               1111,
                                               &out_id,
                                               &out_name) != 0);

    CHECK_TRUE(simple_archiver_get_group_mapping(parsed.mappings,
                                                 parsed.users_infos,
                                                 "audio",
                                                 &out_id,
                                                 &out_name) == 0);
    CHECK_TRUE(out_id == 200);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "group200");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_group_mapping(parsed.mappings,
                                                 parsed.users_infos,
                                                 "lp",
                                                 &out_id,
                                                 &out_name) == 0);
    CHECK_TRUE(out_id == 10);
    CHECK_TRUE(out_name);
    if (out_name) {
      CHECK_STREQ(out_name, "realtime");
      free((void *)out_name);
      out_name = NULL;
    }

    CHECK_TRUE(simple_archiver_get_group_mapping(parsed.mappings,
                                                 parsed.users_infos,
                                                 "nothing",
                                                 &out_id,
                                                 &out_name) != 0);

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

  // Test insert prefix in link path.
  {
    char *cwd = getcwd(NULL, 0);
    int ret = chdir("/tmp");
    CHECK_TRUE(ret == 0);
    if (ret != 0) {
      goto TEST_HELPERS_PREFIX_END;
    }
    ret = mkdir("/tmp/fifty", S_IRWXU);
    CHECK_TRUE(ret == 0 || errno == EEXIST);
    if (ret != 0 && errno != EEXIST) {
      goto TEST_HELPERS_PREFIX_END;
    }

    char *result = simple_archiver_helper_insert_prefix_in_link_path(
      "one/two/three/", "fifty/link", "/tmp/fifty/link_target");
    CHECK_TRUE(result);
    if (result) {
      CHECK_STREQ(result, "/tmp/one/two/three/fifty/link_target");
      free(result);
    }

    result = simple_archiver_helper_insert_prefix_in_link_path(
      "one/two/three/", "fifty/link", "/other");
    CHECK_TRUE(result);
    if (result) {
      CHECK_STREQ(result, "/other");
      free(result);
    }

    result = simple_archiver_helper_insert_prefix_in_link_path(
      "one/two/three/", "fifty/link", "sixty/seventy/other");
    CHECK_TRUE(result);
    if (result) {
      CHECK_STREQ(result, "sixty/seventy/other");
      free(result);
    }

    result = simple_archiver_helper_insert_prefix_in_link_path(
      "one/two/three/", "fifty/link", "../../other");
    CHECK_TRUE(result);
    if (result) {
      CHECK_STREQ(result, "../../../../../other");
      free(result);
    }

TEST_HELPERS_PREFIX_END:
    rmdir("/tmp/fifty");
    chdir(cwd);
    free(cwd);
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

  // Test string parts.
  {
    __attribute__((cleanup(simple_archiver_helper_string_parts_free)))
    SAHelperStringParts string_parts =
      simple_archiver_helper_string_parts_init();

    simple_archiver_helper_string_parts_add(string_parts, "a");

    char *buf = simple_archiver_helper_string_parts_combine(string_parts);
    CHECK_STREQ(buf, "a");
    free(buf);

    simple_archiver_helper_string_parts_add(string_parts, "/b");

    buf = simple_archiver_helper_string_parts_combine(string_parts);
    CHECK_STREQ(buf, "a/b");
    free(buf);

    simple_archiver_helper_string_parts_add(string_parts, "/");

    buf = simple_archiver_helper_string_parts_combine(string_parts);
    CHECK_STREQ(buf, "a/b/");
    free(buf);

    simple_archiver_helper_string_parts_add(string_parts, "c");

    buf = simple_archiver_helper_string_parts_combine(string_parts);
    CHECK_STREQ(buf, "a/b/c");
    free(buf);
  }

  // Test contains/starts/ends helpers.
  {
    CHECK_TRUE(simple_archiver_helper_string_contains(
      "The string is this.", " is ", 0));
    CHECK_FALSE(simple_archiver_helper_string_contains(
      "The string is this.", " is d", 0));
    CHECK_TRUE(simple_archiver_helper_string_contains(
      "TheseTheThesesThe", "Theses", 0));

    CHECK_TRUE(simple_archiver_helper_string_starts(
      "The string is this.", "The ", 0));
    CHECK_FALSE(simple_archiver_helper_string_starts(
      "The string is this.", "tThe ", 0));

    CHECK_TRUE(simple_archiver_helper_string_ends(
      "The string is this.", " this.", 0));
    CHECK_FALSE(simple_archiver_helper_string_ends(
      "The string is this.", " this", 0));


    CHECK_TRUE(simple_archiver_helper_string_contains(
      "The String Is This.", "sTRING", 1));
    CHECK_TRUE(simple_archiver_helper_string_starts(
      "The String Is This.", "tHE", 1));
    CHECK_TRUE(simple_archiver_helper_string_ends(
      "The String Is This.", "tHIS.", 1));
  }

  printf("Checks checked: %" PRId32 "\n", checks_checked);
  printf("Checks passed:  %" PRId32 "\n", checks_passed);
  return checks_passed == checks_checked ? 0 : 1;
}
