#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdint.h>
#include <stddef.h>

typedef struct HashNode {
    char *key;
    uint64_t value;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode **buckets;
    size_t size;
    size_t count;
} HashTable;

HashTable *ht_create(size_t init_size);

int ht_put(HashTable *ht, const char *key, uint64_t value);

uint64_t ht_get(const HashTable *ht, const char *key, int *found);

void ht_free(HashTable *ht);

#endif // HASHTABLE_H
