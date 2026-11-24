#ifndef RADIX_H
#define RADIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RadixTree RadixTree;

void     radix_init(RadixTree *t);
void     radix_free(RadixTree *t);

/* Returns 0 on success, -1 on out of memory or invalid args. */
int      radix_insert(RadixTree *t, const void *key, size_t key_len, void *value);

/* Returns the value or NULL if not found. */
void    *radix_search(const RadixTree *t, const void *key, size_t key_len);

/* Returns 0 if deleted, -1 if not found. */
int      radix_delete(RadixTree *t, const void *key, size_t key_len);

/* Number of keys currently stored. */
size_t   radix_size(const RadixTree *t);

#ifdef __cplusplus
}
#endif

#endif
