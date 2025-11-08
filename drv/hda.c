#include "../inc/hda.h"
#include "../inc/pci.h"
#include "../inc/string.h"
#include "../inc/idt.h"
#include "../inc/paging.h"
#include "../inc/heap.h"
#include <stddef.h>

extern void kprintf(const char *fmt, ...);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern void sleep_ms(uint32_t ms);

/* Global HDA controller state */
static hda_controller_t g_hda_controller = {0};

/* Helper: delay for microseconds (simple busy wait) */
static void udelay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 100; i++)
        ;
}

/* Register access functions */
uint8_t hda_read8(hda_controller_t *hda, uint32_t offset)
{
    return *(volatile uint8_t *)(hda->mmio + offset);
}

uint16_t hda_read16(hda_controller_t *hda, uint32_t offset)
{
    return *(volatile uint16_t *)(hda->mmio + offset);
}

uint32_t hda_read32(hda_controller_t *hda, uint32_t offset)
{
    return *(volatile uint32_t *)(hda->mmio + offset);
}

void hda_write8(hda_controller_t *hda, uint32_t offset, uint8_t value)
{
    *(volatile uint8_t *)(hda->mmio + offset) = value;
}

void hda_write16(hda_controller_t *hda, uint32_t offset, uint16_t value)
{
    *(volatile uint16_t *)(hda->mmio + offset) = value;
}

void hda_write32(hda_controller_t *hda, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(hda->mmio + offset) = value;
}

/* IRQ handler forward declaration */
static void hda_irq_handler(cpu_registers_t *regs);

/**
 * hda_find_controller - Find Intel HDA device via PCI
 */
static pci_device_t *hda_find_controller(void)
{
    int i;
    pci_device_t *devs = pci_get_devices();
    int count = pci_get_device_count();

    for (i = 0; i < count; i++) {
        pci_device_t *dev = &devs[i];
        
        /* Check if it's an audio device (class 0x04, subclass 0x03) */
        if (dev->class_code == HDA_PCI_CLASS && 
            dev->subclass == HDA_PCI_SUBCLASS) {
            kprintf("[HDA] Found audio device: %04x:%04x at %u.%u.%u\n",
                   dev->vendor_id, dev->device_id,
                   dev->bus, dev->device, dev->function);
            return dev;
        }
    }

    kprintf("[HDA] No compatible audio device found\n");
    return NULL;
}

/**
 * hda_map_memory - Map HDA MMIO registers
 */
static int hda_map_memory(hda_controller_t *hda)
{
    pci_device_t *dev = hda->pci_dev;
    
    /* BAR0 contains the MMIO base address */
    uint32_t bar0 = dev->bar[0];
    
    if (bar0 == 0 || bar0 == 0xFFFFFFFF) {
        kprintf("[HDA] Invalid BAR0: 0x%08x\n", bar0);
        return -1;
    }

    /* Check if it's a memory BAR (bit 0 = 0) */
    if (bar0 & 0x1) {
        kprintf("[HDA] BAR0 is I/O space, expected memory space\n");
        return -1;
    }

    /* Extract base address (clear lower 4 bits) */
    uint32_t base_addr = bar0 & ~0xF;
    
    kprintf("[HDA] MMIO base address: 0x%08x\n", base_addr);
    
    /* For now, we'll use identity mapping (no paging setup yet) */
    hda->mmio = (volatile uint8_t *)(uintptr_t)base_addr;
    hda->mmio_size = 0x4000; /* 16KB typical size */
    
    return 0;
}

/*
 * hda_acknowledge_interrupts - Acknowledge pending HDA interrupts
 * Clear global INTSTS, RIRB status, CORB status, and per-stream status
 */
static void hda_acknowledge_interrupts(hda_controller_t *hda)
{
    /* Acknowledge global interrupt status by write-back */
    uint32_t intsts = hda_read32(hda, HDA_REG_INTSTS);
    if (intsts)
        hda_write32(hda, HDA_REG_INTSTS, intsts);

    /* Acknowledge RIRB interrupt if any */
    uint8_t rirbsts = hda_read8(hda, HDA_REG_RIRBSTS);
    if (rirbsts)
        hda_write8(hda, HDA_REG_RIRBSTS, rirbsts);

    /* Acknowledge CORB status (memory error etc.) */
    uint8_t corbsts = hda_read8(hda, HDA_REG_CORBSTS);
    if (corbsts)
        hda_write8(hda, HDA_REG_CORBSTS, corbsts);

    /* Best-effort: acknowledge first few stream descriptor statuses */
    for (int sid = 0; sid < 4; sid++) {
        uint32_t base = HDA_REG_SD0_BASE + (sid * 0x20);
        uint8_t sts = hda_read8(hda, base + HDA_SD_REG_STS);
        if (sts)
            hda_write8(hda, base + HDA_SD_REG_STS, sts);
    }
}

