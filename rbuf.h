#ifndef RBUF_H
#define RBUF_H

#include <stdint.h>
#include <stdlib.h>

#define SIZE 4096

typedef struct rbuf {
	size_t head, tail, base;
	uint32_t size, count;
	uint8_t mem[SIZE];
	uint8_t *slb;
} rbuf;

void rbufdestroy(struct rbuf *buf);
size_t capac(struct rbuf *buf);
void *memcpyrng(void *buff, const void *src, size_t cnt);
struct rbuf *rbufinit();

#endif
