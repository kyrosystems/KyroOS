#ifndef PIPE_H
#define PIPE_H

#include "spinlock.h"
#include "waitqueue.h"
#include <stddef.h>
#include <stdint.h>


#define PIPE_BUF_SIZE 4096

typedef struct pipe {
  uint8_t buffer[PIPE_BUF_SIZE];
  size_t read_pos;
  size_t write_pos;
  size_t data_size;
  spinlock_t lock;
  wait_queue_t read_queue;
  wait_queue_t write_queue;
  int read_closed;
  int write_closed;
} pipe_t;

pipe_t *pipe_create(void);
void pipe_destroy(pipe_t *pipe);
long pipe_read(pipe_t *pipe, void *buf, size_t count);
long pipe_write(pipe_t *pipe, const void *buf, size_t count);
void pipe_close_read(pipe_t *pipe);
void pipe_close_write(pipe_t *pipe);

#endif // PIPE_H
