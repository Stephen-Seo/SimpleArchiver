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
// `helpers.c` is the source for helpful/utility functions.

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

void simple_archiver_helper_cleanup_FILE(FILE **fd) {
  if (fd && *fd) {
    fclose(*fd);
    *fd = NULL;
  }
}

void simple_archiver_helper_cleanup_malloced(void **data) {
  if (data && *data) {
    free(*data);
    *data = NULL;
  }
}

void simple_archiver_helper_cleanup_c_string(char **str) {
  if (str && *str) {
    free(*str);
    *str = NULL;
  }
}

void simple_archiver_helper_cleanup_chdir_back(char **original) {
  if (original && *original) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||   \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN
    __attribute__((unused)) int unused_ret = chdir(*original);
#endif
    free(*original);
    *original = NULL;
  }
}

void simple_archiver_helper_datastructure_cleanup_nop(
    __attribute__((unused)) void *unused) {}

int simple_archiver_helper_is_big_endian(void) {
  union {
    uint32_t i;
    char c[4];
  } bint = {0x01020304};

  return bint.c[0] == 1 ? 1 : 0;
}

void simple_archiver_helper_16_bit_be(uint16_t *value) {
  if (simple_archiver_helper_is_big_endian() == 0) {
    uint8_t c = ((uint8_t *)value)[0];
    ((uint8_t *)value)[0] = ((uint8_t *)value)[1];
    ((uint8_t *)value)[1] = c;
  }
}

void simple_archiver_helper_32_bit_be(uint32_t *value) {
  if (simple_archiver_helper_is_big_endian() == 0) {
    for (uint32_t i = 0; i < 2; ++i) {
      uint8_t c = ((uint8_t *)value)[i];
      ((uint8_t *)value)[i] = ((uint8_t *)value)[3 - i];
      ((uint8_t *)value)[3 - i] = c;
    }
  }
}

void simple_archiver_helper_64_bit_be(uint64_t *value) {
  if (simple_archiver_helper_is_big_endian() == 0) {
    for (uint32_t i = 0; i < 4; ++i) {
      uint8_t c = ((uint8_t *)value)[i];
      ((uint8_t *)value)[i] = ((uint8_t *)value)[7 - i];
      ((uint8_t *)value)[7 - i] = c;
    }
  }
}

char **simple_archiver_helper_cmd_string_to_argv(const char *cmd) {
  uint32_t capacity = 16;
  uint32_t idx = 0;
  // Size of every pointer is the same, so using size of (void*) should be ok.
  char **args = malloc(sizeof(void *) * capacity);
  memset(args, 0, sizeof(void *) * capacity);

  uint32_t word_capacity = 16;
  uint32_t word_idx = 0;
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
      cleanup(simple_archiver_helper_cleanup_c_string))) char *path_dup =
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

int simple_archiver_helper_make_dirs_perms(const char *file_path,
                                           uint32_t perms,
                                           uint32_t uid,
                                           uint32_t gid) {
#if SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_COSMOPOLITAN || \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_MAC ||          \
    SIMPLE_ARCHIVER_PLATFORM == SIMPLE_ARCHIVER_PLATFORM_LINUX
  __attribute__((
      cleanup(simple_archiver_helper_cleanup_c_string))) char *path_dup =
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
      int ret = simple_archiver_helper_make_dirs_perms(dir,
                                                       perms,
                                                       uid,
                                                       gid);
      if (ret != 0) {
        return ret;
      }
      // Now make dir.
      ret = mkdir(dir, perms);
      if (ret != 0) {
        // Error.
        return 2;
      }
      if (geteuid() == 0 && chown(dir, uid, gid) != 0) {
        // Error.
        return 4;
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

char *simple_archiver_helper_cut_substr(const char *s, size_t start_idx,
                                        size_t end_idx) {
  size_t s_len = strlen(s);
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

size_t simple_archiver_helper_num_digits(size_t value) {
  size_t digits = 0;
  do {
    ++digits;
    value /= 10;
  } while (value != 0);

  return digits;
}
