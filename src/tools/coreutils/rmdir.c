#include "../kyroolib.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    print("rmdir: usage: rmdir <directory>\n");
    return 1;
  }

  const char *path = argv[1];

  if (rmdir(path) != 0) {
    print("rmdir: failed to remove directory '");
    print(path);
    print("' (it might not be empty or does not exist)\n");
    return 1;
  }

  return 0;
}