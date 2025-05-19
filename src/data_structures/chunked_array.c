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
// `chunked_array.c` is the source for a chunked-array implementation.

#include "chunked_array.h"

// Standard library includes.
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
# include <stdio.h>
# include <inttypes.h>
#endif

SDArchiverChunkedArr simple_archiver_chunked_array_init(
    void (*elem_cleanup_fn)(void*), uint32_t elem_size) {

  SDArchiverChunkedArr chunked_array =
    (SDArchiverChunkedArr)
      {.chunk_count=1,
       .last_size=0,
       .elem_size=elem_size,
       .elem_cleanup_fn=elem_cleanup_fn,
       .array=malloc(sizeof(void*))
      };
  chunked_array.array[0] =
    malloc(elem_size * SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE);

  return chunked_array;
}

void simple_archiver_chunked_array_cleanup(
    SDArchiverChunkedArr *chunked_array) {

  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return;
  }

  for (uint64_t idx = 0; idx < chunked_array->chunk_count; ++idx) {
    if (idx + 1 != chunked_array->chunk_count) {
      for (size_t inner_idx = 0;
           inner_idx < SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE;
           ++inner_idx) {

        if (chunked_array->elem_cleanup_fn) {
          chunked_array->elem_cleanup_fn(
            (char*)chunked_array->array[idx]
            + inner_idx * chunked_array->elem_size);
        }
      }
    } else {
      for (size_t inner_idx = 0;
           inner_idx < chunked_array->last_size;
           ++inner_idx) {

        if (chunked_array->elem_cleanup_fn) {
          chunked_array->elem_cleanup_fn(
            (char*)chunked_array->array[idx]
            + inner_idx * chunked_array->elem_size);
        }
      }
    }
    free(chunked_array->array[idx]);
  }
  free(chunked_array->array);
  chunked_array->array = 0;
  chunked_array->chunk_count = 0;
}

void *simple_archiver_chunked_array_at(SDArchiverChunkedArr *chunked_array,
                                       uint64_t idx) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return 0;
  }

  const uint64_t chunk_idx = idx / SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE;
  const uint64_t inner_idx = idx % SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE;

  if (chunk_idx >= chunked_array->chunk_count) {
    return 0;
  } else if (chunk_idx + 1 == chunked_array->chunk_count
             && inner_idx >= chunked_array->last_size) {
    return 0;
  }

  return (char*)chunked_array->array[chunk_idx]
           + inner_idx * chunked_array->elem_size;
}

const void *simple_archiver_chunked_array_at_const(
    const SDArchiverChunkedArr *chunked_array, uint64_t idx) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return 0;
  }

  const uint64_t chunk_idx = idx / SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE;
  const uint64_t inner_idx = idx % SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE;

  if (chunk_idx >= chunked_array->chunk_count) {
    return 0;
  } else if (chunk_idx + 1 == chunked_array->chunk_count
             && inner_idx >= chunked_array->last_size) {
    return 0;
  }

  return (char*)chunked_array->array[chunk_idx]
           + inner_idx * chunked_array->elem_size;
}

int simple_archiver_chunked_array_push(SDArchiverChunkedArr *chunked_array,
                                       void *to_copy) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return 1;
  }

  const uint64_t chunk_idx = chunked_array->chunk_count - 1;
  const uint64_t inner_idx = chunked_array->last_size;

  void *elem_ptr = (char*)chunked_array->array[chunk_idx]
                     + inner_idx * chunked_array->elem_size;

  memcpy(elem_ptr, to_copy, chunked_array->elem_size);

  ++chunked_array->last_size;

  if (chunked_array->last_size >= SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE) {
    void **new_array = malloc(sizeof(void*) * (chunked_array->chunk_count + 1));
    memcpy(new_array,
           chunked_array->array,
           chunked_array->chunk_count * sizeof(void*));

    new_array[chunked_array->chunk_count] =
      malloc(chunked_array->elem_size
             * SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE);

    ++chunked_array->chunk_count;
    chunked_array->last_size = 0;

    free(chunked_array->array);
    chunked_array->array = new_array;
  }

  return 0;
}

