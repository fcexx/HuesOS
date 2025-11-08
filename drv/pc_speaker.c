#include <pcspkr.h>
#include <ioport.h>
#include <pit.h>
#include <stdint.h>

// Порты для управления PC Speaker
#define PCSPKR_PORT 0x61
#define PIT_CH2_DATA 0x42
#define PIT_CMD_PORT 0x43

static int pcspkr_initialized = 0;

void pcspkr_init(void) {
    pcspkr_initialized = 1;
    kprintf("PC Speaker driver initialized\n");
}

void pcspkr_play_tone(uint32_t frequency) {
    if (!pcspkr_initialized) return;
    
    if (frequency == 0) {
        pcspkr_silence();
        return;
    }
    
    // Рассчитываем делитель для PIT (канал 2)
    uint32_t divisor = 1193180 / frequency;
    
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;
    
    // Настраиваем PIT канал 2 для генерации квадратной волны
    outb(0xB6, PIT_CMD_PORT);             // Команда: канал 2, режим 3, 16-битный
    outb(divisor & 0xFF, PIT_CH2_DATA);   // Младший байт делителя
    outb((divisor >> 8) & 0xFF, PIT_CH2_DATA); // Старший байт делителя
    
    // Включаем speaker (биты 0 и 1)
    uint8_t speaker_state = inb(PCSPKR_PORT);
    outb(speaker_state | 0x03, PCSPKR_PORT);
}

void pcspkr_silence(void) {
    if (!pcspkr_initialized) return;
    
    // Выключаем speaker (сбрасываем биты 0 и 1)
    uint8_t speaker_state = inb(PCSPKR_PORT);
    outb(speaker_state & 0xFC, PCSPKR_PORT);
}

void pcspkr_beep(uint32_t frequency, uint32_t duration_ms) {
    if (!pcspkr_initialized) pcspkr_init();
    
    pcspkr_play_tone(frequency);
    pit_sleep_ms(duration_ms);
    pcspkr_silence();
}

void pcspkr_stop(void) {
    pcspkr_silence();
}

void pcspkr_play_startup_sound(void) {
    if (!pcspkr_initialized) pcspkr_init();
    
    // Простая стартовая мелодия
    uint32_t startup_notes[] = {NOTE_C5, NOTE_E5, NOTE_G5, NOTE_C6};
    uint32_t durations[] = {150, 150, 150, 300};
    
    for (int i = 0; i < 4; i++) {
        pcspkr_beep(startup_notes[i], durations[i]);
        pit_sleep_ms(50); // Пауза между нотами
    }
}

// Функция для воспроизведения мелодии из аргументов
void pcspkr_play_melody(const uint32_t *notes, const uint32_t *durations, int count) {
    if (!pcspkr_initialized) pcspkr_init();
    
    for (int i = 0; i < count; i++) {
        pcspkr_beep(notes[i], durations[i]);
        pit_sleep_ms(30); // Короткая пауза между нотами
    }
}