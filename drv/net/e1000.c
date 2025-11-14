/* Intel E1000 (8254x) minimal driver: MMIO, reset, RX/TX rings, polling I/O */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <pci.h>
#include <mmio.h>
#include <paging.h>
#include <heap.h>
#include <e1000.h>
#include <debug.h>
#include <idt.h>
#include <pic.h>

/* Register offsets */
#define E1000_REG_CTRL		0x0000
#define E1000_REG_STATUS	0x0008
#define E1000_REG_EERD		0x0014
#define E1000_REG_CTRL_EXT	0x0018
#define E1000_REG_ICR		0x00C0
#define E1000_REG_IMS		0x00D0

#define E1000_REG_RCTL		0x0100
#define E1000_REG_TCTL		0x0400
#define E1000_REG_TIPG		0x0410

#define E1000_REG_RDBAL		0x2800
#define E1000_REG_RDBAH		0x2804
#define E1000_REG_RDLEN		0x2808
#define E1000_REG_RDH		0x2810
#define E1000_REG_RDT		0x2818

#define E1000_REG_TDBAL		0x3800
#define E1000_REG_TDBAH		0x3804
#define E1000_REG_TDLEN		0x3808
#define E1000_REG_TDH		0x3810
#define E1000_REG_TDT		0x3818

#define E1000_REG_RAL0		0x5400
#define E1000_REG_RAH0		0x5404

/* CTRL bits */
#define CTRL_FD			(1u << 0)
#define CTRL_SLU		(1u << 6)
#define CTRL_ASDE		(1u << 5)
#define CTRL_RST		(1u << 26)

/* RCTL bits */
#define RCTL_EN			(1u << 1)
#define RCTL_SBP		(1u << 2)
#define RCTL_UPE		(1u << 3)
#define RCTL_MPE		(1u << 4)
#define RCTL_LPE		(1u << 5)
#define RCTL_BAM		(1u << 15)
#define RCTL_SECRC		(1u << 26)
#define RCTL_BSIZE_MASK		(3u << 16)	/* 00b: 2048 */
#define RCTL_BSEX		(1u << 25)	/* extended sizes */

/* TCTL bits */
#define TCTL_EN			(1u << 1)
#define TCTL_PSP		(1u << 3)
#define TCTL_CT_SHIFT		4
#define TCTL_COLD_SHIFT		12

/* EERD bits */
#define EERD_START		(1u << 0)
#define EERD_DONE		(1u << 4)
#define EERD_ADDR_SHIFT		8
#define EERD_DATA_SHIFT		16

/* TX/RX descriptor structures (legacy) */
struct e1000_tx_desc {
	uint64_t addr;
	uint16_t length;
	uint8_t  cso;
	uint8_t  cmd;
	uint8_t  status;
	uint8_t  css;
	uint16_t special;
} __attribute__((packed, aligned(16)));

struct e1000_rx_desc {
	uint64_t addr;
	uint16_t length;
	uint16_t csum;
	uint8_t  status;
	uint8_t  errors;
	uint16_t special;
} __attribute__((packed, aligned(16)));

/* TX command bits */
#define TX_CMD_EOP		(1u << 0)
#define TX_CMD_IFCS		(1u << 1)
#define TX_CMD_RS		(1u << 3)
#define TX_STATUS_DD		(1u << 0)

/* RX status bits */
#define RX_STATUS_DD		(1u << 0)
#define RX_STATUS_EOP		(1u << 1)

#define RX_RING_SIZE		128
#define TX_RING_SIZE		64
#define RX_BUF_SIZE		2048
#define TX_BUF_SIZE		2048

static volatile uint8_t* reg_base = 0;
static struct e1000_rx_desc* rx_ring = 0;
static struct e1000_tx_desc* tx_ring = 0;
static uint8_t* rx_buffers[RX_RING_SIZE];
static uint8_t* tx_buffers[TX_RING_SIZE];
static uint32_t rx_tail = 0;
static uint32_t tx_tail = 0;
static uint8_t g_mac[6] = {0};
static int nic_ready = 0;
static uint8_t nic_irq_line = 0xFF;
static uint64_t g_e1000_rx_packets = 0;

