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
// `helpers.c` is the source for helpful/utility functions.

#include "helpers.h"

#include <ctype.h>
#include <stdint.h>
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

void simple_archiver_helper_cleanup_uint32(uint32_t **uint) {
  if (uint && *uint) {
    free(*uint);
    *uint = NULL;
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
      ret = mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
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
      ret = chmod(dir, perms);
      if (ret != 0) {
        // Error.
        return 5;
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

const char * simple_archiver_helper_prefix_result_str(
    SAHelperPrefixValResult result) {
  switch (result) {
  case SAHPrefixVal_OK:
    return "OK";
  case SAHPrefixVal_NULL:
    return "Prefix is NULL";
  case SAHPrefixVal_ZERO_LEN:
    return "Prefix has zero length";
  case SAHPrefixVal_ROOT:
    return "Prefix starts with slash (root)";
  case SAHPrefixVal_DOUBLE_SLASH:
    return "Prefix has multiple consecutive slashes";
  default:
    return "Unknown";
  }
}

SAHelperPrefixValResult simple_archiver_helper_validate_prefix(
    const char *prefix) {
  if (!prefix) {
    return SAHPrefixVal_NULL;
  }
  const unsigned long length = strlen(prefix);
  if (length == 0) {
    return SAHPrefixVal_ZERO_LEN;
  } else if (prefix[0] == '/') {
    return SAHPrefixVal_ROOT;
  }

  uint_fast8_t was_slash = 0;
  for (unsigned long idx = 0; idx < length; ++idx) {
    if (prefix[idx] == '/') {
      if (was_slash) {
        return SAHPrefixVal_DOUBLE_SLASH;
      }
      was_slash = 1;
    } else {
      was_slash = 0;
    }
  }

  return SAHPrefixVal_OK;
}

uint16_t simple_archiver_helper_str_slash_count(const char *str) {
  uint16_t count = 0;

  const unsigned long length = strlen(str);
  for (unsigned long idx = 0; idx < length; ++idx) {
    if (str[idx] == '/') {
      ++count;
    }
  }

  return count;
}

char *simple_archiver_helper_insert_prefix_in_link_path(const char *prefix,
                                                        const char *link,
                                                        const char *path) {
  if (!prefix) {
    return NULL;
  }
  uint16_t prefix_slash_count = simple_archiver_helper_str_slash_count(prefix);
  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *cwd = getcwd(NULL, 0);
  unsigned long cwd_length = strlen(cwd);
  if (cwd[cwd_length - 1] != '/') {
    // Ensure the cwd ends with a '/'.
    char *new_cwd = malloc(cwd_length + 2);
    memcpy(new_cwd, cwd, cwd_length);
    new_cwd[cwd_length] = '/';
    new_cwd[cwd_length + 1] = 0;
    free(cwd);
    cwd = new_cwd;
    ++cwd_length;
  }
  const unsigned long prefix_length = strlen(prefix);
  const unsigned long link_length = strlen(link);
  const unsigned long path_length = strlen(path);
  if (path[0] == '/') {
    // Dealing with an absolute path.

    // First check if "path" is in archive.
    size_t diff_idx = 0;
    for (; cwd[diff_idx] == path[diff_idx]
           && diff_idx < cwd_length
           && diff_idx < path_length;
         ++diff_idx);

    if (diff_idx == cwd_length) {
      // "path" is in archive.
      char *result_path = malloc(path_length + prefix_length + 1);
      // Part of path matching cwd.
      memcpy(result_path, path, cwd_length);
      // Insert prefix.
      memcpy(result_path + cwd_length, prefix, prefix_length);
      // Rest of path.
      memcpy(result_path + cwd_length + prefix_length,
             path + cwd_length,
             path_length - cwd_length);
      result_path[path_length + prefix_length] = 0;
      return result_path;
    } else {
      // "path" is not in archive, no need to insert prefix.
      return strdup(path);
    }
  } else {
    // Dealing with a relative path.

    // First check if "path" is in archive.
    const unsigned long filename_full_length = cwd_length + link_length;
    __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
    char *filename_full = malloc(filename_full_length + 1);
    memcpy(filename_full, cwd, cwd_length);
    memcpy(filename_full + cwd_length, link, link_length);
    filename_full[cwd_length + link_length] = 0;

    size_t diff_idx = 0;
    for (; filename_full[diff_idx] == cwd[diff_idx]
           && diff_idx < filename_full_length
           && diff_idx < cwd_length;
         ++diff_idx);
    int32_t level = simple_archiver_helper_str_slash_count(
      filename_full + diff_idx);
    const int32_t level_copy = level;

    size_t prev_start_idx = 0;
    for (size_t path_idx = 0; path_idx < path_length; ++path_idx) {
      if (path[path_idx] == '/') {
        if (path_idx - prev_start_idx == 2
          && path[path_idx - 2] == '.'
          && path[path_idx - 1] == '.') {
          --level;
          if (level < 0) {
            break;
          }
        } else {
          ++level;
        }
        prev_start_idx = path_idx + 1;
      }
    }

    if (level >= 0) {
      // Relative path is in cwd, no need to insert prefix.
      return strdup(path);
    } else {
      // Relative path refers to something outside of archive, "insert" prefix.
      char *result = malloc(path_length + 1 + 3 * (size_t)prefix_slash_count);
      memcpy(result, path, path_length);
      level = level_copy;
      size_t start_side_idx = 0;
      for (size_t idx = 0; idx < path_length; ++idx) {
        if (path[idx] == '/') {
          if (idx - start_side_idx == 2
                && path[start_side_idx] == '.'
                && path[start_side_idx + 1] == '.') {
            --level;
            if (level == -1) {
              char *buf = malloc(path_length - idx - 1);
              memcpy(buf, result + idx + 1, path_length - idx - 1);
              for (size_t l_idx = 0;
                   l_idx < (size_t)prefix_slash_count;
                   ++l_idx) {
                memcpy(result + idx + 1 + l_idx * 3, "../", 3);
              }
              memcpy(result + idx + 1 + (size_t)prefix_slash_count * 3,
                     buf,
                     path_length - idx - 1);
              free(buf);
              result[path_length + 3 * (size_t)prefix_slash_count] = 0;
              return result;
            }
          }
          start_side_idx = idx + 1;
        }
      }
      free(result);
      return NULL;
    }
  }
}

char *simple_archiver_helper_real_path_to_name(const char *filename) {
  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *filename_copy = strdup(filename);
  char *filename_dir = dirname(filename_copy);
  if (!filename_dir) {
    return NULL;
  }

  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *filename_copy2 = strdup(filename);
  char *filename_base = basename(filename_copy2);
  if (!filename_base) {
    return NULL;
  }
  const unsigned long basename_length = strlen(filename_base);

  // Get realpath to dirname.
  __attribute__((cleanup(simple_archiver_helper_cleanup_c_string)))
  char *dir_realpath = realpath(filename_dir, NULL);
  if (!dir_realpath) {
    return NULL;
  }
  const unsigned long dir_realpath_length = strlen(dir_realpath);

  // Concatenate dirname-realpath and basename.
  if (dir_realpath[dir_realpath_length - 1] != '/') {
    char *result = malloc(dir_realpath_length + basename_length + 2);
    memcpy(result, dir_realpath, dir_realpath_length);
    result[dir_realpath_length] = '/';
    memcpy(result + dir_realpath_length + 1, filename_base, basename_length);
    result[dir_realpath_length + 1 + basename_length] = 0;
    return result;
  } else {
    char *result = malloc(dir_realpath_length + basename_length + 1);
    memcpy(result, dir_realpath, dir_realpath_length);
    memcpy(result + dir_realpath_length, filename_base, basename_length);
    result[dir_realpath_length + basename_length] = 0;
    return result;
  }
}

SAHelperStringParts simple_archiver_helper_string_parts_init(void) {
  SAHelperStringParts parts;
  parts.parts = simple_archiver_list_init();
  return parts;
}

void simple_archiver_helper_string_parts_free(SAHelperStringParts *string_parts)
{
  if (string_parts && string_parts->parts) {
    simple_archiver_list_free(&string_parts->parts);
  }
}

void simple_archiver_helper_string_part_free(void *v_part) {
  SAHelperStringPart *part = v_part;
  if (part) {
    if (part->buf) {
      free(part->buf);
    }
    free(part);
  }
}

void simple_archiver_helper_string_parts_add(SAHelperStringParts string_parts,
                                             const char *c_string) {
  if (!c_string || c_string[0] == 0) {
    return;
  }
  SAHelperStringPart *part = malloc(sizeof(SAHelperStringPart));
  part->buf = strdup(c_string);
  part->size = strlen(part->buf);
  simple_archiver_list_add(string_parts.parts,
                           part,
                           simple_archiver_helper_string_part_free);
}

char *simple_archiver_helper_string_parts_combine(
    SAHelperStringParts string_parts) {
  size_t size = 0;
  for (SDArchiverLLNode *node = string_parts.parts->head->next;
       node->next != NULL;
       node = node->next) {
    if (node->data) {
      SAHelperStringPart *part = node->data;
      size += part->size;
    }
  }
  char *buf = malloc(size + 1);
  size_t idx = 0;
  for (SDArchiverLLNode *node = string_parts.parts->head->next;
       node->next != NULL;
       node = node->next) {
    if (node->data) {
      SAHelperStringPart *part = node->data;
      memcpy(buf + idx, part->buf, part->size);
      idx += part->size;
    }
  }
  buf[size] = 0;
  return buf;
}

uint_fast8_t simple_archiver_helper_string_contains(const char *cstring,
                                                    const char *contains,
                                                    uint_fast8_t case_i) {
  const size_t cstring_size = strlen(cstring);
  const size_t contains_size = strlen(contains);
  size_t contains_match_start = 0;
  size_t contains_match_idx = 0;
  for (size_t idx = 0; idx < cstring_size; ++idx) {
    char cstring_next = cstring[idx];
    char contains_next = contains[contains_match_idx];
    if (case_i) {
      if (cstring_next >= 'A' && cstring_next <= 'Z') {
        cstring_next += 0x20;
      }
      if (contains_next >= 'A' && contains_next <= 'Z') {
        contains_next += 0x20;
      }
    }

    if (cstring_next == contains_next) {
      if (contains_match_idx == 0) {
        contains_match_start = idx;
      }
      ++contains_match_idx;
      if (contains_match_idx == contains_size) {
        return 1;
      }
    } else {
      if (contains_match_idx != 0) {
        idx = contains_match_start;
      }
      contains_match_idx = 0;
    }
  }

  return 0;
}

uint_fast8_t simple_archiver_helper_string_starts(const char *cstring,
                                                  const char *starts,
                                                  uint_fast8_t case_i) {
  const size_t cstring_len = strlen(cstring);
  const size_t starts_len = strlen(starts);
  size_t starts_match_idx = 0;

  for (size_t idx = 0; idx < cstring_len; ++idx) {
    char cstring_next = cstring[idx];
    char starts_next = starts[starts_match_idx];
    if (case_i) {
      if (cstring_next >= 'A' && cstring_next <= 'Z') {
        cstring_next += 0x20;
      }
      if (starts_next >= 'A' && starts_next <= 'Z') {
        starts_next += 0x20;
      }
    }

    if (cstring_next == starts_next) {
      if (starts_match_idx == 0) {
        if (idx != 0) {
          return 0;
        }
      }
      ++starts_match_idx;
      if (starts_match_idx == starts_len) {
        return 1;
      }
    } else {
      return 0;
    }
  }

  return 0;
}

uint_fast8_t simple_archiver_helper_string_ends(const char *cstring,
                                                const char *ends,
                                                uint_fast8_t case_i) {
  const size_t cstring_len = strlen(cstring);
  const size_t ends_len = strlen(ends);
  size_t ends_idx = 0;

  for (size_t idx = cstring_len - ends_len; idx < cstring_len; ++idx) {
    char cstring_next = cstring[idx];
    char ends_next = ends[ends_idx];
    if (case_i) {
      if (cstring_next >= 'A' && cstring_next <= 'Z') {
        cstring_next += 0x20;
      }
      if (ends_next >= 'A' && ends_next <= 'Z') {
        ends_next += 0x20;
      }
    }

    if (cstring_next == ends_next) {
      ++ends_idx;
      if (ends_idx == ends_len) {
        return 1;
      }
    } else {
      return 0;
    }
  }

  return 0;
}

uint_fast8_t simple_archiver_helper_string_allowed_lists(
    const char *cstring,
    uint_fast8_t case_i,
    const SDArchiverLinkedList *w_contains,
    const SDArchiverLinkedList *w_begins,
    const SDArchiverLinkedList *w_ends,
    const SDArchiverLinkedList *b_contains,
    const SDArchiverLinkedList *b_begins,
    const SDArchiverLinkedList *b_ends) {
  if (w_contains) {
    for (const SDArchiverLLNode *node = w_contains->head->next;
        node != w_contains->tail;
        node = node->next) {
      if (node->data) {
        if (!simple_archiver_helper_string_contains(
            cstring, node->data, case_i)) {
          return 0;
        }
      }
    }
  }
  if (w_begins) {
    for (const SDArchiverLLNode *node = w_begins->head->next;
        node != w_begins->tail;
        node = node->next) {
      if (node->data) {
        if (!simple_archiver_helper_string_starts(
            cstring, node->data, case_i)) {
          return 0;
        }
      }
    }
  }
  if (w_ends) {
    for (const SDArchiverLLNode *node = w_ends->head->next;
        node != w_ends->tail;
        node = node->next) {
      if (node->data) {
        if (!simple_archiver_helper_string_ends(cstring, node->data, case_i)) {
          return 0;
        }
      }
    }
  }

  if (b_contains) {
    for (const SDArchiverLLNode *node = b_contains->head->next;
        node != b_contains->tail;
        node = node->next) {
      if (node->data) {
        if (simple_archiver_helper_string_contains(
            cstring, node->data, case_i)) {
          return 0;
        }
      }
    }
  }
  if (b_begins) {
    for (const SDArchiverLLNode *node = b_begins->head->next;
        node != b_begins->tail;
        node = node->next) {
      if (node->data) {
        if (simple_archiver_helper_string_starts(cstring, node->data, case_i)) {
          return 0;
        }
      }
    }
  }
  if (b_ends) {
    for (const SDArchiverLLNode *node = b_ends->head->next;
        node != b_ends->tail;
        node = node->next) {
      if (node->data) {
        if (simple_archiver_helper_string_ends(cstring, node->data, case_i)) {
          return 0;
        }
      }
    }
  }

  return 1;
}
