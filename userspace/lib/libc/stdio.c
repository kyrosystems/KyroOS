#include "stdio.h"
#include "kyroolib.h"

static FILE _stdin_file  = {.fd = 0, .eof = 0, .error = 0, .unget = -1};
static FILE _stdout_file = {.fd = 1, .eof = 0, .error = 0, .unget = -1};
static FILE _stderr_file = {.fd = 2, .eof = 0, .error = 0, .unget = -1};

FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (strchr(mode, 'w')) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (strchr(mode, '+')) flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (strchr(mode, 'a')) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (strchr(mode, '+')) flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        flags = O_RDONLY;
        if (strchr(mode, '+')) flags = O_RDWR;
    }

    int fd = open(path, flags);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->unget = -1;
    return f;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    size_t read_bytes = 0;
    uint8_t *dest = (uint8_t *)ptr;

    if (stream->unget != -1 && total_bytes > 0) {
        *dest++ = (uint8_t)stream->unget;
        stream->unget = -1;
        read_bytes++;
    }

    if (read_bytes < total_bytes) {
        int r = read(stream->fd, dest, total_bytes - read_bytes);
        if (r < 0) {
            stream->error = 1;
        } else if (r == 0) {
            stream->eof = 1;
        } else {
            read_bytes += r;
        }
    }
    return read_bytes / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    int w = write(stream->fd, ptr, total_bytes);
    if (w < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)w / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;
    stream->unget = -1;
    stream->eof = 0;
    int r = lseek(stream->fd, offset, whence);
    return (r < 0) ? -1 : 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

int fclose(FILE *stream) {
    if (!stream) return -1;
    close(stream->fd);
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return 0;
}

int feof(FILE *stream) {
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 1;
}

int fflush(FILE *stream) {
    (void)stream;
    print_flush();
    return 0;
}

int fgetc(FILE *stream) {
    if (!stream) return EOF;
    if (stream->unget != -1) {
        int c = stream->unget;
        stream->unget = -1;
        return c;
    }
    uint8_t b;
    int r = read(stream->fd, &b, 1);
    if (r <= 0) {
        if (r == 0) stream->eof = 1;
        else stream->error = 1;
        return EOF;
    }
    return (int)b;
}

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF) return EOF;
    stream->unget = c;
    stream->eof = 0;
    return c;
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0 || !stream) return NULL;
    int idx = 0;
    while (idx < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (idx == 0) return NULL;
            break;
        }
        s[idx++] = (char)c;
        if (c == '\n') break;
    }
    s[idx] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    return (fwrite(s, 1, len, stream) == len) ? 0 : EOF;
}

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    (void)size;
    return vsprintf(str, format, ap);
}
