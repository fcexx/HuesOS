#pragma once

#include <stdint.h>
#include "pci.h"

/**
 * Intel High Definition Audio Controller Driver
 * 
 * This driver implements the Intel HDA specification for PCM audio playback.
 * Supports basic stereo 16-bit 48kHz audio output.
 */

/* PCI Class/Subclass for HDA */
#define HDA_PCI_CLASS       0x04  /* Multimedia controller */
#define HDA_PCI_SUBCLASS    0x03  /* Audio device */

/* Common Intel HDA Vendor/Device IDs */
#define HDA_INTEL_VENDOR_ID 0x8086

/* HDA Controller Register Offsets (MMIO) */
#define HDA_REG_GCAP        0x00   /* Global Capabilities */
#define HDA_REG_VMIN        0x02   /* Minor Version */
#define HDA_REG_VMAJ        0x03   /* Major Version */
#define HDA_REG_OUTPAY      0x04   /* Output Payload Capability */
#define HDA_REG_INPAY       0x06   /* Input Payload Capability */
#define HDA_REG_GCTL        0x08   /* Global Control */
#define HDA_REG_WAKEEN      0x0C   /* Wake Enable */
#define HDA_REG_STATESTS    0x0E   /* State Change Status */
#define HDA_REG_GSTS        0x10   /* Global Status */
#define HDA_REG_OUTSTRMPAY  0x18   /* Output Stream Payload Capability */
#define HDA_REG_INSTRMPAY   0x1A   /* Input Stream Payload Capability */
#define HDA_REG_INTCTL      0x20   /* Interrupt Control */
#define HDA_REG_INTSTS      0x24   /* Interrupt Status */
#define HDA_REG_WALCLK      0x30   /* Wall Clock Counter */
#define HDA_REG_SSYNC       0x38   /* Stream Synchronization */
#define HDA_REG_CORBLBASE   0x40   /* CORB Lower Base Address */
#define HDA_REG_CORBUBASE   0x44   /* CORB Upper Base Address */
#define HDA_REG_CORBWP      0x48   /* CORB Write Pointer */
#define HDA_REG_CORBRP      0x4A   /* CORB Read Pointer */
#define HDA_REG_CORBCTL     0x4C   /* CORB Control */
#define HDA_REG_CORBSTS     0x4D   /* CORB Status */
#define HDA_REG_CORBSIZE    0x4E   /* CORB Size */
#define HDA_REG_RIRBLBASE   0x50   /* RIRB Lower Base Address */
#define HDA_REG_RIRBUBASE   0x54   /* RIRB Upper Base Address */
#define HDA_REG_RIRBWP      0x58   /* RIRB Write Pointer */
#define HDA_REG_RINTCNT     0x5A   /* Response Interrupt Count */
#define HDA_REG_RIRBCTL     0x5C   /* RIRB Control */
#define HDA_REG_RIRBSTS     0x5D   /* RIRB Status */
#define HDA_REG_RIRBSIZE    0x5E   /* RIRB Size */
#define HDA_REG_DPLBASE     0x70   /* DMA Position Lower Base Address */
#define HDA_REG_DPUBASE     0x74   /* DMA Position Upper Base Address */

/* Stream Descriptor Registers (base offset 0x80, 0x20 bytes per stream) */
#define HDA_REG_SD0_BASE    0x80
#define HDA_SD_REG_CTL      0x00   /* Stream Control (offset from SD base) */
#define HDA_SD_REG_STS      0x03   /* Stream Status */
#define HDA_SD_REG_LPIB     0x04   /* Link Position in Buffer */
#define HDA_SD_REG_CBL      0x08   /* Cyclic Buffer Length */
#define HDA_SD_REG_LVI      0x0C   /* Last Valid Index */
#define HDA_SD_REG_FIFOW    0x0E   /* FIFO Watermark (read only) */
#define HDA_SD_REG_FIFOS    0x10   /* FIFO Size */
#define HDA_SD_REG_FMT      0x12   /* Stream Format */
#define HDA_SD_REG_BDPL     0x18   /* BDL Pointer Lower */
#define HDA_SD_REG_BDPU     0x1C   /* BDL Pointer Upper */

/* GCTL Register Bits */
#define HDA_GCTL_CRST       (1 << 0)   /* Controller Reset */
#define HDA_GCTL_FCNTRL     (1 << 1)   /* Flush Control */
#define HDA_GCTL_UNSOL      (1 << 8)   /* Accept Unsolicited Response Enable */

