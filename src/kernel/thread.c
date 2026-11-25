#define KYRO_KERNEL_DISPLAY
#include "display.h"
#include "thread.h"
#include "heap.h"
#include "isr.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "scheduler.h"
#include "vfs.h"
#include "vmm.h"
#include "tss.h"
#include <stddef.h>

static void thread_init_fpu(thread_t *thread) {
  memset(thread->fpu_state, 0, sizeof(thread->fpu_state));
  *(uint16_t *)&thread->fpu_state[0] = 0x037F; // x87 default control word
  *(uint32_t *)&thread->fpu_state[24] = 0x1F80; // SSE default MXCSR

}

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
  thread_init_fpu(current_thread);
  current_thread->id = next_thread_id++;
  current_thread->state = THREAD_RUNNING;
  current_thread->stack = kmalloc(KERNEL_STACK_SIZE);
  current_thread->pml4 = vmm_get_current_pml4();
  current_thread->uid = current_thread->gid = 0;   // root
  current_thread->euid = current_thread->egid = 0;
  current_thread->umask = 022;
  current_thread->start_ticks = timer_get_ticks();
  strncpy(current_thread->name, "kernel", sizeof(current_thread->name) - 1);
  strncpy(current_thread->cwd, "/", sizeof(current_thread->cwd) - 1);
  __asm__ __volatile__("mov %%rsp, %0" : "=r"(current_thread->rsp));
  if (current_thread->stack) {
    tss_set_stack((uint64_t)current_thread->stack + KERNEL_STACK_SIZE);
  }
  scheduler_init();
  scheduler_add_thread(current_thread);
}

thread_t *thread_create(thread_func_t func, void *arg) {
  uint64_t iflags = save_interrupts_and_disable();
  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    restore_interrupts(iflags);
    return NULL;
  }
  memset(thread, 0, sizeof(thread_t));
  thread_init_fpu(thread);
  thread->stack = kmalloc(KERNEL_STACK_SIZE);
  if (!thread->stack) {
    kfree(thread);
    restore_interrupts(iflags);
    return NULL;
  }
  thread->pml4 = kernel_pml4;
  thread->id = next_thread_id++;
  thread->state = THREAD_READY;
  thread->uid = thread->gid = 0;   // kernel threads run as root
  thread->euid = thread->egid = 0;
  thread->umask = 022;
  thread->start_ticks = timer_get_ticks();
  strncpy(thread->cwd, "/", sizeof(thread->cwd) - 1);
  strncpy(thread->name, "kthread", sizeof(thread->name) - 1);
  thread->name[sizeof(thread->name) - 1] = '\0';
  uint64_t *sp = (uint64_t *)((uint64_t)thread->stack + KERNEL_STACK_SIZE);
  *--sp = (uint64_t)arg;
  *--sp = (uint64_t)func;
  *--sp = (uint64_t)thread_starter;
  for (int i = 0; i < 6; i++) *--sp = 0;
  thread->rsp = (uint64_t)sp;
  scheduler_add_thread(thread);
  restore_interrupts(iflags);
  return thread;
}

