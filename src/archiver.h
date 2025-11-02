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
// `archiver.h` is the header for an interface to creating an archive file.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_ARCHIVER_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_ARCHIVER_H_

// Standard library includes.
#include <stdint.h>
#include <stdio.h>

// Local includes.
#include "data_structures/hash_map.h"
#include "data_structures/linked_list.h"
#include "parser.h"

typedef struct SDArchiverState {
  /*
   */
  uint32_t flags;
  SDArchiverParsed *parsed;
  FILE *out_f;
  SDArchiverHashMap *map;
  size_t count;
  uint64_t max;
  uint64_t digits;
} SDArchiverState;

typedef enum SDArchiverStateReturns {
  SDAS_SUCCESS = 0,
  SDAS_HEADER_ALREADY_WRITTEN = 1,
  SDAS_FAILED_TO_WRITE,
  SDAS_NO_COMPRESSOR,
  SDAS_NO_DECOMPRESSOR,
  SDAS_INVALID_PARSED_STATE,
  SDAS_INVALID_FILE,
  SDAS_INTERNAL_ERROR,
  SDAS_FAILED_TO_CREATE_MAP,
  SDAS_FAILED_TO_EXTRACT_SYMLINK,
  SDAS_FAILED_TO_CHANGE_CWD,
  SDAS_INVALID_WRITE_VERSION,
  SDAS_SIGINT,
  SDAS_TOO_MANY_DIRS,
  SDAS_COMPRESSION_ERROR,
  SDAS_DECOMPRESSION_ERROR,
  SDAS_NON_DEC_EXTRACT_ERROR,
  SDAS_FILE_CREATE_FAIL,
  SDAS_COMPRESSED_WRITE_FAIL,
  SDAS_DIR_ENTRY_WRITE_FAIL,
  SDAS_PERMISSION_SET_FAIL,
  SDAS_UID_GID_SET_FAIL,
  SDAS_MAX_RETURN_VAL,
  SDAS_STATUS_RET_MASK = 0x3FFFFFFF,
  // Used by parse v. 1/2 functions.
  SDAS_NOT_TESTED_ONCE = 0x40000000
} SDArchiverStateReturns;

typedef struct SDArchiverStateRetStruct {
  size_t line;
  SDArchiverStateReturns ret;
} SDArchiverStateRetStruct;

#define SDA_RET_STRUCT(enum_val) \
  (SDArchiverStateRetStruct){.line=__LINE__, .ret=enum_val}

#define SDA_PSTATE_CMP_SIZE_KEY "SDA_Compressed_Size_Key"
#define SDA_PSTATE_CMP_SIZE_KEY_SIZE 24
#define SDA_PSTATE_ACT_SIZE_KEY "SDA_Actual_Size_Key"
#define SDA_PSTATE_ACT_SIZE_KEY_SIZE 20

/// Returned pointer must not be freed.
char *simple_archiver_error_to_string(enum SDArchiverStateReturns error);

SDArchiverState *simple_archiver_init_state(SDArchiverParsed *parsed);
void simple_archiver_free_state(SDArchiverState **state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_write_all(
  FILE *out_f,
  SDArchiverState *state);

SDArchiverStateRetStruct simple_archiver_write_v0(
  FILE *out_f,
  SDArchiverState *state,
  SDArchiverHashMap *write_state);

SDArchiverStateRetStruct simple_archiver_write_v1(
  FILE *out_f,
  SDArchiverState *state,
  SDArchiverHashMap *write_state);

SDArchiverStateRetStruct simple_archiver_write_v2(
  FILE *out_f,
  SDArchiverState *state,
  SDArchiverHashMap *write_state);

SDArchiverStateRetStruct simple_archiver_write_v3(
  FILE *out_f,
  SDArchiverState *state,
  SDArchiverHashMap *write_state);

SDArchiverStateRetStruct simple_archiver_write_v4v5(
  FILE *out_f,
  SDArchiverState *state,
  SDArchiverHashMap *write_state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_parse_archive_info(
  FILE *in_f,
  int_fast8_t do_extract,
  SDArchiverState *state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_parse_archive_version_0(
  FILE *in_f,
  int_fast8_t do_extract,
  const SDArchiverState *state,
  SDArchiverHashMap *parse_state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_parse_archive_version_1(
  FILE *in_f,
  int_fast8_t do_extract,
  const SDArchiverState *state,
  SDArchiverHashMap *parse_state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_parse_archive_version_2(
  FILE *in_f,
  int_fast8_t do_extract,
  const SDArchiverState *state,
  SDArchiverHashMap *parse_state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_parse_archive_version_3(
  FILE *in_f,
  int_fast8_t do_extract,
  const SDArchiverState *state,
  SDArchiverHashMap *parse_state);

/// Returns zero in "ret" field on success.
SDArchiverStateRetStruct simple_archiver_parse_archive_version_4_5_6(
  FILE *in_f,
  int_fast8_t do_extract,
  const SDArchiverState *state,
  SDArchiverHashMap *parse_state);

/// Returns zero on success.
int simple_archiver_de_compress(int pipe_fd_in[2], int pipe_fd_out[2],
                                const char *cmd, void *pid_out);

/// If returns non-NULL, must be free'd.
char *simple_archiver_filenames_to_relative_path(const char *from_abs,
                                                 const char *to_abs);

/// Used to validate a file in a ".simplearchive" file to avoid writing outside
/// of current working directory.
/// Returns zero if file is OK.
/// Returns 1 if file starts with '/'.
/// Returns 2 if file contains '../' at the start.
/// Returns 3 if file contains '/../' in the middle.
/// Returns 4 if file contains '/..' at the end.
/// Returns 5 if "filepath" is NULL.
int simple_archiver_validate_file_path(const char *filepath);

/// Removes links from "links_list" in cwd if it is not valid or does not point
/// to a file in "files_map".
void simple_archiver_safe_links_enforce(SDArchiverLinkedList *links_list,
                                        SDArchiverHashMap *files_map);

#endif
