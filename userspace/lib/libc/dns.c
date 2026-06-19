#include <kyroolib.h>
#include <stdint.h>

uint32_t dns_resolve(const char *host) {
    return (uint32_t)syscall(SYS_DNS_RESOLVE, (uint64_t)host, 0, 0);
}