#include <kyroolib.h>
#include <string.h>

/* ifconfig - report interfaces via SYS_NET_IFCONF. Counters come from
 * real driver structures; unavailable data is printed as "-" (or
 * "unconfigured"), never as fake zeros. */

static void print_ip(uint32_t ip) {
    char b[20];
    sprintf(b, "%u.%u.%u.%u",
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    print(b);
}

static void print_mac(const uint8_t *m) {
    char b[22];
    sprintf(b, "%02x:%02x:%02x:%02x:%02x:%02x",
            m[0], m[1], m[2], m[3], m[4], m[5]);
    print(b);
}



static void print_flags(uint32_t f) {
    int first = 1;
    print("<");
    if (f & IFIF_UP)       { print(first ? "UP" : ",UP"); first = 0; }
    if (f & IFIF_LOOPBACK) { print(first ? "LOOPBACK" : ",LOOPBACK"); first = 0; }
    if (f & IFIF_RUNNING)  { print(first ? "RUNNING" : ",RUNNING"); first = 0; }
    if (first) print("-");
    print(">");
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: ifconfig [INTERFACE] [IP]\n");
        print("Configure network interfaces.\n");
        print_flush();
        return 0;
    }
    (void)argc; (void)argv;
    struct ifinfo ifs[8];
    int n = net_ifconf(ifs, 8);
    if (n < 0) {
        print("ifconfig: net_ifconf failed (errno ");
        char b[12]; sprintf(b, "%d", errno); print(b);
        print(")\n");
        return 1;
    }
    if (n == 0) {
        print("ifconfig: no network interfaces registered\n");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        struct ifinfo *d = &ifs[i];
        print(d->name);
        print(": flags=");
        print_flags(d->flags);
        print("  mtu ");
        if (d->mtu) {
            char b[12]; sprintf(b, "%u", d->mtu); print(b);
        } else {
            print("-");
        }
        print("\n");

        print("        ether ");
        print_mac(d->mac);
        print("\n");

        print("        inet ");
        if (d->ip) {
            print_ip(d->ip);
            if (d->netmask) {
                print("  netmask ");
                print_ip(d->netmask);
            }
            if (d->gateway) {
                print("  gateway ");
                print_ip(d->gateway);
            }
        } else {
            print("unconfigured");
        }
        print("\n");

        /* Counters: only shown when the device provides real data. */
        if (d->flags & IFIF_COUNTERS) {
            print("        RX packets ");
            print_u64(d->rx_packets);
            print("  bytes ");
            print_u64(d->rx_bytes);
            print("  errors ");
            print_u64(d->rx_errors);
            print("  dropped ");
            print_u64(d->rx_dropped);
            print("\n");
            print("        TX packets ");
            print_u64(d->tx_packets);
            print("  bytes ");
            print_u64(d->tx_bytes);
            print("  errors ");
            print_u64(d->tx_errors);
            print("  dropped ");
            print_u64(d->tx_dropped);
            print("\n");
            print("        collisions ");
            print_u64(d->collisions);
            print("\n");
        } else {
            print("        counters: not available (driver does not report)\n");
        }
        print("\n");
    }
    return 0;
}