thread_t *thread_create_userspace(uint64_t entry_point, pml4_t *pml4,
                                  uint64_t program_break, int argc, char *argv[],
                                  const char *name) {
  klog(LOG_INFO, "thread_create_userspace: entered");
  uint64_t iflags = save_interrupts_and_disable();
  if (!pml4 || argc < 0 || argc > 64 || (argc > 0 && !argv)) {
    restore_interrupts(iflags);
    return NULL;
  }
  thread_t *thread = (thread_t *)kmalloc(sizeof(thread_t));
  if (!thread) {
    restore_interrupts(iflags);
    return NULL;
  }
  memset(thread, 0, sizeof(thread_t));
  thread_init_fpu(thread);
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
    restore_interrupts(iflags);
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
    restore_interrupts(iflags);
    return NULL;
  }

  uint64_t *user_argv = (uint64_t *)kmalloc((size_t)(argc + 1) * sizeof(uint64_t));
  if (!user_argv) {
    goto fail_cleanup;
  }

  uint64_t user_sp = USER_STACK_TOP;
  for (int i = 0; i < argc; i++) {
    if (!argv[i]) goto fail_cleanup;
    size_t len = strlen(argv[i]) + 1;
    if (len > user_sp - stack_base) goto fail_cleanup;
    user_sp = (user_sp - len) & ~0xFULL;
    if (user_sp < stack_base) goto fail_cleanup;
    vmm_memcpy_to_userspace(pml4, (void *)user_sp, argv[i], len);
    user_argv[i] = user_sp;
  }
  user_argv[argc] = 0;
  size_t argv_bytes = (size_t)(argc + 1) * sizeof(uint64_t);
  if (argv_bytes > user_sp - stack_base) goto fail_cleanup;
  user_sp = (user_sp - argv_bytes) & ~0xFULL;
  if (user_sp < stack_base) goto fail_cleanup;
  vmm_memcpy_to_userspace(pml4, (void *)user_sp, user_argv, argv_bytes);
  kfree(user_argv);
  user_argv = NULL;

  uint64_t argv_ptr = user_sp;
  if (sizeof(uint64_t) > user_sp - stack_base) goto fail_cleanup;
  user_sp = (user_sp - sizeof(uint64_t)) & ~0xFULL;
  if (user_sp < stack_base) goto fail_cleanup;
  vmm_memcpy_to_userspace(pml4, (void *)user_sp, &argv_ptr, sizeof(argv_ptr));

  if (sizeof(uint64_t) > user_sp - stack_base) goto fail_cleanup;
  user_sp = (user_sp - sizeof(uint64_t)) & ~0xFULL;
  if (user_sp < stack_base) goto fail_cleanup;
  uint64_t argc64 = (uint64_t)argc;
  vmm_memcpy_to_userspace(pml4, (void *)user_sp, &argc64, sizeof(argc64));

  thread->id = next_thread_id++;
  thread->state = THREAD_READY;
  thread->program_break = program_break;
  thread->initial_program_break = program_break;
  // Inherit credentials, umask and cwd from the creating (current) thread.
  thread_t *parent = get_current_thread();
  if (parent) {
    thread->parent_id = parent->id;
    thread->uid = parent->uid;   thread->gid = parent->gid;
    thread->euid = parent->euid; thread->egid = parent->egid;
    thread->umask = parent->umask;
    strncpy(thread->cwd, parent->cwd, sizeof(thread->cwd) - 1);
    thread->cwd[sizeof(thread->cwd) - 1] = '\0';
  } else {
    thread->parent_id = 0;
    thread->uid = thread->gid = 0;
    thread->euid = thread->egid = 0;
    thread->umask = 022;
    strncpy(thread->cwd, "/", sizeof(thread->cwd) - 1);
  }
  thread->exit_status = 0;
  thread->start_ticks = timer_get_ticks();
  if (name) {
    strncpy(thread->name, name, sizeof(thread->name) - 1);
    thread->name[sizeof(thread->name) - 1] = '\0';
  }
  vfs_node_t *console = vfs_resolve_path(vfs_root, "/dev/console");
  if (!console) console = vfs_resolve_path(vfs_root, "/dev/tty");
  if (console) {
    for (int i = 0; i < 3; i++) {
      thread->fd_table[i].type = FD_TYPE_FILE;
      thread->fd_table[i].data.file.node = console;
      thread->fd_table[i].data.file.flags = i == 0 ? O_RDONLY : O_WRONLY;
    }
  }
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
  if (!display_active()) console_focus = thread; // compositor retains physical input ownership

  klog(LOG_INFO, "thread_create_userspace: thread ID %d created successfully",
       thread->id);
  scheduler_add_thread(thread);
  restore_interrupts(iflags);
  return thread;

fail_cleanup:
  if (user_argv) kfree(user_argv);
  if (thread->stack) kfree(thread->stack);
  for (uint64_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
    void *phys = vmm_unmap_page(pml4, (void *)(stack_base + i));
    if (phys) pmm_free_page(phys);
  }
  vmm_destroy_address_space(pml4);
  kfree(thread);
  restore_interrupts(iflags);
  return NULL;
}

extern void userspace_fork_entry(void);

