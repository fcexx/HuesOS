#include <idt.h>
#include <vga.h>
#include <pic.h>
#include <thread.h>
#include <rtc.h>
//#include <pit.h>
#include <stdint.h>
//#include <thread.h>
#include <stdint.h>
#include <stddef.h>
#include <debug.h>
// Avoid including <cstdint> because cross-toolchain headers may not provide it; use uint64_t instead

// Forward declare C-linkage helpers from other compilation units
uint64_t dbg_saved_rbx_in;
uint64_t dbg_saved_rbx_out;

// локальные таблицы обработчиков (неиспользуемые предупреждения устраним использованием ниже)
static void (*irq_handlers[16])() = {NULL};
static void (*isr_handlers[256])(cpu_registers_t*) = {NULL};

static struct idt_entry_t idt[256];
static struct idt_ptr_t idt_ptr;
// сообщения об исключениях — определение для внешней декларации из idt.h
const char* exception_messages[] = {
        "Division By Zero","Debug","Non Maskable Interrupt","Breakpoint","Into Detected Overflow",
        "Out of Bounds","Invalid Opcode","No Coprocessor","Double fault","Coprocessor Segment Overrun",
        "Bad TSS","Segment not present","Stack fault","General protection fault","Page fault",
        "Unknown Interrupt","Coprocessor Fault","Alignment Fault","Machine Check",
        "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Reserved",
        "Reserved","Reserved","Reserved","Reserved","Reserved"
};

static inline void read_crs(uint64_t* cr0, uint64_t* cr2, uint64_t* cr3, uint64_t* cr4){
        uint64_t t0=0,t2=0,t3=0,t4=0; (void)t0; (void)t2; (void)t3; (void)t4;
        asm volatile("mov %%cr0, %0" : "=r"(t0));
        asm volatile("mov %%cr2, %0" : "=r"(t2));
        asm volatile("mov %%cr3, %0" : "=r"(t3));
        asm volatile("mov %%cr4, %0" : "=r"(t4));
        if (cr0) *cr0 = t0; if (cr2) *cr2 = t2; if (cr3) *cr3 = t3; if (cr4) *cr4 = t4;
}

static void dump(const char* what, const char* who, cpu_registers_t* regs, uint64_t cr2, uint64_t err, bool user_mode){
        (void)what;
        (void)who;
        (void)regs;
        (void)cr2;
        (void)err;
        (void)user_mode;
}

static void ud_fault_handler(cpu_registers_t* regs) {
        // Invalid Opcode (#UD). В ring3 не эмулируем — завершаем поток.
        if ((regs->cs & 3) == 3) {
                dump("invalid opcode", "user", regs, 0, 0, true);
                for(;;){ asm volatile("sti; hlt" ::: "memory"); }
        }
        // Иначе — ядро: печатаем и стоп
        dump("invalid opcode", "kernel", regs, 0, 0, false);
        for(;;){ asm volatile("sti; hlt":::"memory"); }
}

static inline uint64_t read_cr2_local(void){ uint64_t v=0; asm volatile("mov %%cr2,%0":"=r"(v)); return v; }
static inline uint64_t read_cr3_local(void){ uint64_t v=0; asm volatile("mov %%cr3,%0":"=r"(v)); return v; }