static inline uint32_t reg_read(uint32_t off) {
	return mmio_read32(reg_base + off);
}
static inline void reg_write(uint32_t off, uint32_t val) {
	mmio_write32((void*)(reg_base + off), val);
}

static void e1000_irq(cpu_registers_t* regs) {
	(void)regs;
	/* Reading ICR acknowledges/clears pending causes */
	uint32_t icr = reg_read(E1000_REG_ICR);
	if (!icr) return;
	/* Do not drain RX here: let upper layers poll and process frames */
}

static uint16_t e1000_eerd_read_word(uint16_t idx) {
	uint32_t v = (uint32_t)idx << EERD_ADDR_SHIFT;
	reg_write(E1000_REG_EERD, v | EERD_START);
	for (int i = 0; i < 100000; i++) {
		uint32_t r = reg_read(E1000_REG_EERD);
		if (r & EERD_DONE) {
			return (uint16_t)((r >> EERD_DATA_SHIFT) & 0xFFFF);
		}
	}
	return 0xFFFF;
}

static void e1000_read_mac_from_eeprom(uint8_t mac[6]) {
	uint16_t w0 = e1000_eerd_read_word(0);
	uint16_t w1 = e1000_eerd_read_word(1);
	uint16_t w2 = e1000_eerd_read_word(2);
	mac[0] = (uint8_t)(w0 & 0xFF);
	mac[1] = (uint8_t)(w0 >> 8);
	mac[2] = (uint8_t)(w1 & 0xFF);
	mac[3] = (uint8_t)(w1 >> 8);
	mac[4] = (uint8_t)(w2 & 0xFF);
	mac[5] = (uint8_t)(w2 >> 8);
}

static void e1000_program_mac(const uint8_t mac[6]) {
	uint32_t ral = (uint32_t)mac[0] |
	               ((uint32_t)mac[1] << 8) |
	               ((uint32_t)mac[2] << 16) |
	               ((uint32_t)mac[3] << 24);
	uint32_t rah = ((uint32_t)mac[4]) | ((uint32_t)mac[5] << 8);
	rah |= (1u << 31); /* AV: valid */
	reg_write(E1000_REG_RAL0, ral);
	reg_write(E1000_REG_RAH0, rah);
}

static int e1000_setup_rx(void) {
	rx_ring = (struct e1000_rx_desc*)kmalloc(sizeof(struct e1000_rx_desc) * RX_RING_SIZE);
	if (!rx_ring) return -1;
	memset(rx_ring, 0, sizeof(struct e1000_rx_desc) * RX_RING_SIZE);
	for (uint32_t i = 0; i < RX_RING_SIZE; i++) {
		rx_buffers[i] = (uint8_t*)kmalloc(RX_BUF_SIZE);
		if (!rx_buffers[i]) return -2;
		memset(rx_buffers[i], 0, RX_BUF_SIZE);
		uint64_t phys = paging_virt_to_phys((uint64_t)rx_buffers[i]);
		rx_ring[i].addr = phys;
		rx_ring[i].status = 0;
	}
	uint64_t ring_phys = paging_virt_to_phys((uint64_t)rx_ring);
	reg_write(E1000_REG_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFu));
	reg_write(E1000_REG_RDBAH, (uint32_t)(ring_phys >> 32));
	reg_write(E1000_REG_RDLEN, RX_RING_SIZE * sizeof(struct e1000_rx_desc));
	reg_write(E1000_REG_RDH, 0);
	rx_tail = RX_RING_SIZE - 1;
	reg_write(E1000_REG_RDT, rx_tail);
	qemu_debug_printf("e1000: setup_rx ring=%p phys=0x%llx rdl=%u rdt=%u bufsize=%u\n",
	        (void*)rx_ring, (unsigned long long)ring_phys, RX_RING_SIZE, rx_tail, (unsigned)RX_BUF_SIZE);

	/* Configure RCTL: 2KB buffers, strip CRC, accept broadcast, enable
	 * Also enable unicast and multicast promiscuous for testing RX path.
	 */
	uint32_t rctl = reg_read(E1000_REG_RCTL);
	rctl &= ~(RCTL_BSIZE_MASK | RCTL_BSEX);
	rctl |= RCTL_EN | RCTL_SECRC | RCTL_BAM | RCTL_UPE | RCTL_MPE;
	reg_write(E1000_REG_RCTL, rctl);

	/* Clear Multicast Table Array (MTA) to default zeroes */
	for (uint32_t off = 0x5200; off <= 0x527C; off += 4) {
		reg_write(off, 0);
	}
	return 0;
}

