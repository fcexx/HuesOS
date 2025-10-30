#pragma once

#include <stdint.h>

unsigned char   inb(unsigned short port);
void    outb(unsigned short port, unsigned char data);
unsigned char   inw(unsigned short port);
void outw(unsigned short port, unsigned short data);
void insw(uint16_t port, void *addr, unsigned long count);
void outsw(uint16_t port, const void *addr, unsigned long count);
void insb(uint16_t port, void *addr, unsigned long count);
void reboot_system();
void shutdown_system();
uint32_t inportl(uint16_t _port);
uint16_t inports(uint16_t _port);
void outports(uint16_t _port, uint16_t _data);
void outportl(uint16_t _port, uint32_t _data);