static void idt_dump_regs(cpu_registers_t* r, const char* tag) {
        (void)tag;
        /* Serial */
        qemu_debug_printf("[idt] %s: RIP=%016x RSP=%016x RFLAGS=%016x ERR=%016x\n",
                tag ? tag : "trap",
                (unsigned long long)r->rip,
                (unsigned long long)r->rsp,
                (unsigned long long)r->rflags,
                (unsigned long long)r->error_code);
        qemu_debug_printf("[idt] GPR: RAX=%016x RBX=%016x RCX=%016x RDX=%016x\n",
                (unsigned long long)r->rax, (unsigned long long)r->rbx,
                (unsigned long long)r->rcx, (unsigned long long)r->rdx);
        qemu_debug_printf("[idt] GPR: RSI=%016x RDI=%016x RBP=%016x R8 =%016x\n",
                (unsigned long long)r->rsi, (unsigned long long)r->rdi,
                (unsigned long long)r->rbp, (unsigned long long)r->r8);
        qemu_debug_printf("[idt] GPR: R9 =%016x R10=%016x R11=%016x R12=%016x\n",
                (unsigned long long)r->r9, (unsigned long long)r->r10,
                (unsigned long long)r->r11, (unsigned long long)r->r12);
        qemu_debug_printf("[idt] GPR: R13=%016x R14=%016x R15=%016x CS =%016x SS =%016x\n",
                (unsigned long long)r->r13, (unsigned long long)r->r14,
                (unsigned long long)r->r15, (unsigned long long)r->cs, (unsigned long long)r->ss);
        uint64_t cr2 = read_cr2_local();
        uint64_t cr3 = read_cr3_local();
        qemu_debug_printf("[idt] CR2=%016x CR3=%016x\n",
                (unsigned long long)cr2, (unsigned long long)cr3);
        /* Stack dump (first 8 qwords) */
        qemu_debug_printf("[idt] stack @RSP:\n");
        uint64_t* sp = (uint64_t*)(uintptr_t)r->rsp;
        for (int i = 0; i < 8; i++) {
                uint64_t v = 0;
                /* best-effort read */
                v = sp ? sp[i] : 0;
                qemu_debug_printf("  +%02d: %016x\n", i*8, (unsigned long long)v);
        }
        /* Screen (short summary) */
        kprintf("<(0c)>INT: %s RIP=%016x ERR=%016x\n",
                tag ? tag : "trap",
                (unsigned long long)r->rip,
                (unsigned long long)r->error_code);
	kprintf("<(0c)>RSP=%016x CR2=%016x CR3=%016x\n",
		(unsigned long long)r->rsp,
		(unsigned long long)cr2,
		(unsigned long long)cr3);
	kprintf("<(0c)>RAX=%016x RBX=%016x RCX=%016x RDX=%016x\n",
		(unsigned long long)r->rax, (unsigned long long)r->rbx,
		(unsigned long long)r->rcx, (unsigned long long)r->rdx);
	kprintf("<(0c)>RSI=%016x RDI=%016x RBP=%016x\n",
		(unsigned long long)r->rsi, (unsigned long long)r->rdi,
		(unsigned long long)r->rbp);
	/* Short stack preview on screen (4 qwords) */
	uint64_t* sp_scr = (uint64_t*)(uintptr_t)r->rsp;
	for (int i = 0; i < 4; i++) {
		uint64_t v = sp_scr ? sp_scr[i] : 0;
		kprintf("<(0c)>[SP+%02d]=%016x\n", i*8, (unsigned long long)v);
	}
}

// Handle Divide-by-zero (INT 0). For user faults: kill process and return to idle;
// for kernel faults: print diagnostics and halt.
static void div_zero_handler(cpu_registers_t* regs) {
        qemu_debug_printf("[div0] divide by zero at RIP=0x%llx err=0x%llx\n", (unsigned long long)regs->rip, (unsigned long long)regs->error_code);
        // If fault originated from user mode, terminate the user process safely
        if ((regs->cs & 3) == 3) {
                dump("divide by zero", "user", regs, 0, regs->error_code, true);
                // leave CPU in idle loop to avoid returning into faulty user code
                for(;;){ asm volatile("sti; hlt" ::: "memory"); }
        }
        // Kernel fault: print and halt
        dump("divide by zero", "kernel", regs, 0, regs->error_code, false);
        for(;;){ asm volatile("sti; hlt" ::: "memory"); }
}

static void page_fault_handler(cpu_registers_t* regs) {
        kprint("PAGE FAULT\n");
        (void)regs;
        for (;;) { asm volatile("sti; hlt" ::: "memory"); }
}

static void gp_fault_handler(cpu_registers_t* regs){
    // Никакого рендера/свапа из обработчика GP
        // Строгая семантика для POSIX-подобного поведения: никаких эмуляций в ring3.
        // General Protection Fault в пользовательском процессе рассматривается как фатальная ошибка процесса.
        if ((regs->cs & 3) == 3) {
                idt_dump_regs(regs, "general protection fault");
                asm volatile("sti; hlt" ::: "memory");
                (void)regs;
        }
        // kernel GP — стоп, но оставляем PIT активным для мигания курсора
        (void)regs;
        for(;;){ asm volatile("sti; hlt" ::: "memory"); }
}

