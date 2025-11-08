#include "../inc/hda_codec.h"
#include "../inc/hda.h"
#include "../inc/string.h"
#include <stddef.h>

extern void kprintf(const char *fmt, ...);
extern void *kmalloc(size_t size);
extern void sleep_ms(uint32_t ms);

/* Helper: delay for microseconds */
static void udelay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 100; i++)
        ;
}

/**
 * hda_send_verb - Send a verb command to codec and get response
 * 
 * Improved version that handles unsolicited responses correctly.
 * Checks response tag to match the target codec.
 */
int hda_send_verb(hda_controller_t *hda, uint32_t verb, uint32_t *response)
{
    uint16_t rirbwp;
    int timeout;
    uint8_t codec_addr = (verb >> 28) & 0xF;

    /* Debug: log verb being sent */
    #ifdef HDA_VERB_DEBUG
    kprintf("[HDA] Sending verb 0x%08x to codec %d\n", verb, codec_addr);
    #endif

    /* Write verb to CORB */
    hda->corb_wp = (hda->corb_wp + 1) % HDA_CORB_SIZE;
    hda->corb[hda->corb_wp].data = verb;

    /* Update CORB write pointer */
    hda_write16(hda, HDA_REG_CORBWP, hda->corb_wp);

    /* If response not requested, return immediately */
    if (!response) {
        return 0;
    }

    /* Wait for response with tag matching */
    timeout = 1000; /* ~10ms */
    while (timeout--) {
        rirbwp = hda_read16(hda, HDA_REG_RIRBWP) & 0xFF;
        
        /* Process all new responses */
        while (hda->rirb_rp != rirbwp) {
            hda->rirb_rp = (hda->rirb_rp + 1) % HDA_RIRB_SIZE;
            
            uint32_t res = hda->rirb[hda->rirb_rp].response;
            uint32_t res_ex = hda->rirb[hda->rirb_rp].response_ex;
            uint8_t tag = res_ex & 0xF;
            
            /* Check if this is our response */
            if (tag == codec_addr) {
                *response = res;

                /* Acknowledge the response by clearing the interrupt status bit.
                 * This is crucial even in polling mode to signal the controller
                 * that it can process the next response. We clear it by writing '1'.
                 */
                hda_write8(hda, HDA_REG_RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_ROIS);

                return 0;
            } else {
                /* Unsolicited response or from another codec */
                kprintf("[HDA] Unhandled response: 0x%08x (tag=%d, expected=%d)\n", 
                       res, tag, codec_addr);
            }
        }
        
        udelay(10);
    }
    
    kprintf("[HDA] Verb timeout: 0x%08x (codec %d)\n", verb, codec_addr);
    return -1;
}

/**
 * hda_codec_get_param - Read codec parameter
 */
static int hda_codec_get_param(hda_controller_t *hda, uint8_t codec_addr,
                               uint8_t nid, uint8_t param_id, uint32_t *value)
{
    uint32_t verb = HDA_VERB(codec_addr, nid, HDA_VERB_GET_PARAM, param_id);
    return hda_send_verb(hda, verb, value);
}

/**
 * hda_codec_probe_nodes - Enumerate all nodes in codec AFG
 */
int hda_codec_probe_nodes(hda_controller_t *hda, hda_codec_t *codec)
{
    uint32_t response;
    int i, j, result;
    uint8_t nid;

    kprintf("[HDA] Probing codec %d nodes...\n", codec->codec_addr);

    codec->num_nodes = 0;

    for (i = 0; i < codec->afg_num_nodes; i++) {
        nid = codec->afg_start_nid + i;
        hda_node_t *node = &codec->nodes[codec->num_nodes];
        
        node->nid = nid;

        /* Get widget capabilities */
        result = hda_codec_get_param(hda, codec->codec_addr, nid,
                                    HDA_PARAM_AUDIO_WIDGET_CAP, &response);
        if (result < 0)
            continue;

        node->capabilities = response;
        node->type = HDA_WCAP_GET_TYPE(response);

        /* Get connection list if available */
        if (response & HDA_WCAP_CONN_LIST) {
            uint32_t conn_len;
            result = hda_codec_get_param(hda, codec->codec_addr, nid,
                                        HDA_PARAM_CONN_LIST_LEN, &conn_len);
            if (result == 0) {
                node->num_connections = conn_len & 0x7F;
                
                /* Read connection list */
                uint32_t conn_list;
                uint32_t verb = HDA_VERB(codec->codec_addr, nid,
                                       HDA_VERB_GET_CONN_LIST, 0);
                if (hda_send_verb(hda, verb, &conn_list) == 0) {
                    /* Parse up to 4 connections from one verb */
                    for (j = 0; j < 4 && j < node->num_connections; j++) {
                        node->connections[j] = (conn_list >> (j * 8)) & 0xFF;
                    }
                }
            }
        }

        /* For pin widgets, get configuration default */
        if (node->type == HDA_WIDGET_TYPE_PIN) {
            uint32_t verb = HDA_VERB(codec->codec_addr, nid,
                                   HDA_VERB_GET_CONFIG_DEFAULT, 0);
            if (hda_send_verb(hda, verb, &response) == 0) {
                node->config_default = response;
            }
        }

        codec->num_nodes++;
    }

    kprintf("[HDA] Found %d nodes in AFG\n", codec->num_nodes);
    return 0;
}

