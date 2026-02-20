#include "../../include/kpm.h"
#include "../kyroolib.h"

void usage() {
  print("KyroOOS Package Manager (kpm) v0.1\n");
  print("Usage: kpm <command> [args]\n");
  print("Commands:\n");
  print("  install <file.kpkg>  - Install a local package\n");
  print("  install <name>       - Install a package from repo\n");
  print("  remove <name>        - Remove an installed package\n");
  print("  list                 - List installed packages\n");
  exit(1);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print("No command specified, listing installed packages:\n");
    pkg_list();
    return 0;
  }

  if (strcmp(argv[1], "install") == 0) {
    if (argc < 3)
      usage();
    // Check if it's a file or a name
    if (strcmp(argv[2] + strlen(argv[2]) - 5, ".kpkg") == 0) {
      pkg_install_file(argv[2]);
    } else {
      pkg_fetch(argv[2]);
    }
  } else if (strcmp(argv[1], "remove") == 0) {
    if (argc < 3)
      usage();
    pkg_remove(argv[2]);
  } else if (strcmp(argv[1], "list") == 0) {
    pkg_list();
  } else {
    usage();
  }

  return 0;
}
