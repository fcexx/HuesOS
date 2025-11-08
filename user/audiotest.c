#include "../inc/audio.h"
#include "../inc/vga.h"

extern void kprintf(const char *fmt, ...);

/**
 * Audio Test Program
 * 
 * Simple test application to demonstrate audio playback capabilities.
 */

void audiotest_play_notes(void)
{
    /* Musical notes (frequencies in Hz) */
    uint16_t notes[] = {
        262,  /* C4 */
        294,  /* D4 */
        330,  /* E4 */
        349,  /* F4 */
        392,  /* G4 */
        440,  /* A4 */
        494,  /* B4 */
        523   /* C5 */
    };
    
    const char *note_names[] = {
        "C", "D", "E", "F", "G", "A", "B", "C"
    };
    
    int i;
    
    kprintf("\n=== Playing Musical Scale ===\n");
    
    for (i = 0; i < 8; i++) {
        kprintf("Playing note: %s (%d Hz)\n", note_names[i], notes[i]);
        audio_generate_sine_wave(notes[i], 300, 60);
        
        /* Wait for note to finish */
        /* In a real system, you'd have proper timing */
        volatile int delay;
        for (delay = 0; delay < 10000000; delay++)
            ;
    }
    
    kprintf("Scale complete!\n\n");
}

void audiotest_play_melody(void)
{
    /* Simple melody: "Twinkle Twinkle Little Star" */
    struct {
        uint16_t freq;
        uint32_t duration;
    } melody[] = {
        {262, 300}, /* C */
        {262, 300}, /* C */
        {392, 300}, /* G */
        {392, 300}, /* G */
        {440, 300}, /* A */
        {440, 300}, /* A */
        {392, 600}, /* G */
        {349, 300}, /* F */
        {349, 300}, /* F */
        {330, 300}, /* E */
        {330, 300}, /* E */
        {294, 300}, /* D */
        {294, 300}, /* D */
        {262, 600}, /* C */
    };
    
    int i;
    
    kprintf("\n=== Playing Melody ===\n");
    kprintf("\"Twinkle Twinkle Little Star\"\n\n");
    
    for (i = 0; i < 14; i++) {
        audio_generate_sine_wave(melody[i].freq, melody[i].duration, 50);
        
        /* Wait for note to finish */
        volatile int delay;
        for (delay = 0; delay < 10000000; delay++)
            ;
    }
    
    kprintf("Melody complete!\n\n");
}

void audiotest_test_frequencies(void)
{
    uint16_t frequencies[] = {100, 200, 440, 880, 1000, 2000, 4000};
    int i;
    
    kprintf("\n=== Testing Frequencies ===\n");
    
    for (i = 0; i < 7; i++) {
        kprintf("Playing %d Hz for 500ms\n", frequencies[i]);
        audio_generate_sine_wave(frequencies[i], 500, 50);
        
        volatile int delay;
        for (delay = 0; delay < 15000000; delay++)
            ;
    }
    
    kprintf("Frequency test complete!\n\n");
}

void audiotest_test_volume(void)
{
    uint8_t volumes[] = {10, 25, 50, 75, 100};
    int i;
    
    kprintf("\n=== Testing Volume Levels ===\n");
    
    for (i = 0; i < 5; i++) {
        kprintf("Volume: %d%% (440 Hz for 500ms)\n", volumes[i]);
        audio_generate_sine_wave(440, 500, volumes[i]);
        
        volatile int delay;
        for (delay = 0; delay < 15000000; delay++)
            ;
    }
    
    kprintf("Volume test complete!\n\n");
}

void audiotest_menu(void)
{
    kprintf("\n");
    kprintf("╔════════════════════════════════════════╗\n");
    kprintf("║       AxonOS Audio Test Suite         ║\n");
    kprintf("╠════════════════════════════════════════╣\n");
    kprintf("║  1. Play simple beep (440 Hz)         ║\n");
    kprintf("║  2. Play musical scale (C-D-E-F-G-A-B) ║\n");
    kprintf("║  3. Play melody (Twinkle Twinkle)     ║\n");
    kprintf("║  4. Test various frequencies          ║\n");
    kprintf("║  5. Test volume levels                ║\n");
    kprintf("║  6. Get audio status                  ║\n");
    kprintf("║  7. Stop playback                     ║\n");
    kprintf("║  0. Exit                              ║\n");
    kprintf("╚════════════════════════════════════════╝\n");
    kprintf("\n");
}

void audiotest_show_status(void)
{
    audio_status_t status;
    
    audio_get_status(&status);
    
    kprintf("\n=== Audio Status ===\n");
    kprintf("Initialized: %s\n", status.initialized ? "Yes" : "No");
    kprintf("Playing: %s\n", status.playing ? "Yes" : "No");
    kprintf("Volume: %d%%\n", status.volume);
    kprintf("Position: %d / %d bytes\n", status.position, status.total_size);
    kprintf("==================\n\n");
}

void audiotest_main(void)
{
    int choice;
    int running = 1;
    
    // fixme
    kprintf("\n");
    kprintf("╔═══════════════════════════════════════════╗\n");
    kprintf("║  AxonOS Audio Test Application            ║\n");
    kprintf("║  Testing Intel HDA Driver                 ║\n");
    kprintf("╚═══════════════════════════════════════════╝\n");
    kprintf("\n");
    
    /* Initialize audio subsystem */
    kprintf("Initializing audio subsystem...\n");
    if (audio_init() < 0) {
        kprintf("ERROR: Failed to initialize audio subsystem!\n");
        kprintf("Please ensure:\n");
        kprintf("  - Intel HDA compatible audio device is present\n");
        kprintf("  - PCI subsystem is initialized\n");
        return;
    }
    
    /* Run simple test automatically */
    kprintf("\n=== Running Quick Test ===\n");
    kprintf("Playing 440 Hz tone for 1 second...\n");
    audio_beep(440, 1000);
    
    /* Wait a bit */
    volatile int delay;
    for (delay = 0; delay < 30000000; delay++)
        ;
    
    kprintf("Quick test complete!\n");
    
    /* Show menu */
    audiotest_menu();
    
    kprintf("Audio test application ready!\n");
    kprintf("Use the shell commands to run tests.\n\n");
    
    /* For now, just run a demo sequence */
    kprintf("Running demo sequence...\n\n");
    
    /* Demo: Play some tones */
    kprintf("1. Beep test\n");
    audio_beep(880, 200);
    for (delay = 0; delay < 10000000; delay++);
    
    audio_beep(440, 200);
    for (delay = 0; delay < 10000000; delay++);
    
    audio_beep(880, 200);
    for (delay = 0; delay < 10000000; delay++);
    
    kprintf("\n2. Scale test\n");
    audiotest_play_notes();
    
    kprintf("\nDemo complete!\n");
    kprintf("Audio driver is working!\n\n");
}
