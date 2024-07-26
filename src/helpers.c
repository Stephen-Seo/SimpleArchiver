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
 * `helpers.c` is the source for helpful/utility functions.
 */

#include "helpers.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "platforms.h"
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

void simple_archiver_internal_free_c_string(char **str) {
  if (str && *str) {
    free(*str);
    *str = NULL;
  }
}

int simple_archiver_helper_is_big_endian(void) {
  union {
    uint32_t i;
    char c[4];
  } bint = {0x01020304};

  return bint.c[0] == 1 ? 1 : 0;
}

void simple_archiver_helper_16_bit_be(uint16_t *value) {
  if (simple_archiver_helper_is_big_endian() == 0) {
    unsigned char c = ((unsigned char *)value)[0];
    ((unsigned char *)value)[0] = ((unsigned char *)value)[1];
    ((unsigned char *)value)[1] = c;
  }
}

void simple_archiver_helper_32_bit_be(uint32_t *value) {
  if (simple_archiver_helper_is_big_endian() == 0) {
    for (unsigned int i = 0; i < 2; ++i) {
      unsigned char c = ((unsigned char *)value)[i];
      ((unsigned char *)value)[i] = ((unsigned char *)value)[3 - i];
      ((unsigned char *)value)[3 - i] = c;
    }
  }
}

void simple_archiver_helper_64_bit_be(uint64_t *value) {
  if (simple_archiver_helper_is_big_endian() == 0) {
    for (unsigned int i = 0; i < 4; ++i) {
      unsigned char c = ((unsigned char *)value)[i];
      ((unsigned char *)value)[i] = ((unsigned char *)value)[7 - i];
      ((unsigned char *)value)[7 - i] = c;
    }
  }
}

char **simple_archiver_helper_cmd_string_to_argv(const char *cmd) {
  unsigned int capacity = 16;
  unsigned int idx = 0;
  // Size of every pointer is the same, so using size of (void*) should be ok.
  char **args = malloc(sizeof(void *) * capacity);
  memset(args, 0, sizeof(void *) * capacity);

  unsigned int word_capacity = 16;
  unsigned int word_idx = 0;
  char *word = malloc(word_capacity);
  memset(word, 0, word_capacity);
  for (const char *c = cmd; *c != 0; ++c) {
    if (isspace(*c)) {
      if (word_idx > 0) {
        if (idx >= capacity) {
          capacity *= 2;
          args = realloc(args, sizeof(void *) * capacity);
        }
        args[idx] = malloc(word_idx + 1);
        memcpy(args[idx], word, word_idx);
        args[idx][word_idx] = 0;
        ++idx;
        word_idx = 0;
      }
    } else {
      if (word_idx >= word_capacity) {
        word_capacity *= 2;
        word = realloc(word, word_capacity);
      }
      word[word_idx++] = *c;
    }
  }
  if (word_idx > 0) {
    if (idx >= capacity) {
      capacity *= 2;
      args = realloc(args, sizeof(void *) * capacity);
    }
    args[idx] = malloc(word_idx + 1);
    memcpy(args[idx], word, word_idx);
    args[idx][word_idx] = 0;
    ++idx;
    word_idx = 0;
  }

  free(word);

  if (idx >= capacity) {
    args = realloc(args, sizeof(void *) * (capacity + 1));
    args[capacity] = NULL;
  }

  return args;
}

void simple_archiver_helper_cmd_string_argv_free(char **argv_strs) {
  if (argv_strs) {
    for (char **iter = argv_strs; *iter != 0; ++iter) {
      free(*iter);
    }
    free(argv_strs);
  }
}

void simple_archiver_helper_cmd_string_argv_free_ptr(char ***argv_strs) {
  if (argv_strs) {
    simple_archiver_helper_cmd_string_argv_free(*argv_strs);
    *argv_strs = NULL;
  }
}

int simple_archiver_helper_make_dirs(const char *file_path) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  __attribute__((
      cleanup(simple_archiver_internal_free_c_string))) char *path_dup =
      strdup(file_path);
  if (!path_dup) {
    return 3;
  }
  const char *dir = dirname(path_dup);
  if (strcmp(dir, "/") == 0 || strcmp(dir, ".") == 0) {
    // At root.
    return 0;
  }

  int dir_fd = open(dir, O_RDONLY | O_DIRECTORY);
  if (dir_fd == -1) {
    if (errno == ENOTDIR) {
      // Error, somehow got non-dir in path.
      return 1;
    } else {
      // Directory does not exist. Check parent dir first.
      int ret = simple_archiver_helper_make_dirs(dir);
      if (ret != 0) {
        return ret;
      }
      // Now make dir.
      ret = mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
      if (ret != 0) {
        // Error.
        return 2;
      }
    }
  } else {
    // Exists.
    close(dir_fd);
  }

  return 0;
#else
  return 1;
#endif
}

char *simple_archiver_helper_cut_substr(const char *s, unsigned int start_idx,
                                        unsigned int end_idx) {
  unsigned int s_len = strlen(s);
  if (start_idx > end_idx || start_idx >= s_len || end_idx > s_len) {
    return NULL;
  } else if (end_idx == s_len) {
    if (start_idx == 0) {
      return NULL;
    }
    char *new_s = malloc(start_idx + 1);
    strncpy(new_s, s, start_idx + 1);
    new_s[start_idx] = 0;
    return new_s;
  } else if (start_idx == 0) {
    char *new_s = malloc(s_len - end_idx + 1);
    strncpy(new_s, s + end_idx, s_len - end_idx + 1);
    return new_s;
  } else {
    char *new_s = malloc(start_idx + s_len - end_idx + 1);
    strncpy(new_s, s, start_idx);
    strncpy(new_s + start_idx, s + end_idx, s_len - end_idx + 1);
    return new_s;
  }
}
