#ifndef RBUF_H
#define RBUF_H

#include <stdint.h>
#include <stdlib.h>

//#define SIZE 4096
#define SIZE 100

typedef struct rbuf {
	size_t hd, tl, bs;
	uint32_t sz, cnt;
	uint8_t *slb;
} rbuf;

void rbufdestroy(struct rbuf *buf);
size_t rbfcapac(struct rbuf *buf);
void *memcprng(void *buff, const void *src, size_t cnt);
struct rbuf *rbufinit();
void rbf_rdfr(rbuf *buf, uint8_t *dest, size_t n);

#endif
