#ifndef DNS_H
#define DNS_H

#include <stdint.h>

void dns_init();
uint32_t dns_lookup(const char* hostname);

#endif // DNS_H
