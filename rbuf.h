#ifndef RBUF_H
#define RBUF_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

//#define SIZE 4096
//#define SIZE 65536
//#define SIZE 32768
//#define SIZE 131072
//#define SIZE 262144
//#define SIZE 524288
//#define SIZE 1048576
#define SIZE 5262144
//#define SIZE 100

typedef struct rbuf {
	size_t hd, tl, bs;
	uint32_t sz, cnt;
	uint8_t *slb;
} rbuf;

void rbf_destroy(struct rbuf *buf);
bool rbf_isempty(struct rbuf *buf);
bool rbf_isfull(struct rbuf *buf);
size_t rbf_capac(struct rbuf *buf);
void *memcprng(void *buff, const void *src, size_t cnt);
struct rbuf *rbufinit();
void rbf_rdfr(rbuf *buf, void *dest, size_t n);
uint8_t *rbf_unwr(struct rbuf *bf, void *dest, size_t n);
size_t rbf_nfrmwrp(struct rbuf *bf, bool wr);

#endif
