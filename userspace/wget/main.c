#include <kyroolib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Max length for a URL part or path segment
#define MAX_URL_PART_LEN 128
// Max length for HTTP request buffer
#define HTTP_REQ_BUFFER_SIZE 512
// Max length for HTTP response buffer for headers
#define HTTP_RESP_BUFFER_SIZE 1024
// Max size for file data buffer during download
#define DOWNLOAD_BUFFER_SIZE 4096

// Simplified URL parsing: assumes host is IP:port, and path is everything after that.
// Example: 192.168.1.100:8080/path/to/file.txt
typedef struct {
    char host[MAX_URL_PART_LEN]; // IP address
    uint16_t port;
    char path[MAX_URL_PART_LEN];
} parsed_url_t;

// Custom IP parsing function (replaces sscanf)
// Assumes dotted-decimal format (e.g., "192.168.1.1")
// Returns true on success, false on failure
bool parse_ip_string(const char *ip_str, uint32_t *ip_out) {
    uint32_t ip_parts[4];
    int current_part = 0;
    uint32_t current_val = 0;
    const char *ptr = ip_str;

    while (*ptr && current_part < 4) {
        if (*ptr >= '0' && *ptr <= '9') {
            current_val = current_val * 10 + (*ptr - '0');
        } else if (*ptr == '.') {
            ip_parts[current_part++] = current_val;
            current_val = 0;
        } else {
            return false; // Invalid character
        }
        ptr++;
    }
    if (current_part == 3) { // Last part
        ip_parts[current_part++] = current_val;
    } else {
        return false; // Not enough parts
    }

    if (current_part != 4) {
        return false;
    }

    *ip_out = (ip_parts[0] << 24) | (ip_parts[1] << 16) | (ip_parts[2] << 8) | ip_parts[3];
    return true;
}


