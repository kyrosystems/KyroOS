#include "thread.h"
#include "heap.h"
#include "isr.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "scheduler.h"
#include "vfs.h"
#include "vmm.h"
#include <stddef.h>

static uint64_t next_thread_id = 0;
extern thread_t *current_thread;
extern void thread_starter(void);
extern void userspace_thread_starter(void);

void thread_entry(thread_func_t func, void *arg) {
  serial_print("THREAD: thread_entry started\n");
  enable_interrupts();
  func(arg);
  thread_exit();
}

void thread_init(void) {
  klog(LOG_INFO, "Thread: Initializing...");
  current_thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!current_thread) panic("Failed to allocate main kernel thread!", NULL);
  memset(current_thread, 0, sizeof(thread_t));
  current_thread->id = next_thread_id++;
  current_thread->state = THREAD_RUNNING;
  current_thread->pml4 = vmm_get_current_pml4();
  __asm__ __volatile__("mov %%rsp, %0" : "=r"(current_thread->rsp));
  scheduler_init();
  scheduler_add_thread(current_thread);
}

thread_t *thread_create(thread_func_t func, void *arg) {
  disable_interrupts();
  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    enable_interrupts();
    return NULL;
  }
  memset(thread, 0, sizeof(thread_t));
  thread->stack = kmalloc(KERNEL_STACK_SIZE);
  if (!thread->stack) {
    kfree(thread);
    enable_interrupts();
    return NULL;
  }
  thread->pml4 = vmm_get_current_pml4();
  thread->id = next_thread_id++;
  thread->state = THREAD_READY;
  uint64_t *sp = (uint64_t *)((uint64_t)thread->stack + KERNEL_STACK_SIZE);
  *--sp = (uint64_t)arg;
  *--sp = (uint64_t)func;
  *--sp = (uint64_t)thread_starter;
  for (int i = 0; i < 6; i++) *--sp = 0;
  thread->rsp = (uint64_t)sp;
  scheduler_add_thread(thread);
  enable_interrupts();
  return thread;
}

thread_t *thread_create_userspace(uint64_t entry_point, pml4_t *pml4,
                                  uint64_t program_break, int argc, char *argv[]) {
  klog(LOG_INFO, "thread_create_userspace: entered");
  disable_interrupts();
  if (!pml4 || argc < 0 || (argc > 0 && !argv)) {
    enable_interrupts();
    return NULL;
  }
  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    enable_interrupts();
    return NULL;
  }
  memset(thread, 0, sizeof(thread_t));
  thread->pml4 = pml4;
  uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
  klog(LOG_DEBUG, "Userspace stack: vaddr_start = %p, size = %llu",
       (void *)stack_base, (uint64_t)USER_STACK_SIZE);
  uint64_t mapped = 0;
  for (; mapped < USER_STACK_SIZE; mapped += PAGE_SIZE) {
    void *phys = pmm_alloc_page();
    if (!phys) break;
    vmm_map_page(pml4, (void *)(stack_base + mapped), phys,
                 PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
  }
  if (mapped != USER_STACK_SIZE) {
    for (uint64_t i = 0; i < mapped; i += PAGE_SIZE) {
      void *phys = vmm_unmap_page(pml4, (void *)(stack_base + i));
      if (phys) pmm_free_page(phys);
    }
    vmm_destroy_address_space(pml4);
    kfree(thread);
    enable_interrupts();
    return NULL;
  }
  thread->user_stack_base = (void *)stack_base;
  thread->stack = kmalloc(KERNEL_STACK_SIZE);
  if (!thread->stack) {
    for (uint64_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
      void *phys = vmm_unmap_page(pml4, (void *)(stack_base + i));
      if (phys) pmm_free_page(phys);
    }
    vmm_destroy_address_space(pml4);
    kfree(thread);
    enable_interrupts();
    return NULL;
  }
  thread->id = next_thread_id++;
  thread->state = THREAD_READY;
  thread->program_break = program_break;
  thread->initial_program_break = program_break;
  vfs_node_t *console = vfs_resolve_path(vfs_root, "/dev/console");
  if (!console) console = vfs_resolve_path(vfs_root, "/dev/tty");
  if (console) {
    for (int i = 0; i < 3; i++) {
      thread->fd_table[i].type = FD_TYPE_FILE;
      thread->fd_table[i].data.file.node = console;
      thread->fd_table[i].data.file.flags = i == 0 ? O_RDONLY : O_WRONLY;
    }
  }
  uint64_t user_sp = USER_STACK_TOP;
  uint64_t user_argv[argc + 1];
  for (int i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    user_sp = (user_sp - len) & ~0xFULL;
    if (user_sp < stack_base) panic("Userspace argv exceeds stack", NULL);
    vmm_memcpy_to_userspace(pml4, (void *)user_sp, argv[i], len);
    user_argv[i] = user_sp;
  }
  user_argv[argc] = 0;
  user_sp = (user_sp - (argc + 1) * sizeof(uint64_t)) & ~0xFULL;
  if (user_sp < stack_base) panic("Userspace argv exceeds stack", NULL);
  vmm_memcpy_to_userspace(pml4, (void *)user_sp, user_argv,
                          (argc + 1) * sizeof(uint64_t));
  uint64_t argv_ptr = user_sp;
  user_sp = (user_sp - sizeof(uint64_t)) & ~0xFULL;
  vmm_memcpy_to_userspace(pml4, (void *)user_sp, &argv_ptr, sizeof(argv_ptr));
  user_sp = (user_sp - sizeof(uint64_t)) & ~0xFULL;
  uint64_t argc64 = (uint64_t)argc;
  vmm_memcpy_to_userspace(pml4, (void *)user_sp, &argc64, sizeof(argc64));
  uint64_t *sp = (uint64_t *)((uint64_t)thread->stack + KERNEL_STACK_SIZE);
  klog(LOG_DEBUG, "Userspace IRETQ frame setup: entry_point = %p, USER_STACK_TOP = %p",
       (void *)entry_point, (void *)USER_STACK_TOP);
  *--sp = 0x23;
  *--sp = user_sp;
  *--sp = 0x202;
  *--sp = 0x1B;
  *--sp = entry_point;
  *--sp = (uint64_t)userspace_thread_starter;
  for (int i = 0; i < 6; i++) *--sp = 0;
  thread->rsp = (uint64_t)sp;
  klog(LOG_INFO, "thread_create_userspace: thread ID %d created successfully",
       thread->id);
  scheduler_add_thread(thread);
  enable_interrupts();
  return thread;
}

void thread_exit(void) {
  disable_interrupts();
  get_current_thread()->state = THREAD_DEAD;
  klog(LOG_INFO, "Thread exited.");
  schedule();
  panic("Returned to a dead thread!", NULL);
}
