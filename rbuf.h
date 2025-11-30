#ifndef RBUF_H
#define RBUF_H

#include <stdint.h>
#include <stdlib.h>

#define SIZE 100

typedef struct rbuf {
	size_t head, tail, base;
	uint32_t size, count;
	uint8_t mem[SIZE];
} rbuf;

void put();
void get(int bytes);
void destroy(struct rbuf *buf);
size_t capac(struct rbuf *buf);
void readfd_nbl(struct rbuf *buf, int fd, uint16_t count);

#endif
