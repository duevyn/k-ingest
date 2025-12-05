#include "mempool.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

struct mempool *mempoolinit(size_t blksz)
{
	struct mempool *mp = (struct mempool *)malloc(sizeof(*mp));
	mp->slab = (uint8_t *)mmap(NULL, blksz * POOL_SIZE,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	mp->page = -1;
	for (int i = 0; i < POOL_SIZE; i++) {
		mp->blocks[++mp->page] = mp->slab + (i * BLOCK_SIZE);
	}
	fprintf(stderr, "inital page: %ld, %p -> %p\n", mp->page,
		mp->blocks[99], mp->blocks[99]);
	return mp;
}

void mpdestroy(void *mpool)
{
	struct mempool *mp = mpool;
	if (!mp)
		return;
	if (mp->slab)
		munmap(mp->slab, BLOCK_SIZE * POOL_SIZE);
	free(mp);
	fprintf(stderr, "Destroyed mempool\n");
}

void *palloc(void *mpool)
{
	struct mempool *mp = mpool;
	if (!mp || !mp->page || mp->page < 0)
		return (void *)0;
	uint8_t *buf = mp->blocks[mp->page--];
	bzero(buf, BLOCK_SIZE);
	fprintf(stderr, "cxtmalloc addr: %p\n", buf);
	return buf;
}

void pfree(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	if (!mp || !mp->page || mp->page >= POOL_SIZE - 1)
		return;
	//fprintf(stderr, "cxtfree: fd %d, addr: %p\n", cxt->fd, cxt->buf);
	mp->blocks[++mp->page] = buf;
	//cxt->buf = NULL;
}
