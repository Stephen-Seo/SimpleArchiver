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
 * `linear_congruential_gen.c` is the source for the linear congruential
 * generator algorithm.
 */

#include "linear_congruential_gen.h"

unsigned long long simple_archiver_algo_lcg(unsigned long long seed,
                                            unsigned long long a,
                                            unsigned long long c) {
  // "m" is implicity 2^64.
  return seed * a + c;
}

unsigned long long simple_archiver_algo_lcg_defaults(unsigned long long seed) {
  // "m" is implicity 2^64.
  return seed * SC_ALGO_LCG_DEFAULT_A + SC_ALGO_LCG_DEFAULT_C;
}