static int e1000_setup_tx(void) {
	tx_ring = (struct e1000_tx_desc*)kmalloc(sizeof(struct e1000_tx_desc) * TX_RING_SIZE);
	if (!tx_ring) return -1;
	memset(tx_ring, 0, sizeof(struct e1000_tx_desc) * TX_RING_SIZE);
	for (uint32_t i = 0; i < TX_RING_SIZE; i++) {
		tx_buffers[i] = (uint8_t*)kmalloc(TX_BUF_SIZE);
		if (!tx_buffers[i]) return -2;
		memset(tx_buffers[i], 0, TX_BUF_SIZE);
		uint64_t phys = paging_virt_to_phys((uint64_t)tx_buffers[i]);
		tx_ring[i].addr = phys;
		tx_ring[i].status = TX_STATUS_DD; /* mark free */
	}
	uint64_t ring_phys = paging_virt_to_phys((uint64_t)tx_ring);
	reg_write(E1000_REG_TDBAL, (uint32_t)(ring_phys & 0xFFFFFFFFu));
	reg_write(E1000_REG_TDBAH, (uint32_t)(ring_phys >> 32));
	reg_write(E1000_REG_TDLEN, TX_RING_SIZE * sizeof(struct e1000_tx_desc));
	reg_write(E1000_REG_TDH, 0);
	tx_tail = 0;
	reg_write(E1000_REG_TDT, tx_tail);

	/* Configure TCTL */
	uint32_t tctl = reg_read(E1000_REG_TCTL);
	tctl |= TCTL_EN | TCTL_PSP;
	tctl |= (0x0F << TCTL_CT_SHIFT);
	tctl |= (0x40 << TCTL_COLD_SHIFT);
	reg_write(E1000_REG_TCTL, tctl);

	/* TIPG typical value */
	reg_write(E1000_REG_TIPG, 0x0060200A);
	return 0;
}

static int e1000_reset(void) {
	/* Disable RX/TX */
	uint32_t rctl = reg_read(E1000_REG_RCTL);
	reg_write(E1000_REG_RCTL, rctl & ~RCTL_EN);
	uint32_t tctl = reg_read(E1000_REG_TCTL);
	reg_write(E1000_REG_TCTL, tctl & ~TCTL_EN);

	/* Global reset */
	uint32_t ctrl = reg_read(E1000_REG_CTRL);
	reg_write(E1000_REG_CTRL, ctrl | CTRL_RST);
	/* flushing read */
	(void)reg_read(E1000_REG_CTRL);

	/* Simple delay loop */
	for (volatile int i = 0; i < 100000; i++) { }
	return 0;
}

