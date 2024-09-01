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
// `parser.c` is the source file for parsing args.

#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platforms.h"
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||   \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "data_structures/hash_map.h"
#include "data_structures/linked_list.h"
#include "helpers.h"
#include "parser_internal.h"

/// Gets the first non "./"-like character in the filename.
unsigned int simple_archiver_parser_internal_get_first_non_current_idx(
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

void simple_archiver_parser_internal_remove_end_slash(char *filename) {
  int len = strlen(filename);
  int idx;
  for (idx = len; idx-- > 0;) {
    if (filename[idx] != '/') {
      ++idx;
      break;
    }
  }
  if (idx < len && idx > 0) {
    filename[idx] = 0;
  }
}

void simple_archiver_internal_free_file_info_fn(void *data) {
  SDArchiverFileInfo *file_info = data;
  if (file_info) {
    if (file_info->filename) {
      free(file_info->filename);
    }
    if (file_info->link_dest) {
      free(file_info->link_dest);
    }
  }

  free(data);
}

int list_get_last_fn(void *data, void *ud) {
  char **last = ud;
  *last = data;
  return 0;
}

int list_remove_same_str_fn(void *data, void *ud) {
  if (strcmp((char *)data, (char *)ud) == 0) {
    return 1;
  }

  return 0;
}

void simple_archiver_print_usage(void) {
  fprintf(stderr, "Usage flags:\n");
  fprintf(stderr, "-c : create archive file\n");
  fprintf(stderr, "-x : extract archive file\n");
  fprintf(stderr, "-t : examine archive file\n");
  fprintf(stderr, "-f <filename> : filename to work on\n");
  fprintf(stderr,
          "  Use \"-f -\" to work on stdout when creating archive or stdin "
          "when reading archive\n");
  fprintf(stderr, "  NOTICE: \"-f\" is not affected by \"-C\"!\n");
  fprintf(stderr,
          "-C <dir> : Change current working directory before "
          "archiving/extracting\n");
  fprintf(stderr,
          "--compressor <full_compress_cmd> : requires --decompressor\n");
  fprintf(stderr,
          "--decompressor <full_decompress_cmd> : requires --compressor\n");
  fprintf(stderr,
          "  Specifying \"--decompressor\" when extracting overrides archive "
          "file's stored decompressor cmd\n");
  fprintf(stderr, "--overwrite-create : allows overwriting an archive file\n");
  fprintf(stderr, "--overwrite-extract : allows overwriting when extracting\n");
  fprintf(stderr,
          "--no-abs-symlink : do not store absolute paths for symlinks\n");
  fprintf(stderr,
          "--temp-files-dir <dir> : where to store temporary files created "
          "when compressing (defaults to current working directory)\n");
  fprintf(stderr,
          "-- : specifies remaining arguments are files to archive/extract\n");
  fprintf(
      stderr,
      "If creating archive file, remaining args specify files to archive.\n");
  fprintf(
      stderr,
      "If extracting archive file, remaining args specify files to extract.\n");
}

SDArchiverParsed simple_archiver_create_parsed(void) {
  SDArchiverParsed parsed;

  parsed.flags = 0;
  parsed.filename = NULL;
  parsed.compressor = NULL;
  parsed.decompressor = NULL;
  parsed.working_files = NULL;
  parsed.temp_dir = NULL;
  parsed.user_cwd = NULL;

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
      if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
        simple_archiver_free_parsed(out);
        simple_archiver_print_usage();
        exit(0);
      } else if (strcmp(argv[0], "-c") == 0) {
        // unset first two bits.
        out->flags &= 0xFFFFFFFC;
      } else if (strcmp(argv[0], "-x") == 0) {
        // unset first two bits.
        out->flags &= 0xFFFFFFFC;
        // set first bit.
        out->flags |= 0x1;
      } else if (strcmp(argv[0], "-t") == 0) {
        // unset first two bits.
        out->flags &= 0xFFFFFFFC;
        // set second bit.
        out->flags |= 0x2;
      } else if (strcmp(argv[0], "-f") == 0) {
        if (argc < 2) {
          fprintf(stderr, "ERROR: -f specified but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        if (strcmp(argv[1], "-") == 0) {
          out->flags |= 0x10;
          if (out->filename) {
            free(out->filename);
          }
          out->filename = NULL;
        } else {
          out->flags &= 0xFFFFFFEF;
          int size = strlen(argv[1]) + 1;
          out->filename = malloc(size);
          strncpy(out->filename, argv[1], size);
        }
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "-C") == 0) {
        if (argc < 2) {
          fprintf(stderr, "ERROR: -C specified but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->user_cwd = argv[1];
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "--compressor") == 0) {
        if (argc < 2) {
          fprintf(stderr, "--compressor specfied but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        int size = strlen(argv[1]) + 1;
        out->compressor = malloc(size);
        strncpy(out->compressor, argv[1], size);
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "--decompressor") == 0) {
        if (argc < 2) {
          fprintf(stderr, "--decompressor specfied but missing argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        int size = strlen(argv[1]) + 1;
        out->decompressor = malloc(size);
        strncpy(out->decompressor, argv[1], size);
        --argc;
        ++argv;
      } else if (strcmp(argv[0], "--overwrite-create") == 0) {
        out->flags |= 0x4;
      } else if (strcmp(argv[0], "--overwrite-extract") == 0) {
        out->flags |= 0x8;
      } else if (strcmp(argv[0], "--no-abs-symlink") == 0) {
        out->flags |= 0x20;
      } else if (strcmp(argv[0], "--temp-files-dir") == 0) {
        if (argc < 2) {
          fprintf(stderr, "ERROR: --temp-files-dir is missing an argument!\n");
          simple_archiver_print_usage();
          return 1;
        }
        out->temp_dir = argv[1];
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
            simple_archiver_parser_internal_get_first_non_current_idx(argv[0]);
        unsigned int arg_length = strlen(argv[0] + arg_idx) + 1;
        out->working_files[0] = malloc(arg_length);
        strncpy(out->working_files[0], argv[0] + arg_idx, arg_length);
        simple_archiver_parser_internal_remove_end_slash(out->working_files[0]);
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
            simple_archiver_parser_internal_get_first_non_current_idx(argv[0]);
        int size = strlen(argv[0] + arg_idx) + 1;
        // Set last element to the arg.
        out->working_files[working_size - 1] = malloc(size);
        strncpy(out->working_files[working_size - 1], argv[0] + arg_idx, size);
        simple_archiver_parser_internal_remove_end_slash(
            out->working_files[working_size - 1]);
      }
    }

    --argc;
    ++argv;
  }

  if (!out->temp_dir) {
    out->temp_dir = "./";
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

SDArchiverLinkedList *simple_archiver_parsed_to_filenames(
    const SDArchiverParsed *parsed) {
  SDArchiverLinkedList *files_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *hash_map = simple_archiver_hash_map_init();
  int hash_map_sentinel = 1;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  __attribute__((cleanup(
      simple_archiver_helper_cleanup_chdir_back))) char *original_cwd = NULL;
  if (parsed->user_cwd) {
    original_cwd = realpath(".", NULL);
    if (chdir(parsed->user_cwd)) {
      simple_archiver_list_free(&files_list);
      return NULL;
    }
  }

  for (char **iter = parsed->working_files; iter && *iter; ++iter) {
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    char *file_path = *iter;
    fstatat(AT_FDCWD, file_path, &st, AT_SYMLINK_NOFOLLOW);
    if ((st.st_mode & S_IFMT) == S_IFREG || (st.st_mode & S_IFMT) == S_IFLNK) {
      // Is a regular file or a symbolic link.
      int len = strlen(file_path) + 1;
      char *filename = malloc(len);
      strncpy(filename, file_path, len);
      if (simple_archiver_hash_map_get(hash_map, filename, len - 1) == NULL) {
        SDArchiverFileInfo *file_info = malloc(sizeof(SDArchiverFileInfo));
        file_info->filename = filename;
        if ((st.st_mode & S_IFMT) == S_IFLNK) {
          // Is a symlink.
          file_info->link_dest = malloc(MAX_SYMBOLIC_LINK_SIZE);
          ssize_t count = readlinkat(AT_FDCWD, filename, file_info->link_dest,
                                     MAX_SYMBOLIC_LINK_SIZE - 1);
          if (count >= MAX_SYMBOLIC_LINK_SIZE - 1) {
            file_info->link_dest[MAX_SYMBOLIC_LINK_SIZE - 1] = 0;
          } else if (count > 0) {
            file_info->link_dest[count] = 0;
          } else {
            // Failure.
            fprintf(stderr,
                    "WARNING: Could not get link info for file \"%s\"!\n",
                    file_info->filename);
            free(file_info->link_dest);
            free(file_info);
            free(filename);
            continue;
          }
        } else {
          // Is a regular file.
          file_info->link_dest = NULL;
          // Check that the file is readable by opening it. Easier than to
          // check permissions because that would also require checking if the
          // current USER can open the file.
          FILE *readable_file = fopen(file_info->filename, "rb");
          if (!readable_file) {
            // Cannot open file, so it must be unreadable (at least by the
            // current USER).
            fprintf(stderr, "WARNING: \"%s\" is not readable, skipping!\n",
                    file_info->filename);
            free(file_info->link_dest);
            free(file_info);
            free(filename);
            continue;
          } else {
            fclose(readable_file);
            // fprintf(stderr, "DEBUG: \"%s\" is readable.\n",
            // file_info->filename);
          }
        }
        simple_archiver_list_add(files_list, file_info,
                                 simple_archiver_internal_free_file_info_fn);
        simple_archiver_hash_map_insert(
            &hash_map, &hash_map_sentinel, filename, len - 1,
            simple_archiver_helper_datastructure_cleanup_nop,
            simple_archiver_helper_datastructure_cleanup_nop);
      } else {
        free(filename);
      }
    } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
      // Is a directory.
      __attribute__((cleanup(simple_archiver_list_free)))
      SDArchiverLinkedList *dir_list = simple_archiver_list_init();
      simple_archiver_list_add(
          dir_list, file_path,
          simple_archiver_helper_datastructure_cleanup_nop);
      char *next;
      while (dir_list->count != 0) {
        simple_archiver_list_get(dir_list, list_get_last_fn, &next);
        if (!next) {
          break;
        }
        DIR *dir = opendir(next);
        struct dirent *dir_entry;
        do {
          dir_entry = readdir(dir);
          if (dir_entry) {
            if (strcmp(dir_entry->d_name, ".") == 0 ||
                strcmp(dir_entry->d_name, "..") == 0) {
              continue;
            }
            // fprintf(stderr, "dir entry in %s is %s\n", next,
            // dir_entry->d_name);
            int combined_size = strlen(next) + strlen(dir_entry->d_name) + 2;
            char *combined_path = malloc(combined_size);
            snprintf(combined_path, combined_size, "%s/%s", next,
                     dir_entry->d_name);
            unsigned int valid_idx =
                simple_archiver_parser_internal_get_first_non_current_idx(
                    combined_path);
            if (valid_idx > 0) {
              char *new_path = malloc(combined_size - valid_idx);
              strncpy(new_path, combined_path + valid_idx,
                      combined_size - valid_idx);
              free(combined_path);
              combined_path = new_path;
              combined_size -= valid_idx;
            }
            memset(&st, 0, sizeof(struct stat));
            fstatat(AT_FDCWD, combined_path, &st, AT_SYMLINK_NOFOLLOW);
            if ((st.st_mode & S_IFMT) == S_IFREG ||
                (st.st_mode & S_IFMT) == S_IFLNK) {
              // Is a file or a symbolic link.
              if (simple_archiver_hash_map_get(hash_map, combined_path,
                                               combined_size - 1) == NULL) {
                SDArchiverFileInfo *file_info =
                    malloc(sizeof(SDArchiverFileInfo));
                file_info->filename = combined_path;
                if ((st.st_mode & S_IFMT) == S_IFLNK) {
                  // Is a symlink.
                  file_info->link_dest = malloc(MAX_SYMBOLIC_LINK_SIZE);
                  ssize_t count =
                      readlinkat(AT_FDCWD, combined_path, file_info->link_dest,
                                 MAX_SYMBOLIC_LINK_SIZE - 1);
                  if (count >= MAX_SYMBOLIC_LINK_SIZE - 1) {
                    file_info->link_dest[MAX_SYMBOLIC_LINK_SIZE - 1] = 0;
                  } else if (count > 0) {
                    file_info->link_dest[count] = 0;
                  } else {
                    // Failure.
                    free(file_info->link_dest);
                    free(file_info);
                    free(combined_path);
                    continue;
                  }
                } else {
                  // Is a regular file.
                  file_info->link_dest = NULL;
                  // Check that the file is readable by opening it. Easier than
                  // to check permissions because that would also require
                  // checking if the current USER can open the file.
                  FILE *readable_file = fopen(file_info->filename, "rb");
                  if (!readable_file) {
                    // Cannot open file, so it must be unreadable (at least by
                    // the current USER).
                    fprintf(stderr,
                            "WARNING: \"%s\" is not readable, skipping!\n",
                            file_info->filename);
                    free(file_info->link_dest);
                    free(file_info);
                    free(combined_path);
                    continue;
                  } else {
                    fclose(readable_file);
                    // fprintf(stderr, "DEBUG: \"%s\" is readable.\n",
                    // file_info->filename);
                  }
                }
                simple_archiver_list_add(
                    files_list, file_info,
                    simple_archiver_internal_free_file_info_fn);
                simple_archiver_hash_map_insert(
                    &hash_map, &hash_map_sentinel, combined_path,
                    combined_size - 1,
                    simple_archiver_helper_datastructure_cleanup_nop,
                    simple_archiver_helper_datastructure_cleanup_nop);
              } else {
                free(combined_path);
              }
            } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
              // Is a directory.
              simple_archiver_list_add_front(dir_list, combined_path, NULL);
            } else {
              // Unhandled type. TODO handle this.
              free(combined_path);
            }
          }
        } while (dir_entry != NULL);
        closedir(dir);
        if (simple_archiver_list_remove(dir_list, list_remove_same_str_fn,
                                        next) == 0) {
          break;
        }
      }
    } else {
      // Unhandled type. TODO handle this.
    }
  }
#endif

  for (SDArchiverLLNode *iter = files_list->head->next;
       iter != files_list->tail; iter = iter->next) {
    SDArchiverFileInfo *file_info = iter->data;

    // Remove leading "./" entries from files_list.
    unsigned int idx =
        simple_archiver_parser_internal_get_first_non_current_idx(
            file_info->filename);
    if (idx > 0) {
      int len = strlen(file_info->filename) + 1 - idx;
      char *substr = malloc(len);
      strncpy(substr, file_info->filename + idx, len);
      free(file_info->filename);
      file_info->filename = substr;
    }

    // Remove "./" entries inside the file path.
    int slash_found = 0;
    int dot_found = 0;
    for (idx = strlen(file_info->filename); idx-- > 0;) {
      if (file_info->filename[idx] == '/') {
        if (dot_found) {
          char *temp = simple_archiver_helper_cut_substr(file_info->filename,
                                                         idx + 1, idx + 3);
          free(file_info->filename);
          file_info->filename = temp;
        } else {
          slash_found = 1;
          continue;
        }
      } else if (file_info->filename[idx] == '.' && slash_found) {
        dot_found = 1;
        continue;
      }
      slash_found = 0;
      dot_found = 0;
    }
  }

  return files_list;
}
