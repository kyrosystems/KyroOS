#include "audio.h"
#include "log.h"
#include <stddef.h> // For NULL

// Function pointer for the active audio driver's play function
static void (*active_audio_play)(const int16_t* buffer, size_t size) = NULL;

void audio_init() {
    active_audio_play = NULL;
    klog(LOG_INFO, "Audio Subsystem initialized.");
}

// Called by an audio driver (e.g., ac97) to register its functions
void audio_register_driver(void (*play_func)(const int16_t*, size_t)) {
    if (play_func) {
        active_audio_play = play_func;
        klog(LOG_INFO, "An audio driver has been registered.");
    }
}

void audio_play(const int16_t* buffer, size_t size) {
    if (active_audio_play) {
        active_audio_play(buffer, size);
    } else {
        klog(LOG_WARN, "No audio driver available to play sound.");
    }
}


// skibidi rizzler toilet? 
// skibidi rizzler toilet?
// skibidi rizzler toilet!
// skibidi rizzler toilet?
// skibidi rizzler toilet...
// skibidi rizzler toilet!!!
// skibidi rizzler toilet???
// skibidi rizzler toilet...
// linux users be like: skibidi rizzler toilet!
// windows users be like: skibidi rizzler toilet!!!
// macOS users be like: skibidi rizzler toilet!
// freebsd users be like: skibidi rizzler toilet???
// openbsd users be like: skibidi rizzler toilet...
// netbsd users be like: skibidi rizzler toilet...
// arch linux users be like: skibidi rizzler toilet!!!
// debian users be like: skibidi rizzler toilet???
// fedora users be like: skibidi rizzler toilet???
// gentoo users be like: skibidi rizzler toilet...
// void gentoo_users_be_like_skibidi_rizzler_toilet() {
//     // skibidi rizzler toilet...
// }    
// void arch_linux_users_be_like_skibidi_rizzler_toilet() {
//     // skibidi rizzler toilet!!! 
// }
// brainrot.exe has stopped working