int e1000_init(void) {
	pci_device_t* devs = pci_get_devices();
	int n = pci_get_device_count();
	pci_device_t* nic = 0;
	for (int i = 0; i < n; i++) {
		if (devs[i].class_code == 0x02 && devs[i].subclass == 0x00) {
			/* Ethernet Controller */
			nic = &devs[i];
			break;
		}
	}
	if (!nic) {
		qemu_debug_printf("e1000: no ethernet controller found\n");
		return -1;
	}

	/* Enable PCI Memory Space and Bus Master for DMA; ensure INTx not disabled */
	uint32_t cmd = pci_config_read_dword(nic->bus, nic->device, nic->function, 0x04);
	cmd |= (1u << 1) | (1u << 2); /* Memory Space Enable, Bus Master Enable */
	cmd &= ~(1u << 10);           /* Clear Interrupt Disable (INTx enable) */
	pci_config_write_dword(nic->bus, nic->device, nic->function, 0x04, cmd);

	/* BAR0 MMIO */
	uint32_t bar0 = nic->bar[0];
	if ((bar0 & 0x1) != 0) {
		qemu_debug_printf("e1000: BAR0 is I/O space, unsupported\n");
		return -2;
	}
	uint64_t mmio_phys = (uint64_t)(bar0 & ~0xFu);
	/* Map at least 128 KiB of registers; our mapper uses 2 MiB granularity */
	reg_base = (volatile uint8_t*)ioremap(mmio_phys, 128 * 1024);
	if (!reg_base) {
		qemu_debug_printf("e1000: ioremap failed\n");
		return -3;
	}

	/* Reset and basic link setup */
	e1000_reset();
	uint32_t ctrl = reg_read(E1000_REG_CTRL);
	ctrl |= CTRL_SLU | CTRL_ASDE | CTRL_FD;
	reg_write(E1000_REG_CTRL, ctrl);

	/* Read MAC from EEPROM and program RAL/RAH */
	e1000_read_mac_from_eeprom(g_mac);
	e1000_program_mac(g_mac);
	qemu_debug_printf("e1000: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);

	/* Setup RX/TX */
	if (e1000_setup_rx() != 0) return -4;
	if (e1000_setup_tx() != 0) return -5;

	/* Stay in pure polling mode for stability in this environment */
	nic_irq_line = 0xFF;
	qemu_debug_printf("e1000: IRQ disabled, polling mode\n");

	/* Do not enable device interrupts (polling only) */
	(void)reg_read(E1000_REG_ICR);
	reg_write(E1000_REG_IMS, 0x00000000u);

	nic_ready = 1;
	return 0;
}

int e1000_get_mac(uint8_t mac[6]) {
	if (!nic_ready) return -1;
	memcpy(mac, g_mac, 6);
	return 0;
}

int e1000_send(const void* data, size_t length) {
	if (!nic_ready) return -1;
	if (length == 0 || length > TX_BUF_SIZE) return -2;

	uint32_t idx = tx_tail;
	struct e1000_tx_desc* d = &tx_ring[idx];
	/* Wait for descriptor availability */
	for (int spins = 0; spins < 100000; spins++) {
		if (d->status & TX_STATUS_DD) break;
	}
	if (!(d->status & TX_STATUS_DD)) {
		return -3; /* still busy */
	}
	d->status = 0;
	memcpy(tx_buffers[idx], data, length);
	d->length = (uint16_t)length;
	d->cso = 0;
	d->cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
	mmio_wmb();

	/* Advance tail */
	idx = (idx + 1) % TX_RING_SIZE;
	tx_tail = idx;
	reg_write(E1000_REG_TDT, idx);

	/* Optionally wait for completion briefly */
	for (int spins = 0; spins < 100000; spins++) {
		if (d->status & TX_STATUS_DD) return 0;
	}
	return 0; /* consider success */
}

int e1000_poll(uint8_t* buf, size_t bufsize, size_t* out_len) {
	if (!nic_ready) return -1;
	uint32_t idx = (rx_tail + 1) % RX_RING_SIZE;
	struct e1000_rx_desc* d = &rx_ring[idx];
	if (!(d->status & RX_STATUS_DD)) {
		return 0; /* no packet */
	}
	if (!(d->status & RX_STATUS_EOP)) {
		/* We use one-buffer-per-packet; unexpected */
		d->status = 0;
		rx_tail = idx;
		reg_write(E1000_REG_RDT, rx_tail);
		return 0;
	}
	size_t len = d->length;
	if (out_len) *out_len = len;
	if (len > bufsize) {
		return -1;
	}
	/* Diagnostic: log descriptor info */
	g_e1000_rx_packets++;
	qemu_debug_printf("e1000: rx idx=%u len=%u status=0x%02x errors=0x%02x tail_before=%u\n",
	        idx, (unsigned)len, (unsigned)d->status, (unsigned)d->errors, (unsigned)rx_tail);
	memcpy(buf, rx_buffers[idx], len);
	qemu_debug_printf("e1000: copied %u bytes from rx_buf[%u] total_pkts=%llu\n", (unsigned)len, idx, (unsigned long long)g_e1000_rx_packets);

	/* Recycle descriptor */
	d->status = 0;
	mmio_wmb();
	rx_tail = idx;
	reg_write(E1000_REG_RDT, rx_tail);
	qemu_debug_printf("e1000: rdt set -> %u\n", (unsigned)rx_tail);
	return 1;
}


