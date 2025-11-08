#pragma once

#include <stdint.h>
#include "hda.h"

/**
 * Intel HDA Codec Driver
 * 
 * Handles HD Audio codec communication via verbs,
 * node enumeration, and audio path configuration.
 */

/* Verb encoding macros */
#define HDA_VERB(codec, node, verb, payload) \
    ((((uint32_t)(codec) & 0xF) << 28) | \
     (((uint32_t)(node) & 0xFF) << 20) | \
     (((uint32_t)(verb) & 0xFFF) << 8) | \
     (((uint32_t)(payload) & 0xFF) << 0))

#define HDA_VERB_12BIT(codec, node, verb, payload) \
    ((((uint32_t)(codec) & 0xF) << 28) | \
     (((uint32_t)(node) & 0xFF) << 20) | \
     (((uint32_t)(verb) & 0xF) << 16) | \
     (((uint32_t)(payload) & 0xFFFF) << 0))

/* Standard Verbs */
#define HDA_VERB_GET_PARAM              0xF00
#define HDA_VERB_GET_CONN_SELECT        0xF01
#define HDA_VERB_SET_CONN_SELECT        0x701
#define HDA_VERB_GET_CONN_LIST          0xF02
#define HDA_VERB_GET_PROC_STATE         0xF03
#define HDA_VERB_SET_PROC_STATE         0x703
#define HDA_VERB_GET_SDI_SELECT         0xF04
#define HDA_VERB_GET_POWER_STATE        0xF05
#define HDA_VERB_SET_POWER_STATE        0x705
#define HDA_VERB_GET_STREAM_FORMAT      0xA00
#define HDA_VERB_SET_STREAM_FORMAT      0x200
#define HDA_VERB_GET_AMP_GAIN_MUTE      0xB00
#define HDA_VERB_SET_AMP_GAIN_MUTE      0x300
#define HDA_VERB_GET_CONV_CTRL          0xF06
#define HDA_VERB_SET_CONV_CTRL          0x706
#define HDA_VERB_GET_PIN_CTRL           0xF07
#define HDA_VERB_SET_PIN_CTRL           0x707
#define HDA_VERB_GET_EAPD_ENABLE        0xF0C
#define HDA_VERB_SET_EAPD_ENABLE        0x70C
#define HDA_VERB_GET_CONFIG_DEFAULT     0xF1C
#define HDA_VERB_GET_PIN_SENSE          0xF09
#define HDA_VERB_EXEC_PIN_SENSE         0x709

/* Parameter IDs for GET_PARAM verb */
#define HDA_PARAM_VENDOR_ID             0x00
#define HDA_PARAM_REVISION_ID           0x02
#define HDA_PARAM_SUB_NODE_COUNT        0x04
#define HDA_PARAM_FUNC_GROUP_TYPE       0x05
#define HDA_PARAM_AUDIO_WIDGET_CAP      0x09
#define HDA_PARAM_PCM_SIZE_RATE         0x0A
#define HDA_PARAM_STREAM_FORMATS        0x0B
#define HDA_PARAM_PIN_CAP               0x0C
#define HDA_PARAM_AMP_CAP_INPUT         0x0D
#define HDA_PARAM_AMP_CAP_OUTPUT        0x12
#define HDA_PARAM_CONN_LIST_LEN         0x0E
#define HDA_PARAM_POWER_STATES          0x0F
#define HDA_PARAM_PROC_CAP              0x10
#define HDA_PARAM_GPIO_COUNT            0x11
#define HDA_PARAM_VOL_KNOB_CAP          0x13

/* Function Group Types */
#define HDA_FG_TYPE_AFG                 0x01  /* Audio Function Group */
#define HDA_FG_TYPE_MODEM               0x02  /* Modem Function Group */

/* Audio Widget Types */
#define HDA_WIDGET_TYPE_OUTPUT          0x0   /* Audio Output (DAC) */
#define HDA_WIDGET_TYPE_INPUT           0x1   /* Audio Input (ADC) */
#define HDA_WIDGET_TYPE_MIXER           0x2   /* Mixer */
#define HDA_WIDGET_TYPE_SELECTOR        0x3   /* Selector */
#define HDA_WIDGET_TYPE_PIN             0x4   /* Pin Complex */
#define HDA_WIDGET_TYPE_POWER           0x5   /* Power Widget */
#define HDA_WIDGET_TYPE_VOL_KNOB        0x6   /* Volume Knob */
#define HDA_WIDGET_TYPE_BEEP            0x7   /* Beep Generator */
#define HDA_WIDGET_TYPE_VENDOR          0xF   /* Vendor Defined */

