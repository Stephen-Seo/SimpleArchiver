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
  const SDArchiverParsed *parsed;
  FILE *out_f;
  SDArchiverHashMap *map;
  size_t count;
  size_t max;
  size_t digits;
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
  SDAS_TOO_MANY_DIRS
} SDArchiverStateReturns;

/// Returned pointer must not be freed.
char *simple_archiver_error_to_string(enum SDArchiverStateReturns error);

SDArchiverState *simple_archiver_init_state(const SDArchiverParsed *parsed);
void simple_archiver_free_state(SDArchiverState **state);

/// Returns zero on success. Otherwise one value from SDArchiverStateReturns
/// enum.
int simple_archiver_write_all(FILE *out_f, SDArchiverState *state,
                              const SDArchiverLinkedList *filenames);

int simple_archiver_write_v0(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames);

int simple_archiver_write_v1(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames);

int simple_archiver_write_v2(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames);

int simple_archiver_write_v3(FILE *out_f, SDArchiverState *state,
                             const SDArchiverLinkedList *filenames);

/// Returns zero on success.
int simple_archiver_parse_archive_info(FILE *in_f, int_fast8_t do_extract,
                                       const SDArchiverState *state);

/// Returns zero on success.
int simple_archiver_parse_archive_version_0(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state);

/// Returns zero on success.
int simple_archiver_parse_archive_version_1(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state);

/// Returns zero on success.
int simple_archiver_parse_archive_version_2(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state);

/// Returns zero on success.
int simple_archiver_parse_archive_version_3(FILE *in_f, int_fast8_t do_extract,
                                            const SDArchiverState *state);

/// Returns zero on success.
int simple_archiver_de_compress(int pipe_fd_in[2], int pipe_fd_out[2],
                                const char *cmd, void *pid_out);

/// If returns non-NULL, must be free'd.
char *simple_archiver_filenames_to_relative_path(const char *from_abs,
                                                 const char *to_abs);

/// Gets the absolute path to a file given a path to a file.
/// Should also work on symlinks such that the returned string is the path to
/// the link itself, not what it points to.
/// Non-NULL on success, and must be free'd if non-NULL.
char *simple_archiver_file_abs_path(const char *filename);

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
