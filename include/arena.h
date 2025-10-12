#pragma once

#include <cstdlib>
#include <stddef.h>
#include <stdint.h>

#define KB 1024
#define ARENA_BLOCK_DEFAULT_CAPACITY (4 * KB)

typedef struct arena_block arena_block_t;

typedef struct arena_block {
  arena_block_t *next;
  size_t capacity;
  size_t used;
  uint8_t data[];
} arena_block_t;

typedef struct arena {
  arena_block_t *head;
} arena_t;

arena_block_t *arena_new_block(size_t capacity);
inline void arena_init(arena_t *arena);
void *arena_alloc(arena_t *arena, size_t size);
void arena_reset(arena_t *arena);
void arena_destroy(arena_t *arena);

#ifdef ARENA_IMPLEMENTATION
static inline void *arena_block_allocate(arena_block_t *block, size_t size) {
  void *obj = block->data + block->used;
  block->used += size;
  return obj;
}

arena_block_t *arena_new_block(size_t capacity) {
  arena_block_t *blk =
      (arena_block_t *)malloc(sizeof(arena_block_t) + capacity);
  blk->capacity = capacity;
  blk->used = 0;
  blk->next = nullptr;
  return blk;
}

inline void arena_init(arena_t *arena) { arena->head = nullptr; }

void *arena_alloc(arena_t *arena, size_t size) {
  arena_block_t *blk = arena->head;
  while (blk) {
    if (blk->used + size < blk->capacity) {
      void *obj = arena_block_allocate(blk, size);
      if (obj)
        return obj;
    }
    blk = blk->next;
  }
  size_t blk_capacity =
      ARENA_BLOCK_DEFAULT_CAPACITY > size ? ARENA_BLOCK_DEFAULT_CAPACITY : size;
  blk = arena_new_block(blk_capacity);
  blk->next = arena->head;
  arena->head = blk;

  return arena_block_allocate(blk, size);
}

void arena_reset(arena_t *arena) {
  for (arena_block_t *blk = arena->head; blk; blk = blk->next) {
    blk->used = 0;
  }
}
void arena_destroy(arena_t *arena) {
  arena_block_t *blk = arena->head;
  while (blk) {
    arena_block_t *next = blk->next;
    free(blk);
    blk = next;
  }
}
#endif
