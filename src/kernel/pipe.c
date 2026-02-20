#include "pipe.h"
#include "errno.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "scheduler.h"


pipe_t *pipe_create(void) {
  pipe_t *pipe = kmalloc(sizeof(pipe_t));
  if (!pipe) {
    return NULL;
  }

  pipe->read_pos = 0;
  pipe->write_pos = 0;
  pipe->data_size = 0;
  spinlock_init(&pipe->lock);
  waitqueue_init(&pipe->read_queue);
  waitqueue_init(&pipe->write_queue);
  pipe->read_closed = 0;
  pipe->write_closed = 0;

  return pipe;
}

void pipe_destroy(pipe_t *pipe) {
  if (!pipe)
    return;

  // Wake up all waiting threads
  waitqueue_wakeup_all(&pipe->read_queue);
  waitqueue_wakeup_all(&pipe->write_queue);

  kfree(pipe);
}

long pipe_read(pipe_t *pipe, void *buf, size_t count) {
  if (!pipe || !buf) {
    return -EINVAL;
  }

  spinlock_acquire(&pipe->lock);

  // Wait while pipe is empty and write end is still open
  while (pipe->data_size == 0 && !pipe->write_closed) {
    thread_t *current = get_current_thread();
    waitqueue_add(&pipe->read_queue, current);
    spinlock_release(&pipe->lock);
    schedule(); // Block until data available
    spinlock_acquire(&pipe->lock);
  }

  // Check if pipe is empty and write end closed (EOF)
  if (pipe->data_size == 0 && pipe->write_closed) {
    spinlock_release(&pipe->lock);
    return 0; // EOF
  }

  // Read data from pipe
  size_t bytes_read = 0;
  uint8_t *dest = (uint8_t *)buf;

  while (bytes_read < count && pipe->data_size > 0) {
    dest[bytes_read++] = pipe->buffer[pipe->read_pos];
    pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUF_SIZE;
    pipe->data_size--;
  }

  // Wake up one waiting writer
  waitqueue_wakeup_one(&pipe->write_queue);

  spinlock_release(&pipe->lock);
  return bytes_read;
}

long pipe_write(pipe_t *pipe, const void *buf, size_t count) {
  if (!pipe || !buf) {
    return -EINVAL;
  }

  spinlock_acquire(&pipe->lock);

  // Check if read end is closed
  if (pipe->read_closed) {
    spinlock_release(&pipe->lock);
    return -EPIPE; // Broken pipe
  }

  long bytes_written = 0;
  const uint8_t *src = (const uint8_t *)buf;

  while (bytes_written < (long)count) {
    // Wait while pipe is full
    while (pipe->data_size >= PIPE_BUF_SIZE && !pipe->read_closed) {
      thread_t *current = get_current_thread();
      waitqueue_add(&pipe->write_queue, current);
      spinlock_release(&pipe->lock);
      schedule(); // Block until space available
      spinlock_acquire(&pipe->lock);
    }

    // Check if read end closed
    if (pipe->read_closed) {
      spinlock_release(&pipe->lock);
      return bytes_written > 0 ? bytes_written : (long)-EPIPE;
    }

    // Write one byte
    pipe->buffer[pipe->write_pos] = src[bytes_written++];
    pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUF_SIZE;
    pipe->data_size++;
  }

  // Wake up one waiting reader
  waitqueue_wakeup_one(&pipe->read_queue);

  spinlock_release(&pipe->lock);
  return bytes_written;
}

void pipe_close_read(pipe_t *pipe) {
  if (!pipe)
    return;

  spinlock_acquire(&pipe->lock);
  pipe->read_closed = 1;

  // Wake up all waiting writers (they'll get EPIPE)
  waitqueue_wakeup_all(&pipe->write_queue);

  spinlock_release(&pipe->lock);
}

void pipe_close_write(pipe_t *pipe) {
  if (!pipe)
    return;

  spinlock_acquire(&pipe->lock);
  pipe->write_closed = 1;

  // Wake up all waiting readers (they'll get EOF)
  waitqueue_wakeup_all(&pipe->read_queue);

  spinlock_release(&pipe->lock);
}
