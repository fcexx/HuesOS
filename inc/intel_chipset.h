#pragma once

#include <stdint.h>
#include <stddef.h>

#define INTEL_VENDOR_ID          0x8086

/* LPC Controller Device IDs */
#define INTEL_LPC_ICH0           0x2410
#define INTEL_LPC_ICH1           0x2420
#define INTEL_LPC_ICH2           0x2440
#define INTEL_LPC_ICH3           0x2480
#define INTEL_LPC_ICH4           0x24C0
#define INTEL_LPC_ICH5           0x24D0
#define INTEL_LPC_ICH6           0x2660
#define INTEL_LPC_ICH7           0x27B8
#define INTEL_LPC_ICH8           0x2810
#define INTEL_LPC_ICH9           0x2918
#define INTEL_LPC_ICH10          0x3A18
#define INTEL_LPC_PCH            0x1C4C
#define INTEL_LPC_Z97            0x8CC4

/* Chipset Registers */
#define INTEL_LPC_ACPI_CNTL      0x44
#define INTEL_LPC_PM1_CNT        0x04
#define INTEL_LPC_SMB_BASE       0x90

/* Power Management */
#define INTEL_PM_ACPI_ENABLE     0x0001
#define INTEL_PM_SUSPEND_ENABLE  0x2000

/* Feature Flags */
#define INTEL_FEATURE_ACPI       0x01
#define INTEL_FEATURE_USB        0x02
#define INTEL_FEATURE_SATA       0x04
#define INTEL_FEATURE_ETHERNET   0x08
#define INTEL_FEATURE_POWER_MGMT 0x10
#define INTEL_FEATURE_SMBUS      0x20
#define INTEL_FEATURE_HD_AUDIO   0x40

typedef struct intel_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint32_t bar[6];
    uint8_t irq;
    const char* name;
} intel_device_t;

typedef struct intel_chipset {
    intel_device_t* lpc_controller;
    intel_device_t* usb_controller;
    intel_device_t* sata_controller;
    intel_device_t* ethernet_controller;
    intel_device_t* graphics_controller;
    intel_device_t* audio_controller;

    const char* chipset_name;
    uint16_t supported_features;
    uint8_t usb_ports;
    uint8_t sata_ports;
} intel_chipset_t;

/* Core Functions */
void intel_chipset_init(void);
int intel_detect_chipset(void);
void intel_chipset_reset(void);
void intel_print_chipset_info(void);

/* Utility Functions */
uint32_t intel_pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void intel_pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);

/* Status */
int intel_is_detected(void);