/* IRQ handler for HDA (legacy INTx) */
static void hda_irq_handler(cpu_registers_t *regs)
{
    (void)regs;
    hda_controller_t *hda = &g_hda_controller;
    if (!hda->initialized || !hda->mmio)
        return;

    /* Read INTSTS to see if this interrupt is for HDA */
    uint32_t intsts = hda_read32(hda, HDA_REG_INTSTS);
    if (!intsts) {
        /* Spurious */
        return;
    }

    /* Acknowledge and perform minimal per-source clears */
    hda_acknowledge_interrupts(hda);
}

/**
 * hda_reset_controller - Reset the HDA controller
 */
int hda_reset_controller(hda_controller_t *hda)
{
    uint32_t gctl;
    int timeout;

    kprintf("[HDA] Resetting controller...\n");

    /* Enter reset state (clear CRST bit) */
    gctl = hda_read32(hda, HDA_REG_GCTL);
    gctl &= ~HDA_GCTL_CRST;
    hda_write32(hda, HDA_REG_GCTL, gctl);

    /* Wait for reset to take effect */
    timeout = 1000;
    while (timeout--) {
        gctl = hda_read32(hda, HDA_REG_GCTL);
        if ((gctl & HDA_GCTL_CRST) == 0)
            break;
        udelay(10);
    }

    if (timeout <= 0) {
        kprintf("[HDA] Controller reset timeout (enter reset)\n");
        return -1;
    }

    /* Wait a bit for controller to stabilize */
    udelay(100);

    /* Exit reset state (set CRST bit) */
    gctl = hda_read32(hda, HDA_REG_GCTL);
    gctl |= HDA_GCTL_CRST;
    hda_write32(hda, HDA_REG_GCTL, gctl);

    /* Wait for controller to come out of reset */
    timeout = 1000;
    while (timeout--) {
        gctl = hda_read32(hda, HDA_REG_GCTL);
        if (gctl & HDA_GCTL_CRST)
            break;
        udelay(10);
    }

    if (timeout <= 0) {
        kprintf("[HDA] Controller reset timeout (exit reset)\n");
        return -1;
    }

    /* Enable unsolicited responses from codecs so controller accepts
     * and processes all codec responses placed into RIRB. Without this
     * the controller may ignore subsequent responses leading to timeouts.
     */
    gctl = hda_read32(hda, HDA_REG_GCTL);
    gctl |= HDA_GCTL_UNSOL;
    hda_write32(hda, HDA_REG_GCTL, gctl);
    kprintf("[HDA] Unsolicited responses enabled.\n");

    /* Wait for codecs to stabilize */
    udelay(1000);

    kprintf("[HDA] Controller reset successful\n");
    return 0;
}

/**
 * hda_setup_corb - Setup Command Output Ring Buffer
 */
