#include "../inc/hda_stream.h"
#include "../inc/hda.h"
#include "../inc/string.h"
#include "../inc/paging.h"
#include <stddef.h>

extern void kprintf(const char *fmt, ...);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Helper: delay for microseconds */
static void udelay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 100; i++)
        ;
}

/* Global stream storage */
static hda_stream_t g_streams[HDA_MAX_STREAMS] = {0};

/**
 * hda_stream_alloc - Allocate and initialize a stream
 */
hda_stream_t *hda_stream_alloc(hda_controller_t *hda, uint8_t stream_id, uint8_t direction)
{
    hda_stream_t *stream;
    int i;

    /* If stream_id is 0, auto-allocate */
    if (stream_id == 0) {
        for (i = 1; i < HDA_MAX_STREAMS; i++) {
            if (!g_streams[i].active) {
                stream_id = i;
                break;
            }
        }
        
        if (stream_id == 0) {
            kprintf("[HDA] No free streams available\n");
            return NULL;
        }
    }

    if (stream_id >= HDA_MAX_STREAMS) {
        kprintf("[HDA] Invalid stream ID: %d\n", stream_id);
        return NULL;
    }

    stream = &g_streams[stream_id];
    
    if (stream->active) {
        kprintf("[HDA] Stream %d already in use\n", stream_id);
        return NULL;
    }

    /* Initialize stream structure */
    memset(stream, 0, sizeof(hda_stream_t));
    stream->stream_id = stream_id;
    stream->direction = direction;
    stream->active = 1;

    /* Calculate stream descriptor base offset */
    /* Input streams start at 0x80, output streams after them */
    /* For simplicity, we'll use stream 1 for output at offset 0x80 */
    stream->base_offset = HDA_REG_SD0_BASE + (stream_id * HDA_STREAM_SIZE);

    kprintf("[HDA] Allocated stream %d (%s) at offset 0x%03x\n",
           stream_id, direction ? "input" : "output", stream->base_offset);

    return stream;
}

/**
 * hda_stream_free - Free a stream
 */
void hda_stream_free(hda_controller_t *hda, hda_stream_t *stream)
{
    int i;

    if (!stream || !stream->active)
        return;

    kprintf("[HDA] Freeing stream %d\n", stream->stream_id);

    /* Stop stream if running */
    hda_stream_stop(hda, stream);

    /* Free BDL */
    if (stream->bdl)
        kfree(stream->bdl);

    /* Free audio buffers */
    for (i = 0; i < HDA_BDL_ENTRIES; i++) {
        if (stream->buffers[i])
            kfree(stream->buffers[i]);
    }

    stream->active = 0;
}

/**
 * hda_stream_reset - Reset stream
 */
int hda_stream_reset(hda_controller_t *hda, hda_stream_t *stream)
{
    uint32_t ctl;
    int timeout;

    kprintf("[HDA] Resetting stream %d\n", stream->stream_id);

    /* Set stream reset bit */
    ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
    ctl |= HDA_SD_CTL_SRST;
    hda_write32(hda, stream->base_offset + HDA_SD_REG_CTL, ctl);

    /* Wait for reset to be acknowledged */
    timeout = 1000;
    while (timeout--) {
        ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
        if (ctl & HDA_SD_CTL_SRST)
            break;
        udelay(10);
    }

    if (timeout <= 0) {
        kprintf("[HDA] Stream reset timeout (enter)\n");
        return -1;
    }

    udelay(100);

    /* Clear stream reset bit */
    ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
    ctl &= ~HDA_SD_CTL_SRST;
    hda_write32(hda, stream->base_offset + HDA_SD_REG_CTL, ctl);

    /* Wait for reset to clear */
    timeout = 1000;
    while (timeout--) {
        ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
        if ((ctl & HDA_SD_CTL_SRST) == 0)
            break;
        udelay(10);
    }

    if (timeout <= 0) {
        kprintf("[HDA] Stream reset timeout (exit)\n");
        return -1;
    }

    kprintf("[HDA] Stream reset complete\n");
    return 0;
}

/**
 * hda_stream_setup - Setup stream BDL and parameters
 */