// Function to parse the URL
// Returns true on success, false on failure
bool parse_url(const char *url_str, parsed_url_t *parsed_url) {
    memset(parsed_url, 0, sizeof(parsed_url_t)); // Clear struct

    // Find host part (up to first colon or slash)
    const char *host_start = url_str;
    const char *port_sep = strchr(host_start, ':');
    const char *path_start = strchr(host_start, '/');

    const char *host_end = host_start;
    if (port_sep && (path_start == NULL || port_sep < path_start)) { // IP:Port
        host_end = port_sep;
    } else if (path_start) { // IP/Path
        host_end = path_start;
    } else { // Just IP
        host_end = host_start + strlen(host_start);
    }

    if (host_end - host_start >= MAX_URL_PART_LEN) {
        print("wget: Hostname too long.\n");
        return false;
    }
    memcpy(parsed_url->host, host_start, host_end - host_start);
    parsed_url->host[host_end - host_start] = '\0';

    if (port_sep && (path_start == NULL || port_sep < path_start)) { // Port specified
        parsed_url->port = atoi(port_sep + 1);
        if (parsed_url->port == 0) { // Default to 80 if atoi fails or port is 0
            parsed_url->port = 80;
        }
        if (path_start) {
            strncpy(parsed_url->path, path_start, MAX_URL_PART_LEN - 1);
            parsed_url->path[MAX_URL_PART_LEN - 1] = '\0';
        } else {
            strcpy(parsed_url->path, "/");
        }
    } else if (path_start) { // No port, path specified
        parsed_url->port = 80; // Default HTTP port
        strncpy(parsed_url->path, path_start, MAX_URL_PART_LEN - 1);
        parsed_url->path[MAX_URL_PART_LEN - 1] = '\0';
    } else { // Only host/IP, no port, no path
        parsed_url->port = 80; // Default HTTP port
        strcpy(parsed_url->path, "/");
    }

    // Ensure path starts with /
    if (parsed_url->path[0] != '/') {
        char temp_path[MAX_URL_PART_LEN];
        sprintf(temp_path, "/%s", parsed_url->path);
        strcpy(parsed_url->path, temp_path);
    }

    return true;
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        print("wget: Usage: wget <URL> [output_filename]\n");
        print("  Example: wget 192.168.1.10:80/index.html mypage.html\n");
        print("  Currently only supports HTTP to IP addresses, no DNS.\n");
        return 1;
    }

    const char *url_str = argv[1];
    char output_filename_buf[MAX_URL_PART_LEN];
    const char *output_filename = output_filename_buf;

    parsed_url_t parsed_url;
    if (!parse_url(url_str, &parsed_url)) {
        return 1;
    }

    // Determine output filename
    if (argc >= 3) {
        strncpy(output_filename_buf, argv[2], MAX_URL_PART_LEN - 1);
        output_filename_buf[MAX_URL_PART_LEN - 1] = '\0';
    } else {
        // Extract filename from URL path
        const char *last_slash = strrchr(parsed_url.path, '/');
        if (last_slash && strlen(last_slash + 1) > 0) {
            strncpy(output_filename_buf, last_slash + 1, MAX_URL_PART_LEN - 1);
            output_filename_buf[MAX_URL_PART_LEN - 1] = '\0';
        } else {
            strcpy(output_filename_buf, "index.html"); // Default filename
        }
    }

    print("wget: Downloading "); print(url_str); print(" to "); print(output_filename); print("\n");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        print("wget: Failed to create socket.\n");
        return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(parsed_url.port);
    
    // Convert IP string to uint32_t using custom parser
    if (!parse_ip_string(parsed_url.host, &serv_addr.sin_addr)) {
        print("wget: Invalid IP address format in URL.\n");
        close(sockfd);
        return 1;
    }
    
    if (connect(sockfd, &serv_addr, sizeof(serv_addr)) < 0) { // Removed unnecessary cast
        print("wget: Failed to connect to server.\n");
        close(sockfd);
        return 1;
    }

    char http_request[HTTP_REQ_BUFFER_SIZE];
    sprintf(http_request, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
            parsed_url.path, parsed_url.host);

    if (write(sockfd, http_request, strlen(http_request)) < 0) {
        print("wget: Failed to send HTTP request.\n");
        close(sockfd);
        return 1;
    }

    char http_response_header[HTTP_RESP_BUFFER_SIZE];
    memset(http_response_header, 0, HTTP_RESP_BUFFER_SIZE);
    int total_read = 0;
    int bytes_read;
    bool headers_parsed = false;
    uint32_t content_length = 0;
    char *body_start = NULL;

    // Read response headers
    while (!headers_parsed && (bytes_read = read(sockfd, http_response_header + total_read, HTTP_RESP_BUFFER_SIZE - 1 - total_read)) > 0) {
        total_read += bytes_read;
        http_response_header[total_read] = '\0';
        // Look for end of headers (\r\n\r\n)
        char *header_end = strstr(http_response_header, "\r\n\r\n");
        if (header_end) {
            *header_end = '\0'; // Null-terminate headers
            body_start = header_end + 4; // Point to start of body

            // Basic status check
            if (!strstr(http_response_header, "HTTP/1.0 200 OK") && !strstr(http_response_header, "HTTP/1.1 200 OK")) {
                print("wget: Server returned non-200 OK status.\n");
                print("Headers:\n"); print(http_response_header); print("\n");
                close(sockfd);
                return 1;
            }

            // Look for Content-Length
            char *cl_line = strstr(http_response_header, "Content-Length:");
            if (cl_line) {
                cl_line += strlen("Content-Length:");
                while (*cl_line == ' ') cl_line++; // Skip spaces
                content_length = atoi(cl_line);
            }
            headers_parsed = true;
        }
    }

    if (!headers_parsed) {
        print("wget: Failed to read HTTP headers.\n");
        close(sockfd);
        return 1;
    }

    print("wget: Headers received. Content-Length: ");
    char cl_buf[16];
    sprintf(cl_buf, "%u\n", content_length);
    print(cl_buf);

    int file_fd = open(output_filename, O_CREAT | O_TRUNC | O_WRONLY);
    if (file_fd < 0) {
        print("wget: Failed to create output file.\n");
        close(sockfd);
        return 1;
    }

    // Write any body data already read with headers
    if (body_start && total_read - (body_start - http_response_header) > 0) {
        write(file_fd, body_start, total_read - (body_start - http_response_header));
    }

    // Read remaining body data
    uint32_t downloaded_bytes = (body_start && total_read - (body_start - http_response_header) > 0) ? (total_read - (body_start - http_response_header)) : 0;
    char download_buffer[DOWNLOAD_BUFFER_SIZE];

    while (downloaded_bytes < content_length || content_length == 0) { // Continue if content_length is unknown (chunked, etc.)
        bytes_read = read(sockfd, download_buffer, DOWNLOAD_BUFFER_SIZE);
        if (bytes_read <= 0) {
            break; // Connection closed or error
        }
        write(file_fd, download_buffer, bytes_read);
        downloaded_bytes += bytes_read;
    }

    print("wget: Download complete. Total bytes: ");
    sprintf(cl_buf, "%u\n", downloaded_bytes);
    print(cl_buf);

    close(file_fd);
    close(sockfd);

    return 0;
}