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
 * `parser.h` is the header for parsing args.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_PARSER_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_PARSER_H_

typedef struct SDArchiverParsed {
  /// Each bit is a flag.
  /// 0b0 - is creating.
  /// 0b1 - is extracting.
  unsigned int flags;
  /// Null-terminated string.
  char *filename;
  /// Null-terminated string.
  char *compressor;
  /// Null-terminated string.
  char *decompressor;
  /// Null-terminated strings in array of strings.
  /// Last entry should be NULL.
  /// Not used when extracting.
  char **working_files;
} SDArchiverParsed;

extern void simple_archiver_print_usage(void);

extern SDArchiverParsed simple_archiver_create_parsed(void);

/// Expects the user to pass a pointer to an SDArchiverParsed.
/// This means the user should have a SDArchiverParsed variable
/// and it should be passed with e.g. "&var".
extern int simple_archiver_parse_args(int argc, const char **argv,
                                      SDArchiverParsed *out);

extern void simple_archiver_free_parsed(SDArchiverParsed *parsed);

#endif
