#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>
#include <stddef.h>

void dhcp_client_start(void);
void dhcp_init(void);
uint32_t dhcp_get_local_ip(void);
uint32_t dhcp_get_gateway_ip(void);
uint32_t dhcp_get_subnet_mask(void);
void dhcp_handle_packet(const uint8_t *data, size_t len);

#endif // DHCP_H