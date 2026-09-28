#include "epstein.h"

int epstein = 0; // Default OFF: debug logging costs a full framebuffer
                 // flush per syscall and floods the console. Enable with
                 // `debugmode on` (SYS_DEBUGMODE_SET, root only).
char epstein_files[] = "Debug features are enabled"; // Default message
