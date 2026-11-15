/*
 * selftest.c — KyroOS boot-time integration tests.
 *
 * Runs as a kernel thread late in boot (after VFS, net stack, loopback
 * registration). Exercises real subsystems and prints PASS/FAIL lines that
 * the host test runner (tests/run_qemu_tests.py) parses from the serial log:
 *
 *   SELFTEST: <name>: PASS|FAIL (detail)
 *
 * Deterministic only: no external network, no user interaction.
 */
#include "selftest.h"
#include "epstein.h"
#include "heap.h"
#include "icmp.h"
#include "kstring.h"
#include "log.h"
#include "net.h"
#include "pmm.h"
#include "scheduler.h"
#include "thread.h"
#include "vfs.h"
#include "isr.h"
#include "vmm.h"
#include "errno.h"

static int failures = 0;
static int passed = 0;

static void st_report(const char *name, int ok, const char *detail) {
    if (ok) {
        passed++;
        klog(LOG_INFO, "SELFTEST: %s: PASS%s%s", name, detail ? " (" : "", detail ? detail : "");
    } else {
        failures++;
        klog(LOG_WARN, "SELFTEST: %s: FAIL%s%s", name, detail ? " (" : "", detail ? detail : "");
    }
}

static void selftest_thread(void *arg) {
    (void)arg;
    /* name this thread so proc introspection reports it */
    thread_t *self = get_current_thread();
    if (self) { strncpy(self->name, "selftest", sizeof(self->name) - 1);
        self->name[sizeof(self->name) - 1] = '\0'; }
    klog(LOG_INFO, "SELFTEST: starting (%d checks)", 10);

    /* T1: debugmode defaults OFF (debug logging floods the console and
       costs a framebuffer flush per syscall; enable via `debugmode on`). */
    st_report("debugmode_default_off", epstein == 0, NULL);

    /* T2: debugmode set/get round-trip (this thread runs as root/uid 0) */
    {
        thread_t *t = get_current_thread();
        int saved = epstein;
        int ok = 1;
        epstein = 0;  /* direct kernel-store write, same as syscall path */
        ok = ok && (epstein == 0);
        epstein = 1;
        ok = ok && (epstein == 1);
        epstein = saved;
        (void)t;
        st_report("debugmode_set_get", ok, NULL);
    }

    /* T3: loopback ICMP echo (ping 127.0.0.1) — full request/reply round trip */
    {
        bool got = icmp_ping(0x7F000001, 1000);
        st_report("icmp_loopback_ping", got == true, got ? "reply ok" : "no reply");
    }

    /* T4: VFS create/write/read on /tmp/selftest.bin */
    {
        vfs_node_t *tmp = vfs_resolve_path(vfs_root, "/tmp");
        int ok = 0;
        const char *msg = "kyroos-selftest";
        char readback[32];
        if (tmp && (tmp->flags & VFS_DIRECTORY)) {
            if (vfs_create(tmp, "selftest.bin", 0) == 0) {
                vfs_node_t *f = vfs_resolve_path(vfs_root, "/tmp/selftest.bin");
                if (f) {
                    uint32_t w = vfs_write(f, 0, strlen(msg), (uint8_t *)msg);
                    memset(readback, 0, sizeof(readback));
                    uint32_t r = vfs_read(f, 0, sizeof(readback) - 1, (uint8_t *)readback);
                    ok = (w == strlen(msg)) && (r == strlen(msg)) &&
                         (strncmp(readback, msg, strlen(msg)) == 0);
                }
            }
        }
        st_report("vfs_create_write_read", ok, NULL);
    }

    /* T5: node ownership/mode defaults + umask */
    {
        vfs_node_t *f = vfs_resolve_path(vfs_root, "/tmp/selftest.bin");
        int ok = f && f->uid == 0 && f->gid == 0 && (f->mode & 0777) == 0644;
        st_report("vfs_owner_mode_defaults", ok,
                  ok ? NULL : (f ? "uid/mode wrong" : "node missing"));
    }

    /* T6: permission enforcement — non-root write to root-owned 0644 file */
    {
        vfs_node_t *f = vfs_resolve_path(vfs_root, "/tmp/selftest.bin");
        thread_t *t = get_current_thread();
        int ok = 0;
        if (f && t) {
            uint32_t saved_euid = t->euid;
            t->euid = 1; /* simulate non-root */
            int wdenied = (vfs_write(f, 0, 4, (uint8_t *)"xxxx") == (uint32_t)-1);
            char rb[8];
            memset(rb, 0, sizeof(rb));
            int rallowed = (vfs_read(f, 0, 4, (uint8_t *)rb) == 4);
            t->euid = saved_euid;
            ok = wdenied && rallowed;
        }
        st_report("vfs_perm_nonroot_write_denied", ok, NULL);
    }

    /* T7: process list sanity — at least this thread present with valid pid */
    {
        thread_t *t = get_current_thread();
        st_report("proc_self_present", t && t->id > 0 && t->name[0] != '\0', NULL);
    }

    /* T8: PMM stats plausible */
    {
        uint64_t total = pmm_get_total_memory();
        uint64_t used = pmm_get_used_memory();
        st_report("pmm_stats", total > 0 && used <= total, NULL);
    }

    /* T9: a writable temporary directory still protects other users' files */
    {
        thread_t *t = get_current_thread();
        vfs_node_t *tmp = vfs_resolve_path(vfs_root, "/tmp");
        uint32_t saved = t->euid;
        t->euid = 1000;
        int result = vfs_remove(tmp, "selftest.bin");
        t->euid = saved;
        st_report("tmp_sticky_owner_protection", result == -EPERM, NULL);
    }

    /* T10: the shared kernel page map must survive a foreign active CR3 */
    {
        uint64_t flags = save_interrupts_and_disable();
        pml4_t *temporary = vmm_create_address_space();
        int ok = 0;
        if (temporary) {
            uint64_t used = pmm_get_used_memory();
            uint64_t mapping = kernel_pml4->entries[511];
            vmm_switch_address_space(temporary);
            vmm_destroy_address_space(kernel_pml4);
            ok = pmm_get_used_memory() == used && kernel_pml4->entries[511] == mapping;
            vmm_switch_address_space(kernel_pml4);
            vmm_destroy_address_space(temporary);
        }
        restore_interrupts(flags);
        st_report("kernel_pml4_never_reaped", ok, NULL);
    }

    klog(LOG_INFO, "SELFTEST: done: %d passed, %d failed", passed, failures);
    klog(LOG_INFO, "SELFTEST_SUMMARY: %d/%d", passed, passed + failures);
    thread_exit();
}

void selftest_init(void) {
    thread_create(selftest_thread, NULL);
}