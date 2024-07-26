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
 * `parser_internal.h` is the header for parsing args with internal functions.
 */

#ifndef SEODISPARATE_COM_SIMPLE_ARCHIVER_PARSER_INTERNAL_H_
#define SEODISPARATE_COM_SIMPLE_ARCHIVER_PARSER_INTERNAL_H_

#include "parser.h"

unsigned int simple_archiver_parser_internal_get_first_non_current_idx(
    const char *filename);

#endif
