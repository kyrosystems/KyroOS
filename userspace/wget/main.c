#include <kyroolib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_URL_PART_LEN    256
#define HTTP_REQ_BUF_SIZE   1024
#define HTTP_RESP_HDR_SIZE  4096
#define DOWNLOAD_BUF_SIZE   8192
#define MAX_HOSTNAME_LEN    256

typedef struct {
    char     scheme[8];      // "http" or "https" (https not supported temporarily)
    char     host[MAX_URL_PART_LEN];
    uint16_t port;
    char     path[MAX_URL_PART_LEN];
} parsed_url_t;

// helpers

static bool parse_ip_string(const char *s, uint32_t *out) {
    uint32_t parts[4];
    int      n    = 0;
    uint32_t cur  = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            cur = cur * 10 + (*p - '0');
            if (cur > 255) return false;
        } else if (*p == '.' || *p == '\0') {
            if (n >= 4) return false;
            parts[n++] = cur; cur = 0;
            if (*p == '\0') break;
        } else { return false; }
    }
    if (n != 4) return false;
    *out = (parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8)|parts[3];
    return true;
}

// Resolve host: try IP first, then DNS
static uint32_t resolve_host(const char *host) {
    uint32_t ip = 0;
    if (parse_ip_string(host, &ip)) return ip;
    // dns_resolve is provided by kernel DNS module via syscall / kyroolib 
    ip = dns_resolve(host);
    return ip;
}

// Determine default filename from URL path
static bool parse_url(const char *url, parsed_url_t *out) {
    memset(out, 0, sizeof(*out));
    out->port = 80;
    strcpy(out->scheme, "http");

    const char *p = url;

    // Parse scheme if present
    if (strncmp(p, "http://", 7) == 0)  { p += 7; }
    else if (strncmp(p, "https://", 8) == 0) {
        strcpy(out->scheme, "https");
        p += 8;
        out->port = 443;
    }

    // Parse host and optional port
    const char *host_start = p;
    while (*p && *p != ':' && *p != '/') p++;
    size_t hlen = (size_t)(p - host_start);
    if (hlen == 0 || hlen >= MAX_URL_PART_LEN) {
        print("wget: Invalid host in URL.\n"); return false;
    }
    memcpy(out->host, host_start, hlen);
    out->host[hlen] = '\0';

    if (*p == ':') {
        p++;
        uint32_t port = 0;
        while (*p >= '0' && *p <= '9') port = port*10 + (*p++ - '0');
        if (port == 0 || port > 65535) { print("wget: Invalid port.\n"); return false; }
        out->port = (uint16_t)port;
    }

    if (*p == '/') {
        strncpy(out->path, p, MAX_URL_PART_LEN - 1);
        out->path[MAX_URL_PART_LEN - 1] = '\0';
    } else {
        strcpy(out->path, "/");
    }
    return true;
}

// Determine default filename from URL path
static void default_filename(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    const char *name  = (slash && *(slash+1)) ? slash+1 : NULL;
    if (name && strlen(name) > 0) {
        strncpy(out, name, outsz - 1);
        out[outsz - 1] = '\0';
    } else {
        strncpy(out, "index.html", outsz - 1);
        out[outsz - 1] = '\0';
    }
}

// Main function
int main(int argc, char **argv) {
    const char *url_str    = NULL;
    const char *out_file   = NULL;
    bool        verbose    = false;
    int         max_redirect = 5;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if ((strcmp(argv[i], "-O") == 0 || strcmp(argv[i], "--output-document") == 0) && i+1 < argc) {
            out_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print("Usage: wget [-v] [-O output] <URL>\n");
            print("  Supports: http://host[:port]/path\n");
            print("  DNS resolution supported.\n");
            return 0;
        } else if (argv[i][0] != '-') {
            url_str = argv[i];
        }
    }

    if (!url_str) {
        print("wget: No URL specified. Try --help.\n");
        return 1;
    }

    parsed_url_t pu;
    if (!parse_url(url_str, &pu)) return 1;

    char out_buf[MAX_URL_PART_LEN];
    if (!out_file) {
        default_filename(pu.path, out_buf, sizeof(out_buf));
        out_file = out_buf;
    }

    if (strcmp(pu.scheme, "https") == 0) {
        print("wget: HTTPS not supported (no TLS). Try http://\n");
        return 1;
    }

    print("wget: Resolving "); print(pu.host); print("...\n");
    uint32_t server_ip = resolve_host(pu.host);
    if (!server_ip) {
        print("wget: Could not resolve host: "); print(pu.host); print("\n");
        return 1;
    }

    char ip_str[20];
    sprintf(ip_str, "%u.%u.%u.%u",
            (server_ip>>24)&0xFF, (server_ip>>16)&0xFF,
            (server_ip>>8)&0xFF,   server_ip&0xFF);
    if (verbose) { print("wget: Connecting to "); print(ip_str); print("\n"); }

    // Copy path to mutable buffer for potential redirect handling
    char current_path[MAX_URL_PART_LEN];
    strncpy(current_path, pu.path, MAX_URL_PART_LEN - 1);
    current_path[MAX_URL_PART_LEN - 1] = '\0';

    int redirect_count = 0;
