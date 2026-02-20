#include "dhcp.h"
#include "udp.h"
#include "ip.h"
#include "ip.h"
#include "net.h"
#include "log.h"
#include "kstring.h"
#include <stdbool.h>

// DHCP states
typedef enum {
    DHCP_STATE_INIT,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
} dhcp_state_t;

// DHCP op codes
#define DHCP_BOOTREQUEST    1
#define DHCP_BOOTREPLY      2

// DHCP options
#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS_SERVER    6
#define DHCP_OPTION_REQ_IP_ADDR   50
#define DHCP_OPTION_LEASE_TIME    51
#define DHCP_OPTION_MSG_TYPE      53
#define DHCP_OPTION_SERVER_ID     54
#define DHCP_OPTION_PARAM_REQ     55
#define DHCP_OPTION_END           255

// DHCP message types
#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPDECLINE     4
#define DHCPACK         5
#define DHCPNAK         6
#define DHCPRELEASE     7

// DHCP packet structure
typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic_cookie;
    uint8_t options[312];
} __attribute__((packed)) dhcp_packet_t;

static dhcp_state_t dhcp_state = DHCP_STATE_INIT;
static uint32_t local_ip = 0;
static uint32_t gateway_ip = 0;
static uint32_t subnet_mask = 0;
static uint32_t dhcp_server_ip = 0;
static uint32_t dhcp_xid = 0x12345678; // Transaction ID

static void dhcp_send_discover();
static void dhcp_send_request();

void dhcp_client_start() {
    if (dhcp_state == DHCP_STATE_INIT) {
        klog(LOG_INFO, "DHCP: Starting client...");
        dhcp_send_discover();
        dhcp_state = DHCP_STATE_SELECTING;
    }
}

static void dhcp_send_discover() {
    dhcp_packet_t discover_packet;
    memset(&discover_packet, 0, sizeof(dhcp_packet_t));

    discover_packet.op = DHCP_BOOTREQUEST;
    discover_packet.htype = 1; // Ethernet
    discover_packet.hlen = 6;  // MAC address length
    discover_packet.xid = __builtin_bswap32(dhcp_xid);
    discover_packet.flags = __builtin_bswap16(0x8000); // Broadcast flag

    memcpy(discover_packet.chaddr, network_devices->mac_addr, 6);

    discover_packet.magic_cookie = __builtin_bswap32(0x63825363);

    uint8_t *options = discover_packet.options;
    // DHCP Message Type: Discover
    *options++ = DHCP_OPTION_MSG_TYPE;
    *options++ = 1;
    *options++ = DHCPDISCOVER;

    // Parameter Request List
    *options++ = DHCP_OPTION_PARAM_REQ;
    *options++ = 3;
    *options++ = DHCP_OPTION_SUBNET_MASK;
    *options++ = DHCP_OPTION_ROUTER;
    *options++ = DHCP_OPTION_DNS_SERVER;

    *options++ = DHCP_OPTION_END;

    klog(LOG_INFO, "DHCP: Sending DISCOVER packet...");
    udp_send_packet(0xFFFFFFFF, 68, 67, (uint8_t *)&discover_packet, sizeof(dhcp_packet_t));
}

static void dhcp_send_request() {
    dhcp_packet_t request_packet;
    memset(&request_packet, 0, sizeof(dhcp_packet_t));

    request_packet.op = DHCP_BOOTREQUEST;
    request_packet.htype = 1; // Ethernet
    request_packet.hlen = 6;  // MAC address length
    request_packet.xid = __builtin_bswap32(dhcp_xid);
    request_packet.flags = __builtin_bswap16(0x8000); // Broadcast flag

    memcpy(request_packet.chaddr, network_devices->mac_addr, 6);

    request_packet.magic_cookie = __builtin_bswap32(0x63825363);

    uint8_t *options = request_packet.options;
    // DHCP Message Type: Request
    *options++ = DHCP_OPTION_MSG_TYPE;
    *options++ = 1;
    *options++ = DHCPREQUEST;

    // Requested IP Address
    *options++ = DHCP_OPTION_REQ_IP_ADDR;
    *options++ = 4;
    *(uint32_t*)options = local_ip; // The IP offered
    options += 4;

    // Server Identifier
    if (dhcp_server_ip != 0) {
        *options++ = DHCP_OPTION_SERVER_ID;
        *options++ = 4;
        *(uint32_t*)options = dhcp_server_ip;
        options += 4;
    }

    *options++ = DHCP_OPTION_END;

    klog(LOG_INFO, "DHCP: Sending REQUEST packet...");
    udp_send_packet(0xFFFFFFFF, 68, 67, (uint8_t *)&request_packet, sizeof(dhcp_packet_t));
}

