#include "../inc/audio.h"
#include "../inc/hda.h"
#include "../inc/hda_codec.h"
#include "../inc/hda_stream.h"
#include "../inc/string.h"
#include "../inc/math.h"
#include <stddef.h>

extern void kprintf(const char *fmt, ...);

/* Global audio state */
static struct {
    hda_controller_t *hda;
    hda_codec_t codec;
    hda_stream_t *stream;
    uint8_t initialized;
    uint8_t playing;
    uint8_t volume;
} g_audio = {0};

/**
 * audio_init - Initialize audio subsystem
 */
int audio_init(void)
{
    int result;
    /* Initialize HDA controller */
    result = hda_init();
    if (result < 0) {
        kprintf("[Audio] Failed to initialize HDA controller\n");
        return -1;
    }

    g_audio.hda = hda_get_controller();
    if (!g_audio.hda) {
        kprintf("[Audio] Failed to get HDA controller\n");
        return -1;
    }

    /* Initialize codec */
    result = hda_codec_init(g_audio.hda, g_audio.hda->codec_addr, &g_audio.codec);
    if (result < 0) {
        kprintf("[Audio] Failed to initialize codec\n");
        return -1;
    }
    kprintf("[Audio] Codec initialized successfully\n");

    /* Dump codec info for debugging */
    
    kprintf("[Debug] Address of g_audio.codec is %p\n", &g_audio.codec);
    kprintf("[Debug] g_audio.codec.codec_addr = %d\n", g_audio.codec.codec_addr);
    kprintf("[Debug] g_audio.codec.vendor_id = 0x%08x\n", g_audio.codec.vendor_id);
    kprintf("[Debug] About to call hda_codec_dump_info...\n");
    hda_codec_dump_info(&g_audio.codec);

    /* Allocate output stream */
    g_audio.stream = hda_stream_alloc(g_audio.hda, 1, HDA_STREAM_OUTPUT);
    if (!g_audio.stream) {
        kprintf("[Audio] Failed to allocate stream\n");
        return -1;
    }

    /* Setup stream with default format (48kHz, 16-bit, stereo) */
    result = hda_stream_setup(g_audio.hda, g_audio.stream, HDA_FMT_48KHZ_16BIT_STEREO);
    if (result < 0) {
        kprintf("[Audio] Failed to setup stream\n");
        return -1;
    }

    /* Configure codec output */
    result = hda_codec_configure_output(g_audio.hda, &g_audio.codec,
                                       g_audio.stream->stream_id,
                                       HDA_FMT_48KHZ_16BIT_STEREO);
    if (result < 0) {
        kprintf("[Audio] Failed to configure codec output\n");
        return -1;
    }

    g_audio.volume = 80; /* Default volume ~63% */
    audio_set_volume(g_audio.volume);

    g_audio.initialized = 1;
    g_audio.playing = 0;

    return 0;
}

/**
 * audio_shutdown - Shutdown audio subsystem
 */
void audio_shutdown(void)
{
    if (!g_audio.initialized)
        return;

    kprintf("[Audio] Shutting down...\n");

    if (g_audio.playing) {
        audio_stop();
    }

    if (g_audio.stream) {
        hda_stream_free(g_audio.hda, g_audio.stream);
    }

    hda_shutdown();

    g_audio.initialized = 0;
}

/**
 * audio_play_pcm - Play PCM audio buffer
 */
int audio_play_pcm(pcm_buffer_t *pcm)
{
    uint32_t i, offset;
    int result;

    if (!g_audio.initialized) {
        kprintf("[Audio] Audio system not initialized\n");
        return -1;
    }

    if (!pcm || !pcm->buffer || pcm->size == 0) {
        kprintf("[Audio] Invalid PCM buffer\n");
        return -1;
    }

    kprintf("[Audio] Playing PCM: %d Hz, %d-bit, %d channels, %d bytes\n",
           pcm->sample_rate, pcm->bit_depth, pcm->channels, pcm->size);

    /* Stop current playback if any */
    if (g_audio.playing) {
        audio_stop();
    }

    /* Fill all stream buffers with PCM data */
    offset = 0;
    for (i = 0; i < g_audio.stream->bdl_entries; i++) {
        uint32_t bytes_to_copy = g_audio.stream->buffer_size;
        
        if (offset + bytes_to_copy > pcm->size) {
            bytes_to_copy = pcm->size - offset;
        }

        if (bytes_to_copy > 0) {
            hda_stream_write_buffer(g_audio.stream, i,
                                   pcm->buffer + offset, bytes_to_copy);
            offset += bytes_to_copy;
        } else {
            /* Fill remaining buffers with silence */
            hda_stream_write_buffer(g_audio.stream, i, NULL, 0);
        }

        /* Loop audio if it's smaller than buffer */
        if (offset >= pcm->size) {
            offset = 0;
        }
    }

    /* Start stream */
    result = hda_stream_start(g_audio.hda, g_audio.stream);
    if (result < 0) {
        kprintf("[Audio] Failed to start stream\n");
        return -1;
    }

    g_audio.playing = 1;
    return 0;
}