/* CORBCTL Register Bits */
#define HDA_CORBCTL_RUN     (1 << 1)   /* Enable CORB DMA Engine */
#define HDA_CORBCTL_MEIE    (1 << 0)   /* Memory Error Interrupt Enable */

/* RIRBCTL Register Bits */
#define HDA_RIRBCTL_RUN     (1 << 1)   /* Enable RIRB DMA Engine */
#define HDA_RIRBCTL_RINTCTL (1 << 0)   /* Response Interrupt Control */
#define HDA_RIRBCTL_OVERRUN (1 << 2)   /* Overrun Interrupt Control */

/* RIRBSTS Register Bits */
#define HDA_RIRBSTS_RINTFL  (1 << 0)   /* Response Interrupt Flag */
#define HDA_RIRBSTS_ROIS    (1 << 2)   /* Response Overrun Interrupt Status */

/* CORBRP Register Bits */
#define HDA_CORBRP_RST      (1 << 15)  /* CORB Read Pointer Reset */

/* RIRBWP Register Bits */
#define HDA_RIRBWP_RST      (1 << 15)  /* RIRB Write Pointer Reset */

/* Stream Control Register Bits */
#define HDA_SD_CTL_RUN      (1 << 1)   /* Stream Run */
#define HDA_SD_CTL_SRST     (1 << 0)   /* Stream Reset */
#define HDA_SD_CTL_IOCE     (1 << 2)   /* Interrupt On Completion Enable */
#define HDA_SD_CTL_FEIE     (1 << 3)   /* FIFO Error Interrupt Enable */
#define HDA_SD_CTL_DEIE     (1 << 4)   /* Descriptor Error Interrupt Enable */
#define HDA_SD_CTL_STRIPE   (3 << 16)  /* Stripe Control */
#define HDA_SD_CTL_TP       (1 << 18)  /* Traffic Priority */
#define HDA_SD_CTL_DIR      (1 << 19)  /* Bidirectional Direction Control */
#define HDA_SD_CTL_STRM(n)  (((n) & 0xF) << 20) /* Stream Number */

/* Stream Status Register Bits */
#define HDA_SD_STS_FIFORDY  (1 << 5)   /* FIFO Ready */
#define HDA_SD_STS_DESE     (1 << 4)   /* Descriptor Error */
#define HDA_SD_STS_FIFOE    (1 << 3)   /* FIFO Error */
#define HDA_SD_STS_BCIS     (1 << 2)   /* Buffer Completion Interrupt Status */

/* INTCTL Register Bits */
#define HDA_INTCTL_GIE      (1 << 31)  /* Global Interrupt Enable */
#define HDA_INTCTL_CIE      (1 << 30)  /* Controller Interrupt Enable */

/* Buffer sizes */
#define HDA_CORB_SIZE       256        /* CORB entries (256 * 4 bytes = 1KB) */
#define HDA_RIRB_SIZE       256        /* RIRB entries (256 * 8 bytes = 2KB) */
#define HDA_BDL_SIZE        256        /* BDL entries (256 * 16 bytes = 4KB) */
#define HDA_AUDIO_BUF_SIZE  4096       /* Audio buffer per BDL entry (4KB) */
#define HDA_BDL_ENTRIES     4          /* Number of BDL entries in ring buffer */

/* Stream Format Encoding */
#define HDA_FMT_SAMPLE_BASE_441  (1 << 14)   /* 44.1kHz base */
#define HDA_FMT_SAMPLE_BASE_48   (0 << 14)   /* 48kHz base */
#define HDA_FMT_SAMPLE_MULT(x)   (((x) - 1) << 11) /* Sample rate multiplier */
#define HDA_FMT_SAMPLE_DIV(x)    (((x) - 1) << 8)  /* Sample rate divisor */
#define HDA_FMT_BITS_8           (0 << 4)    /* 8-bit samples */
#define HDA_FMT_BITS_16          (1 << 4)    /* 16-bit samples */
#define HDA_FMT_BITS_20          (2 << 4)    /* 20-bit samples */
#define HDA_FMT_BITS_24          (3 << 4)    /* 24-bit samples */
#define HDA_FMT_BITS_32          (4 << 4)    /* 32-bit samples */
#define HDA_FMT_CHAN(x)          (((x) - 1) << 0)  /* Number of channels - 1 */

