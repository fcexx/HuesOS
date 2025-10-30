#include <gdt.h>
#include <string.h>
#include <stdint.h>


extern void gdt_flush(uint32_t);
extern void tss_flush();

struct gdt_entry_struct gdt_entries[6];
struct gdt_ptr_struct gdt_ptr;
struct tss_entry_struct tss_entry;

void init_gdt(){
    gdt_ptr.limit = (sizeof(struct gdt_entry_struct) * 6) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;
    // Setting up GDT entries (gates)
    set_gdt_gate(0,0,0,0,0); // Null segment
    set_gdt_gate(1,0,0xFFFFFFFF, 0x9A, 0xCF); // Kernel code segment
    set_gdt_gate(2,0,0xFFFFFFFF, 0x92, 0xCF); // Kernel data segment
    set_gdt_gate(3,0,0xFFFFFFFF, 0xFA, 0xCF); // User code segment
    set_gdt_gate(4,0,0xFFFFFFFF, 0xF2, 0xCF); // User data segment
    write_tts(5,0x10, 0x0);

    // Flushing GDT and TSS (important)
    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
    
}

void write_tts(uint32_t num, uint16_t ss0, uint32_t esp0){
    uint32_t base = (uint32_t) &tss_entry;
    uint32_t limit = base + sizeof(tss_entry);

    set_gdt_gate(num, base, limit, 0xE9, 0x00);
    memset(&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;

    tss_entry.cs = 0x08 | 0x3;
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x10 | 0x3;
}

void set_gdt_gate(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran){

    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit = (limit & 0xFFFF);
    gdt_entries[num].flags = (limit >> 16) & 0x0F;
    gdt_entries[num].flags |= (gran & 0xF0);

    gdt_entries[num].access = access;

}