#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

//Uncomment the below code before implementing cache functioncs.
static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  if (cache != NULL) {
    return -1;
  }
  if (num_entries < 2 || num_entries > 4096) {
    return -1;
  }
  cache = (cache_entry_t *)malloc(sizeof(cache_entry_t) * num_entries);
  if (cache == NULL) {
    return -1;
  }
  for (int i = 0; i < num_entries; i++) {
    cache[i].valid = false;
    cache[i].disk_num = -1;
    cache[i].block_num = -1;
    cache[i].clock_accesses = 0;
  }
  cache_size = num_entries;
  clock = 0;
  return 1;
}

int cache_destroy(void) {
  if (cache == NULL) {
    return -1;
  }

  free(cache);
  cache = NULL;
  cache_size = 0;
  clock = 0;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if (cache == NULL || buf == NULL) {
    return -1;
  }
  if (disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK) {
    return -1;
  }
  num_queries++;
  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits++;
      clock++;
      cache[i].clock_accesses = clock;
      return 1;
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  if (cache == NULL || buf == NULL) {
    return;
  }
  if (disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK) {
    return;
  }
  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      clock++;
      cache[i].clock_accesses = clock;
      return;
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if (cache == NULL || buf == NULL) {
    return -1;
  }
  if (disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK) {
    return -1;
}
  for (int i = 0; i < cache_size; i++) {
    if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
      return -1;
    }
  }
  for (int i = 0; i < cache_size; i++) {
    if (!cache[i].valid) {
      cache[i].valid = true;
      cache[i].disk_num = disk_num;
      cache[i].block_num = block_num;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      clock++;
      cache[i].clock_accesses = clock;
      return 1;
    }
  }
  int mru = 0;
  int max_clock = cache[0].clock_accesses;
  for (int i = 1; i < cache_size; i++) {
    if (cache[i].clock_accesses > max_clock) {
      max_clock = cache[i].clock_accesses;
      mru = i;
    }
  }
  cache[mru].valid = true;
  cache[mru].disk_num = disk_num;
  cache[mru].block_num = block_num;
  memcpy(cache[mru].block, buf, JBOD_BLOCK_SIZE);
  clock++;
  cache[mru].clock_accesses = clock;
  return 1;
}

bool cache_enabled(void) {
  return (cache != NULL);
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}

int cache_resize(int new_num_entries) {
  if (cache == NULL) {
    return -1;
  }
  if (new_num_entries < 2 || new_num_entries > 4096) {
    return -1;
  }
  cache_entry_t *new_cache = (cache_entry_t *)malloc(sizeof(cache_entry_t) * new_num_entries);
  if (new_cache == NULL) {
    return -1;
  }
  for (int i = 0; i < new_num_entries; i++) {
    new_cache[i].valid = false;
    new_cache[i].disk_num = -1;
    new_cache[i].block_num = -1;
    new_cache[i].clock_accesses = 0;
  }
  if (new_num_entries >= cache_size) {
    for (int i = 0; i < cache_size; i++) {
      new_cache[i] = cache[i];
    }
  } else {
    int *p = (int *)malloc(sizeof(int) * cache_size);
    for (int i = 0; i < cache_size; i++) {
      p[i] = i;
    }
    for (int i = 0; i < cache_size - 1; i++) {
      for (int j = i + 1; j < cache_size; j++) {
        if (cache[p[i]].clock_accesses < cache[p[j]].clock_accesses) {
          int temp = p[i];
          p[i] = p[j];
          p[j] = temp;
        }
      }
    }
    for (int i = 0; i < new_num_entries; i++) {
      new_cache[i] = cache[p[i]];
    }
    free(p);
  }
  free(cache);
  cache = new_cache;
  cache_size = new_num_entries;
  return 1;
}