static void df_fault_handler(cpu_registers_t* regs){
        // Double Fault (#DF) — используем отдельный IST стек, чтобы избежать triple fault
        kprint("DOUBLE FAULT\n");
        // Застываем в безопасной петле с включёнными прерываниями
        for(;;){ asm volatile("sti; hlt" ::: "memory"); }
}

/* Simple ISR stack canary diagnostics */
static volatile uint64_t g_last_isr_canary_addr = 0;
static volatile uint64_t g_last_isr_canary_val  = 0;
static volatile int      g_last_isr_canary_bad  = 0;

void isr_dispatch(cpu_registers_t* regs) {
        /* Place a canary on the current ISR stack frame */
        volatile uint64_t canary = 0xA5A5DEADBEEFC0DEULL;
        g_last_isr_canary_addr = (uint64_t)(uintptr_t)&canary;
        g_last_isr_canary_val  = canary;
        uint8_t vec = (uint8_t)regs->interrupt_number;

        // Если пришёл IRQ1 (клавиатура) — гарантируем EOI даже при отсутствии обработчика
        if (vec == 33) {
                if (isr_handlers[vec]) {
                        isr_handlers[vec](regs);
                }
                pic_send_eoi(1);
                return;
        }

        // IRQ 32..47: EOI required
        if (vec >= 32 && vec <= 47) {
                if (isr_handlers[vec]) {
                        isr_handlers[vec](regs);
                } else {
                        qemu_debug_printf("Unhandled IRQ %d\n", vec - 32);
                }
                pic_send_eoi(vec - 32);
                return;
                }
                
        // Any other vector: call registered handler if present (e.g., int 0x80)
        if (isr_handlers[vec]) {
                isr_handlers[vec](regs);
                return;
        }
        
        // Exceptions 0..31 without specific handler: print and halt
        if (vec < 32) {
                idt_dump_regs(regs, "exception");
                for (;;){ asm volatile("sti; hlt" ::: "memory"); }
        }
        
        // Unknown vector
        qemu_debug_printf("Unknown interrupt %d (0x%x)\n", vec, vec);
        idt_dump_regs(regs, "unknown");
        for (;;){ asm volatile("sti; hlt" ::: "memory"); }
        // no swap in VGA text mode
        for (;;){ asm volatile("sti; hlt" ::: "memory"); }

        /* Canary check (not reached in fatal paths above) */
        if (canary != 0xA5A5DEADBEEFC0DEULL) {
                g_last_isr_canary_bad = 1;
                qemu_debug_printf("[idt] ISR stack canary CORRUPTED at %016x\n",
                        (unsigned long long)g_last_isr_canary_addr);
                kprintf("<(0c)>ISR stack canary CORRUPTED\n");
        }
}

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t flags) {
        idt[num].offset_low = handler & 0xFFFF;
        idt[num].offset_mid = (handler >> 16) & 0xFFFF;
        idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
        idt[num].selector = selector;
        idt[num].ist = 0;
        idt[num].flags = flags;
        idt[num].reserved = 0;
}

void idt_set_handler(uint8_t num, void (*handler)(cpu_registers_t*)) {
        isr_handlers[num] = handler;
}

void idt_init() {
        idt_ptr.limit = sizeof(idt) - 1;
        idt_ptr.base = (uint64_t)&idt;
        
        for (int i = 0; i < 256; i++) {
                idt_set_gate(i, isr_stub_table[i], 0x08, 0x8E);
        }
        
        // Register detailed page fault handler
        idt_set_handler(14, page_fault_handler);
        // Register divide-by-zero handler (#0)
        idt_set_handler(0, div_zero_handler);
        // Register UD handler (#6)
        idt_set_handler(6, ud_fault_handler);
        // Register GP fault handler (#13)
        idt_set_handler(13, gp_fault_handler);
        // Register DF handler (#8) and put it on IST1
        idt_set_handler(8, df_fault_handler);
        // Пометим IST=1 у вектора 8
        idt[8].ist = 1;
        
        // Register RTC handler (IRQ 8 = vector 40)
        idt_set_handler(40, rtc_handler);
        
        asm volatile("lidt %0" : : "m"(idt_ptr));
}