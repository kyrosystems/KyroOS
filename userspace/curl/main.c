#include <kyroolib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

uint32_t dns_resolve(const char *host); 

#define MAX_URL_LEN      512
#define MAX_HEADER_LEN   256
#define REQ_BUF_SIZE     4096
#define HDR_BUF_SIZE     8192
#define BODY_BUF_SIZE    8192

typedef struct {
    char     scheme[8];
    char     host[MAX_URL_LEN];
    uint16_t port;
    char     path[MAX_URL_LEN];
} url_t;

typedef struct {
    const char *name;
    const char *value;
} header_t;

#define MAX_EXTRA_HEADERS 16
#define MAX_DATA_LEN      4096

typedef struct {
    const char *method;
    url_t       url;
    const char *output_file;
    bool        include_headers;
    bool        silent;
    bool        verbose;
    bool        head_only;
    bool        follow_redirect;
    char        data[MAX_DATA_LEN];
    header_t    headers[MAX_EXTRA_HEADERS];
    int         header_count;
    const char *upload_file;
} curl_opts_t;

static uint32_t resolve_host(const char *host) {
    uint32_t parts[4]; int n=0; uint32_t cur=0; bool ok=true;
    for (const char *p = host; ; p++) {
        if (*p>='0'&&*p<='9') { cur=cur*10+(*p-'0'); if(cur>255){ok=false;break;} }
        else if (*p=='.'||*p=='\0') {
            if(n>=4){ok=false;break;}
            parts[n++]=cur; cur=0; if(*p=='\0') break;
        } else { ok=false; break; }
    }
    if (ok && n==4) return (parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8)|parts[3];
    return dns_resolve(host);
}

static bool parse_url(const char *raw, url_t *out) {
    memset(out, 0, sizeof(*out));
    out->port = 80;
    strcpy(out->scheme, "http");
    const char *p = raw;
    if      (strncmp(p,"http://",7)==0)  { p+=7; }
    else if (strncmp(p,"https://",8)==0) { strcpy(out->scheme,"https"); p+=8; out->port=443; }

    const char *hs = p;
    while (*p && *p!=':' && *p!='/') p++;
    size_t hl = (size_t)(p-hs);
    if (!hl || hl>=MAX_URL_LEN) return false;
    memcpy(out->host, hs, hl); out->host[hl]='\0';

    if (*p==':') {
        p++; uint32_t port=0;
        while (*p>='0'&&*p<='9') port=port*10+(*p++-'0');
        if (!port||port>65535) return false;
        out->port=(uint16_t)port;
    }
    if (*p=='/') { strncpy(out->path,p,MAX_URL_LEN-1); out->path[MAX_URL_LEN-1]='\0'; }
    else strcpy(out->path,"/");
    return true;
}

static void print_ip_from_u32(uint32_t ip) {
    char buf[20];
    sprintf(buf,"%u.%u.%u.%u",(ip>>24)&0xFF,(ip>>16)&0xFF,(ip>>8)&0xFF,ip&0xFF);
    print(buf);
}

