#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include "isr.h" // For struct registers
#include "vmm.h" // For pml4_t, USER_STACK_SIZE, USER_STACK_TOP
#include "signal.h"

#define KERNEL_STACK_SIZE 32768 // 32KB stack for kernel threads

#define MAX_FILES 16

// vfs.h defines MAX_FILENAME_LEN; keep thread.h decoupled from vfs.h
// (only a forward declaration of vfs_node_t is needed here), but the
// value must match vfs.h. Guarded so including vfs.h first wins.
#ifndef MAX_FILENAME_LEN
#define MAX_FILENAME_LEN 256
#endif

// Forward declare vfs_node_t to avoid circular dependency
struct vfs_node;
struct socket; // Forward declare socket for fd_entry_t union
struct pipe;   // Forward declare pipe for fd_entry_t union

typedef enum {
    FD_TYPE_NONE,
    FD_TYPE_FILE,
    FD_TYPE_SOCKET,
    FD_TYPE_PIPE
} fd_type_t;

typedef struct {
    uint32_t refs;
    uint64_t offset;
} fd_shared_t;

// Define file descriptor entry
typedef struct {
    fd_type_t type;
    fd_shared_t *shared;
    union {
        struct { // For files
            struct vfs_node* node;
            uint64_t offset; // Current read/write offset
            int flags; // Flags used when opening the file
        } file;
        struct socket* sock; // For sockets
        struct {             // For pipes
            struct pipe *pipe;
            int is_write;
        } pipe;
    } data;
} fd_entry_t;

#define FD_OFFSET(entry) (*((entry).shared ? &(entry).shared->offset : &(entry).data.file.offset))
int fd_duplicate(fd_entry_t *dest, fd_entry_t *source);
void fd_release(fd_entry_t *entry);

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
  void *stack; // Kernel stack (offset 16)
  void *user_stack_base; // Base address of userspace stack (offset 24)
  uint64_t rsp; // Stack pointer (offset 32)
  pml4_t *pml4; // Page map level 4 for virtual memory (offset 40)
  uint64_t program_break;
  uint64_t initial_program_break;
  fd_entry_t fd_table[MAX_FILES]; // File descriptor table
  struct thread *next; // For scheduler linked list

  uint64_t parent_id; // Parent process/thread ID
  int exit_status;    // Exit status code

  // Credentials (POSIX-style). Kernel threads and exec default to root (0).
  uint32_t uid, gid;     // real
  uint32_t euid, egid;   // effective (access checks use these)
  uint32_t umask;        // file-creation mask
  char name[32];         // process name (basename of exec path)
  uint64_t cpu_ticks;    // total scheduler ticks consumed (sysmon/ps)
  uint64_t start_ticks;  // scheduler ticks at creation
  uint64_t wake_ticks;   // wake tick for timed sleep (0 = not sleeping)
  char cwd[MAX_FILENAME_LEN]; // current working directory (path text)
  sighandler_t sig_handlers[32]; // POSIX signal handlers
  uint32_t sig_pending;          // Pending signal bitmask
  uint32_t sig_mask;             // Blocked signal bitmask
  uint8_t fpu_state[512] __attribute__((aligned(16))); // x87, MXCSR and SSE context
} thread_t;

// Function pointer for thread entry point
typedef void (*thread_func_t)(void*);

void thread_init();
thread_t* thread_create(thread_func_t func, void* arg); // For kernel threads
thread_t* thread_create_userspace(uint64_t entry_point, pml4_t* pml4, uint64_t program_break, int argc, char* argv[], const char* name); // For userspace ELFs
thread_t* thread_fork(struct registers *parent_regs);
void thread_exit();

// Assembly function for context switching
void thread_switch(thread_t* old_thread, thread_t* new_thread);

extern void thread_starter();
extern void userspace_trampoline();

thread_t *get_current_thread(); // Declare get_current_thread here

#endif // THREAD_H
