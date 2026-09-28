#ifndef PROCINFO_H
#define PROCINFO_H

/*
 * Shared kernel/userspace structures for process & system introspection.
 * Used by the KyroOS-specific syscalls proc_list / sysinfo / net_ifconf
 * (consumers: sysmon, ps, free, ifconfig). These are KyroOS ABI structures,
 * NOT POSIX <sys/procinfo.h>.
 */

#include <stdint.h>
#include <stddef.h>

/* Thread states mirror thread_state_t (kept numeric for ABI stability) */
#define PROC_STATE_RUNNING 0
#define PROC_STATE_READY   1
#define PROC_STATE_BLOCKED 2
#define PROC_STATE_DEAD    3

struct procinfo {
    uint32_t pid;          /* thread id (KyroOS: thread == process) */
    uint32_t uid;          /* real uid */
    uint32_t euid;         /* effective uid */
    uint8_t  state;        /* PROC_STATE_* */
    char     name[32];     /* process name (basename of exec path) */
    uint64_t cpu_ticks;    /* scheduler ticks consumed (100 Hz) */
    uint64_t start_ticks;  /* scheduler tick at creation */
} __attribute__((packed));

struct sysinfo_k {
    uint32_t mem_total_kb;
    uint32_t mem_used_kb;
    uint32_t uptime_ticks; /* 100 Hz timer ticks since boot */
    uint32_t procs_total;  /* runnable + blocked threads */
    uint64_t rx_bytes;     /* sum across network devices */
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
};

/* Interface flags mirror IFF_* from net.h */
#define IFIF_UP       0x01
#define IFIF_LOOPBACK 0x02
#define IFIF_RUNNING  0x04
#define IFIF_COUNTERS 0x08 /* device provides real rx/tx counters */

struct ifinfo {
    char     name[16];     /* device name from deviceman (e.g. "e1000") */
    uint8_t  mac[6];
    uint32_t ip;           /* host-readable order, 0 = unconfigured */
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;          /* 0 = not reported by driver */
    uint32_t flags;        /* IFIF_* */
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_errors, tx_errors;
    uint64_t rx_dropped, tx_dropped;
    uint64_t collisions;
} __attribute__((packed));

#endif /* PROCINFO_H */
