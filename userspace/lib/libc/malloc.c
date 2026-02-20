#include <stddef.h> // For NULL, size_t
#include <stdint.h> // For uintptr_t
#include <kyroolib.h> // For print
#include <errno.h> // For EINVAL

extern int __set_errno(int err);
static void *heap_end = NULL;
static void *heap_start = NULL;

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    if (heap_start == NULL) {
        // Initialize heap_start to the current program break
        heap_start = sbrk(0);
        if (heap_start == (void*)-1) {
            print("malloc: failed to get initial program break\n");
            return NULL;
        }
        heap_end = heap_start;
    }

    // Align size to 8 bytes for simplicity and common alignment needs
    size = (size + 7) & ~7;

    void *prev_heap_end = heap_end;
    void *new_heap_end_ptr = (void*)((uintptr_t)prev_heap_end + size); // Calculate requested new end

    void *actual_new_brk = (void*)syscall(SYS_SBRK, size, 0, 0); // Try to increment the break
    
    if (actual_new_brk == (void*)-1) {
        print("malloc: sbrk failed to extend heap\n");
        return NULL;
    }
    
    // Our sbrk returns the *old* program break on success.
    // So, if successful, `actual_new_brk` should be `prev_heap_end`.
    // And `heap_end` should be updated to `new_heap_end_ptr`.
    if (actual_new_brk == prev_heap_end) {
        heap_end = new_heap_end_ptr;
        return prev_heap_end;
    } else {
        // This indicates an unexpected behavior from sbrk, possibly a failure
        // or a different interpretation of return value.
        // Given our sbrk wrapper's current return semantics, this is an error.
        print("malloc: sbrk returned unexpected value or failed internally\n");
        __set_errno(EINVAL); // Indicate internal error or unexpected state
        return NULL;
    }
}

void free(void *ptr) {
    // With a simple bump-pointer allocator, free is a no-op.
    // Real allocators would manage blocks.
    (void)ptr; // Suppress unused parameter warning
}

