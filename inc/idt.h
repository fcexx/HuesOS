#pragma once

#include <stdint.h>

// cpu registers structure
typedef struct {
        uint64_t interrupt_number;          // interrupt number
        uint64_t error_code;        // error code (or 0)
        // saved cpu registers
        uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
        uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
        // saved hardware values
        uint64_t rip, cs, rflags, rsp, ss;
} cpu_registers_t;

// idt entry structure
struct idt_entry_t {
        uint16_t offset_low;
        uint16_t selector;
        uint8_t ist;
        uint8_t flags;
        uint16_t offset_mid;
        uint32_t offset_high;
        uint32_t reserved;
} __attribute__((packed));

// idt pointer structure
struct idt_ptr_t {
        uint16_t limit;
        uint64_t base;
} __attribute__((packed));

extern uint64_t isr_stub_table[];
extern void isr_dispatch(cpu_registers_t* regs);
extern const char* exception_messages[];

void idt_init();
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags);
void idt_set_handler(uint8_t num, void (*handler)(cpu_registers_t*));
// Debug helper
void idt_dbg_dump_vec(uint8_t vec);