static int do_request(curl_opts_t *opts, int redirect_depth) {
    if (redirect_depth > 10) { print("curl: Too many redirects.\n"); return 1; }

    if (strcmp(opts->url.scheme,"https")==0) {
        print("curl: HTTPS not supported (no TLS).\n"); return 1;
    }

    if (opts->verbose) {
        print("* Resolving "); print(opts->url.host); print("...\n");
    }

    uint32_t server_ip = resolve_host(opts->url.host);
    if (!server_ip) {
        print("curl: Could not resolve host: "); print(opts->url.host); print("\n");
        return 6;
    }

    if (opts->verbose) {
        print("* Connecting to "); print(opts->url.host); print(" (");
        print_ip_from_u32(server_ip); print(")\n");
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { print("curl: socket() failed.\n"); return 1; }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(opts->url.port);
    addr.sin_addr   = server_ip;

    if (connect(sockfd, &addr, sizeof(addr)) < 0) {
        print("curl: connect() failed.\n"); close(sockfd); return 7;
    }
    if (opts->verbose) print("* Connected.\n");

    static char req[REQ_BUF_SIZE];
    int pos = 0;

    static char upload_buf[MAX_DATA_LEN];
    int upload_len = 0;
    if (opts->upload_file) {
        int ufd = open(opts->upload_file, O_RDONLY);
        if (ufd < 0) { print("curl: Cannot open upload file.\n"); close(sockfd); return 1; }
        upload_len = read(ufd, upload_buf, sizeof(upload_buf)-1);
        close(ufd);
        if (upload_len < 0) upload_len = 0;
    }

    const char *method = opts->method;
    if (!method) {
        if (opts->head_only)           method = "HEAD";
        else if (opts->data[0])        method = "POST";
        else if (opts->upload_file)    method = "PUT";
        else                           method = "GET";
    }

    pos += sprintf(req+pos, "%s %s HTTP/1.1\r\n", method, opts->url.path);
    pos += sprintf(req+pos, "Host: %s\r\n", opts->url.host);
    pos += sprintf(req+pos, "User-Agent: KyroOS-curl/1.0\r\n");
    pos += sprintf(req+pos, "Accept: */*\r\n");
    pos += sprintf(req+pos, "Connection: close\r\n");

    for (int i=0; i<opts->header_count; i++) {
        pos += sprintf(req+pos, "%s: %s\r\n",
                       opts->headers[i].name, opts->headers[i].value);
    }

    const char *body     = NULL;
    int         body_len = 0;
    if (opts->data[0]) { body = opts->data; body_len = (int)strlen(opts->data); }
    else if (upload_len > 0) { body = upload_buf; body_len = upload_len; }

    if (body_len > 0) {
        pos += sprintf(req+pos, "Content-Length: %d\r\n", body_len);
        if (!opts->data[0])
            pos += sprintf(req+pos, "Content-Type: application/octet-stream\r\n");
        else
            pos += sprintf(req+pos, "Content-Type: application/x-www-form-urlencoded\r\n");
    }
    pos += sprintf(req+pos, "\r\n");

    if (opts->verbose) { print("> "); }

    write(sockfd, req, pos);
    if (body_len > 0) write(sockfd, body, body_len);

    static char hdr_buf[HDR_BUF_SIZE];
    memset(hdr_buf, 0, sizeof(hdr_buf));
    int   hdr_total    = 0;
    char *body_start   = NULL;
    int   body_preread = 0;

    while (hdr_total < (int)sizeof(hdr_buf)-1) {
        int n = read(sockfd, hdr_buf+hdr_total, sizeof(hdr_buf)-1-hdr_total);
        if (n<=0) break;
        hdr_total += n; hdr_buf[hdr_total]='\0';
        char *sep = strstr(hdr_buf,"\r\n\r\n");
        if (sep) {
            body_start   = sep+4;
            body_preread = (int)(hdr_buf+hdr_total-body_start);
            *sep = '\0'; break;
        }
    }

    if (!body_start) {
        print("curl: Incomplete HTTP response.\n");
        close(sockfd); return 1;
    }

    int status = 0;
    { char *sp=strchr(hdr_buf,' '); if(sp) status=atoi(sp+1); }

    if (opts->verbose || !opts->silent) {
        if (opts->verbose) {
            char sb[8]; sprintf(sb,"%d",status);
            print("< HTTP/1.1 "); print(sb); print("\n");
        }
    }

    if (opts->include_headers || opts->head_only) {
        print(hdr_buf); print("\r\n\r\n");
        if (opts->head_only) { close(sockfd); return 0; }
    }

    if ((status==301||status==302||status==303||status==307||status==308)
        && opts->follow_redirect) {
        char *loc = strstr(hdr_buf,"Location:");
        if (!loc) loc=strstr(hdr_buf,"location:");
        if (loc) {
            loc += 9; while(*loc==' ') loc++;
            char *eol=strstr(loc,"\r\n"); if(!eol) eol=loc+strlen(loc);
            char new_url[MAX_URL_LEN*2];
            size_t ulen=(size_t)(eol-loc);
            if(ulen>=sizeof(new_url)) ulen=sizeof(new_url)-1;
            memcpy(new_url,loc,ulen); new_url[ulen]='\0';
            close(sockfd);
            if (!opts->silent) { print("* Redirect -> "); print(new_url); print("\n"); }
            if (!parse_url(new_url,&opts->url)) return 1;
            return do_request(opts, redirect_depth+1);
        }
    }

    if (status < 200 || status >= 300) {
        if (!opts->silent) {
            char sb[8]; sprintf(sb,"%d",status);
            print("curl: Server returned "); print(sb); print("\n");
        }
    }

    uint32_t content_length = 0; bool has_cl = false;
    {
        char *cl=strstr(hdr_buf,"Content-Length:");
        if(!cl) cl=strstr(hdr_buf,"content-length:");
        if(cl) { cl+=15; while(*cl==' ')cl++; content_length=(uint32_t)atoi(cl); has_cl=true; }
    }

    int out_fd = -1;
    if (opts->output_file && strcmp(opts->output_file,"-")!=0) {
        out_fd = open(opts->output_file, O_CREAT|O_TRUNC|O_WRONLY);
        if (out_fd < 0) {
            print("curl: Cannot create file: "); print(opts->output_file); print("\n");
            close(sockfd); return 1;
        }
    }

    uint32_t downloaded = 0;

    if (body_preread > 0) {
        if (out_fd >= 0) write(out_fd, body_start, body_preread);
        else if (!opts->silent) {
            char tmp[BODY_BUF_SIZE+1];
            int chunk = body_preread < BODY_BUF_SIZE ? body_preread : BODY_BUF_SIZE;
            memcpy(tmp, body_start, chunk); tmp[chunk]='\0'; print(tmp);
        }
        downloaded += body_preread;
    }

    static char dl_buf[BODY_BUF_SIZE];
    while (1) {
        if (has_cl && downloaded >= content_length) break;
        int n = read(sockfd, dl_buf, sizeof(dl_buf)-1);
        if (n<=0) break;
        if (out_fd >= 0) write(out_fd, dl_buf, n);
        else if (!opts->silent) { dl_buf[n]='\0'; print(dl_buf); }
        downloaded += n;
    }

    if (out_fd >= 0) {
        close(out_fd);
        if (!opts->silent) {
            char buf[64]; sprintf(buf,"\n  %% Total received: %u bytes\n",downloaded);
            print(buf);
        }
    }

    close(sockfd);
    return (status >= 200 && status < 400) ? 0 : 22;
}

static void usage(void) {
    print("Usage: curl [options] <URL>\n");
    print("  -o <file>       Write output to file\n");
    print("  -O              Write to filename from URL\n");
    print("  -i              Include response headers in output\n");
    print("  -I / --head     Fetch headers only (HEAD request)\n");
    print("  -L              Follow redirects\n");
    print("  -s              Silent mode\n");
    print("  -v              Verbose\n");
    print("  -X <method>     HTTP method (GET POST PUT DELETE ...)\n");
    print("  -d <data>       POST data\n");
    print("  -H <hdr:val>    Add request header (name: value)\n");
    print("  -T <file>       Upload file (PUT)\n");
}

int main(int argc, char **argv) {
    static curl_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.follow_redirect = false;
    const char *url_str  = NULL;
    bool        auto_out = false;

    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0) {
            usage(); return 0;
        } else if (strcmp(argv[i],"-o")==0 && i+1<argc) {
            opts.output_file = argv[++i];
        } else if (strcmp(argv[i],"-O")==0) {
            auto_out = true;
        } else if (strcmp(argv[i],"-i")==0) {
            opts.include_headers = true;
        } else if (strcmp(argv[i],"-I")==0 || strcmp(argv[i],"--head")==0) {
            opts.head_only = true;
        } else if (strcmp(argv[i],"-L")==0) {
            opts.follow_redirect = true;
        } else if (strcmp(argv[i],"-s")==0) {
            opts.silent = true;
        } else if (strcmp(argv[i],"-v")==0) {
            opts.verbose = true;
        } else if (strcmp(argv[i],"-X")==0 && i+1<argc) {
            opts.method = argv[++i];
        } else if (strcmp(argv[i],"-d")==0 && i+1<argc) {
            strncpy(opts.data, argv[++i], MAX_DATA_LEN-1);
        } else if (strcmp(argv[i],"-H")==0 && i+1<argc && opts.header_count<MAX_EXTRA_HEADERS) {
            char *hv = argv[++i];
            char *colon = strchr(hv,':');
            if (colon) {
                *colon = '\0';
                opts.headers[opts.header_count].name  = hv;
                opts.headers[opts.header_count].value = colon+1;
                while (*opts.headers[opts.header_count].value==' ')
                    opts.headers[opts.header_count].value++;
                opts.header_count++;
                *colon = ':';
            }
        } else if (strcmp(argv[i],"-T")==0 && i+1<argc) {
            opts.upload_file = argv[++i];
        } else if (argv[i][0]!='-') {
            url_str = argv[i];
        }
    }

    if (!url_str) { usage(); return 1; }
    if (!parse_url(url_str, &opts.url)) {
        print("curl: Invalid URL: "); print(url_str); print("\n"); return 1;
    }

    if (auto_out && !opts.output_file) {
        static char auto_name[MAX_URL_LEN];
        const char *sl = strrchr(opts.url.path,'/');
        const char *nm = (sl && *(sl+1)) ? sl+1 : NULL;
        strncpy(auto_name, nm ? nm : "index.html", MAX_URL_LEN-1);
        opts.output_file = auto_name;
    }

    if (!opts.silent) {
        print("  % Total    % Received\n");
    }

    return do_request(&opts, 0);
}