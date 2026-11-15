#include "kyroolib.h"

// Simple atoi implementation
int atoi(const char *str) {
  int result = 0;
  int sign = 1;

  // Skip whitespace
  while (*str == ' ' || *str == '\t' || *str == '\n') {
    str++;
  }

  // Handle sign
  if (*str == '-') {
    sign = -1;
    str++;
  } else if (*str == '+') {
    str++;
  }

  // Convert digits
  while (*str >= '0' && *str <= '9') {
    result = result * 10 + (*str - '0');
    str++;
  }

  return result * sign;
}

// Userspace heap allocator on top of sys_sbrk
typedef struct block_meta {
    size_t size;
    struct block_meta *next;
    int free;
    uint32_t magic;
} block_meta_t;

#define BLOCK_META_MAGIC 0x4D414C43 // "MALC"
#define BLOCK_SIZE sizeof(block_meta_t)

static block_meta_t *g_heap_base = NULL;

static block_meta_t *find_free_block(block_meta_t **last, size_t size) {
    block_meta_t *current = g_heap_base;
    while (current && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }
    return current;
}

static block_meta_t *request_space(block_meta_t *last, size_t size) {
    size_t alloc_size = size + BLOCK_SIZE;
    if (alloc_size < 65536) alloc_size = 65536; // 64KB chunk minimum

    void *request = sbrk((intptr_t)alloc_size);
    if (request == (void *)-1 || !request) {
        return NULL;
    }

    block_meta_t *block = (block_meta_t *)request;
    block->size = alloc_size - BLOCK_SIZE;
    block->next = NULL;
    block->free = 0;
    block->magic = BLOCK_META_MAGIC;

    if (last) {
        last->next = block;
    }
    return block;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 15) & ~((size_t)15); // 16-byte align

    block_meta_t *block;
    if (!g_heap_base) {
        block = request_space(NULL, size);
        if (!block) return NULL;
        g_heap_base = block;
    } else {
        block_meta_t *last = g_heap_base;
        block = find_free_block(&last, size);
        if (!block) {
            block = request_space(last, size);
            if (!block) return NULL;
        } else {
            block->free = 0;
        }
    }
    return (void *)(block + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_meta_t *block = (block_meta_t *)ptr - 1;
    if (block->magic == BLOCK_META_MAGIC) {
        block->free = 1;
    }
}

void *calloc(size_t num, size_t nsize) {
    size_t total = num * nsize;
    void *p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    block_meta_t *block = (block_meta_t *)ptr - 1;
    if (block->magic == BLOCK_META_MAGIC && block->size >= size) {
        return ptr;
    }
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    size_t copy_size = (block->magic == BLOCK_META_MAGIC && block->size < size) ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int gettimeofday(void *tv, void *tz) {
    (void)tz;
    if (!tv) return -1;
    uint32_t ms = uptime_ms();
    struct my_timeval {
        long tv_sec;
        long tv_usec;
    } *t = (struct my_timeval *)tv;
    t->tv_sec = ms / 1000;
    t->tv_usec = (ms % 1000) * 1000;
    return 0;
}

uint64_t clock(void) {
    return (uint64_t)uptime_ms();
}

void abort(void) {
    exit(134);
}
