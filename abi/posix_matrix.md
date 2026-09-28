# KyroOS POSIX Matrix

Status of POSIX-like interfaces in KyroOS as of the ABI freeze
(`abi/syscalls.def`, `abi/abi_frozen.json`). Generated numbers live in
`src/include/sysnum.h`; do not edit them by hand.

Legend: ✅ implemented · 🟡 partial / simplified · ❌ not implemented

## Processes

| Interface | Status | Notes |
|---|---|---|
| `exit` | ✅ | terminates thread; reaper frees kernel stack + address space |
| `getpid` | ✅ | thread id (KyroOS: thread == process) |
| `kill` | 🟡 | terminates only (SIGKILL/SIGTERM equivalent); no signal delivery |
| `exec`-like spawn | 🟡 | `SYS_EXEC` spawns a new thread; no true exec-replacement, no `fork` |
| `wait`/`waitpid` | ❌ | no child tracking (shell runs children asynchronously) |
| signals | ❌ | terminate-only semantics |

## Memory

| Interface | Status | Notes |
|---|---|---|
| `brk`/`sbrk` | ✅ | per-thread program break |
| `malloc`/`free` (kernel heap) | ✅ | via SYS_MALLOC/SYS_FREE |
| mmap / shared memory | ❌ | — |

## Files

| Interface | Status | Notes |
|---|---|---|
| `open`/`close`/`read`/`write` | ✅ | O_CREAT/O_TRUNC supported; errors return `-errno` |
| `stat`/`fstat` | ✅ | reports uid/gid/mode/type |
| `mkdir`/`unlink`/`rmdir` | 🟡 | works on KyroFS; permission-checked |
| `readdir` | ✅ | fd-based; directory-only (-ENOTDIR otherwise) |
| `chdir`/`getcwd` | ✅ | per-thread cwd; relative paths resolved for new syscalls |
| `dup`/`dup2` | 🟡 | duplicates fd entries; offsets are NOT shared (deviation from POSIX) |
| `lseek` | ✅ | files only; sockets → -ESPIPE |
| permissions (R/W/X) | ✅ | kernel/VFS level: owner/group/other, root bypass |
| `umask` | ✅ | inherited by exec; applied at file creation |
| `chmod`/`fchmod` | ✅ | owner or root only |
| `chown`/`fchown` | ✅ | root only (per registry) |
| symlinks, hardlinks | ❌ | VFS has no link count model |

## Users & credentials

| Interface | Status | Notes |
|---|---|---|
| `getuid/geteuid/getgid/getegid` | ✅ | real + effective credentials |
| `setuid/setgid` | ✅ | root only (no saved-set-uid model) |
| `seteuid/setegid` | 🟡 | root: any value; non-root: drop back to real uid |
| password store | ✅ | `/etc/shadow`: per-user random salt + SHA-256(salt‖pw) |
| `passwd`, `su` | ✅ | su is root→user only (no fork) |
| supplementary groups | ❌ | single gid per thread |

## Sockets / network

| Interface | Status | Notes |
|---|---|---|
| `socket(AF_INET, SOCK_STREAM, TCP)` | ✅ | client side; listen/accept stubs |
| `socket(AF_INET, SOCK_DGRAM, UDP)` | ✅ | bind/send/recv/recvfrom |
| `socket(AF_INET, SOCK_RAW, ICMP)` | ✅ | root-only (EACCES otherwise); used by ping |
| `sendto`/`recvfrom` | ✅ | address in/out; SO_RCVTIMEO honored (tick deadline) |
| `setsockopt` | 🟡 | SOL_SOCKET: SO_RCVTIMEO/SO_SNDTIMEO only |
| `shutdown`/`getsockname` | ✅ | — |
| DNS | 🟡 | kernel resolver SYS_DNS_RESOLVE; 5 s deadline, no cache |
| `epoll`/`select`/`poll` | ❌ | — |

## Time

| Interface | Status | Notes |
|---|---|---|
| `times`-like uptime | ✅ | SYS_GET_TICKS (100 Hz PIT), SYS_SYSINFO uptime |
| wall clock | ❌ | RTC exists but no libc wrapper/time_t API yet |

## Known contract

- Kernel returns `(uint64_t)-errno` (1..133 range); libc wrapper maps to
  `-1` + `errno`. Legacy `-1` returns remain in a few old handlers and are
  documented in the registry as partial.
- No COW/fork: `SYS_EXEC` creates an independent address space.

## TLS/HTTPS status (honest)

**Not implemented.** The kernel crypto module provides SHA-256 only
(`SYS_SHA256`). TLS 1.2/1.3 requires X.509 parsing, ECDHE/RSA key exchange,
AES-GCM/ChaCha20-Poly1305, record layer and handshake state machine — none of
which exist. Bundling a vendored TLS stack (mbedTLS/wolfSSL) is the planned
path; until then `wget`/`curl` work over plain HTTP only and MUST NOT present
HTTPS as functional. System trust store layout (`/etc/ssl/certs`) is reserved
for the future stack.