/**
 * audio_stop - Stop audio playback
 */
int audio_stop(void)
{
    if (!g_audio.initialized)
        return -1;

    if (!g_audio.playing)
        return 0;

    hda_stream_stop(g_audio.hda, g_audio.stream);
    g_audio.playing = 0;

    kprintf("[Audio] Playback stopped\n");
    return 0;
}

/**
 * audio_pause - Pause playback
 */
int audio_pause(void)
{
    return audio_stop();
}

/**
 * audio_resume - Resume playback
 */
int audio_resume(void)
{
    if (!g_audio.initialized)
        return -1;

    if (g_audio.playing)
        return 0;

    return hda_stream_start(g_audio.hda, g_audio.stream);
}

/**
 * audio_set_volume - Set output volume
 */
int audio_set_volume(uint8_t volume)
{
    if (!g_audio.initialized)
        return -1;

    /* Clamp to 0-100 range */
    if (volume > 100)
        volume = 100;

    /* Convert 0-100 to 0-127 range */
    uint8_t hda_volume = (volume * 127) / 100;

    g_audio.volume = volume;
    
    return hda_codec_set_volume(g_audio.hda, &g_audio.codec, hda_volume);
}

/**
 * audio_get_status - Get audio status
 */
void audio_get_status(audio_status_t *status)
{
    if (!status)
        return;

    status->initialized = g_audio.initialized;
    status->playing = g_audio.playing;
    status->volume = g_audio.volume;
    
    if (g_audio.initialized && g_audio.stream) {
        status->position = hda_stream_get_position(g_audio.hda, g_audio.stream);
        status->total_size = g_audio.stream->cyclic_buffer_length;
    } else {
        status->position = 0;
        status->total_size = 0;
    }
}

/**
 * audio_is_playing - Check if audio is playing
 */
int audio_is_playing(void)
{
    return g_audio.playing;
}

/**
 * audio_generate_sine_wave - Generate sine wave tone
 */
int audio_generate_sine_wave(uint16_t frequency, uint32_t duration_ms, uint8_t volume)
{
    uint32_t sample_rate = 48000;
    uint32_t num_samples = (sample_rate * duration_ms) / 1000;
    uint32_t buffer_size = num_samples * 2 * 2; /* 16-bit stereo */
    int16_t *buffer;
    uint32_t i;
    pcm_buffer_t pcm;

    kprintf("[Audio] Generating %d Hz sine wave, %d ms, volume %d%%\n",
           frequency, duration_ms, volume);

    /* Allocate buffer */
    buffer = (int16_t *)kmalloc(buffer_size);
    if (!buffer) {
        kprintf("[Audio] Failed to allocate sine wave buffer\n");
        return -1;
    }

    /* Generate sine wave */
    double amplitude = (volume / 100.0) * 32767.0 * 0.5; /* 50% max to avoid clipping */
    
    for (i = 0; i < num_samples; i++) {
        double t = (double)i / (double)sample_rate;
        double sample = amplitude * sin(2.0 * M_PI * frequency * t);
        int16_t sample_value = (int16_t)sample;
        
        /* Stereo - same value for left and right */
        buffer[i * 2 + 0] = sample_value; /* Left */
        buffer[i * 2 + 1] = sample_value; /* Right */
    }

    /* Create PCM descriptor */
    pcm.buffer = (uint8_t *)buffer;
    pcm.size = buffer_size;
    pcm.sample_rate = sample_rate;
    pcm.channels = 2;
    pcm.bit_depth = 16;

    /* Play it */
    int result = audio_play_pcm(&pcm);

    /* Note: Don't free buffer immediately - it's being played via DMA */
    /* In a real system, you'd need proper buffer management */

    return result;
}

/**
 * audio_beep - Play a beep sound
 */
int audio_beep(uint16_t frequency, uint32_t duration_ms)
{
    return audio_generate_sine_wave(frequency, duration_ms, 50);
}
