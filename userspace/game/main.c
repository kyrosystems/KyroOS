#include <kyroolib.h>
#include <kyroos_gfx.h>

#define GRID_SIZE 20
#define MAX_SNAKE_LENGTH 100

typedef struct {
  int x;
  int y;
} Point;

int main() {
  if (gfx_init() != 0) {
    return 1;
  }

  int width = gfx_get_width();
  int height = gfx_get_height();
  int cols = width / GRID_SIZE;
  int rows = height / GRID_SIZE;

  Point snake[MAX_SNAKE_LENGTH];
  int snake_length = 3;
  // Initial snake position
  snake[0].x = cols / 2;
  snake[0].y = rows / 2;
  snake[1].x = cols / 2;
  snake[1].y = rows / 2 + 1;
  snake[2].x = cols / 2;
  snake[2].y = rows / 2 + 2;

  Point food;
  food.x = 5;
  food.y = 5;

  int dx = 0;
  int dy = -1; // Moving Up

  int score = 0;
  int game_over = 0;

  gfx_event_t event;
  unsigned int seed = 12345;

  while (!game_over) {
    // Input
    while (gfx_poll_event(&event)) {
      if (event.type == 1) { // KEY_DOWN
        char key = (char)event.data1;
        if (key == 'q')
          game_over = 1;
        if (key == 'w' && dy == 0) {
          dx = 0;
          dy = -1;
        }
        if (key == 's' && dy == 0) {
          dx = 0;
          dy = 1;
        }
        if (key == 'a' && dx == 0) {
          dx = -1;
          dy = 0;
        }
        if (key == 'd' && dx == 0) {
          dx = 1;
          dy = 0;
        }
      }
    }

    // Logic
    Point new_head;
    new_head.x = snake[0].x + dx;
    new_head.y = snake[0].y + dy;

    // Walls
    if (new_head.x < 0 || new_head.x >= cols || new_head.y < 0 ||
        new_head.y >= rows) {
      game_over = 1;
    }

    // Self
    for (int i = 0; i < snake_length; i++) {
      if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
        game_over = 1;
      }
    }

    if (game_over)
      break;

    // Move
    for (int i = snake_length; i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snake[0] = new_head;

    // Eat
    if (new_head.x == food.x && new_head.y == food.y) {
      score++;
      if (snake_length < MAX_SNAKE_LENGTH)
        snake_length++;
      // Respawn
      seed = seed * 1103515245 + 12345;
      food.x = (seed / 65536) % cols;
      seed = seed * 1103515245 + 12345;
      food.y = (seed / 65536) % rows;
    } else {
      // Tail handling implicit if not growing:
      // We shifted 0..length. previous tail at length-1 is discarded.
      // Wait, if we didn't eat, we effectively just moved.
      // If we DID eat, we incremented length, so strict new tail is at new
      // index? No, standard array logic: Old: 0, 1, 2 (len=3) Shift: 1->2,
      // 0->1. New->0. If eat: len=4. The item at 3 is old item at 2. Correct.
      // If not eat: len=3. The item at 3 is old item at 2, but we ignore it.
      // Correct.
    }

    // Draw
    // Clear (black rects)
    gfx_draw_rect(0, 0, width, height, 0x000000);

    // Food (Red)
    gfx_draw_rect(food.x * GRID_SIZE, food.y * GRID_SIZE, GRID_SIZE, GRID_SIZE,
                  0xFF0000);

    // Snake (Green)
    for (int i = 0; i < snake_length; i++) {
      gfx_draw_rect(snake[i].x * GRID_SIZE, snake[i].y * GRID_SIZE, GRID_SIZE,
                    GRID_SIZE, 0x00FF00);
    }

    // Delay
    syscall(SYS_GET_TICKS, 0, 0, 0);
    for (volatile int k = 0; k < 200000; k++)
      ;
  }

  syscall(SYS_EXIT, 0, 0, 0);
  return 0;
}