/* Standard format: 48kHz, 16-bit, Stereo */
#define HDA_FMT_48KHZ_16BIT_STEREO  (HDA_FMT_SAMPLE_BASE_48 | \
                                     HDA_FMT_SAMPLE_MULT(1) | \
                                     HDA_FMT_SAMPLE_DIV(1) | \
                                     HDA_FMT_BITS_16 | \
                                     HDA_FMT_CHAN(2))

/* Buffer Descriptor List Entry */
typedef struct __attribute__((packed)) {
    uint32_t address_low;       /* Lower 32 bits of buffer address */
    uint32_t address_high;      /* Upper 32 bits of buffer address */
    uint32_t length;            /* Buffer length in bytes */
    uint32_t flags;             /* IOC (Interrupt On Completion) bit */
} hda_bdl_entry_t;

#define HDA_BDL_FLAG_IOC    (1 << 0)   /* Interrupt on completion */

/* CORB Entry (Command Output Ring Buffer) */
typedef struct __attribute__((packed)) {
    uint32_t data;              /* Command verb data */
} hda_corb_entry_t;

/* RIRB Entry (Response Input Ring Buffer) */
typedef struct __attribute__((packed)) {
    uint32_t response;          /* Response data */
    uint32_t response_ex;       /* Extended response (codec address, etc.) */
} hda_rirb_entry_t;

#define HDA_RIRB_EX_UNSOL   (1 << 4)   /* Unsolicited response */
#define HDA_RIRB_EX_CODEC(x) ((x) & 0xF) /* Codec address */

/* HDA Controller State */
typedef struct {
    pci_device_t *pci_dev;      /* PCI device descriptor */
    volatile uint8_t *mmio;     /* MMIO base address */
    uint32_t mmio_size;         /* MMIO region size */
    
    /* CORB/RIRB */
    hda_corb_entry_t *corb;     /* CORB buffer (physical memory) */
    hda_rirb_entry_t *rirb;     /* RIRB buffer (physical memory) */
    uint16_t corb_wp;           /* CORB write pointer */
    uint16_t rirb_rp;           /* RIRB read pointer */
    
    /* Stream descriptor */
    uint8_t output_stream_id;   /* Output stream number (1-based) */
    hda_bdl_entry_t *bdl;       /* Buffer Descriptor List */
    uint8_t *audio_buffers[HDA_BDL_ENTRIES]; /* Audio data buffers */
    uint32_t current_buffer;    /* Current buffer being filled */
    
    /* Codec info */
    uint8_t codec_addr;         /* Address of active codec (usually 0) */
    uint8_t num_codecs;         /* Number of detected codecs */
    
    /* State */
    uint8_t initialized;        /* Driver initialized flag */
    uint8_t playing;            /* Currently playing audio */
} hda_controller_t;

/* Public API */

/**
 * hda_init - Initialize the HDA controller
 * 
 * Discovers the HDA device via PCI, maps MMIO registers,
 * resets the controller, and sets up CORB/RIRB.
 * 
 * Returns: 0 on success, negative on error
 */
int hda_init(void);

/**
 * hda_shutdown - Shutdown the HDA controller
 */
void hda_shutdown(void);

/**
 * hda_get_controller - Get the controller state structure
 * 
 * Returns: Pointer to hda_controller_t or NULL if not initialized
 */
hda_controller_t *hda_get_controller(void);

/* Low-level register access */
uint8_t hda_read8(hda_controller_t *hda, uint32_t offset);
uint16_t hda_read16(hda_controller_t *hda, uint32_t offset);
uint32_t hda_read32(hda_controller_t *hda, uint32_t offset);
void hda_write8(hda_controller_t *hda, uint32_t offset, uint8_t value);
void hda_write16(hda_controller_t *hda, uint32_t offset, uint16_t value);
void hda_write32(hda_controller_t *hda, uint32_t offset, uint32_t value);

/* Internal functions (used by other HDA modules) */
int hda_reset_controller(hda_controller_t *hda);
int hda_setup_corb(hda_controller_t *hda);
int hda_setup_rirb(hda_controller_t *hda);
int hda_enable_interrupts(hda_controller_t *hda);
int hda_enumerate_codecs(hda_controller_t *hda);