redirect:;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { print("wget: socket() failed.\n"); return 1; }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(pu.port);
    addr.sin_addr   = server_ip;

    if (connect(sockfd, &addr, sizeof(addr)) < 0) {
        print("wget: connect() failed.\n");
        close(sockfd);
        return 1;
    }
    // Send HTTP GET request
    char req[HTTP_REQ_BUF_SIZE];
    int  req_len = sprintf(req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: KyroOS-wget/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        current_path, pu.host);

    if (write(sockfd, req, req_len) < 0) {
        print("wget: Failed to send request.\n");
        close(sockfd); return 1;
    }

    // Read headers into buffer 
    static char hdr_buf[HTTP_RESP_HDR_SIZE];
    memset(hdr_buf, 0, sizeof(hdr_buf));
    int   hdr_total  = 0;
    char *body_ptr   = NULL;
    int   body_preread = 0;

    while (hdr_total < (int)sizeof(hdr_buf) - 1) {
        int n = read(sockfd, hdr_buf + hdr_total, sizeof(hdr_buf) - 1 - hdr_total);
        if (n <= 0) break;
        hdr_total += n;
        hdr_buf[hdr_total] = '\0';
        char *sep = strstr(hdr_buf, "\r\n\r\n");
        if (sep) {
            body_ptr    = sep + 4;
            body_preread = (int)(hdr_buf + hdr_total - body_ptr);
            *sep = '\0'; // terminate header string
            break;
        }
    }

    if (!body_ptr) {
        print("wget: Incomplete HTTP response.\n");
        close(sockfd); return 1;
    }

    // Parse HTTP status code from the first line of the response
    int status_code = 0;
    {
        char *sp = strchr(hdr_buf, ' ');
        if (sp) status_code = atoi(sp + 1);
    }

    if (verbose) {
        print("wget: HTTP status "); char sc[8]; sprintf(sc, "%d\n", status_code); print(sc);
    }

    // Handle redirects (301, 302, 303, 307, 308)
    if ((status_code == 301 || status_code == 302 ||
         status_code == 303 || status_code == 307 || status_code == 308)
         && redirect_count < max_redirect) {
        char *loc = strstr(hdr_buf, "Location:");
        if (loc) {
            loc += 9;
            while (*loc == ' ') loc++;
            char *eol = strstr(loc, "\r\n");
            if (!eol) eol = loc + strlen(loc);
            char new_url[MAX_URL_PART_LEN * 2];
            size_t ulen = (size_t)(eol - loc);
            if (ulen >= sizeof(new_url)) ulen = sizeof(new_url) - 1;
            memcpy(new_url, loc, ulen); new_url[ulen] = '\0';
            close(sockfd);
            redirect_count++;
            print("wget: Redirect -> "); print(new_url); print("\n");
            if (!parse_url(new_url, &pu)) return 1;
            strncpy(current_path, pu.path, MAX_URL_PART_LEN - 1);
            server_ip = resolve_host(pu.host);
            if (!server_ip) { print("wget: DNS failed after redirect.\n"); return 1; }
            goto redirect;
        }
    }

    if (status_code != 200) {
        print("wget: Server returned "); char sc[8]; sprintf(sc, "%d\n", status_code); print(sc);
        close(sockfd); return 1;
    }

    // Extract Content-Length if present
    uint32_t content_length = 0;
    bool     has_cl = false;
    {
        char *cl = strstr(hdr_buf, "Content-Length:");
        if (!cl) cl = strstr(hdr_buf, "content-length:");
        if (cl) {
            cl += 15;
            while (*cl == ' ') cl++;
            content_length = (uint32_t)atoi(cl);
            has_cl = true;
        }
    }

    if (verbose && has_cl) {
        char cls[16]; sprintf(cls, "%u", content_length); print("wget: Content-Length: "); print(cls); print("\n");
    }

    int file_fd = open(out_file, O_CREAT | O_TRUNC | O_WRONLY);
    if (file_fd < 0) {
        print("wget: Cannot create file: "); print(out_file); print("\n");
        close(sockfd); return 1;
    }

    // Write any preread body data to file
    uint32_t downloaded = 0;
    if (body_preread > 0) {
        write(file_fd, body_ptr, body_preread);
        downloaded += body_preread;
    }

    // Read the rest of the response body and write to file
    static char dl_buf[DOWNLOAD_BUF_SIZE];
    while (1) {
        if (has_cl && downloaded >= content_length) break;
        int n = read(sockfd, dl_buf, sizeof(dl_buf));
        if (n <= 0) break;
        write(file_fd, dl_buf, n);
        downloaded += n;
    }

    close(file_fd);
    close(sockfd);

    print("wget: Saved '"); print(out_file); print("' (");
    char sz[16]; sprintf(sz, "%u bytes)\n", downloaded); print(sz);
    return 0;
}