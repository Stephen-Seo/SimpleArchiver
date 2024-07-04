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

#include "platforms.h"
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||   \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "data_structures/hash_map.h"
#include "data_structures/linked_list.h"
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

int list_get_last_fn(void *data, void *ud) {
  char **last = ud;
  *last = data;
  return 0;
}

void container_no_free_fn(__attribute__((unused)) void *data) { return; }

int list_remove_same_str_fn(void *data, void *ud) {
  if (strcmp((char *)data, (char *)ud) == 0) {
    return 1;
  }

  return 0;
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

SDArchiverLinkedList *simple_archiver_parsed_to_filenames(
    const SDArchiverParsed *parsed) {
  SDArchiverLinkedList *files_list = simple_archiver_list_init();
  __attribute__((cleanup(simple_archiver_hash_map_free)))
  SDArchiverHashMap *hash_map = simple_archiver_hash_map_init();
  int hash_map_sentinel = 1;
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  for (char **iter = parsed->working_files; iter && *iter; ++iter) {
    struct stat st;
    fstatat(AT_FDCWD, *iter, &st, AT_SYMLINK_NOFOLLOW);
    if ((st.st_mode & S_IFMT) == S_IFLNK) {
      // Is a symbolic link. TODO handle this.
    } else if ((st.st_mode & S_IFMT) == S_IFREG) {
      // Is a regular file.
      int len = strlen(*iter) + 1;
      char *filename = malloc(len);
      strncpy(filename, *iter, len);
      if (simple_archiver_hash_map_get(hash_map, filename, len - 1) == NULL) {
        simple_archiver_list_add(files_list, filename, NULL);
        simple_archiver_hash_map_insert(&hash_map, &hash_map_sentinel, filename,
                                        len - 1, container_no_free_fn,
                                        container_no_free_fn);
      } else {
        free(filename);
      }
    } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
      // Is a directory.
      __attribute__((cleanup(simple_archiver_list_free)))
      SDArchiverLinkedList *dir_list = simple_archiver_list_init();
      simple_archiver_list_add(dir_list, *iter, container_no_free_fn);
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
            printf("dir entry in %s is %s\n", next, dir_entry->d_name);
            int combined_size = strlen(next) + strlen(dir_entry->d_name) + 2;
            char *combined_path = malloc(combined_size);
            snprintf(combined_path, combined_size, "%s/%s", next,
                     dir_entry->d_name);
            fstatat(AT_FDCWD, combined_path, &st, AT_SYMLINK_NOFOLLOW);
            if ((st.st_mode & S_IFMT) == S_IFLNK) {
              // Is a symbolic link. TODO handle this.
            } else if ((st.st_mode & S_IFMT) == S_IFREG) {
              // Is a file.
              if (simple_archiver_hash_map_get(hash_map, combined_path,
                                               combined_size - 1) == NULL) {
                simple_archiver_list_add(files_list, combined_path, NULL);
                simple_archiver_hash_map_insert(
                    &hash_map, &hash_map_sentinel, combined_path,
                    combined_size - 1, container_no_free_fn,
                    container_no_free_fn);
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

  // Remove leading "./" entries from files_list.
  for (SDArchiverLLNode *iter = files_list->head->next;
       iter != files_list->tail; iter = iter->next) {
    unsigned int idx = simple_archiver_parser_internal_filename_idx(iter->data);
    if (idx > 0) {
      int len = strlen((char *)iter->data) + 1 - idx;
      char *substr = malloc(len);
      strncpy(substr, (char *)iter->data + idx, len);
      free(iter->data);
      iter->data = substr;
    }
  }

  return files_list;
}
