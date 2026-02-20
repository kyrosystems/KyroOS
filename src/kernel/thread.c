#include "thread.h"
#include "heap.h"
#include "isr.h"
#include "log.h"
#include "pmm.h" // For pmm_alloc_page
#include "scheduler.h"
#include "tty.h"
#include "vfs.h"
#include "vmm.h" // For vmm_map_page, PAGE_PRESENT, PAGE_WRITE, PAGE_USER, vmm_get_phys_addr, vmm_memcpy_to_userspace
#include <stddef.h> // for NULL
#include <string.h> // For memcpy, strlen

// HHDM offset from kernel.c, needed for V_TO_P
extern uint64_t hhdm_offset;
#define P_TO_V(p) ((void *)((uint64_t)(p) + hhdm_offset))
#define V_TO_P(v) ((void *)((uint64_t)(v) - hhdm_offset))

static uint64_t next_thread_id = 0;
extern thread_t *current_thread;

// This function is called from assembly when a thread starts for the first time
void thread_entry(thread_func_t func, void *arg) {
  // Re-enable interrupts for the new thread
  enable_interrupts();

  // Call the actual thread function
  func(arg);

  // If the thread function returns, exit the thread
  thread_exit();
}

extern pml4_t *kernel_pml4;

void thread_init() {
  klog(LOG_INFO, "Thread: Initializing...");
  // Create a thread structure for the main kernel thread (the
  // kmain)
  current_thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!current_thread) {
    panic("Failed to allocate main kernel thread!", NULL);
  }
  current_thread->id = next_thread_id++;
  current_thread->state = THREAD_RUNNING;
  current_thread->stack = NULL; // Kernel main stack is managed separately
  current_thread->user_stack_base =
      NULL; // No userspace stack for kernel thread
  current_thread->pml4 =
      vmm_get_current_pml4(); // Kernel thread uses kernel pml4
  // Save the current RSP for the initial kernel thread
  __asm__ __volatile__("mov %%rsp, %0" : "=r"(current_thread->rsp));
  current_thread->next = NULL;

  for (int i = 0; i < MAX_FILES; i++) {
    current_thread->fd_table[i].type = FD_TYPE_NONE;
    current_thread->fd_table[i].data.file.node = NULL;
    current_thread->fd_table[i].data.file.offset = 0;
    current_thread->fd_table[i].data.file.flags = 0;
    current_thread->fd_table[i].data.sock = NULL;
  }

  scheduler_init();
}

extern void thread_starter();

thread_t *thread_create(thread_func_t func, void *arg) {
  disable_interrupts();

  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    klog(LOG_ERROR, "Failed to allocate thread structure.");
    enable_interrupts();
    return NULL;
  }

  thread->stack = kmalloc(KERNEL_STACK_SIZE);
  if (!thread->stack) {
    klog(LOG_ERROR, "Failed to allocate thread stack.");
    kfree(thread);
    enable_interrupts();
    return NULL;
  }

  thread->user_stack_base = NULL;        // No userspace stack for kernel thread
  thread->pml4 = vmm_get_current_pml4(); // Kernel thread uses kernel pml4

  thread->id = next_thread_id++;
  thread->state = THREAD_READY;

  for (int i = 0; i < MAX_FILES; i++) {
    thread->fd_table[i].type = FD_TYPE_NONE;
    thread->fd_table[i].data.file.node = NULL;
    thread->fd_table[i].data.file.offset = 0;
    thread->fd_table[i].data.file.flags = 0;
    thread->fd_table[i].data.sock = NULL;
  }

  // Set up the initial stack for the new thread
  uint64_t *stack_ptr =
      (uint64_t *)((uint64_t)thread->stack + KERNEL_STACK_SIZE);

  // Initial arguments for thread_entry, which are passed to thread_starter on
  // the stack.
  *--stack_ptr = (uint64_t)arg;
  *--stack_ptr = (uint64_t)func;

  // The 'return address' for thread_switch is thread_starter.
  *--stack_ptr = (uint64_t)thread_starter;

  // Fake callee-saved registers for thread_switch to pop.
  // The order of these pushes must result in a stack layout that matches
  // the reverse of the 'pop' sequence in thread_switch.
  // thread_switch pops: r15, r14, r13, r12, rbx, rbp
  // So, we push fake values for rbp, rbx, r12, r13, r14, r15.
  // Since we decrement the stack pointer before writing, we push rbp first,
  // then rbx, and so on, so that r15 is at the lowest address (top of stack).
  *--stack_ptr = 0; // rbp
  *--stack_ptr = 0; // rbx
  *--stack_ptr = 0; // r12
  *--stack_ptr = 0; // r13
  *--stack_ptr = 0; // r14
  *--stack_ptr = 0; // r15

  thread->rsp = (uint64_t)stack_ptr;

  scheduler_add_thread(thread);

  enable_interrupts();
  return thread;
}