/**
 * hda_codec_find_dac - Find a suitable DAC node
 */
uint8_t hda_codec_find_dac(hda_codec_t *codec)
{
    int i;

    for (i = 0; i < codec->num_nodes; i++) {
        hda_node_t *node = &codec->nodes[i];
        
        if (node->type == HDA_WIDGET_TYPE_OUTPUT) {
            kprintf("[HDA] Found DAC at node 0x%02x\n", node->nid);
            return node->nid;
        }
    }

    kprintf("[HDA] No DAC found\n");
    return 0;
}

/**
 * hda_codec_find_output_pin - Find output pin (speaker/headphones)
 */
uint8_t hda_codec_find_output_pin(hda_codec_t *codec)
{
    int i;

    for (i = 0; i < codec->num_nodes; i++) {
        hda_node_t *node = &codec->nodes[i];
        
        if (node->type == HDA_WIDGET_TYPE_PIN) {
            /* Extract port connectivity and device type from config_default */
            uint32_t port_conn = (node->config_default >> 30) & 0x3;
            uint32_t device = (node->config_default >> 20) & 0xF;
            
            /* Check for output devices */
            if (device == HDA_CFG_DEVICE_LINE_OUT ||
                device == HDA_CFG_DEVICE_SPEAKER ||
                device == HDA_CFG_DEVICE_HP_OUT) {
                
                /* Skip unconnected pins */
                if (port_conn == HDA_CFG_PORT_CONN_NONE)
                    continue;
                
                kprintf("[HDA] Found output pin at node 0x%02x (device type: %d)\n",
                       node->nid, device);
                return node->nid;
            }
        }
    }

    /* Fallback: find any pin with output capability */
    for (i = 0; i < codec->num_nodes; i++) {
        hda_node_t *node = &codec->nodes[i];
        
        if (node->type == HDA_WIDGET_TYPE_PIN) {
            kprintf("[HDA] Using pin at node 0x%02x as fallback\n", node->nid);
            return node->nid;
        }
    }

    kprintf("[HDA] No output pin found\n");
    return 0;
}

/**
 * hda_codec_build_path - Build audio path from DAC to pin
 */
int hda_codec_build_path(hda_controller_t *hda, hda_codec_t *codec)
{
    uint8_t dac_nid, pin_nid;
    int i;

    /* Find DAC and output pin */
    dac_nid = hda_codec_find_dac(codec);
    if (!dac_nid) {
        kprintf("[HDA] Cannot build path: no DAC\n");
        return -1;
    }

    pin_nid = hda_codec_find_output_pin(codec);
    if (!pin_nid) {
        kprintf("[HDA] Cannot build path: no output pin\n");
        return -1;
    }

    codec->dac_nid = dac_nid;
    codec->output_pin_nid = pin_nid;

    /* Find pin node */
    hda_node_t *pin_node = NULL;
    for (i = 0; i < codec->num_nodes; i++) {
        if (codec->nodes[i].nid == pin_nid) {
            pin_node = &codec->nodes[i];
            break;
        }
    }

    if (!pin_node) {
        kprintf("[HDA] Pin node not found\n");
        return -1;
    }

    /* Check if pin is connected to DAC */
    if (pin_node->num_connections > 0) {
        /* Set connection to DAC */
        for (i = 0; i < pin_node->num_connections; i++) {
            if (pin_node->connections[i] == dac_nid) {
                /* Select this connection */
                uint32_t verb = HDA_VERB(codec->codec_addr, pin_nid,
                                       HDA_VERB_SET_CONN_SELECT, i);
                hda_send_verb(hda, verb, NULL);
                
                kprintf("[HDA] Connected pin 0x%02x to DAC 0x%02x (index %d)\n",
                       pin_nid, dac_nid, i);
                break;
            }
        }
    }

    kprintf("[HDA] Audio path: DAC 0x%02x -> Pin 0x%02x\n", dac_nid, pin_nid);
    return 0;
}

