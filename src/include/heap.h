#ifndef HEAP_H
#define HEAP_H

#include <stddef.h> // for size_t

void heap_init();

// Standard dynamic memory allocation functions
void* kmalloc(size_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);

#endif // HEAP_H