void *simple_archiver_chunked_array_pop(SDArchiverChunkedArr *chunked_array,
                                        int no_cleanup) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return 0;
  }

  void *ret = malloc(chunked_array->elem_size);

  uint64_t chunk_idx;
  uint64_t inner_idx;

  if (chunked_array->last_size == 0) {
    if (chunked_array->chunk_count <= 1) {
      free(ret);
      return 0;
    }

    chunk_idx = chunked_array->chunk_count - 2;
    inner_idx = SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE - 1;

    void **new_array = malloc(sizeof(void*) * chunked_array->chunk_count - 1);
    memcpy(new_array,
           chunked_array->array,
           sizeof(void*) * chunked_array->chunk_count - 1);
    free(chunked_array->array[chunked_array->chunk_count - 1]);
    free(chunked_array->array);
    chunked_array->array = new_array;

    --chunked_array->chunk_count;
    chunked_array->last_size = SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE - 1;
  } else {
    chunk_idx = chunked_array->chunk_count - 1;
    inner_idx = chunked_array->last_size - 1;

    --chunked_array->last_size;
  }

  memcpy(ret,
         (char*)chunked_array->array[chunk_idx]
           + inner_idx * chunked_array->elem_size,
         chunked_array->elem_size);

  if (no_cleanup == 0 && chunked_array->elem_cleanup_fn) {
    chunked_array->elem_cleanup_fn(
      (char*)chunked_array->array[chunked_array->chunk_count - 1]
        + chunked_array->last_size * chunked_array->elem_size);
  }

  return ret;
}

int simple_archiver_chunked_array_pop_no_ret(
    SDArchiverChunkedArr *chunked_array) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return 0;
  }

  if (chunked_array->last_size == 0) {
    if (chunked_array->chunk_count <= 1) {
      return 0;
    }

    void **new_array = malloc(sizeof(void*) * chunked_array->chunk_count - 1);
    memcpy(new_array,
           chunked_array->array,
           sizeof(void*) * chunked_array->chunk_count - 1);
    free(chunked_array->array[chunked_array->chunk_count - 1]);
    free(chunked_array->array);
    chunked_array->array = new_array;

    --chunked_array->chunk_count;
    chunked_array->last_size = SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE - 1;
  } else {
    --chunked_array->last_size;
  }

  if (chunked_array->elem_cleanup_fn) {
    chunked_array->elem_cleanup_fn(
      (char*)chunked_array->array[chunked_array->chunk_count - 1]
        + chunked_array->last_size * chunked_array->elem_size);
  }

  return 1;
}

void simple_archiver_chunked_array_clear(SDArchiverChunkedArr *chunked_array) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return;
  }

  void (*elem_cleanup)(void *) = chunked_array->elem_cleanup_fn;
  uint32_t elem_size = chunked_array->elem_size;
  simple_archiver_chunked_array_cleanup(chunked_array);
  *chunked_array = simple_archiver_chunked_array_init(elem_cleanup, elem_size);
}

uint64_t simple_archiver_chunked_array_size(
    const SDArchiverChunkedArr *chunked_array) {
  if (chunked_array->chunk_count == 0 || !chunked_array->array) {
    return 0;
  }

  return (chunked_array->chunk_count - 1)
           * SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE
         + chunked_array->last_size;
}

void *simple_archiver_chunked_array_top(SDArchiverChunkedArr *ca) {
  if (!ca
      || ca->chunk_count == 0
      || !ca->array
      || (ca->chunk_count == 1 && ca->last_size == 0)) {
    return NULL;
  }

  if (ca->last_size == 0) {
    if (ca->chunk_count < 2) {
      return NULL;
    }
    char *ptr = ca->array[ca->chunk_count - 2];
    ptr += (SD_SA_DS_CHUNKED_ARR_DEFAULT_CHUNK_SIZE - 1) * ca->elem_size;
    return ptr;
  } else {
    char *ptr = ca->array[ca->chunk_count - 1];
    ptr += (ca->last_size - 1) * ca->elem_size;
    return ptr;
  }
}

void *simple_archiver_chunked_array_bottom(SDArchiverChunkedArr *ca) {
  if (!ca
      || ca->chunk_count == 0
      || !ca->array
      || (ca->chunk_count == 1 && ca->last_size == 0)) {
    return NULL;
  }

  if (ca->last_size == 0) {
    if (ca->chunk_count < 2) {
      return NULL;
    }
    return ca->array[0];
  } else {
    return ca->array[0];
  }
}
