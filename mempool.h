#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#define POOL_SIZE 100
#define BLOCK_SIZE 1024
//#define BLOCK_SIZE 8192
//#define BLOCK_SIZE 16384
typedef struct mempool {
	uint8_t *slab;
	uint8_t *blocks[POOL_SIZE];
	uint64_t page;
} mempool;

struct mempool *mempoolinit();
uint8_t *palloc(struct mempool *mp);
void pfree(struct mempool *mp, uint8_t *buf);
void mpdestroy(struct mempool *mp);

#endif
