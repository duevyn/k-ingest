#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "rbuf.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#define POOL_SIZE 100
#define MAGIC 0xDEADBEEF

#define BLOCK_SIZE 1024
//#define BLOCK_SIZE 8192
//#define BLOCK_SIZE 16384

typedef struct mempool {
	uint8_t *slab;
	uint8_t *blocks[POOL_SIZE];
	uint64_t page;
} mempool;
typedef struct packethd {
	uint32_t magic;
	uint8_t type;
	uint16_t byts;

} packethd;

typedef struct packet {
	struct packethd head;
	uint8_t *data;
} packet;

typedef struct context {
	int fd;
	uint8_t *buf;
	enum {
		EAGAIN,
		READING_HEADER,
		READING_DATA,
		READING,
	} state;

	uint16_t base;
	uint16_t offwr;
	uint16_t offrd;
	uint16_t pnd;

} context;

struct context *cxtinit(int fd);
int processcxt(struct context *cxt, struct rbuf *buf);
int processhd(struct context *cxt);

struct mempool *mempoolinit();
uint8_t *palloc(struct mempool *mp);
uint8_t *cxtmalloc(struct mempool *mp, struct context *cxt);
void mpdestroy(struct mempool *mp);
void hndlev(struct mempool *mp, struct rbuf *buf, struct context *cxt);

#endif
