#ifndef COM_SEODISPARATE_SIMPLE_ARCHIVER_DATA_STRUCTURE_SKEY_HASH_MAP_H_
#define COM_SEODISPARATE_SIMPLE_ARCHIVER_DATA_STRUCTURE_SKEY_HASH_MAP_H_

#include <stdint.h>
#include <stdlib.h>

// Every instance of this node that contains data will have its string key
// located at index: sizeof(SDASKeyHMNode)
// str_len is the length of the C-string key NOT including the NULL terminator.
typedef struct SDASKeyHMNode {
  struct SDASKeyHMNode *next;
  void *data;
  void (*data_cleanup_fn)(void *);
  uint64_t str_len;
} SDASKeyHMNode;

typedef struct SDArchiverSKeyHashMap {
  SDASKeyHMNode **buckets;
  size_t buckets_size;
  size_t count;
  uint64_t (*hash_fn)(const void *, size_t);
} SDArchiverSKeyHashMap;

SDArchiverSKeyHashMap *simple_archiver_skey_hash_map_init(void);

/// Creates a hash map that will use the custom hash function instead of the
/// default. Note that the hash function must return a 64-bit unsigned integer
/// as specified by the function's api. The first parameter of hash_fn is a
/// pointer to the key to be hashed, and the second parameter is the size of
/// the key in bytes.
SDArchiverSKeyHashMap *simple_archiver_skey_hash_map_init_custom_hasher(
    uint64_t (*hash_fn)(const void *, size_t));

/// It is recommended to use the double-pointer version of hash-map free as
/// that will ensure the variable holding the pointer will end up pointing to
/// NULL after free.
void simple_archiver_skey_hash_map_free_single_ptr(SDArchiverSKeyHashMap *map);
void simple_archiver_skey_hash_map_free(SDArchiverSKeyHashMap **map);

/// Returns zero on success.
/// On failure, frees the value using the given function.
/// If value_cleanup_fn is NULL, then "free" is used instead.
/// NOTICE: You must not pass NULL to value, otherwise all "get" checks will
/// fail for the inserted key.
int simple_archiver_skey_hash_map_insert(SDArchiverSKeyHashMap *map,
                                         void *value,
                                         const char *key,
                                         void (*value_cleanup_fn)(void *));

/// Returns NULL if not found.
void *simple_archiver_skey_hash_map_get(const SDArchiverSKeyHashMap *map,
                                        const char *key);

/// Returns zero on success. Returns one if more than one entry was removed.
/// Otherwise returns non-zero and non-one value on error.
int simple_archiver_skey_hash_map_remove(SDArchiverSKeyHashMap *map,
                                         const char *key);

/// Iterates through the hash map with the "iter_check_fn", which is passed the
/// key, value, and user_data. This function will call "iter_check_fn" on every
/// entry in the given hash_map. If "iter_check_fn" returns non-zero, iteration
/// will halt and this function will return the same value. If "iter_check_fn"
/// returns zero for every call, then this function will return zero after
/// having iterated through every key-value pair.
/// Returns -1 on error. Ensure that iter_check_fn does not return -1.
int simple_archiver_skey_hash_map_iter(const SDArchiverSKeyHashMap *map,
                                       int (*iter_check_fn)(const char *,
                                                            const void *,
                                                            void *),
                                       void *user_data);

#endif
