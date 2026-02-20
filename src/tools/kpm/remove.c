#include "../../include/kpm.h"
#include "../kyroolib.h"

#define MAX_DB_SIZE 16384 // Max size for the in-memory package database (16KB)

// Helper to recursively remove files and directories
static int remove_recursive(const char *path) {
    char entry_full_path[MAX_FILENAME_LEN * 2]; // Path for current entry
    struct stat st;

    // Get stat of the current path
    if (stat(path, &st) != 0) {
        print("kpm: Failed to stat "); print(path); print("\n");
        return -1;
    }

    if (S_ISREG(st.st_mode)) { // It's a regular file
        print("kpm: Unlinking file: "); print(path); print("\n");
        return unlink(path);
    } else if (S_ISDIR(st.st_mode)) { // It's a directory
        print("kpm: Entering directory: "); print(path); print("\n");

        int dir_fd = open(path, O_RDONLY); // Open directory for reading
        if (dir_fd < 0) {
            print("kpm: Failed to open directory "); print(path); print("\n");
            return -1;
        }

        struct dirent de;
        int index = 0;
        // Loop through directory entries
        while (readdir(dir_fd, index++, &de)) {
            // Skip . and ..
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
                continue;
            }

            sprintf(entry_full_path, "%s/%s", path, de.name);
            if (remove_recursive(entry_full_path) != 0) {
                close(dir_fd);
                return -1; // Propagate error
            }
        }
        close(dir_fd); // Close the directory

        print("kpm: Removing directory: "); print(path); print("\n");
        return rmdir(path); // Remove the now empty directory
    }
    return -1; // Unknown type (or not a file/dir)
}

int pkg_remove(const char *name) {
  int deletion_successful = 1;
  print("Attempting to remove package: ");
  print(name);
  print("\n");

  int fd = open(PKG_DB_PATH, O_RDONLY); // Open read-only
  if (fd < 0) {
    print("kpm: No package database found. No packages installed.\n");
    return 0;
  }

  char db_buffer[MAX_DB_SIZE];
  int bytes_read = read(fd, db_buffer, MAX_DB_SIZE - 1);
  close(fd);

  if (bytes_read <= 0) {
    print("kpm: Package database is empty or could not be read.\n");
    return 0;
  }
  db_buffer[bytes_read] = '\0'; // Null-terminate the buffer

  char new_db_buffer[MAX_DB_SIZE];
  new_db_buffer[0] = '\0'; // Initialize new buffer as empty
  
  char *line_start = db_buffer;
  char *line_end;

  int found = 0;

  while (line_start < db_buffer + bytes_read) {
    line_end = line_start;
    while (*line_end != '\n' && *line_end != '\0') {
      line_end++;
    }

    // Extract current line
    char current_line[256]; // Max line length
    int line_len = line_end - line_start;
    if ((size_t)line_len >= sizeof(current_line)) line_len = sizeof(current_line) - 1; // Prevent buffer overflow
    memcpy(current_line, line_start, line_len);
    current_line[line_len] = '\0';

    // Parse package name from current_line (name|version|path)
    char pkg_name_in_db[128];
    char *pipe_pos = strchr(current_line, '|');
    if (pipe_pos) {
        int name_len = pipe_pos - current_line;
        if ((size_t)name_len >= sizeof(pkg_name_in_db)) name_len = sizeof(pkg_name_in_db) - 1;
        memcpy(pkg_name_in_db, current_line, name_len);
        pkg_name_in_db[name_len] = '\0';
    } else {
        // Malformed line, treat the whole line as name for safety
        strcpy(pkg_name_in_db, current_line);
    }
    

    if (strcmp(pkg_name_in_db, name) != 0) {
      // If it's not the package to remove, append to new_db_buffer
      strcat(new_db_buffer, current_line);
      strcat(new_db_buffer, "\n");
    } else {
      found = 1;
      print("  Package '");
      print(name);
      print("' found in database.\n");
      // Parse install_path from current_line
      char install_path_in_db[MAX_FILENAME_LEN];
      char *first_pipe = strchr(current_line, '|');
      char *second_pipe = NULL;
      if (first_pipe) {
          second_pipe = strchr(first_pipe + 1, '|');
      }

      if (second_pipe) {
          char *path_start = second_pipe + 1;
          // Find end of path (before newline)
          char *path_end = path_start;
          while (*path_end != '\n' && *path_end != '\0') path_end++;
          
          int path_len = path_end - path_start;
          if ((size_t)path_len >= sizeof(install_path_in_db)) path_len = sizeof(install_path_in_db) - 1;
          strncpy(install_path_in_db, path_start, path_len);
          install_path_in_db[path_len] = '\0';

          // Delete the package files recursively
          if (remove_recursive(install_path_in_db) != 0) {
              print("kpm: Failed to remove package files for '"); print(name); print("'\n");
              // File deletion failed. Keep the entry in the DB.
              deletion_successful = 0;
              strcat(new_db_buffer, current_line);
              strcat(new_db_buffer, "\n");
          } else {
              print("  Package '");
              print(name);
              print("' files removed.\n");
          }
      } else {
          print("kpm: Warning: Malformed entry in DB, could not get install path for "); print(name); print("\n");
      }
    }

    if (*line_end == '\n') {
      line_start = line_end + 1;
    } else {
      line_start = line_end; // End of buffer
    }
  }

  if (!found) {
    print("kpm: Package '");
    print(name);
    print("' not found in database.\n");
    return -1; // Indicate failure to remove
  }

  if (!deletion_successful) {
    // File deletion failed earlier, so we return an error.
    return -1;
  }

  // Rewrite the database file
  fd = open(PKG_DB_PATH, O_CREAT | O_TRUNC | O_WRONLY); // create truncates existing file
  if (fd < 0) {
    print("kpm: Could not open package database for writing.\n");
    return -1;
  }
  write(fd, new_db_buffer, strlen(new_db_buffer));
  close(fd);

  if (deletion_successful) {
    print("Package '");
    print(name);
    print("' removed from database.\n");
  }
  return 0;
}
