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
 * `main.c` is the entry-point of this software/program.
 */

#include <stdio.h>

#include "archiver.h"
#include "parser.h"

int print_list_fn(void *data, __attribute__((unused)) void *ud) {
  const SDArchiverFileInfo *file_info = data;
  if (file_info->link_dest == NULL) {
    fprintf(stderr, "  REGULAR FILE:  %s\n", file_info->filename);
  } else {
    fprintf(stderr, "  SYMBOLIC LINK: %s -> %s\n", file_info->filename,
            file_info->link_dest);
  }
  return 0;
}

int main(int argc, const char **argv) {
  __attribute__((
      cleanup(simple_archiver_free_parsed))) SDArchiverParsed parsed =
      simple_archiver_create_parsed();

  simple_archiver_parse_args(argc, argv, &parsed);

  if ((parsed.flags & 0x3) == 0 && (parsed.flags & 0x4) == 0) {
    FILE *file = fopen(parsed.filename, "r");
    if (file != NULL) {
      fclose(file);
      fprintf(
          stderr,
          "ERROR: Archive file exists but --overwrite-create not specified!\n");
      simple_archiver_print_usage();
      return 1;
    }
  }

  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *filenames =
      simple_archiver_parsed_to_filenames(&parsed);

  if (filenames->count > 0) {
    fprintf(stderr, "Filenames:\n");
    simple_archiver_list_get(filenames, print_list_fn, NULL);
  }

  if ((parsed.flags & 3) == 0) {
    FILE *file = fopen(parsed.filename, "wb");
    if (!file) {
      fprintf(stderr, "ERROR: Failed to open \"%s\" for writing!\n",
              parsed.filename);
      return 2;
    }

    __attribute__((cleanup(simple_archiver_free_state)))
    SDArchiverState *state = simple_archiver_init_state(&parsed);

    int ret = simple_archiver_write_all(file, state, filenames);
    if (ret != SDAS_SUCCESS) {
      fprintf(stderr, "Error during writing.\n");
      char *error_str = simple_archiver_error_to_string(ret);
      fprintf(stderr, "  %s\n", error_str);
    }
    fclose(file);
  } else if ((parsed.flags & 3) == 2) {
    FILE *file = fopen(parsed.filename, "rb");
    if (!file) {
      fprintf(stderr, "ERROR: Failed to open \"%s\" for reading!\n",
              parsed.filename);
      return 3;
    }

    int ret = simple_archiver_parse_archive_info(file, 0, NULL);
    if (ret != 0) {
      fprintf(stderr, "Error during archive checking/examining.\n");
      char *error_str = simple_archiver_error_to_string(ret);
      fprintf(stderr, "  %s\n", error_str);
    }
    fclose(file);
  } else if ((parsed.flags & 3) == 1) {
    FILE *file = fopen(parsed.filename, "rb");
    if (!file) {
      fprintf(stderr, "ERROR: Failed to open \"%s\" for reading!\n",
              parsed.filename);
      return 3;
    }

    __attribute__((cleanup(simple_archiver_free_state)))
    SDArchiverState *state = simple_archiver_init_state(&parsed);

    int ret = simple_archiver_parse_archive_info(file, 1, state);
    if (ret != SDAS_SUCCESS) {
      fprintf(stderr, "Error during archive extracting.\n");
      char *error_str = simple_archiver_error_to_string(ret);
      fprintf(stderr, "  %s\n", error_str);
    }
    fclose(file);
  }

  return 0;
}
