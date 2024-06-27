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
 * `main.c` is the entry-point of this software/program.
 */

#include <stdio.h>

#include "parser.h"

int main(int argc, const char **argv) {
  simple_archiver_print_usage();

  __attribute__((cleanup(simple_archiver_free_parsed)))
  SDArchiverParsed parsed = simple_archiver_create_parsed();

  simple_archiver_parse_args(argc, argv, &parsed);

  return 0;
}