/**
 * hda_codec_configure_output - Configure DAC and pin for playback
 */
int hda_codec_configure_output(hda_controller_t *hda, hda_codec_t *codec,
                                uint8_t stream_id, uint16_t format)
{
    uint32_t verb;

    kprintf("[HDA] Configuring output for stream %d, format 0x%04x\n",
           stream_id, format);

    /* Power on AFG */
    verb = HDA_VERB(codec->codec_addr, codec->afg_nid,
                   HDA_VERB_SET_POWER_STATE, HDA_PS_D0);
    hda_send_verb(hda, verb, NULL);

    /* Power on DAC */
    verb = HDA_VERB(codec->codec_addr, codec->dac_nid,
                   HDA_VERB_SET_POWER_STATE, HDA_PS_D0);
    hda_send_verb(hda, verb, NULL);

    /* Power on output pin */
    verb = HDA_VERB(codec->codec_addr, codec->output_pin_nid,
                   HDA_VERB_SET_POWER_STATE, HDA_PS_D0);
    hda_send_verb(hda, verb, NULL);

    udelay(100);

    /* Set stream ID and channel for DAC */
    uint8_t stream_chan = (stream_id << HDA_CONV_STREAM_SHIFT) | 0;
    verb = HDA_VERB(codec->codec_addr, codec->dac_nid,
                   HDA_VERB_SET_CONV_CTRL, stream_chan);
    hda_send_verb(hda, verb, NULL);

    /* Set format for DAC */
    verb = HDA_VERB_12BIT(codec->codec_addr, codec->dac_nid,
                         HDA_VERB_SET_STREAM_FORMAT >> 8, format);
    hda_send_verb(hda, verb, NULL);

    /* Unmute and set gain for DAC output amplifier */
    if (codec->nodes[0].capabilities & HDA_WCAP_OUT_AMP) {
        uint16_t amp_val = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT | 
                          HDA_AMP_SET_RIGHT | 0x7F; /* Max gain, unmute */
        verb = HDA_VERB_12BIT(codec->codec_addr, codec->dac_nid,
                             HDA_VERB_SET_AMP_GAIN_MUTE >> 8, amp_val);
        hda_send_verb(hda, verb, NULL);
    }

    /* Enable output on pin */
    verb = HDA_VERB(codec->codec_addr, codec->output_pin_nid,
                   HDA_VERB_SET_PIN_CTRL, HDA_PIN_CTL_OUT_ENABLE);
    hda_send_verb(hda, verb, NULL);

    /* Unmute pin output amplifier */
    uint16_t amp_val = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT | 
                      HDA_AMP_SET_RIGHT | 0x7F; /* Max gain, unmute */
    verb = HDA_VERB_12BIT(codec->codec_addr, codec->output_pin_nid,
                         HDA_VERB_SET_AMP_GAIN_MUTE >> 8, amp_val);
    hda_send_verb(hda, verb, NULL);

    /* Enable EAPD if present (External Amplifier Power Down) */
    verb = HDA_VERB(codec->codec_addr, codec->output_pin_nid,
                   HDA_VERB_SET_EAPD_ENABLE, 0x02);
    hda_send_verb(hda, verb, NULL);

    kprintf("[HDA] Output configuration complete\n");
    return 0;
}

/**
 * hda_codec_set_volume - Set output volume
 */
int hda_codec_set_volume(hda_controller_t *hda, hda_codec_t *codec, uint8_t volume)
{
    uint32_t verb;
    uint16_t amp_val;

    /* Clamp volume to 0-127 range */
    if (volume > 127)
        volume = 127;

    /* Set volume for DAC */
    amp_val = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT | volume;
    verb = HDA_VERB_12BIT(codec->codec_addr, codec->dac_nid,
                         HDA_VERB_SET_AMP_GAIN_MUTE >> 8, amp_val);
    hda_send_verb(hda, verb, NULL);

    /* Set volume for output pin */
    verb = HDA_VERB_12BIT(codec->codec_addr, codec->output_pin_nid,
                         HDA_VERB_SET_AMP_GAIN_MUTE >> 8, amp_val);
    hda_send_verb(hda, verb, NULL);

    return 0;
}

/**
 * hda_codec_init - Initialize a codec
 */