void dhcp_handle_packet(const uint8_t *data, size_t len) {
    if (len < sizeof(dhcp_packet_t) - 312) { // Minimum size
        return;
    }
    dhcp_packet_t *packet = (dhcp_packet_t *)data;

    if (packet->op != DHCP_BOOTREPLY || packet->xid != __builtin_bswap32(dhcp_xid)) {
        return;
    }

    uint8_t *options = packet->options;
    uint8_t msg_type = 0;
    uint8_t *opt_ptr;

    // Find DHCP Message Type
    for (opt_ptr = options; opt_ptr < data + len && *opt_ptr != DHCP_OPTION_END; ) {
        if (*opt_ptr == DHCP_OPTION_MSG_TYPE) {
            msg_type = *(opt_ptr + 2);
            break;
        }
        opt_ptr += *(opt_ptr + 1) + 2;
    }
    
    if (dhcp_state == DHCP_STATE_SELECTING && msg_type == DHCPOFFER) {
        klog(LOG_INFO, "DHCP: Received OFFER, yiaddr=%x", __builtin_bswap32(packet->yiaddr));
        local_ip = packet->yiaddr; // Store offered IP
        // Extract server IP from options
        for (opt_ptr = options; opt_ptr < data + len && *opt_ptr != DHCP_OPTION_END; ) {
            if (*opt_ptr == DHCP_OPTION_SERVER_ID) {
                memcpy(&dhcp_server_ip, opt_ptr + 2, 4);
                break;
            }
            opt_ptr += *(opt_ptr + 1) + 2;
        }
        dhcp_state = DHCP_STATE_REQUESTING;
        dhcp_send_request();
    } else if (dhcp_state == DHCP_STATE_REQUESTING && msg_type == DHCPACK) {
        klog(LOG_INFO, "DHCP: Received ACK, IP address is now %x", __builtin_bswap32(packet->yiaddr));
        local_ip = packet->yiaddr;
        ip_set_local_ip(local_ip); // Update IP layer
        // Parse subnet mask and gateway
        for (opt_ptr = options; opt_ptr < data + len && *opt_ptr != DHCP_OPTION_END; ) {
            if (*opt_ptr == DHCP_OPTION_SUBNET_MASK) {
                memcpy(&subnet_mask, opt_ptr + 2, 4);
                ip_set_subnet_mask(subnet_mask); // Update IP layer
            } else if (*opt_ptr == DHCP_OPTION_ROUTER) {
                memcpy(&gateway_ip, opt_ptr + 2, 4);
                ip_set_default_gateway(gateway_ip); // Update IP layer
            }
            opt_ptr += *(opt_ptr + 1) + 2;
        }
        dhcp_state = DHCP_STATE_BOUND;
        klog(LOG_INFO, "DHCP: State is BOUND. IP: %x, GW: %x, Mask: %x", __builtin_bswap32(local_ip), __builtin_bswap32(gateway_ip), __builtin_bswap32(subnet_mask));
    }
}

uint32_t dhcp_get_local_ip() {
    return local_ip;
}

uint32_t dhcp_get_gateway_ip() {
    return gateway_ip;
}

uint32_t dhcp_get_subnet_mask() {
    return subnet_mask;
}

void dhcp_init() {
    // No specific initialization needed for now before starting the client
    klog(LOG_INFO, "DHCP: Initialized (stub).");
}
