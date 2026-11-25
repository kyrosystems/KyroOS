# 13. Networking

The KyroOS networking subsystem is implemented as a multi-layered protocol stack, which provides network interaction, including low-level operations with network hardware and a high-level socket API for user applications.

## 13.1. Network Stack Architecture

The KyroOS network stack has a traditional multi-layered architecture:

1.  **Data Link Layer:** Represented by network drivers (e.g., E1000). Responsible for data transmission within a single network segment (LAN) using MAC addresses. Includes the ARP protocol.
2.  **Network Layer:** Represented by the IP protocol (IPv4). Responsible for routing packets between different networks using IP addresses.
3.  **Transport Layer:** Represented by the UDP and TCP protocols. Responsible for reliable or unreliable data delivery between applications on different hosts.
4.  **Sockets Layer:** Provides a standardized programming interface (API) for user-space applications to interact with the network stack.

At the current stage, the **implementation of the network core (`net_init`, `net_register_device`) resides directly within the source code of the E1000 driver (`src/kernel/e1000.c`)**. This indicates a tight coupling and an early stage of abstraction, where general network device management functions have not yet been separated into a distinct module.

### Packet Path

#### Incoming Packet:

1.  **Network Card Driver (E1000):** Upon receiving a frame, the E1000 interrupts the CPU. The interrupt handler (`e1000_interrupt_handler`) reads the frame from the DMA buffers.
2.  **Ethernet Demultiplexing:** The driver analyzes the `EtherType` field of the Ethernet header:
    *   If `ARP_ETHER_TYPE`, the frame is passed to `arp_handle_packet()`.
    *   If `ETHERTYPE_IPV4`, the frame is passed to `ip_handle_packet()`.
3.  **IP Layer:** `ip_handle_packet()` processes the IP header (checks checksum, TTL) and demultiplexes the packet based on the `Protocol` field:
    *   If `IP_PROTOCOL_UDP`, the packet is passed to `udp_handle_packet()`.
    *   If `IP_PROTOCOL_TCP`, the packet is passed to `tcp_handle_packet()`.
    *   If `IP_PROTOCOL_ICMP`, the packet is passed to `icmp_handle_packet()`.
4.  **Transport Layer (UDP/TCP/ICMP):** The corresponding handler analyzes its protocol header.
    *   For UDP/TCP, packets intended for open sockets are passed to `sock_handle_incoming_packet()`.
5.  **Sockets Layer:** `sock_handle_incoming_packet()` finds the appropriate `socket_t` endpoint based on IP address, port, and protocol, and places the data into the socket's internal buffer. If there is a waiting thread, it may be unblocked.

#### Outgoing Packet:

1.  **User Application:** Calls a system call (e.g., `SYS_SEND`), which through the `sock_send()` wrapper, passes data to the sockets layer.
2.  **Sockets Layer:** The `sock_send()` (or `sock_connect`) function determines which transport protocol (UDP/TCP) should be used and calls the corresponding protocol-specific function (e.g., `udp_send_packet` or `tcp_send_tcb`).
3.  **Transport Layer (UDP/TCP):** An UDP/TCP header is formed. Then `ip_send_packet()` is called.
4.  **IP Layer:** `ip_send_packet()` forms the IP header, determines the recipient's MAC address via ARP requests (if necessary), and calls the `net_dev->send_packet` function of the registered network device (e.g., `e1000_send_packet`).
5.  **Network Card Driver (E1000):** `e1000_send_packet()` forms the Ethernet frame, places it in the DMA buffer, and initiates transmission over the network.

## 13.2. Supported Protocols

KyroOS supports the following network protocols:

-   **ARP (Address Resolution Protocol):** For mapping IP addresses to MAC addresses.
-   **IP (Internet Protocol, IPv4):** The basic network layer protocol.
-   **ICMP (Internet Control Message Protocol):** Protocol for exchanging error messages and other operational messages over a network (e.g., ping).
-   **UDP (User Datagram Protocol):** A simple, connectionless, unreliable transport layer protocol.
-   **TCP (Transmission Control Protocol):** A connection-oriented, reliable, flow-controlled transport layer protocol. The implementation is partial but functional, with support for the TCP state machine.
-   **DHCP (Dynamic Host Configuration Protocol):** For automatically obtaining an IP address and other network settings from a DHCP server.

## 13.3. Network Drivers

Currently, KyroOS supports a driver for **Intel E1000 Ethernet (Gigabit Ethernet Controller)**, which is widely used in virtual machines (QEMU, VMware) and some real systems.

-   **Registration:** The E1000 driver registers itself with the Device Manager and, after discovering and initializing the hardware, registers its `net_dev_t` structure with the network subsystem (`net_register_device()`).
-   **Functionality:** Provides `send_packet` functions for sending Ethernet frames and handles incoming frames via its interrupt handler.
