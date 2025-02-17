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
// `main.c` is the entry-point of this software/program.

#include <stdio.h>

#include "platforms.h"
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
#include <unistd.h>
#endif

#include "archiver.h"
#include "parser.h"

int print_list_fn(void *data, __attribute__((unused)) void *ud) {
  const SDArchiverFileInfo *file_info = data;
  if (file_info->link_dest == NULL) {
    if (file_info->flags & 1) {
      fprintf(stderr, "  DIRECTORY:     %s\n", file_info->filename);
    } else {
      fprintf(stderr, "  REGULAR FILE:  %s\n", file_info->filename);
    }
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

  if (simple_archiver_parse_args(argc, argv, &parsed)) {
    fprintf(stderr, "Failed to parse args.\n");
    return 7;
  }

  if (!parsed.filename && (parsed.flags & 0x10) == 0) {
    fprintf(stderr, "ERROR: Filename not specified!\n");
    simple_archiver_print_usage();
    return 6;
  }

  if ((parsed.flags & 0x3) == 0 && (parsed.flags & 0x2000) != 0) {
    fprintf(stderr,
            "WARNING: --force-dir-permissions specified, but has no effect "
            "during archive creation!\nNOTE: Use "
            "\"--force-empty-dir-permissions\" for empty directories!\n");
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
    sleep(2);
#endif
  }

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

  SDArchiverParsedStatus parsed_status;
  __attribute__((cleanup(simple_archiver_list_free)))
  SDArchiverLinkedList *filenames =
      simple_archiver_parsed_to_filenames(&parsed, &parsed_status);
  if (!filenames || parsed_status != SDAPS_SUCCESS) {
    fprintf(stderr, "ERROR: %s!\n",
            simple_archiver_parsed_status_to_str(parsed_status));
    return 8;
  }

  if (filenames->count > 0) {
    fprintf(stderr, "Filenames:\n");
    simple_archiver_list_get(filenames, print_list_fn, NULL);
  }

  if ((parsed.flags & 3) == 0) {
    // Is creating archive.
    __attribute__((cleanup(simple_archiver_free_state)))
    SDArchiverState *state = simple_archiver_init_state(&parsed);
    if ((parsed.flags & 0x10) == 0) {
      FILE *file = fopen(parsed.filename, "wb");
      if (!file) {
        fprintf(stderr, "ERROR: Failed to open \"%s\" for writing!\n",
                parsed.filename);
        return 2;
      }

      int ret = simple_archiver_write_all(file, state, filenames);
      if (ret != SDAS_SUCCESS) {
        fprintf(stderr, "Error during writing.\n");
        char *error_str =
            simple_archiver_error_to_string((SDArchiverStateReturns)ret);
        fprintf(stderr, "  %s\n", error_str);
      }
      fclose(file);
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
      if (ret != SDAS_SUCCESS) {
        unlink(parsed.filename);
        return 3;
      }
#endif
    } else {
      int ret = simple_archiver_write_all(stdout, state, filenames);
      if (ret != SDAS_SUCCESS) {
        fprintf(stderr, "Error during writing.\n");
        char *error_str =
            simple_archiver_error_to_string((SDArchiverStateReturns)ret);
        fprintf(stderr, "  %s\n", error_str);
      }
    }
  } else if ((parsed.flags & 3) == 2) {
    // Is checking archive.
    if ((parsed.flags & 0x10) == 0) {
      FILE *file = fopen(parsed.filename, "rb");
      if (!file) {
        fprintf(stderr, "ERROR: Failed to open \"%s\" for reading!\n",
                parsed.filename);
        return 4;
      }

      int ret = simple_archiver_parse_archive_info(file, 0, NULL);
      if (ret != 0) {
        fprintf(stderr, "Error during archive checking/examining.\n");
        char *error_str =
            simple_archiver_error_to_string((SDArchiverStateReturns)ret);
        fprintf(stderr, "  %s\n", error_str);
      }
      fclose(file);
    } else {
      int ret = simple_archiver_parse_archive_info(stdin, 0, NULL);
      if (ret != 0) {
        fprintf(stderr, "Error during archive checking/examining.\n");
        char *error_str =
            simple_archiver_error_to_string((SDArchiverStateReturns)ret);
        fprintf(stderr, "  %s\n", error_str);
      }
    }
  } else if ((parsed.flags & 3) == 1) {
    // Is extracting archive.
    __attribute__((cleanup(simple_archiver_free_state)))
    SDArchiverState *state = simple_archiver_init_state(&parsed);
    if ((parsed.flags & 0x10) == 0) {
      FILE *file = fopen(parsed.filename, "rb");
      if (!file) {
        fprintf(stderr, "ERROR: Failed to open \"%s\" for reading!\n",
                parsed.filename);
        return 5;
      }

      int ret = simple_archiver_parse_archive_info(file, 1, state);
      if (ret != SDAS_SUCCESS) {
        fprintf(stderr, "Error during archive extracting.\n");
        char *error_str =
            simple_archiver_error_to_string((SDArchiverStateReturns)ret);
        fprintf(stderr, "  %s\n", error_str);
      }
      fclose(file);
    } else {
      int ret = simple_archiver_parse_archive_info(stdin, 1, state);
      if (ret != SDAS_SUCCESS) {
        fprintf(stderr, "Error during archive extracting.\n");
        char *error_str =
            simple_archiver_error_to_string((SDArchiverStateReturns)ret);
        fprintf(stderr, "  %s\n", error_str);
      }
    }
  }

  return 0;
}
