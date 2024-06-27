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

void simple_archiver_print_usage(void) {
  puts("Usage flags:");
  puts("-c : create archive file");
  puts("-x : extract archive file");
  puts("-f <filename> : filename to work on");
  puts("--compressor <full_compress_cmd> : requires --decompressor");
  puts("--decompressor <full_decompress_cmd> : requires --compressor");
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
      } else if (argv[0][0] != '-') {
        is_remaining_args = 1;
        continue;
      }
    } else {
      if (out->working_files == NULL) {
        puts("first addition to working_files");
        out->working_files = malloc(sizeof(char *) * 2);
        int arg_length = strlen(argv[0]) + 1;
        out->working_files[0] = malloc(arg_length);
        strncpy(out->working_files[0], argv[0], arg_length);
        out->working_files[1] = NULL;
      } else {
        puts("later addition to working_files");
        int working_size = 1;
        char **ptr = out->working_files;
        while (ptr && *ptr) {
          ++working_size;
          ++ptr;
        }
        printf("working_size is %u\n", working_size);

        // TODO verify this is necessary, using different variables.
        ptr = out->working_files;
        out->working_files = realloc(ptr, working_size + 1);

        // Set new actual last element to NULL.
        out->working_files[working_size] = NULL;
        int size = strlen(argv[0]) + 1;
        // Set last element to the arg.
        out->working_files[working_size - 1] = malloc(size);
        strncpy(out->working_files[working_size - 1], argv[0], size);
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
    puts("freeing working_files strings...");
    while (ptr[idx]) {
      printf("Freeing at idx %u\n", idx);
      free(ptr[idx]);
      ++idx;
    }
    puts("freeing string array...");
    free(parsed->working_files);
    parsed->working_files = NULL;

    puts("free_parsed is done.");
  }
}
