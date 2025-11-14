#include "../inc/pci.h"
#include <stdint.h>
#include <stddef.h>
extern void kprintf(const char *fmt, ...);
#include "../inc/serial.h"

/*
 * pci.c - basic PCI enumeration using CF8/CFC mechanism
 *
 * Simple, non-PCIe, allocation-free enumerator meant for early init.
 */

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static pci_device_t pci_devices[256];
static int pci_device_count = 0;

static inline uint32_t pci_make_address(uint8_t bus, uint8_t device,
                                       uint8_t function, uint8_t offset)
{
    return (uint32_t)((1U << 31) |
                      ((uint32_t)bus << 16) |
                      ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) |
                      (offset & 0xFC));
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t device,
                               uint8_t function, uint8_t offset)
{
    uint32_t addr = pci_make_address(bus, device, function, offset);
    outportl(PCI_CONFIG_ADDRESS, addr);
    return inportl(PCI_CONFIG_DATA);
}

void pci_config_write_dword(uint8_t bus, uint8_t device,
                           uint8_t function, uint8_t offset, uint32_t value)
{
    uint32_t addr = pci_make_address(bus, device, function, offset);
    outportl(PCI_CONFIG_ADDRESS, addr);
    outportl(PCI_CONFIG_DATA, value);
}

/*
 * pci_init - enumerate all buses/devices/functions and record descriptors
 */
void pci_init(void)
{
    pci_device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            /* probe function 0 */
            uint32_t id = pci_config_read_dword((uint8_t)bus, device, 0, 0x00);
            uint16_t vendor = (uint16_t)(id & 0xFFFF);
            if (vendor == 0xFFFF)
                continue;

            uint8_t header_type = (uint8_t)((pci_config_read_dword((uint8_t)bus, device, 0, 0x0C) >> 16) & 0xFF);
            int multifunction = (header_type & 0x80) != 0;

            uint8_t max_functions = multifunction ? 8 : 1;

            for (uint8_t function = 0; function < max_functions; function++) {
                uint32_t dword0 = pci_config_read_dword((uint8_t)bus, device, function, 0x00);
                uint16_t vend = (uint16_t)(dword0 & 0xFFFF);
                if (vend == 0xFFFF)
                    continue;

                pci_device_t *pdev = &pci_devices[pci_device_count];
                pdev->bus = (uint8_t)bus;
                pdev->device = device;
                pdev->function = function;
                pdev->vendor_id = vend;
                pdev->device_id = (uint16_t)((dword0 >> 16) & 0xFFFF);

                uint32_t dword2 = pci_config_read_dword((uint8_t)bus, device, function, 0x08);
                pdev->class_code = (uint8_t)((dword2 >> 24) & 0xFF);
                pdev->subclass = (uint8_t)((dword2 >> 16) & 0xFF);
                pdev->prog_if = (uint8_t)((dword2 >> 8) & 0xFF);

                uint32_t dword3 = pci_config_read_dword((uint8_t)bus, device, function, 0x0C);
                pdev->header_type = (uint8_t)((dword3 >> 16) & 0xFF);

                uint32_t irq_dword = pci_config_read_dword((uint8_t)bus, device, function, 0x3C);
                pdev->irq = (uint8_t)(irq_dword & 0xFF);

                /* read raw BAR values */
                for (int i = 0; i < 6; i++)
                    pdev->bar[i] = pci_config_read_dword((uint8_t)bus, device, function, 0x10 + i * 4);

                pci_device_count++;
                if (pci_device_count >= (int)(sizeof(pci_devices) / sizeof(pci_devices[0])))
                    return;
            }
        }
    }
}

int pci_get_device_count(void)
{
    return pci_device_count;
}

pci_device_t *pci_get_devices(void)
{
    return pci_devices;
}

pci_device_t *pci_find_device_by_id(uint16_t vendor_id, uint16_t device_id)
{
    int i;

    for (i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id)
            return &pci_devices[i];
    }

    return NULL;
}

/*
 * pci_dump_devices - print enumerated PCI devices to console
 *
 * Output format aims to be compact and similar to kernel "lspci" style:
 *  BB:DD.F vendor:device class/subclass prog_if hdr irq
 */
void pci_dump_devices(void)
{
    int i, b;
    pci_device_t *devs = pci_get_devices();

    for (i = 0; i < pci_get_device_count(); i++) {
        pci_device_t *d = &devs[i];

        /*
         * Format: "PCI <bus>.<device>.<function>: ..."
         * Example: "PCI 0.0.0: vendor=0x8086 device=0x1234 class=0x02/00 prog_if=0x00 hdr=0x00 irq=11"
         */
        if (d->irq == 0) {
            kprintf("PCI %u.%u.%u: vendor=%04x device=%04x class=%02x/%02x prog_if=%02x hdr=%02x irq=N/A\n",
                    d->bus, d->device, d->function,
                    d->vendor_id, d->device_id,
                    d->class_code, d->subclass, d->prog_if,
                    d->header_type);
        } else {
        kprintf("PCI %u.%u.%u: vendor=%04x device=%04x class=%02x/%02x prog_if=%02x hdr=%02x irq=%u\n",
                d->bus, d->device, d->function,
                d->vendor_id, d->device_id,
                d->class_code, d->subclass, d->prog_if,
                d->header_type, d->irq);
        }
    }
}