int hda_stream_setup(hda_controller_t *hda, hda_stream_t *stream, uint16_t format)
{
    int i;
    uint32_t ctl;

    kprintf("[HDA] Setting up stream %d\n", stream->stream_id);

    /* Reset stream first */
    if (hda_stream_reset(hda, stream) < 0) {
        return -1;
    }

    /* Allocate Buffer Descriptor List */
    stream->bdl_entries = HDA_BDL_ENTRIES;
    stream->bdl = (hda_bdl_entry_t *)kmalloc(stream->bdl_entries * sizeof(hda_bdl_entry_t));
    if (!stream->bdl) {
        kprintf("[HDA] Failed to allocate BDL\n");
        return -1;
    }

    memset(stream->bdl, 0, stream->bdl_entries * sizeof(hda_bdl_entry_t));

    /* Allocate audio buffers */
    stream->buffer_size = HDA_AUDIO_BUF_SIZE;
    
    for (i = 0; i < HDA_BDL_ENTRIES; i++) {
        stream->buffers[i] = (uint8_t *)kmalloc(stream->buffer_size);
        if (!stream->buffers[i]) {
            kprintf("[HDA] Failed to allocate audio buffer %d\n", i);
            return -1;
        }
        
        /* Clear buffer */
        memset(stream->buffers[i], 0, stream->buffer_size);

        /* Setup BDL entry with PHYSICAL address for DMA */
        uintptr_t buf_virt_addr = (uintptr_t)stream->buffers[i];
        uint64_t buf_phys_addr = virtual_to_physical(buf_virt_addr);
        
        if (buf_phys_addr == 0) {
            kprintf("[HDA] ERROR: Failed to get physical address for buffer %d\n", i);
            return -1;
        }
        
        stream->bdl[i].address_low = (uint32_t)buf_phys_addr;
        stream->bdl[i].address_high = (uint32_t)(buf_phys_addr >> 32);
        stream->bdl[i].length = stream->buffer_size;
        
        /* Set IOC (Interrupt On Completion) for each buffer */
        stream->bdl[i].flags = HDA_BDL_FLAG_IOC;
    }

    /* Set BDL address using PHYSICAL address */
    uintptr_t bdl_virt_addr = (uintptr_t)stream->bdl;
    uint64_t bdl_phys_addr = virtual_to_physical(bdl_virt_addr);
    
    if (bdl_phys_addr == 0) {
        kprintf("[HDA] ERROR: Failed to get physical address for BDL\n");
        return -1;
    }
    
    kprintf("[HDA] BDL virtual address: 0x%016lx\n", bdl_virt_addr);
    kprintf("[HDA] BDL physical address: 0x%016lx\n", bdl_phys_addr);
    
    /* Check alignment (HDA requires 128-byte alignment) */
    if (bdl_phys_addr & 0x7F) {
        kprintf("[HDA] WARNING: BDL physical address not 128-byte aligned!\n");
    }
    
    hda_write32(hda, stream->base_offset + HDA_SD_REG_BDPL, (uint32_t)bdl_phys_addr);
    hda_write32(hda, stream->base_offset + HDA_SD_REG_BDPU, (uint32_t)(bdl_phys_addr >> 32));

    /* Set cyclic buffer length */
    stream->cyclic_buffer_length = stream->buffer_size * stream->bdl_entries;
    hda_write32(hda, stream->base_offset + HDA_SD_REG_CBL, 
                stream->cyclic_buffer_length);

    /* Set Last Valid Index (LVI) */
    stream->lvi = stream->bdl_entries - 1;
    hda_write16(hda, stream->base_offset + HDA_SD_REG_LVI, stream->lvi);

    /* Set stream format */
    stream->format = format;
    hda_write16(hda, stream->base_offset + HDA_SD_REG_FMT, format);

    /* Setup stream control */
    ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
    
    /* Set stream number (tag) */
    ctl &= ~(0xF << 20);
    ctl |= HDA_SD_CTL_STRM(stream->stream_id);
    
    /* Enable interrupts */
    ctl |= HDA_SD_CTL_IOCE | HDA_SD_CTL_FEIE | HDA_SD_CTL_DEIE;
    
    hda_write32(hda, stream->base_offset + HDA_SD_REG_CTL, ctl);

    stream->current_buffer = 0;

    kprintf("[HDA] Stream setup complete\n");
    kprintf("[HDA]   BDL entries: %d\n", stream->bdl_entries);
    kprintf("[HDA]   Buffer size: %d bytes\n", stream->buffer_size);
    kprintf("[HDA]   Cyclic buffer: %d bytes\n", stream->cyclic_buffer_length);
    kprintf("[HDA]   Format: 0x%04x\n", format);

    return 0;
}