int hda_setup_corb(hda_controller_t *hda)
{
    uint16_t corbctl, corbrp;
    int timeout;

    kprintf("[HDA] Setting up CORB...\n");

    /* Stop CORB if running */
    corbctl = hda_read8(hda, HDA_REG_CORBCTL);
    corbctl &= ~HDA_CORBCTL_RUN;
    hda_write8(hda, HDA_REG_CORBCTL, corbctl);

    /* Wait for CORB DMA to stop */
    timeout = 1000;
    while (timeout--) {
        corbctl = hda_read8(hda, HDA_REG_CORBCTL);
        if ((corbctl & HDA_CORBCTL_RUN) == 0)
            break;
        udelay(10);
    }

    /* Allocate CORB buffer with 128-byte alignment */
    hda->corb = (hda_corb_entry_t *)kmalloc_aligned(HDA_CORB_SIZE * sizeof(hda_corb_entry_t), 128);
    if (!hda->corb) {
        kprintf("[HDA] Failed to allocate aligned CORB buffer\n");
        return -1;
    }

    /* Clear CORB buffer */
    memset(hda->corb, 0, HDA_CORB_SIZE * sizeof(hda_corb_entry_t));

    /* Set CORB base address using PHYSICAL address for DMA */
    uintptr_t corb_virt_addr = (uintptr_t)hda->corb;
    uint64_t corb_phys_addr = virtual_to_physical(corb_virt_addr);
    
    if (corb_phys_addr == 0) {
        kprintf("[HDA] ERROR: Failed to get physical address for CORB\n");
        kfree_aligned(hda->corb);
        hda->corb = NULL;
        return -1;
    }
    
    kprintf("[HDA] CORB virtual address: 0x%016lx\n", corb_virt_addr);
    kprintf("[HDA] CORB physical address: 0x%016lx\n", corb_phys_addr);
    
    /* Check alignment (HDA requires 128-byte alignment) */
    if (corb_phys_addr & 0x7F) {
        kprintf("[HDA] WARNING: CORB physical address not 128-byte aligned!\n");
    }
    
    hda_write32(hda, HDA_REG_CORBLBASE, (uint32_t)corb_phys_addr);
    hda_write32(hda, HDA_REG_CORBUBASE, (uint32_t)(corb_phys_addr >> 32)); /* Upper 32 bits */

    /* Set CORB size to 256 entries */
    hda_write8(hda, HDA_REG_CORBSIZE, 0x02); /* 0x02 = 256 entries */

    /* Reset CORB read pointer */
    corbrp = hda_read16(hda, HDA_REG_CORBRP);
    corbrp |= HDA_CORBRP_RST;
    hda_write16(hda, HDA_REG_CORBRP, corbrp);
    
    udelay(10);
    
    corbrp &= ~HDA_CORBRP_RST;
    hda_write16(hda, HDA_REG_CORBRP, corbrp);

    /* Reset CORB write pointer */
    hda_write16(hda, HDA_REG_CORBWP, 0);
    hda->corb_wp = 0;

    /* Enable CORB DMA */
    corbctl = hda_read8(hda, HDA_REG_CORBCTL);
    corbctl |= HDA_CORBCTL_RUN;
    hda_write8(hda, HDA_REG_CORBCTL, corbctl);

    /* Verify CORB is running */
    udelay(10);
    corbctl = hda_read8(hda, HDA_REG_CORBCTL);
    if (corbctl & HDA_CORBCTL_RUN) {
        kprintf("[HDA] CORB DMA engine started successfully\n");
    } else {
        kprintf("[HDA] WARNING: CORB DMA engine failed to start!\n");
    }

    kprintf("[HDA] CORB setup complete\n");
    return 0;
}

/**
 * hda_setup_rirb - Setup Response Input Ring Buffer
 */
