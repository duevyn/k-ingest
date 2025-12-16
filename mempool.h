#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>

#define POOL_SIZE 200
#define BLOCK_SIZE 1024
//#define BLOCK_SIZE 8192
//#define BLOCK_SIZE 16384
typedef struct mempool {
	uint8_t *slab;
	uint8_t *blocks[POOL_SIZE];
	uint32_t page;
	uint32_t sz;

	uint8_t *cxslb;
	uint8_t **cxblks;
	uint32_t cxind;
} mempool;

//uint8_t *palloc(struct mempool *mp);
struct mempool *mmp_init(size_t blksz, size_t cxsz, size_t cxcnt);
void *mmp_mallocblk(void *mpool);
void *mmp_malloccx(void *mpool);
void mmp_freeblk(void *mpool, void *buf);
void mmp_freecx(void *mpool, void *buf);
void mmp_destroy(void *mpool);
void *mmp_pshblk(void *mmpool, void *buf);
void mmp_popblk(void *mmpool, void *buf);
//void mpdestroy(struct mempool *mp);

#endif