int hda_codec_init(hda_controller_t *hda, uint8_t codec_addr, hda_codec_t *codec)
{
    uint32_t response;
    int result;

    kprintf("\n[HDA] Initializing codec %d...\n", codec_addr);

    memset(codec, 0, sizeof(hda_codec_t));
    codec->codec_addr = codec_addr;

    /* Get vendor ID */
    result = hda_codec_get_param(hda, codec_addr, 0,
                                HDA_PARAM_VENDOR_ID, &response);
    if (result < 0) {
        kprintf("[HDA] Failed to read codec vendor ID (result=%d) [IGNORED]\n", result);
        //return -1;
    }
    codec->vendor_id = response;
    kprintf("[HDA] Vendor ID: 0x%08x\n", codec->vendor_id);

    /* Get revision ID */
    result = hda_codec_get_param(hda, codec_addr, 0,
                                HDA_PARAM_REVISION_ID, &response);
    if (result == 0) {
        codec->revision_id = response;
        kprintf("[HDA] Revision ID: 0x%08x\n", codec->revision_id);
    }

    /* Get sub-node count for root node (node 0) */
    result = hda_codec_get_param(hda, codec_addr, 0,
                                HDA_PARAM_SUB_NODE_COUNT, &response);
    if (result < 0) {
        kprintf("[HDA] Failed to get sub-node count (result=%d)\n", result);
        return -1;
    }

    uint8_t start_nid = (response >> 16) & 0xFF;
    uint8_t num_nodes = response & 0xFF;

    kprintf("[HDA] Root has %d sub-nodes starting at 0x%02x\n", 
           num_nodes, start_nid);

    /* Find Audio Function Group */
    int i;
    for (i = 0; i < num_nodes; i++) {
        uint8_t nid = start_nid + i;
        
        result = hda_codec_get_param(hda, codec_addr, nid,
                                    HDA_PARAM_FUNC_GROUP_TYPE, &response);
        if (result < 0)
            continue;

        uint8_t fg_type = response & 0xFF;
        
        if (fg_type == HDA_FG_TYPE_AFG) {
            codec->afg_nid = nid;
            kprintf("[HDA] Found Audio Function Group at node 0x%02x\n", nid);
            
            /* Get AFG sub-nodes */
            result = hda_codec_get_param(hda, codec_addr, nid,
                                        HDA_PARAM_SUB_NODE_COUNT, &response);
            if (result == 0) {
                codec->afg_start_nid = (response >> 16) & 0xFF;
                codec->afg_num_nodes = response & 0xFF;
                kprintf("[HDA] AFG has %d nodes starting at 0x%02x\n",
                       codec->afg_num_nodes, codec->afg_start_nid);
            }
            break;
        }
    }

    if (!codec->afg_nid) {
        kprintf("[HDA] No Audio Function Group found\n");
        return -1;
    }

    /* Probe all nodes in AFG */
    result = hda_codec_probe_nodes(hda, codec);
    if (result < 0) {
        kprintf("[HDA] Failed to probe nodes\n");
        return -1;
    }

    /* Build audio path */
    result = hda_codec_build_path(hda, codec);
    if (result < 0) {
        kprintf("[HDA] Failed to build audio path\n");
        return -1;
    }

    codec->initialized = 1;
    kprintf("[HDA] Codec %d initialization complete\n\n", codec_addr);

    return 0;
}

/**
 * hda_codec_dump_info - Print codec information
 */
void hda_codec_dump_info(hda_codec_t *codec)
{
    int i;

    kprintf("\n=== Codec %d Information ===\n", codec->codec_addr);
    kprintf("Vendor ID: 0x%08x\n", codec->vendor_id);
    kprintf("Revision ID: 0x%08x\n", codec->revision_id);
    kprintf("AFG Node: 0x%02x\n", codec->afg_nid);
    kprintf("DAC Node: 0x%02x\n", codec->dac_nid);
    kprintf("Output Pin: 0x%02x\n", codec->output_pin_nid);
    kprintf("\nNodes (%d total):\n", codec->num_nodes);

    for (i = 0; i < codec->num_nodes; i++) {
        hda_node_t *node = &codec->nodes[i];
        const char *type_names[] = {
            "Output", "Input", "Mixer", "Selector",
            "Pin", "Power", "VolKnob", "Beep",
            "Res8", "Res9", "ResA", "ResB",
            "ResC", "ResD", "ResE", "Vendor"
        };
        
        // Map node->type to a safe string name and also print raw type
        const char *type_str;
        if (node->type < 16) {
            type_str = type_names[node->type];
        } else {
            type_str = "Unknown";
        }

        kprintf("  [0x%02x] %s (type=0x%x), caps=0x%08x",
               node->nid, type_str, node->type, node->capabilities);
        
        if (node->num_connections > 0) {
            kprintf(", conns=%d", node->num_connections);
        }
        
        kprintf("\n");
    }
    kprintf("===========================\n\n");
}