int hda_setup_rirb(hda_controller_t *hda)
{
    uint8_t rirbctl;
    uint16_t rirbwp;
    int timeout;

    kprintf("[HDA] Setting up RIRB...\n");

    /* Stop RIRB if running */
    rirbctl = hda_read8(hda, HDA_REG_RIRBCTL);
    rirbctl &= ~HDA_RIRBCTL_RUN;
    hda_write8(hda, HDA_REG_RIRBCTL, rirbctl);

    /* Wait for RIRB DMA to stop */
    timeout = 1000;
    while (timeout--) {
        rirbctl = hda_read8(hda, HDA_REG_RIRBCTL);
        if ((rirbctl & HDA_RIRBCTL_RUN) == 0)
            break;
        udelay(10);
    }

    /* Allocate RIRB buffer with 128-byte alignment */
    hda->rirb = (hda_rirb_entry_t *)kmalloc_aligned(HDA_RIRB_SIZE * sizeof(hda_rirb_entry_t), 128);
    if (!hda->rirb) {
        kprintf("[HDA] Failed to allocate aligned RIRB buffer\n");
        return -1;
    }

    /* Clear RIRB buffer */
    memset(hda->rirb, 0, HDA_RIRB_SIZE * sizeof(hda_rirb_entry_t));

    /* Set RIRB base address using PHYSICAL address for DMA */
    uintptr_t rirb_virt_addr = (uintptr_t)hda->rirb;
    uint64_t rirb_phys_addr = virtual_to_physical(rirb_virt_addr);
    
    if (rirb_phys_addr == 0) {
        kprintf("[HDA] ERROR: Failed to get physical address for RIRB\n");
        kfree_aligned(hda->rirb);
        hda->rirb = NULL;
        return -1;
    }
    
    kprintf("[HDA] RIRB virtual address: 0x%016lx\n", rirb_virt_addr);
    kprintf("[HDA] RIRB physical address: 0x%016lx\n", rirb_phys_addr);
    
    /* Check alignment (HDA requires 128-byte alignment) */
    if (rirb_phys_addr & 0x7F) {
        kprintf("[HDA] WARNING: RIRB physical address not 128-byte aligned!\n");
    }
    
    hda_write32(hda, HDA_REG_RIRBLBASE, (uint32_t)rirb_phys_addr);
    hda_write32(hda, HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys_addr >> 32)); /* Upper 32 bits */

    /* Set RIRB size to 256 entries */
    hda_write8(hda, HDA_REG_RIRBSIZE, 0x02); /* 0x02 = 256 entries */

    /* Reset RIRB write pointer */
    rirbwp = hda_read16(hda, HDA_REG_RIRBWP);
    rirbwp |= HDA_RIRBWP_RST;
    hda_write16(hda, HDA_REG_RIRBWP, rirbwp);
    
    udelay(10);
    
    rirbwp &= ~HDA_RIRBWP_RST;
    hda_write16(hda, HDA_REG_RIRBWP, rirbwp);

    hda->rirb_rp = 0;

    /* Set response interrupt count (1 response) */
    hda_write16(hda, HDA_REG_RINTCNT, 1);

    /* Enable RIRB DMA and interrupts */
    rirbctl = hda_read8(hda, HDA_REG_RIRBCTL);
    rirbctl |= HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINTCTL;
    hda_write8(hda, HDA_REG_RIRBCTL, rirbctl);

    /* Verify RIRB is running */
    udelay(10);
    rirbctl = hda_read8(hda, HDA_REG_RIRBCTL);
    if (rirbctl & HDA_RIRBCTL_RUN) {
        kprintf("[HDA] RIRB DMA engine started successfully\n");
    } else {
        kprintf("[HDA] WARNING: RIRB DMA engine failed to start!\n");
    }

    kprintf("[HDA] RIRB setup complete\n");
    return 0;
}

/**
 * hda_enumerate_codecs - Detect connected codecs
 */
int hda_enumerate_codecs(hda_controller_t *hda)
{
    uint16_t statests;
    int i;

    kprintf("[HDA] Enumerating codecs...\n");

    /* Read codec status */
    statests = hda_read16(hda, HDA_REG_STATESTS);
    
    kprintf("[HDA] STATESTS = 0x%04x\n", statests);

    hda->num_codecs = 0;
    for (i = 0; i < 15; i++) {
        if (statests & (1 << i)) {
            kprintf("[HDA] Codec %d detected\n", i);
            hda->num_codecs++;
            
            /* Use first codec */
            if (hda->codec_addr == 0xFF) {
                hda->codec_addr = i;
            }
        }
    }

    if (hda->num_codecs == 0) {
        kprintf("[HDA] No codecs detected!\n");
        return -1;
    }

    kprintf("[HDA] Found %d codec(s), using codec %d\n", 
           hda->num_codecs, hda->codec_addr);

    return 0;
}

/**
 * hda_enable_interrupts - Enable HDA interrupts
 */
int hda_enable_interrupts(hda_controller_t *hda)
{
    uint32_t intctl;

    kprintf("[HDA] Enabling interrupts...\n");

    /* Enable global interrupt and controller interrupt */
    intctl = hda_read32(hda, HDA_REG_INTCTL);
    intctl |= HDA_INTCTL_GIE | HDA_INTCTL_CIE;
    hda_write32(hda, HDA_REG_INTCTL, intctl);

    kprintf("[HDA] Interrupts enabled\n");
    return 0;
}

/**
 * hda_init - Initialize HDA controller
 */