extern void userspace_trampoline();
extern void userspace_thread_starter();

// New function for userspace threads
thread_t *thread_create_userspace(uint64_t entry_point, pml4_t *pml4,
                                  uint64_t program_break, int argc,
                                  char *argv[]) {
  klog(LOG_INFO, "thread_create_userspace: entered");
  disable_interrupts();

  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    klog(LOG_ERROR, "Failed to allocate userspace thread structure.");
    enable_interrupts();
    return NULL;
  }
  thread->pml4 = pml4;

  // Allocate userspace stack
  void *user_stack_vaddr_start = (void *)(USER_STACK_TOP - USER_STACK_SIZE);
  klog(LOG_DEBUG, "Userspace stack: vaddr_start = %p, size = %llu",
       user_stack_vaddr_start, (uint64_t)USER_STACK_SIZE);

  for (uint64_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
    void *phys_page = pmm_alloc_page();
    if (!phys_page) {
      klog(LOG_ERROR, "Failed to allocate physical page for userspace stack.");
      vmm_destroy_address_space(thread->pml4);
      kfree(thread);
      enable_interrupts();
      return NULL;
    }
    vmm_map_page(thread->pml4, (void *)((uint64_t)user_stack_vaddr_start + i),
                 phys_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
  }

  thread->user_stack_base = user_stack_vaddr_start;

  // Also allocate a kernel stack for syscalls/interrupts
  thread->stack = kmalloc(KERNEL_STACK_SIZE);
  if (!thread->stack) {
    klog(LOG_ERROR, "Failed to allocate kernel stack for userspace thread.");

    // The user stack is part of the address space, which we are about to
    // destroy. We just need to free the physical pages.
    for (uint64_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
      void *vaddr = (void *)((uint64_t)user_stack_vaddr_start + i);
      void *phys_addr = vmm_unmap_page(thread->pml4, vaddr);
      if (phys_addr) {
        pmm_free_page(phys_addr);
      }
    }
    vmm_destroy_address_space(thread->pml4);
    kfree(thread);
    enable_interrupts();
    return NULL;
  }

  thread->id = next_thread_id++;
  thread->state = THREAD_READY;
  thread->program_break = program_break;
  thread->initial_program_break = program_break;

  // Initialize FD table
  // Pre-allocate 0, 1, 2 for stdin, stdout, stderr
  // We try to find a suitable device node
  vfs_node_t *console_node = vfs_resolve_path(vfs_root, "/dev/console");
  if (!console_node)
    console_node = vfs_resolve_path(vfs_root, "/dev/tty");

  // If we still don't have a console node, implies system is not fully ready or
  // VFS unpopulated But we need to let the thread run. If console_node is NULL,
  // sys_write will fail for these FDs. However, for debugging, we might assume
  // there's a kernel fallback if we pass a special node or flags? No, sys_write
  // needs a valid node. Let's rely on finding /dev/console or tty. If missing,
  // we log a warning.

  if (console_node) {
    for (int i = 0; i < 3; i++) {
      thread->fd_table[i].type = FD_TYPE_FILE;
      thread->fd_table[i].data.file.node = console_node;
      thread->fd_table[i].data.file.offset = 0;
      thread->fd_table[i].data.file.flags = (i == 0) ? O_RDONLY : O_WRONLY;
    }
  } else {
    klog(LOG_WARN, "Thread Create: Could not find /dev/console or /dev/tty for "
                   "standard FDs.");
    // Initialize as NONE, so userspace prints will fail silently
    for (int i = 0; i < 3; i++) {
      thread->fd_table[i].type = FD_TYPE_NONE;
    }
  }

  // Initialize the rest as empty
  for (int i = 3; i < MAX_FILES; i++) {
    thread->fd_table[i].type = FD_TYPE_NONE;
    thread->fd_table[i].data.file.node = NULL;
    thread->fd_table[i].data.file.offset = 0;
    thread->fd_table[i].data.file.flags = 0;
    thread->fd_table[i].data.sock = NULL;
  }

  // Open stdin, stdout, stderr
  // FDs 0, 1, 2 initialized above if console found.

  // Set up the initial KERNEL stack for the new userspace thread.
  // This stack is what `thread_switch` will restore. It needs to be
  // crafted to eventually `iretq` to userspace.
  uint64_t *kernel_stack_ptr =
      (uint64_t *)((uint64_t)thread->stack + KERNEL_STACK_SIZE);

  klog(LOG_DEBUG,
       "Userspace IRETQ frame setup: entry_point = %p, USER_STACK_TOP = %p",
       (void *)entry_point, (void *)USER_STACK_TOP);

  // --- Setup Userspace Stack with argc and argv ---
  // User stack grows downwards. USER_STACK_TOP is the highest address.
  uint64_t user_stack_current = USER_STACK_TOP;

  // 1. Copy argv strings to userspace stack
  //    We need to copy the strings and then pointers to those strings.
  //    The strings themselves should be at higher addresses (lower on stack)
  //    and the argv array of pointers at lower addresses (higher on stack).

  // Store actual argv strings
  uint64_t argv_pointers_on_user_stack[argc + 1]; // Array to hold the VIRTUAL
                                                  // addresses of the strings
                                                  // (+1 for NULL)
  for (int i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1; // +1 for null terminator
    user_stack_current -= len;
    // Align to 16 bytes (x86_64 stack alignment requirement before call)
    user_stack_current &= ~0xF;

    // Copy string to userspace stack
    vmm_memcpy_to_userspace(pml4, (void *)user_stack_current, argv[i], len);
    argv_pointers_on_user_stack[i] = user_stack_current;
  }
  argv_pointers_on_user_stack[argc] = 0; // Null terminate the argv array

  // 2. Copy argv array (pointers to strings) to userspace stack
  user_stack_current -=
      ((argc + 1) * sizeof(uint64_t)); // Space for argc pointers + NULL
  user_stack_current &= ~0xF;          // Align again
  vmm_memcpy_to_userspace(pml4, (void *)user_stack_current,
                          argv_pointers_on_user_stack,
                          (argc + 1) * sizeof(uint64_t));
  uint64_t argv_ptr_for_userspace = user_stack_current;

  // 3. Push argc and argv pointer onto the userspace stack
  //    The userspace_trampoline will set RDI = argc, RSI =
  //    argv_ptr_for_userspace The IRETQ frame will restore RSP to
  //    user_stack_current (which now points to the location of argc on stack)

  // The arguments to main are passed in RDI (argc) and RSI (argv)
  // The userspace_trampoline will pop these from the stack and put them into
  // registers. So, we need to push argv_ptr_for_userspace, then argc, then the
  // return address (which is main's entry_point). The stack looks like:
  // ...
  // argv[...strings...]
  // NULL
  // argv[0] -> string0
  // argv[1] -> string1
  // ...
  // argv[argc-1] -> string(argc-1)
  // argv_ptr_for_userspace
  // argc
  // entry_point (main) <-- this is what userspace_trampoline expects to pop
  // first

  // --- IRETQ frame (survives thread_switch) ---
  *--kernel_stack_ptr = 0x23;               // SS
  *--kernel_stack_ptr = user_stack_current; // RSP
  *--kernel_stack_ptr = 0x202;              // RFLAGS
  *--kernel_stack_ptr = 0x1B;               // CS
  *--kernel_stack_ptr = (uint64_t)entry_point;

  // --- Return address for thread_switch ---
  // The `ret` in thread_switch will pop this value into RIP.
  *--kernel_stack_ptr = (uint64_t)userspace_thread_starter;

  // --- Callee-saved registers for thread_switch ---
  // thread_switch pops: r15, r14, r13, r12, rbx, rbp
  // So we push them in order: rbp, rbx, r12, r13, r14, r15
  *--kernel_stack_ptr = 0;                      // rbp
  *--kernel_stack_ptr = (uint64_t)argc;         // rbx
  *--kernel_stack_ptr = argv_ptr_for_userspace; // r12
  *--kernel_stack_ptr = 0;                      // r13
  *--kernel_stack_ptr = 0;                      // r14
  *--kernel_stack_ptr = 0;                      // r15

  klog(LOG_INFO, "thread_create_userspace: thread ID %d created successfully",
       thread->id);
  scheduler_add_thread(thread);
  enable_interrupts();
  return thread;
}

void thread_exit() {
  disable_interrupts();
  get_current_thread()->state = THREAD_DEAD;
  klog(LOG_INFO, "Thread exited.");
  // The scheduler will handle cleaning up the dead thread
  schedule();
  // We should never get here
  panic("Returned to a dead thread!", NULL);
}