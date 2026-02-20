#include <errno.h>
#include <kyroolib.h> // For print and ksprintf
#include <string.h>

int errno = 0; // Global errno variable

void __set_errno(int err_code) { errno = err_code; }

// Minimal error strings. For a full implementation, this would be much larger.
static const char *str_error[] = {
    "Success",                   // 0 (no error)
    "Operation not permitted",   // EPERM (1)
    "No such file or directory", // ENOENT (2)
    "No such process",           // ESRCH (3)
    "Interrupted system call",   // EINTR (4)
    "I/O error",                 // EIO (5)
    "No such device or address", // ENXIO (6)
    "Argument list too long",    // E2BIG (7)
    "Exec format error",         // ENOEXEC (8)
    "Bad file number",           // EBADF (9)
    "No child processes",        // ECHILD (10)
    "Try again",                 // EAGAIN (11)
    "Out of memory",             // ENOMEM (12)
    "Permission denied",         // EACCES (13)
    "Bad address",               // EFAULT (14)
    "Block device required",     // ENOTBLK (15)
    "Device or resource busy",   // EBUSY (16)
    "File exists",               // EEXIST (17)
    "Cross-device link",         // EXDEV (18)
    "No such device",            // ENODEV (19)
    "Not a directory",           // ENOTDIR (20)
    "Is a directory",            // EISDIR (21)
    "Invalid argument",          // EINVAL (22)
    "File table overflow",       // ENFILE (23)
    "Too many open files",       // EMFILE (24)
    "Not a typewriter",          // ENOTTY (25)
    "Text file busy",            // ETXTBSY (26)
    "File too large",            // EFBIG (27)
    "No space left on device",   // ENOSPC (28)
    "Illegal seek",              // ESPIPE (29)
    "Read-only file system",     // EROFS (30)
    "Too many links",            // EMLINK (31)
    "Broken pipe",               // EPIPE (32)
    // EDOM, ERANGE, EDEADLK etc. would be here
    // Skip to the most common higher ones if necessary
    "Function not implemented" // ENOSYS (38)
};

#define MAX_ERROR_CODE                                                         \
  39 // Max code currently covered by str_error array (excluding specific higher
     // ones)

void perror(const char *s) {
  char buffer[256];
  int current_errno = errno;
  const char *err_str = "Unknown error";

  if (current_errno > 0 &&
      current_errno < MAX_ERROR_CODE) { // errno is positive
    err_str = str_error[current_errno];
  } else if (current_errno ==
             ENOSYS) { // Specific check for ENOSYS if its value is higher
    err_str = str_error[ENOSYS];
  }
  // Add more specific handling for higher error codes if needed

  if (s && strlen(s) > 0) {
    ksprintf(buffer, "%s: %s\n", s, err_str);
  } else {
    ksprintf(buffer, "%s\n", err_str);
  }
  print(buffer);
}
