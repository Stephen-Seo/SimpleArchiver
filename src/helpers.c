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