/* Widget Capabilities bits */
#define HDA_WCAP_STEREO                 (1 << 0)  /* Stereo */
#define HDA_WCAP_IN_AMP                 (1 << 1)  /* Input amplifier present */
#define HDA_WCAP_OUT_AMP                (1 << 2)  /* Output amplifier present */
#define HDA_WCAP_AMP_OVRD               (1 << 3)  /* Amp param override */
#define HDA_WCAP_FORMAT_OVRD            (1 << 4)  /* Format override */
#define HDA_WCAP_STRIPE                 (1 << 5)  /* Stripe */
#define HDA_WCAP_PROC_WIDGET            (1 << 6)  /* Processing widget */
#define HDA_WCAP_UNSOL_CAPABLE          (1 << 7)  /* Unsolicited response capable */
#define HDA_WCAP_CONN_LIST              (1 << 8)  /* Connection list */
#define HDA_WCAP_DIGITAL                (1 << 9)  /* Digital I/O */
#define HDA_WCAP_POWER_CTRL             (1 << 10) /* Power control */
#define HDA_WCAP_LR_SWAP                (1 << 11) /* L/R swap */
#define HDA_WCAP_TYPE_SHIFT             20
#define HDA_WCAP_TYPE_MASK              0xF
#define HDA_WCAP_GET_TYPE(x)            (((x) >> HDA_WCAP_TYPE_SHIFT) & HDA_WCAP_TYPE_MASK)

/* Pin Control bits */
#define HDA_PIN_CTL_VREF_ENABLE         (1 << 0)  /* VRef Enable */
#define HDA_PIN_CTL_IN_ENABLE           (1 << 5)  /* Input Enable */
#define HDA_PIN_CTL_OUT_ENABLE          (1 << 6)  /* Output Enable */
#define HDA_PIN_CTL_HP_ENABLE           (1 << 7)  /* Headphone Enable */

/* Amplifier Gain/Mute bits */
#define HDA_AMP_MUTE                    (1 << 7)  /* Mute */
#define HDA_AMP_GAIN_MASK               0x7F      /* Gain value mask */
#define HDA_AMP_SET_OUTPUT              (1 << 15) /* Set output amp */
#define HDA_AMP_SET_INPUT               (1 << 14) /* Set input amp */
#define HDA_AMP_SET_LEFT                (1 << 13) /* Set left channel */
#define HDA_AMP_SET_RIGHT               (1 << 12) /* Set right channel */
#define HDA_AMP_SET_INDEX(x)            (((x) & 0xF) << 8) /* Input index */

/* Converter Control bits */
#define HDA_CONV_STREAM_SHIFT           4
#define HDA_CONV_STREAM_MASK            0xF
#define HDA_CONV_CHANNEL_MASK           0xF

/* Power States */
#define HDA_PS_D0                       0x0  /* Fully on */
#define HDA_PS_D1                       0x1  /* Partial power down */
#define HDA_PS_D2                       0x2  /* Partial power down */
#define HDA_PS_D3                       0x3  /* Off */

/* Pin Config Default - Connection Type */
#define HDA_CFG_PORT_CONN_JACK          0x0
#define HDA_CFG_PORT_CONN_NONE          0x1
#define HDA_CFG_PORT_CONN_FIXED         0x2
#define HDA_CFG_PORT_CONN_BOTH          0x3

/* Pin Config Default - Device Type */
#define HDA_CFG_DEVICE_LINE_OUT         0x0
#define HDA_CFG_DEVICE_SPEAKER          0x1
#define HDA_CFG_DEVICE_HP_OUT           0x2
#define HDA_CFG_DEVICE_CD               0x3
#define HDA_CFG_DEVICE_SPDIF_OUT        0x4
#define HDA_CFG_DEVICE_DIGITAL_OTHER    0x5
#define HDA_CFG_DEVICE_MODEM_LINE       0x6
#define HDA_CFG_DEVICE_MODEM_HANDSET    0x7
#define HDA_CFG_DEVICE_LINE_IN          0x8
#define HDA_CFG_DEVICE_AUX              0x9
#define HDA_CFG_DEVICE_MIC_IN           0xA
#define HDA_CFG_DEVICE_TELEPHONY        0xB
#define HDA_CFG_DEVICE_SPDIF_IN         0xC
#define HDA_CFG_DEVICE_RESERVED         0xE
#define HDA_CFG_DEVICE_OTHER            0xF

