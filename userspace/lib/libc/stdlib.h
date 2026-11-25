#ifndef KYRO_STDLIB_H
#define KYRO_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int atoi(const char *text);
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
/* exit is provided by kyroolib.h's syscall wrapper. */
#endif