thread_t *thread_fork(struct registers *parent_regs) {
  uint64_t iflags = save_interrupts_and_disable();
  thread_t *parent = get_current_thread();
  if (!parent) {
    restore_interrupts(iflags);
    return NULL;
  }

  pml4_t *child_pml4 = vmm_clone_address_space(parent->pml4);
  if (!child_pml4) {
    restore_interrupts(iflags);
    return NULL;
  }

  thread_t *child = (thread_t *)kmalloc(sizeof(thread_t));
  if (!child) {
    vmm_destroy_address_space(child_pml4);
    restore_interrupts(iflags);
    return NULL;
  }
  memset(child, 0, sizeof(thread_t));
  thread_init_fpu(child);

  child->stack = kmalloc(KERNEL_STACK_SIZE);
  if (!child->stack) {
    vmm_destroy_address_space(child_pml4);
    kfree(child);
    restore_interrupts(iflags);
    return NULL;
  }

  child->id = next_thread_id++;
  child->parent_id = parent->id;
  child->state = THREAD_READY;
  child->pml4 = child_pml4;
  child->user_stack_base = parent->user_stack_base;
  child->program_break = parent->program_break;
  child->initial_program_break = parent->initial_program_break;
  child->uid = parent->uid;
  child->gid = parent->gid;
  child->euid = parent->euid;
  child->egid = parent->egid;
  child->umask = parent->umask;
  child->start_ticks = timer_get_ticks();
  child->exit_status = 0;
  strncpy(child->name, parent->name, sizeof(child->name) - 1);
  strncpy(child->cwd, parent->cwd, sizeof(child->cwd) - 1);

  for (int i = 0; i < MAX_FILES; i++) {
    if (parent->fd_table[i].type != FD_TYPE_NONE) {
      if (fd_duplicate(&child->fd_table[i], &parent->fd_table[i]) < 0) {
        for (int j = 0; j < i; j++) fd_release(&child->fd_table[j]);
        kfree(child->stack);
        vmm_destroy_address_space(child_pml4);
        kfree(child);
        restore_interrupts(iflags);
        return NULL;
      }
    }
  }

  memcpy(child->fpu_state, parent->fpu_state, sizeof(child->fpu_state));

  uint64_t *sp = (uint64_t *)((uint64_t)child->stack + KERNEL_STACK_SIZE);

  // Match the iretq stack layout expected by userspace_fork_entry
  *--sp = parent_regs->ss;
  *--sp = parent_regs->rsp;
  *--sp = parent_regs->rflags;
  *--sp = parent_regs->cs;
  *--sp = parent_regs->rip;
  *--sp = 0;   // dummy err_code
  *--sp = 128; // int_no
  *--sp = parent_regs->r15;
  *--sp = parent_regs->r14;
  *--sp = parent_regs->r13;
  *--sp = parent_regs->r12;
  *--sp = parent_regs->r11;
  *--sp = parent_regs->r10;
  *--sp = parent_regs->r9;
  *--sp = parent_regs->r8;
  *--sp = parent_regs->rbp;
  *--sp = parent_regs->rdi;
  *--sp = parent_regs->rsi;
  *--sp = parent_regs->rdx;
  *--sp = parent_regs->rcx;
  *--sp = parent_regs->rbx;
  *--sp = 0; // rax = 0 for child return from fork

  // thread_switch return address
  *--sp = (uint64_t)userspace_fork_entry;
  for (int i = 0; i < 6; i++) *--sp = 0; // r15, r14, r13, r12, rbx, rbp

  child->rsp = (uint64_t)sp;

  scheduler_add_thread(child);
  restore_interrupts(iflags);
  return child;
}

void thread_exit(void) {
  disable_interrupts();
  thread_t *cur = get_current_thread();
  if (cur) {
    cur->state = THREAD_DEAD;
    if (cur->parent_id) {
      extern thread_t *scheduler_find_thread(uint64_t pid);
      thread_t *parent = scheduler_find_thread(cur->parent_id);
      if (parent) {
        parent->sig_pending |= (1 << 17); // SIGCHLD
      }
    }
  }
  klog(LOG_INFO, "Thread exited.");
  schedule();
  panic("Returned to a dead thread!", NULL);
}
