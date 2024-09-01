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
// `linear_congruential_gen.h` is the header for the linear congruential
// generator algorithm.

#ifndef SEODISPARATE_COM_ALGORITHMS_LINEAR_CONGRUENTIAL_GEN_H_
#define SEODISPARATE_COM_ALGORITHMS_LINEAR_CONGRUENTIAL_GEN_H_

#define SC_ALGO_LCG_DEFAULT_A 0x9ABD
#define SC_ALGO_LCG_DEFAULT_C 0x2A9A9A9

unsigned long long simple_archiver_algo_lcg(unsigned long long seed,
                                            unsigned long long a,
                                            unsigned long long c);

unsigned long long simple_archiver_algo_lcg_defaults(unsigned long long seed);

#endif
