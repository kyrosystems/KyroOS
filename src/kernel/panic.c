#include "../include/panic.h"
#include "../include/panic_screen.h"
#include "../include/kstring.h"
#include "../include/log.h"
#include "../include/scheduler.h" // Assuming scheduler_disable exists or similar
#include "../include/isr.h" // For timer_get_ticks for a simple "random" seed
#include <stddef.h> // For NULL

// Array of human-readable panic reason strings, matching panic_reason_t enum
static const char *panic_reasons_strings[] = {
    "Generic, unspecified kernel panic.",
    "Framebuffer Initialization Failure: Bootloader did not provide a valid framebuffer.",
    "No Runnable Threads in Scheduler: The system has no tasks to execute.",
    "Failed to Allocate Main Kernel Thread: Insufficient memory at boot.",
    "Returned to a Dead Thread: Scheduler or context switching error.",
    "General Memory Allocation Failure (kmalloc): Kernel ran out of memory."
};

// Array of panic quotes from panic_quotes.txt
static const char *panic_quotes[] = {
    "The cake is a lie.",
    "It was working yesterday.",
    "You had one job.",
    "Task failed successfully.",
    "42",
    "Well yes, but actually no.",
    "Nothing is under control.",
    "Congratulations, you broke it.",
    "Here we go again.",
    "Oops.",
    "Bruh.",
    "Skill issue.",
    "Just works™",
    "Trust me bro.",
    "Everything is broken.",
    "Achievement unlocked.",
    "Welcome to the void.",
    "It’s not a bug, it’s a feature.",
    "Abandon hope.",
    "Hello darkness, my old friend.",
    "Guess I’ll die.",
    "Blue screen of happiness.",
    "Powered by spaghetti code.",
    "Have you tried turning it off and on again?",
    "Works on my machine.",
    "Mom, pick me up.",
    "use arch btw.",
    "bro wat de fak",
    "This is fine.",
    "Wait, that's illegal.",
    "Internal screaming intensifies.",
    "Whoopsie daisy.",
    "I have no idea what I'm doing.",
    "Keyboard not found. Press F1 to continue.",
    "Error: Error occurred while handling the error.",
    "Don't panic.",
    "Maybe it's a hardware problem?",
    "Reality is often disappointing.",
    "You weren't supposed to see this.",
    "Look at me, I am the captain now.",
    "Houston, we have a problem.",
    "F in the chat.",
    "Windows is updating (0%).",
    "Segmented and lonely.",
    "Null pointer's revenge.",
    "A wild bug appeared!",
    "I think, therefore I crash.",
    "System.exit(0) was not enough.",
    "One does not simply run this code.",
    "To be or not to be... that is the— [SEGFAULT]",
    "Game over, man! Game over!",
    "Don't look back, the compiler is crying.",
    "Unexpected item in bagging area.",
    "I'm sorry, Dave. I'm afraid I can't do that.",
    "Error 404: Brain not found.",
    "Why are we still here? Just to suffer?",
    "It's evolving, just backwards.",
    "You should have gone for the head.",
    "This is why we can't have nice things.",
    "I'll be back.",
    "May the Force be with your debugger.",
    "To infinity and— [REBOOT]",
    "So long, and thanks for all the fish.",
    "The bits are escaping!",
    "Divide by zero? Don't mind if I do!",
    "Magic smoke detected.",
    "If you can read this, I failed.",
    "The technical term is: 'Oopsie Woopsie'.",
    "Warning: Reality.sys is corrupted.",
    "Life is short, but this stack trace is long.",
    "I felt a great disturbance in the Source.",
    "It's dangerous to go alone! Take this.",
    "Peace was never an option.",
    "Critical failure of common sense.",
    "You've met with a terrible fate, haven't you?",
    "Something happened.",
    "Basically, I am very smol.",
    "Goodbye, cruel world.",
    "PC LOAD LETTER",
    "Everything is awesome... except this.",
    "I'm not lazy, I'm just on energy saving mode.",
    "Let's just call it a 'surprise feature'.",
    "My code works, I have no idea why.",
    "A massive error has occurred, but you probably knew that.",
    "Instructions unclear, got stuck in an infinite loop.",
    "Your computer has been haunted by a spooky ghost.",
    "The hamsters running the server have died.",
    "Please don't tell the boss.",
    "It's not me, it's you.",
    "RIP in peace.",
    "Press F to pay respects.",
    "I'm doing my best, okay?",
    "This incident will be reported.",
    "Maybe the real code was the friends we made along the way.",
    "Keep calm and... oh, forget it.",
    "Everything is a file, except when it's a disaster.",
    "I can't believe you've done this.",
    "Top 10 anime betrayals: This Error.",
    "You've reached the end of the internet.",
    "Searching for a solution... 0 matches found.",
    "Gone reduced to atoms.",
    "I'm in danger.",
    "Why is it always like this?",
    "It's a trap!",
    "I am inevitable.",
    "Delete System32 to continue.",
    "Your trial of existence has expired.",
    "Don't worry, it's just a flesh wound.",
    "The singularity is near, but not today.",
    "Out of memory, out of time, out of luck.",
    "The ghost in the machine is angry.",
    "You shouldn't have played God.",
    "Something's wrong, I can feel it.",
    "Look Ma, no hands!",
    "End of line.",
    "To be continued..."
};
#define NUM_PANIC_QUOTES (sizeof(panic_quotes) / sizeof(panic_quotes[0]))

// Function to trigger a kernel panic
void trigger_kernel_panic(panic_reason_t reason) {
    __asm__ volatile("cli");

    // Stop the scheduler if it's running
    // Assuming scheduler_disable() exists or similar mechanism.
    // If not, a simple for(;;) will halt this thread, but others might continue
    // for a short while before an interrupt attempts to occur or they run out of work.
    // For a hard panic, cli is usually sufficient.
    // scheduler_disable(); // Placeholder if available

    panic_screen_init(); // Initialize the panic screen (clears console, sets colors)

    char panic_message[512];
    const char *reason_str = "Unknown or unhandled panic reason.";
    if (reason < sizeof(panic_reasons_strings) / sizeof(panic_reasons_strings[0])) {
        reason_str = panic_reasons_strings[reason];
    }
    
    // Simple pseudo-random quote selection
    // Using timer ticks for a slightly different quote each boot/panic
    uint64_t ticks = timer_get_ticks();
    size_t quote_index = ticks % NUM_PANIC_QUOTES;
    const char *quote = panic_quotes[quote_index];

    ksprintf(panic_message, 
             "*** KERNEL PANIC ***\n"
             "Reason: %s\n"
             "Quote: \"%s\"\n\n"
             "System halted.", 
             reason_str, quote);
    
    panic_screen_show(panic_message, NULL); // Display the panic message, no register dump for software panic

    // Halt the system
    for (;;) {
        __asm__ volatile("hlt");
    }
}
