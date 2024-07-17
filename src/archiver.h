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
 * `archiver.h` is the header for an interface to creating an archive file.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_ARCHIVER_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_ARCHIVER_H_

#include <stdio.h>

#include "data_structures/linked_list.h"
#include "parser.h"

typedef struct SDArchiverState {
  /*
   */
  unsigned int flags;
  const SDArchiverParsed *parsed;
  FILE *out_f;
  unsigned int count;
  unsigned int max;
} SDArchiverState;

enum SDArchiverStateReturns {
  SDAS_SUCCESS = 0,
  SDAS_HEADER_ALREADY_WRITTEN = 1,
  SDAS_FAILED_TO_WRITE,
  SDAS_NO_COMPRESSOR,
  SDAS_NO_DECOMPRESSOR,
  SDAS_INVALID_PARSED_STATE,
  SDAS_INVALID_FILE
};

SDArchiverState *simple_archiver_init_state(const SDArchiverParsed *parsed);
void simple_archiver_free_state(SDArchiverState **state);

/// Returns zero on success. Otherwise one value from SDArchiverStateReturns
/// enum.
int simple_archiver_write_all(FILE *out_f, SDArchiverState *state,
                              const SDArchiverLinkedList *filenames);

/// Returns zero on success.
int simple_archiver_print_archive_info(FILE *in_f);

/// Returns zero on success.
int simple_archiver_de_compress(int pipe_fd_in[2], int pipe_fd_out[2],
                                const char *cmd);

#endif
