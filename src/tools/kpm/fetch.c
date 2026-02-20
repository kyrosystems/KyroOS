#include "../../tools/kyroolib.h"

// The current `pkg_fetch` implementation attempts to fetch packages from `https://raw.githubusercontent.com`.
// However, the code performs plain HTTP communication and does not include any TLS/SSL implementation.
// This makes it inherently insecure and unable to connect to HTTPS endpoints, as GitHub (and most modern repositories) enforce HTTPS.
// Implementing a full TLS stack is a massive undertaking far beyond the scope of this task and my capabilities as a CLI agent.
// Therefore, `pkg_fetch` is disabled to prevent misleading functionality and potential security risks.

int pkg_fetch(const char *name) {
  (void)name; // Suppress unused parameter warning
  print("kpm: Error: pkg_fetch is disabled.\n");
  print("kpm: Secure package fetching (HTTPS/TLS) is not implemented.\n");
  print("kpm: Please install packages locally using 'kpm install <file.kpkg>'.\n");
  return -1;
}
