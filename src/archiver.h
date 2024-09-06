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

#include <stdio.h>

#include "data_structures/hash_map.h"
#include "data_structures/linked_list.h"
#include "parser.h"

typedef struct SDArchiverState {
  /*
   */
  unsigned int flags;
  const SDArchiverParsed *parsed;
  FILE *out_f;
  SDArchiverHashMap *map;
  size_t count;
  size_t max;
  size_t digits;
} SDArchiverState;

enum SDArchiverStateReturns {
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
  SDAS_FAILED_TO_CHANGE_CWD
};

/// Returned pointer must not be freed.
char *simple_archiver_error_to_string(enum SDArchiverStateReturns error);

SDArchiverState *simple_archiver_init_state(const SDArchiverParsed *parsed);
void simple_archiver_free_state(SDArchiverState **state);

/// Returns zero on success. Otherwise one value from SDArchiverStateReturns
/// enum.
int simple_archiver_write_all(FILE *out_f, SDArchiverState *state,
                              const SDArchiverLinkedList *filenames);

/// Returns zero on success.
int simple_archiver_parse_archive_info(FILE *in_f, int do_extract,
                                       const SDArchiverState *state);

/// Returns zero on success.
int simple_archiver_de_compress(int pipe_fd_in[2], int pipe_fd_out[2],
                                const char *cmd, void *pid_out);

#endif
