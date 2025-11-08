#ifndef PCSPKR_H
#define PCSPKR_H

#include <stdint.h>

void pcspkr_init(void);
void pcspkr_beep(uint32_t frequency, uint32_t duration_ms);
void pcspkr_play_tone(uint32_t frequency);
void pcspkr_stop(void);
void pcspkr_silence(void);
void pcspkr_play_startup_sound(void);

// Ноты для мелодий
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523

#endif