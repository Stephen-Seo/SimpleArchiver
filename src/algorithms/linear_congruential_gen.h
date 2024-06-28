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
 * `linear_congruential_gen.h` is the header for the linear congruential
 * generator algorithm.
 */

#ifndef SEODISPARATE_COM_ALGORITHMS_LINEAR_CONGRUENTIAL_GEN_H_
#define SEODISPARATE_COM_ALGORITHMS_LINEAR_CONGRUENTIAL_GEN_H_

#define SC_ALGO_LCG_DEFAULT_A 0x9ABD
#define SC_ALGO_LCG_DEFAULT_C 0x2A9A9A9

unsigned long long simple_archiver_algo_lcg(unsigned long long seed,
                                            unsigned long long a,
                                            unsigned long long c);

#endif
