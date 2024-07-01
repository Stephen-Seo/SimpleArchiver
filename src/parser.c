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
 * `parser.c` is the source file for parsing args.
 */

#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser_internal.h"

/// Gets the first non "./"-like character in the filename.
unsigned int simple_archiver_parser_internal_filename_idx(
    const char *filename) {
  unsigned int idx = 0;
  unsigned int known_good_idx = 0;
  const unsigned int length = strlen(filename);

  // 0b0001 - checked that idx char is '.'
  // 0b0010 - checked that idx char is '/'
  unsigned int flags = 0;

  for (; idx < length; ++idx) {
    if ((flags & 3) == 0) {
      if (filename[idx] == 0) {
        return known_good_idx;
      } else if (filename[idx] == '.') {
        flags |= 1;
      } else {
        return idx;
      }
    } else if ((flags & 3) == 1) {
      if (filename[idx] == 0) {
        return known_good_idx;
      } else if (filename[idx] == '/') {
        flags |= 2;
      } else {
        return idx - 1;
      }
    } else if ((flags & 3) == 3) {
      if (filename[idx] == 0) {
        return known_good_idx;
      } else if (filename[idx] == '/') {
        continue;
      } else if (filename[idx] == '.') {
        flags &= 0xFFFFFFFC;
        known_good_idx = idx;
        --idx;
        continue;
      } else {
        break;
      }
    }
  }

  if (filename[idx] == 0) {
    return known_good_idx;
  }

  return idx;
}

void simple_archiver_print_usage(void) {
  puts("Usage flags:");
  puts("-c : create archive file");
  puts("-x : extract archive file");
  puts("-f <filename> : filename to work on");
  puts("--compressor <full_compress_cmd> : requires --decompressor");
  puts("--decompressor <full_decompress_cmd> : requires --compressor");
  puts("-- : specifies remaining arguments are files to archive/extract");
  puts("If creating archive file, remaining args specify files to archive.");
  puts("If extracting archive file, remaining args specify files to extract.");
}

SDArchiverParsed simple_archiver_create_parsed(void) {
  SDArchiverParsed parsed;

  parsed.flags = 0;
  parsed.filename = NULL;
  parsed.compressor = NULL;
  parsed.decompressor = NULL;
  parsed.working_files = NULL;

  return parsed;
}

int simple_archiver_parse_args(int argc, const char **argv,
                               SDArchiverParsed *out) {
  if (out->filename) {
    free(out->filename);
    out->filename = NULL;
  }
  if (out->compressor) {
    free(out->compressor);
    out->compressor = NULL;
  }
  if (out->decompressor) {
    free(out->decompressor);
    out->decompressor = NULL;
  }

  // Skip program name as it is the first arg usually.
  --argc;
  ++argv;

  int is_remaining_args = 0;

  while (argc > 0) {
    if (!is_remaining_args) {
      if (strcmp(argv[0], "-c") == 0) {
        // unset first bit.
        out->flags &= 0xFFFFFFFE;
      } else if (strcmp(argv[0], "-x") == 0) {
        // set first bit.
        out->flags |= 0x1;
      } else if (strcmp(argv[0], "-f") == 0 && argc > 1) {
        int size = strlen(argv[1]) + 1;
        out->filename = malloc(size);
        strncpy(out->filename, argv[1], size);
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "--compressor") == 0 && argc > 1) {
        int size = strlen(argv[1]) + 1;
        out->compressor = malloc(size);
        strncpy(out->compressor, argv[1], size);
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "--decompressor") == 0 && argc > 1) {
        int size = strlen(argv[1]) + 1;
        out->decompressor = malloc(size);
        strncpy(out->decompressor, argv[1], size);
        --argc;
        ++argv;
      } else if (argv[0][0] == '-' && argv[0][1] == '-' && argv[0][2] == 0) {
        is_remaining_args = 1;
      } else if (argv[0][0] != '-') {
        is_remaining_args = 1;
        continue;
      }
    } else {
      if (out->working_files == NULL) {
        out->working_files = malloc(sizeof(char *) * 2);
        unsigned int arg_idx =
            simple_archiver_parser_internal_filename_idx(argv[0]);
        int arg_length = strlen(argv[0] + arg_idx) + 1;
        out->working_files[0] = malloc(arg_length);
        strncpy(out->working_files[0], argv[0] + arg_idx, arg_length);
        out->working_files[1] = NULL;
      } else {
        int working_size = 1;
        char **ptr = out->working_files;
        while (ptr && *ptr) {
          ++working_size;
          ++ptr;
        }

        // TODO verify this is necessary, using different variables.
        ptr = out->working_files;
        out->working_files = realloc(ptr, sizeof(char *) * (working_size + 1));

        // Set new actual last element to NULL.
        out->working_files[working_size] = NULL;
        unsigned int arg_idx =
            simple_archiver_parser_internal_filename_idx(argv[0]);
        int size = strlen(argv[0] + arg_idx) + 1;
        // Set last element to the arg.
        out->working_files[working_size - 1] = malloc(size);
        strncpy(out->working_files[working_size - 1], argv[0] + arg_idx, size);
      }
    }

    --argc;
    ++argv;
  }

  return 0;
}

void simple_archiver_free_parsed(SDArchiverParsed *parsed) {
  parsed->flags = 0;
  if (parsed->filename) {
    free(parsed->filename);
    parsed->filename = NULL;
  }
  if (parsed->compressor) {
    free(parsed->compressor);
    parsed->compressor = NULL;
  }
  if (parsed->decompressor) {
    free(parsed->decompressor);
    parsed->decompressor = NULL;
  }
  if (parsed->working_files) {
    char **ptr = parsed->working_files;
    unsigned int idx = 0;
    while (ptr[idx]) {
      free(ptr[idx]);
      ++idx;
    }
    free(parsed->working_files);
    parsed->working_files = NULL;
  }
}
