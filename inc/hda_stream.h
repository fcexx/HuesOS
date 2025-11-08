#pragma once

#include <stdint.h>
#include "hda.h"

/**
 * Intel HDA Stream Engine Driver
 * 
 * Manages DMA streams for audio playback/recording.
 * Handles Buffer Descriptor Lists (BDL) and stream control.
 */

/* Stream descriptor size (0x20 bytes per stream) */
#define HDA_STREAM_SIZE         0x20

/* Maximum number of streams */
#define HDA_MAX_STREAMS         16

/* Stream directions */
#define HDA_STREAM_OUTPUT       0
#define HDA_STREAM_INPUT        1

/* Stream State */
typedef struct {
    uint8_t stream_id;          /* Stream ID (1-15, 0 = unused) */
    uint8_t direction;          /* 0 = output, 1 = input */
    uint8_t active;             /* Stream is active */
    
    uint32_t base_offset;       /* MMIO offset of stream descriptor */
    
    /* Buffer Descriptor List */
    hda_bdl_entry_t *bdl;       /* BDL physical address */
    uint32_t bdl_entries;       /* Number of BDL entries */
    
    /* Audio buffers */
    uint8_t *buffers[HDA_BDL_ENTRIES];  /* Audio data buffers */
    uint32_t buffer_size;       /* Size of each buffer in bytes */
    uint32_t current_buffer;    /* Current buffer index */
    
    /* Stream parameters */
    uint32_t cyclic_buffer_length; /* Total cyclic buffer length */
    uint16_t format;            /* Stream format */
    uint8_t lvi;                /* Last Valid Index */
    
} hda_stream_t;

/* Public API */

/**
 * hda_stream_alloc - Allocate and initialize a stream
 * @hda: HDA controller
 * @stream_id: Desired stream ID (1-15), or 0 to auto-allocate
 * @direction: HDA_STREAM_OUTPUT or HDA_STREAM_INPUT
 * 
 * Returns: Pointer to stream structure, or NULL on error
 */
hda_stream_t *hda_stream_alloc(hda_controller_t *hda, uint8_t stream_id, uint8_t direction);

/**
 * hda_stream_free - Free a stream
 * @hda: HDA controller
 * @stream: Stream to free
 */
void hda_stream_free(hda_controller_t *hda, hda_stream_t *stream);

/**
 * hda_stream_setup - Setup stream parameters and BDL
 * @hda: HDA controller
 * @stream: Stream to setup
 * @format: Stream format (use HDA_FMT_* macros)
 * 
 * Returns: 0 on success, negative on error
 */
int hda_stream_setup(hda_controller_t *hda, hda_stream_t *stream, uint16_t format);

/**
 * hda_stream_start - Start stream DMA
 * @hda: HDA controller
 * @stream: Stream to start
 * 
 * Returns: 0 on success, negative on error
 */
int hda_stream_start(hda_controller_t *hda, hda_stream_t *stream);

/**
 * hda_stream_stop - Stop stream DMA
 * @hda: HDA controller
 * @stream: Stream to stop
 * 
 * Returns: 0 on success, negative on error
 */
int hda_stream_stop(hda_controller_t *hda, hda_stream_t *stream);

/**
 * hda_stream_reset - Reset stream to initial state
 * @hda: HDA controller
 * @stream: Stream to reset
 * 
 * Returns: 0 on success, negative on error
 */
int hda_stream_reset(hda_controller_t *hda, hda_stream_t *stream);

/**
 * hda_stream_get_position - Get current playback position
 * @hda: HDA controller
 * @stream: Stream
 * 
 * Returns: Current position in bytes within cyclic buffer
 */
uint32_t hda_stream_get_position(hda_controller_t *hda, hda_stream_t *stream);

/**
 * hda_stream_write_buffer - Write audio data to stream buffer
 * @stream: Stream
 * @buffer_index: Buffer index (0 to HDA_BDL_ENTRIES-1)
 * @data: Audio data to write
 * @size: Size of data in bytes (must be <= buffer_size)
 * 
 * Returns: Number of bytes written
 */
uint32_t hda_stream_write_buffer(hda_stream_t *stream, uint32_t buffer_index,
                                  const uint8_t *data, uint32_t size);

/**
 * hda_stream_handle_interrupt - Handle stream interrupt (Buffer Completion)
 * @hda: HDA controller
 * @stream: Stream that triggered interrupt
 * 
 * This function should be called from the interrupt handler when a
 * Buffer Completion Interrupt (BCIS) occurs.
 */
void hda_stream_handle_interrupt(hda_controller_t *hda, hda_stream_t *stream);
