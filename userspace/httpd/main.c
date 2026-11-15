#include "kyroolib.h"
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE  2048

static void send_string(int fd, const char *str) {
    size_t len = strlen(str);
    size_t total_sent = 0;
    while (total_sent < len) {
        int n = write(fd, str + total_sent, len - total_sent);
        if (n <= 0) break;
        total_sent += n;
    }
}

int main(int argc, char **argv) {
    uint16_t port = DEFAULT_PORT;
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p <= 65535) {
            port = (uint16_t)p;
        }
    }

    printf("\n\033[1;36m=====================================================\033[0m\n");
    printf("\033[1;32m      KyroOS Carbon HTTP Server (v26.11.13)          \033[0m\n");
    printf("\033[1;36m=====================================================\033[0m\n");
    printf("[httpd] Initializing TCP server socket...\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("\033[1;31m[httpd] Error: Failed to create socket!\033[0m\n");
        return 1;
    }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr = 0; // INADDR_ANY (0.0.0.0)

    if (bind(server_fd, &srv_addr, sizeof(srv_addr)) != 0) {
        printf("\033[1;31m[httpd] Error: Failed to bind to port %u!\033[0m\n", port);
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) != 0) {
        printf("\033[1;31m[httpd] Error: Failed to listen on socket!\033[0m\n");
        close(server_fd);
        return 1;
    }

    printf("[httpd] Listening for HTTP connections on \033[1;33m0.0.0.0:%u\033[0m\n", port);
    printf("[httpd] Ready to serve requests. Press Ctrl+C to terminate.\n\n");
    print_flush();

    static char req_buf[BUFFER_SIZE];
    uint32_t request_count = 0;

    while (1) {
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        int client_fd = accept(server_fd, &client_addr, NULL);
        if (client_fd < 0) {
            continue;
        }

        request_count++;
        memset(req_buf, 0, sizeof(req_buf));
        int bytes = read(client_fd, req_buf, sizeof(req_buf) - 1);
        if (bytes <= 0) {
            close(client_fd);
            continue;
        }

        // Parse first line: METHOD URI PROTOCOL
        char method[16] = {0};
        char uri[256] = {0};
        int i = 0, j = 0;
        while (i < bytes && req_buf[i] != ' ' && j < 15) {
            method[j++] = req_buf[i++];
        }
        method[j] = '\0';
        while (i < bytes && req_buf[i] == ' ') i++;
        j = 0;
        while (i < bytes && req_buf[i] != ' ' && req_buf[i] != '\r' && req_buf[i] != '\n' && j < 255) {
            uri[j++] = req_buf[i++];
        }
        uri[j] = '\0';

        uint32_t rip = client_addr.sin_addr;
        uint16_t rport = htons(client_addr.sin_port);
        printf("[httpd #%u] %u.%u.%u.%u:%u -> %s %s (%d bytes)\n",
               request_count,
               (rip >> 24) & 0xFF, (rip >> 16) & 0xFF, (rip >> 8) & 0xFF, rip & 0xFF,
               rport, method, uri, bytes);
        print_flush();

        const char *http_header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Server: KyroOS-Carbon/26.11.13\r\n"
            "Connection: close\r\n\r\n";

        const char *html_part1 =
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "  <meta charset=\"UTF-8\">\n"
            "  <title>KyroOS 26.11.13 Carbon</title>\n"
            "  <style>\n"
            "    body { margin:0; padding:40px; background:#121214; color:#e0e0e0; font-family:'Segoe UI',system-ui,sans-serif; }\n"
            "    .container { max-width:850px; margin:0 auto; background:#1c1d22; border:1px solid #2d3039; border-radius:12px; padding:32px; box-shadow:0 8px 32px rgba(0,0,0,0.5); }\n"
            "    h1 { color:#00e5ff; margin-top:0; font-size:28px; font-weight:700; }\n"
            "    .tag { display:inline-block; background:#00e5ff22; color:#00e5ff; border:1px solid #00e5ff55; padding:4px 10px; border-radius:6px; font-size:12px; font-weight:600; text-transform:uppercase; margin-bottom:20px; }\n"
            "    p.lead { font-size:16px; color:#a0a0a8; line-height:1.6; }\n"
            "    .grid { display:grid; grid-template-columns:1fr 1fr; gap:16px; margin:24px 0; }\n"
            "    .card { background:#24262e; border:1px solid #363945; border-radius:8px; padding:18px; }\n"
            "    .card h3 { margin:0 0 8px 0; font-size:16px; color:#76ff03; }\n"
            "    .card p { margin:0; font-size:13px; color:#b0b0ba; line-height:1.5; }\n"
            "    .footer { margin-top:32px; border-top:1px solid #2d3039; padding-top:16px; font-size:12px; color:#6b7280; text-align:center; }\n"
            "  </style>\n"
            "</head>\n"
            "<body>\n"
            "  <div class=\"container\">\n"
            "    <span class=\"tag\">Kernel Architecture v26.11.13</span>\n"
            "    <h1>KyroOS 26.11.13 Carbon</h1>\n"
            "    <p class=\"lead\">Welcome to KyroOS Carbon, a high-performance 64-bit operating system featuring modern kernel primitives, SMP multicore scheduling, and a full POSIX-compliant userspace layer.</p>\n"
            "    <div class=\"grid\">\n"
            "      <div class=\"card\">\n"
            "        <h3>SMP & Synchronization</h3>\n"
            "        <p>Limine SMP AP core initialization, atomic spinlocks with IRQ state preservation, and wait queues.</p>\n"
            "      </div>\n"
            "      <div class=\"card\">\n"
            "        <h3>Copy-on-Write fork()</h3>\n"
            "        <p>Physical page refcounting in PMM, user page table cloning with PAGE_COW, and page fault demand-allocation.</p>\n"
            "      </div>\n"
            "      <div class=\"card\">\n"
            "        <h3>Disk FS Block Bitmap</h3>\n"
            "        <p>Bitmap block allocator with dynamic allocation and freeing on unlink, plus direct/indirect inode mapping.</p>\n"
            "      </div>\n"
            "      <div class=\"card\">\n"
            "        <h3>AHCI & VirtIO Hardware</h3>\n"
            "        <p>SATA AHCI DMA driver, virtio-blk, virtio-net, and FAT32 filesystem mounting.</p>\n"
            "      </div>\n"
            "      <div class=\"card\">\n"
            "        <h3>Doom & Scripting</h3>\n"
            "        <p>Full stdio libc implementation, Doom generic engine port, and embedded Lua 5.3 interpreter.</p>\n"
            "      </div>\n"
            "      <div class=\"card\">\n"
            "        <h3>POSIX Signals & Network</h3>\n"
            "        <p>sigaction, sigprocmask, Ctrl+C SIGINT handling, SIGCHLD, TCP server stack (bind, listen, accept).</p>\n"
            "      </div>\n"
            "    </div>\n"
            "    <div class=\"footer\">\n"
            "      KyroOS Carbon 26.11.13 &bull; Powered by KyroOS Kernel &amp; Built-in HTTP Server\n"
            "    </div>\n"
            "  </div>\n"
            "</body>\n"
            "</html>\n";

        send_string(client_fd, http_header);
        send_string(client_fd, html_part1);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