/**
 * hda_stream_start - Start stream DMA
 */
int hda_stream_start(hda_controller_t *hda, hda_stream_t *stream)
{
    uint32_t ctl;
    int timeout;

    kprintf("[HDA] Starting stream %d\n", stream->stream_id);

    /* Set RUN bit */
    ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
    ctl |= HDA_SD_CTL_RUN;
    hda_write32(hda, stream->base_offset + HDA_SD_REG_CTL, ctl);

    /* Wait for FIFO to be ready */
    timeout = 1000;
    while (timeout--) {
        uint8_t sts = hda_read8(hda, stream->base_offset + HDA_SD_REG_STS);
        if (sts & HDA_SD_STS_FIFORDY)
            break;
        udelay(10);
    }

    if (timeout <= 0) {
        kprintf("[HDA] Stream start timeout (FIFO not ready)\n");
        return -1;
    }

    kprintf("[HDA] Stream %d started\n", stream->stream_id);
    return 0;
}

/**
 * hda_stream_stop - Stop stream DMA
 */
int hda_stream_stop(hda_controller_t *hda, hda_stream_t *stream)
{
    uint32_t ctl;
    int timeout;

    kprintf("[HDA] Stopping stream %d\n", stream->stream_id);

    /* Clear RUN bit */
    ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
    ctl &= ~HDA_SD_CTL_RUN;
    hda_write32(hda, stream->base_offset + HDA_SD_REG_CTL, ctl);

    /* Wait for stream to stop */
    timeout = 1000;
    while (timeout--) {
        ctl = hda_read32(hda, stream->base_offset + HDA_SD_REG_CTL);
        if ((ctl & HDA_SD_CTL_RUN) == 0)
            break;
        udelay(10);
    }

    if (timeout <= 0) {
        kprintf("[HDA] Stream stop timeout\n");
        return -1;
    }

    kprintf("[HDA] Stream %d stopped\n", stream->stream_id);
    return 0;
}

/**
 * hda_stream_get_position - Get current playback position
 */
uint32_t hda_stream_get_position(hda_controller_t *hda, hda_stream_t *stream)
{
    return hda_read32(hda, stream->base_offset + HDA_SD_REG_LPIB);
}

/**
 * hda_stream_write_buffer - Write audio data to stream buffer
 */
uint32_t hda_stream_write_buffer(hda_stream_t *stream, uint32_t buffer_index,
                                  const uint8_t *data, uint32_t size)
{
    if (buffer_index >= stream->bdl_entries) {
        return 0;
    }

    if (size > stream->buffer_size) {
        size = stream->buffer_size;
    }

    memcpy(stream->buffers[buffer_index], data, size);
    
    /* Clear remaining bytes if size < buffer_size */
    if (size < stream->buffer_size) {
        memset(stream->buffers[buffer_index] + size, 0, 
               stream->buffer_size - size);
    }

    return size;
}

/**
 * hda_stream_handle_interrupt - Handle buffer completion interrupt
 */
void hda_stream_handle_interrupt(hda_controller_t *hda, hda_stream_t *stream)
{
    uint8_t sts;

    /* Read and acknowledge stream status */
    sts = hda_read8(hda, stream->base_offset + HDA_SD_REG_STS);
    
    if (sts & HDA_SD_STS_BCIS) {
        /* Buffer Completion Interrupt */
        hda_write8(hda, stream->base_offset + HDA_SD_REG_STS, HDA_SD_STS_BCIS);
        
        /* Move to next buffer */
        stream->current_buffer = (stream->current_buffer + 1) % stream->bdl_entries;
    }
    
    if (sts & HDA_SD_STS_FIFOE) {
        /* FIFO Error */
        kprintf("[HDA] Stream %d FIFO error\n", stream->stream_id);
        hda_write8(hda, stream->base_offset + HDA_SD_REG_STS, HDA_SD_STS_FIFOE);
    }
    
    if (sts & HDA_SD_STS_DESE) {
        /* Descriptor Error */
        kprintf("[HDA] Stream %d descriptor error\n", stream->stream_id);
        hda_write8(hda, stream->base_offset + HDA_SD_REG_STS, HDA_SD_STS_DESE);
    }
}
