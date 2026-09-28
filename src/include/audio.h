#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stddef.h>

void audio_init();
void audio_play(const int16_t* buffer, size_t size);
void audio_register_driver(void (*play_func)(const int16_t*, size_t));

#endif // AUDIO_H