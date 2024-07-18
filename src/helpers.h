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
 * `helpers.h` is the header for helpful/utility functions.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_HELPERS_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_HELPERS_H_

#include <stdint.h>

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

#endif
