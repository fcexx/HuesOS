#pragma once

#include <stdint.h>

void pic_init();
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_set_irq_handler(uint8_t irq, void (*handler)());