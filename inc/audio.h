#pragma once

#include <stdint.h>

/**
 * High-Level Audio API for AxonOS
 * 
 * Provides a simple interface for PCM audio playback.
 * Abstracts the underlying HDA hardware implementation.
 */

/* Audio format */
#define AUDIO_FORMAT_PCM_8BIT_MONO      0
#define AUDIO_FORMAT_PCM_8BIT_STEREO    1
#define AUDIO_FORMAT_PCM_16BIT_MONO     2
#define AUDIO_FORMAT_PCM_16BIT_STEREO   3

/* Sample rates */
#define AUDIO_SAMPLE_RATE_8KHZ      8000
#define AUDIO_SAMPLE_RATE_11KHZ     11025
#define AUDIO_SAMPLE_RATE_16KHZ     16000
#define AUDIO_SAMPLE_RATE_22KHZ     22050
#define AUDIO_SAMPLE_RATE_32KHZ     32000
#define AUDIO_SAMPLE_RATE_44KHZ     44100
#define AUDIO_SAMPLE_RATE_48KHZ     48000

/* PCM Buffer descriptor */
typedef struct {
    uint8_t *buffer;            /* PCM data buffer */
    uint32_t size;              /* Buffer size in bytes */
    uint32_t sample_rate;       /* Sample rate in Hz */
    uint8_t channels;           /* 1 = mono, 2 = stereo */
    uint8_t bit_depth;          /* 8 or 16 bits per sample */
} pcm_buffer_t;

/* Audio status */
typedef struct {
    uint8_t initialized;        /* Audio system initialized */
    uint8_t playing;            /* Currently playing audio */
    uint8_t volume;             /* Current volume (0-100) */
    uint32_t position;          /* Current playback position in bytes */
    uint32_t total_size;        /* Total buffer size in bytes */
} audio_status_t;

/* Public API */

/**
 * audio_init - Initialize the audio subsystem
 * 
 * Initializes the HDA controller, codec, and stream engine.
 * 
 * Returns: 0 on success, negative on error
 */
int audio_init(void);

/**
 * audio_shutdown - Shutdown the audio subsystem
 */
void audio_shutdown(void);

/**
 * audio_play_pcm - Play PCM audio data
 * @pcm: PCM buffer descriptor
 * 
 * Plays the provided PCM audio buffer. The function returns immediately
 * and audio plays in the background via DMA.
 * 
 * Returns: 0 on success, negative on error
 */
int audio_play_pcm(pcm_buffer_t *pcm);

/**
 * audio_stop - Stop audio playback
 * 
 * Returns: 0 on success, negative on error
 */
int audio_stop(void);

/**
 * audio_pause - Pause audio playback
 * 
 * Returns: 0 on success, negative on error
 */
int audio_pause(void);

/**
 * audio_resume - Resume audio playback
 * 
 * Returns: 0 on success, negative on error
 */
int audio_resume(void);

/**
 * audio_set_volume - Set output volume
 * @volume: Volume level (0-100)
 * 
 * Returns: 0 on success, negative on error
 */
int audio_set_volume(uint8_t volume);

/**
 * audio_get_status - Get current audio status
 * @status: Pointer to status structure to fill
 */
void audio_get_status(audio_status_t *status);

/**
 * audio_generate_sine_wave - Generate a sine wave tone
 * @frequency: Frequency in Hz (e.g., 440 for A4)
 * @duration_ms: Duration in milliseconds
 * @volume: Volume level (0-100)
 * 
 * Generates and plays a sine wave tone for testing.
 * 
 * Returns: 0 on success, negative on error
 */
int audio_generate_sine_wave(uint16_t frequency, uint32_t duration_ms, uint8_t volume);

/**
 * audio_beep - Play a simple beep sound
 * @frequency: Frequency in Hz
 * @duration_ms: Duration in milliseconds
 * 
 * Convenience function for system beeps.
 * 
 * Returns: 0 on success, negative on error
 */
int audio_beep(uint16_t frequency, uint32_t duration_ms);

/**
 * audio_is_playing - Check if audio is currently playing
 * 
 * Returns: 1 if playing, 0 if not
 */
int audio_is_playing(void);
