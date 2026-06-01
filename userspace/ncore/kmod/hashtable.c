#include "hashtable.h"
#include <stdlib.h>
#include <string.h>

// FNV-1a 64-bit
static uint64_t hash_fnv1a(const char *str)
{
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void ht_rehash(HashTable *ht)
{
    size_t new_size = ht->size * 2;
    HashNode **new_buckets = calloc(new_size, sizeof(HashNode *));
    if (!new_buckets)
        return;

    for (size_t i = 0; i < ht->size; i++) {
        HashNode *node = ht->buckets[i];
        while (node) {
            HashNode *next = node->next;
            uint64_t h = hash_fnv1a(node->key) % new_size;

            node->next = new_buckets[h];
            new_buckets[h] = node;

            node = next;
        }
    }
    free(ht->buckets);
    ht->buckets = new_buckets;
    ht->size = new_size;
}

HashTable *ht_create(size_t init_size)
{
    HashTable *ht = malloc(sizeof(HashTable));
    if (!ht)
        return NULL;

    ht->size = init_size > 0 ? init_size : 65536;
    ht->count = 0;
    ht->buckets = calloc(ht->size, sizeof(HashNode *));

    if (!ht->buckets) {
        free(ht);
        return NULL;
    }
    return ht;
}

int ht_put(HashTable *ht, const char *key, uint64_t value)
{
    if (!ht || !key)
        return -1;

    if (ht->count >= ht->size * 3 / 4) {
        ht_rehash(ht);
    }

    uint64_t h = hash_fnv1a(key) % ht->size;
    HashNode *curr = ht->buckets[h];

    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            curr->value = value;
            return 0;
        }
        curr = curr->next;
    }

    HashNode *node = malloc(sizeof(HashNode));
    if (!node)
        return -1;

    node->key = strdup(key);
    if (!node->key) {
        free(node);
        return -1;
    }

    node->value = value;
    node->next = ht->buckets[h];
    ht->buckets[h] = node;
    ht->count++;

    return 0;
}

uint64_t ht_get(const HashTable *ht, const char *key, int *found)
{
    if (found)
        *found = 0;
    if (!ht || !key || ht->size == 0)
        return 0;

    uint64_t h = hash_fnv1a(key) % ht->size;
    HashNode *curr = ht->buckets[h];

    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            if (found)
                *found = 1;
            return curr->value;
        }
        curr = curr->next;
    }
    return 0;
}

void ht_free(HashTable *ht)
{
    if (!ht)
        return;

    for (size_t i = 0; i < ht->size; i++) {
        HashNode *node = ht->buckets[i];
        while (node) {
            HashNode *tmp = node->next;
            free(node->key);
            free(node);
            node = tmp;
        }
    }
    free(ht->buckets);
    free(ht);
}