/* Codec Node Structure */
typedef struct {
    uint8_t nid;                /* Node ID */
    uint8_t type;               /* Widget type */
    uint32_t capabilities;      /* Widget capabilities */
    uint8_t num_connections;    /* Number of connections */
    uint8_t connections[32];    /* Connection list */
    uint32_t config_default;    /* Pin configuration default */
} hda_node_t;

/* Codec State */
typedef struct {
    uint8_t codec_addr;         /* Codec address (0-14) */
    uint32_t vendor_id;         /* Vendor ID */
    uint32_t revision_id;       /* Revision ID */
    
    /* Function Group info */
    uint8_t afg_nid;            /* Audio Function Group NID */
    uint8_t afg_start_nid;      /* First node in AFG */
    uint8_t afg_num_nodes;      /* Number of nodes in AFG */
    
    /* Audio path nodes */
    uint8_t dac_nid;            /* DAC (Digital-to-Analog Converter) NID */
    uint8_t output_pin_nid;     /* Output pin NID */
    
    /* Node list */
    hda_node_t nodes[128];      /* All codec nodes */
    uint8_t num_nodes;          /* Number of nodes */
    
    uint8_t initialized;        /* Codec initialized flag */
} hda_codec_t;

/* Public API */

/**
 * hda_codec_init - Initialize a codec
 * @hda: HDA controller
 * @codec_addr: Codec address (0-14)
 * @codec: Codec state structure to fill
 * 
 * Returns: 0 on success, negative on error
 */
int hda_codec_init(hda_controller_t *hda, uint8_t codec_addr, hda_codec_t *codec);

/**
 * hda_send_verb - Send a verb command to codec
 * @hda: HDA controller
 * @verb: Encoded verb command
 * @response: Pointer to store response (can be NULL)
 * 
 * Returns: 0 on success, negative on error
 */
int hda_send_verb(hda_controller_t *hda, uint32_t verb, uint32_t *response);

/**
 * hda_codec_probe_nodes - Enumerate all nodes in codec
 * @hda: HDA controller
 * @codec: Codec state
 * 
 * Returns: 0 on success, negative on error
 */
int hda_codec_probe_nodes(hda_controller_t *hda, hda_codec_t *codec);

/**
 * hda_codec_find_dac - Find a suitable DAC node
 * @codec: Codec state
 * 
 * Returns: DAC node ID or 0 if not found
 */
uint8_t hda_codec_find_dac(hda_codec_t *codec);

/**
 * hda_codec_find_output_pin - Find an output pin (speaker/headphones)
 * @codec: Codec state
 * 
 * Returns: Pin node ID or 0 if not found
 */
uint8_t hda_codec_find_output_pin(hda_codec_t *codec);

/**
 * hda_codec_build_path - Build audio path from DAC to output pin
 * @hda: HDA controller
 * @codec: Codec state
 * 
 * Returns: 0 on success, negative on error
 */
int hda_codec_build_path(hda_controller_t *hda, hda_codec_t *codec);

/**
 * hda_codec_configure_output - Configure DAC and output pin for playback
 * @hda: HDA controller
 * @codec: Codec state
 * @stream_id: Stream ID (1-15)
 * @format: Stream format (use HDA_FMT_* macros)
 * 
 * Returns: 0 on success, negative on error
 */
int hda_codec_configure_output(hda_controller_t *hda, hda_codec_t *codec,
                                uint8_t stream_id, uint16_t format);

/**
 * hda_codec_set_volume - Set output volume
 * @hda: HDA controller
 * @codec: Codec state
 * @volume: Volume level (0-127, 0 = mute)
 * 
 * Returns: 0 on success, negative on error
 */
int hda_codec_set_volume(hda_controller_t *hda, hda_codec_t *codec, uint8_t volume);

/**
 * hda_codec_dump_info - Print codec information to console
 * @codec: Codec state
 */
void hda_codec_dump_info(hda_codec_t *codec);
