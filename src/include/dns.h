#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <stddef.h>

/* Resolve a hostname to IPv4 (host byte order). Returns 0 on failure.
   Blocks until reply or timeout. */
uint32_t dns_resolve(const char *hostname);

void dns_init(uint32_t nameserver_ip);

#endif // DNS_H
