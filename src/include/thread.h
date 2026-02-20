#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include "isr.h" // For struct registers
#include "vmm.h" // For pml4_t, USER_STACK_SIZE, USER_STACK_TOP

#define KERNEL_STACK_SIZE 8192 // 8KB stack for kernel threads

#define MAX_FILES 16

// Forward declare vfs_node_t to avoid circular dependency
struct vfs_node;
struct socket; // Forward declare socket for fd_entry_t union

typedef enum {
    FD_TYPE_NONE,
    FD_TYPE_FILE,
    FD_TYPE_SOCKET
} fd_type_t;

// Define file descriptor entry
typedef struct {
    fd_type_t type;
    union {
        struct { // For files
            struct vfs_node* node;
            uint64_t offset; // Current read/write offset
            int flags; // Flags used when opening the file
        } file;
        struct socket* sock; // For sockets
    } data;
} fd_entry_t;

// State of a thread
typedef enum {
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_BLOCKED,
    THREAD_DEAD
} thread_state_t;

typedef struct thread {
  uint64_t id;
  thread_state_t state;
  void *stack; // Kernel stack
  void *user_stack_base; // Base address of userspace stack
  uint64_t rsp; // Stack pointer
  pml4_t *pml4; // Page map level 4 for virtual memory
  uint64_t program_break;
  uint64_t initial_program_break;
  fd_entry_t fd_table[MAX_FILES]; // File descriptor table
  struct thread *next; // For scheduler linked list
} thread_t;

// Function pointer for thread entry point
typedef void (*thread_func_t)(void*);

void thread_init();
thread_t* thread_create(thread_func_t func, void* arg); // For kernel threads
thread_t* thread_create_userspace(uint64_t entry_point, pml4_t* pml4, uint64_t program_break, int argc, char* argv[]); // For userspace ELFs
void thread_exit();

// Assembly function for context switching
void thread_switch(thread_t* old_thread, thread_t* new_thread);

extern void thread_starter();
extern void userspace_trampoline();

thread_t *get_current_thread(); // Declare get_current_thread here

#endif // THREAD_H