int hda_init(void)
{
    hda_controller_t *hda = &g_hda_controller;
    uint16_t gcap;
    uint8_t vmaj, vmin;
    int result;

    kprintf("\n[HDA] Intel High Definition Audio Driver v1.0\n");
    kprintf("[HDA] Initializing...\n");

    /* Clear controller state */
    memset(hda, 0, sizeof(hda_controller_t));
    hda->codec_addr = 0xFF; /* Invalid initially */

    /* Find HDA device via PCI */
    hda->pci_dev = hda_find_controller();
    if (!hda->pci_dev) {
        kprintf("[HDA] No HDA controller found\n");
        return -1;
    }

    /* Enable PCI bus mastering (required for DMA) */
    uint32_t cmd = pci_config_read_dword(hda->pci_dev->bus, 
                                         hda->pci_dev->device,
                                         hda->pci_dev->function, 0x04);
    cmd |= 0x06; /* Enable Memory Space and Bus Master */
    pci_config_write_dword(hda->pci_dev->bus, 
                          hda->pci_dev->device,
                          hda->pci_dev->function, 0x04, cmd);

    /* Map MMIO registers */
    result = hda_map_memory(hda);
    if (result < 0) {
        kprintf("[HDA] Failed to map MMIO registers\n");
        return -1;
    }

    /* Read controller capabilities */
    gcap = hda_read16(hda, HDA_REG_GCAP);
    vmaj = hda_read8(hda, HDA_REG_VMAJ);
    vmin = hda_read8(hda, HDA_REG_VMIN);

    kprintf("[HDA] Version: %d.%d\n", vmaj, vmin);
    kprintf("[HDA] Capabilities: 0x%04x\n", gcap);
    kprintf("[HDA]   Output streams: %d\n", (gcap >> 12) & 0xF);
    kprintf("[HDA]   Input streams: %d\n", (gcap >> 8) & 0xF);
    kprintf("[HDA]   Bidirectional streams: %d\n", (gcap >> 3) & 0x1F);

    /* Reset controller */
    result = hda_reset_controller(hda);
    if (result < 0) {
        kprintf("[HDA] Controller reset failed\n");
        return -1;
    }

    /* Setup CORB (Command Output Ring Buffer) */
    result = hda_setup_corb(hda);
    if (result < 0) {
        kprintf("[HDA] CORB setup failed\n");
        return -1;
    }

    /* Setup RIRB (Response Input Ring Buffer) */
    result = hda_setup_rirb(hda);
    if (result < 0) {
        kprintf("[HDA] RIRB setup failed\n");
        return -1;
    }

    /* Enumerate codecs */
    result = hda_enumerate_codecs(hda);
    if (result < 0) {
        kprintf("[HDA] Codec enumeration failed\n");
        return -1;
    }

    /* Enable interrupts */
    result = hda_enable_interrupts(hda);
    if (result < 0) {
        kprintf("[HDA] Interrupt setup failed\n");
        return -1;
    }

    /* Register IRQ handler for legacy INTx line (vector = 32 + IRQ) */
    if (hda->pci_dev->irq) {
        uint8_t vector = 32 + hda->pci_dev->irq;
        idt_set_handler(vector, hda_irq_handler);
        kprintf("[HDA] Registered IRQ handler at vector %u (IRQ %u)\n", vector, hda->pci_dev->irq);
    } else {
        kprintf("[HDA] Warning: PCI device reports IRQ 0; IRQ handler not registered\n");
    }

    hda->initialized = 1;
    kprintf("[HDA] Initialization complete!\n\n");

    return 0;
}

/**
 * hda_shutdown - Shutdown HDA controller
 */
void hda_shutdown(void)
{
    hda_controller_t *hda = &g_hda_controller;

    if (!hda->initialized)
        return;

    kprintf("[HDA] Shutting down...\n");

    /* Disable interrupts */
    hda_write32(hda, HDA_REG_INTCTL, 0);

    /* Stop CORB */
    hda_write8(hda, HDA_REG_CORBCTL, 0);

    /* Stop RIRB */
    hda_write8(hda, HDA_REG_RIRBCTL, 0);

    /* Free buffers */
    if (hda->corb)
        kfree_aligned(hda->corb);
    if (hda->rirb)
        kfree_aligned(hda->rirb);

    /* Reset controller */
    hda_write32(hda, HDA_REG_GCTL, 0);

    hda->initialized = 0;
    kprintf("[HDA] Shutdown complete\n");
}

/**
 * hda_get_controller - Get controller state
 */
hda_controller_t *hda_get_controller(void)
{
    if (!g_hda_controller.initialized)
        return NULL;
    return &g_hda_controller;
}
