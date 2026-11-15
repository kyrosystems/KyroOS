#include "thread.h"
#include "heap.h"
#include "kstring.h"
#include "vfs.h"
#include "socket.h"
#include "pipe.h"

// Descriptors share lifetime and file position after dup or spawn
int fd_duplicate(fd_entry_t *dest, fd_entry_t *source) {
  if (dest == source) return 0;
  if (source->type == FD_TYPE_NONE) {
    fd_release(dest);
    return 0;
  }
  if (!source->shared) {
    source->shared = kmalloc(sizeof(fd_shared_t));
    if (!source->shared) return -1;
    source->shared->refs = 1;
    source->shared->offset = source->type == FD_TYPE_FILE ? source->data.file.offset : 0;
  }
  source->shared->refs++;
  fd_release(dest);
  *dest = *source;
  return 0;
}

void fd_release(fd_entry_t *entry) {
  if (entry->type == FD_TYPE_NONE) return;
  bool last = !entry->shared || --entry->shared->refs == 0;
  if (last) {
    if (entry->type == FD_TYPE_FILE) {
      struct vfs_node *node = entry->data.file.node;
      if (node && node->close) node->close(node);
    } else if (entry->type == FD_TYPE_SOCKET && entry->data.sock) {
      sock_close(entry->data.sock);
    } else if (entry->type == FD_TYPE_PIPE && entry->data.pipe.pipe) {
      pipe_close_end(entry->data.pipe.pipe, entry->data.pipe.is_write);
    }
    if (entry->shared) kfree(entry->shared);
  }
  memset(entry, 0, sizeof(*entry));
}
