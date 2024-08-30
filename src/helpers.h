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
// `helpers.h` is the header for helpful/utility functions.

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_HELPERS_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_HELPERS_H_

#include <stdint.h>
#include <stdio.h>

static const unsigned int MAX_SYMBOLIC_LINK_SIZE = 512;

/// Returns non-zero if this system is big-endian.
int simple_archiver_helper_is_big_endian(void);

/// Swaps value from/to big-endian. Nop on big-endian systems.
void simple_archiver_helper_16_bit_be(uint16_t *value);

/// Swaps value from/to big-endian. Nop on big-endian systems.
void simple_archiver_helper_32_bit_be(uint32_t *value);

/// Swaps value from/to big-endian. Nop on big-endian systems.
void simple_archiver_helper_64_bit_be(uint64_t *value);

/// Returns a array of c-strings on success, NULL on error.
/// The returned array must be free'd with
/// simple_archiver_helper_cmd_string_argv_free(...).
char **simple_archiver_helper_cmd_string_to_argv(const char *cmd);

void simple_archiver_helper_cmd_string_argv_free(char **argv_strs);
void simple_archiver_helper_cmd_string_argv_free_ptr(char ***argv_strs);

/// Returns zero on success.
int simple_archiver_helper_make_dirs(const char *file_path);

/// Returns non-NULL on success.
/// Must be free'd with "free()" if non-NULL.
/// start_idx is inclusive and end_idx is exclusive.
char *simple_archiver_helper_cut_substr(const char *s, unsigned int start_idx,
                                        unsigned int end_idx);

unsigned int simple_archiver_helper_num_digits(unsigned int value);

void simple_archiver_helper_cleanup_FILE(FILE **fd);
void simple_archiver_helper_cleanup_malloced(void **data);
void simple_archiver_helper_cleanup_c_string(char **str);
void simple_archiver_helper_cleanup_chdir_back(char **original);

void simple_archiver_helper_datastructure_cleanup_nop(void *unused);

#endif
