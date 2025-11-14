// inc/rtc.h

#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <idt.h>

// Порты ввода-вывода для RTC
#define RTC_COMMAND_PORT    0x70
#define RTC_DATA_PORT       0x71

// Регистры RTC (CMOS)
#define RTC_REG_SECONDS     0x00
#define RTC_REG_MINUTES     0x02
#define RTC_REG_HOURS       0x04
#define RTC_REG_DAY         0x07
#define RTC_REG_MONTH       0x08
#define RTC_REG_YEAR        0x09

// Регистры состояния
#define RTC_REG_STATUS_A    0x0A
#define RTC_REG_STATUS_B    0x0B
#define RTC_REG_STATUS_C    0x0C

// Структура для хранения даты и времени
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_datetime_t;

// Глобальный счетчик тиков RTC
extern volatile uint64_t rtc_ticks;

// Инициализация RTC и его периодического прерывания
void rtc_init();

// Чтение текущей даты и времени из RTC
void rtc_read_datetime(rtc_datetime_t* dt);

// Обработчик прерывания от RTC (IRQ 8)
void rtc_handler(cpu_registers_t* regs);

#endif // RTC_H
