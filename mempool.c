#include "mempool.h"
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#define SYS_PAGE 4096

struct mempool *mmp_init(size_t blksz, size_t cxsz, size_t cxcnt)
{
	struct mempool *mp = (struct mempool *)malloc(sizeof(*mp));
	mp->sz = blksz * POOL_SIZE + cxcnt * cxsz;
	mp->cxslb = (uint8_t *)mmap(NULL, blksz * POOL_SIZE + cxcnt * cxsz,
				    PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	mp->slab = mp->cxslb + (cxcnt * cxsz);
	//mp->svind = POOL_SIZE - 1;

	mp->cxind = -1;
	mp->cxblks = malloc(sizeof(size_t) * cxcnt);
	for (int i = 0; i < cxcnt; i++)
		mp->cxblks[++mp->cxind] = mp->cxslb + (i * cxsz);

	mp->page = -1;
	for (int i = 0; i < POOL_SIZE; i++)
		mp->blocks[++mp->page] = mp->slab + (i * blksz);

	fprintf(stderr,
		"blk[0] %p, [1] %p, [99] %p, slb %p\ncxblk[0] %p, [1] %p, [99] %p slb %p\n",
		mp->blocks[0], mp->blocks[1], mp->blocks[POOL_SIZE - 1],
		mp->slab, mp->cxblks[0], mp->cxblks[1],
		mp->cxblks[MAX_CONN - 1], mp->cxslb);
	return mp;
}

void mmp_destroy(void *mpool)
{
	struct mempool *mp = mpool;
	if (!mp)
		return;
	if (mp->slab)
		munmap(mp->slab, mp->sz);
	fprintf(stderr, "Destroyed mempool\n");
}

void *mmp_mallocblk(void *mpool)
{
	struct mempool *mp = mpool;
	if (!mp || mp->page < 0) {
		fprintf(stderr, "WARN: invalid  malloblk. page %d -- ",
			mp->page);
		return (void *)NULL;
	}

	return mp->blocks[mp->page--];
}

void *mmp_malloccx(void *mpool)
{
	struct mempool *mp = mpool;
	if (!mp || mp->cxind < 0) {
		fprintf(stderr, "WARN: invalid  malloccx. cxind %d\n",
			mp->cxind);
		return (void *)NULL;
	}

	uint8_t *buf = mp->cxblks[mp->cxind--];
	//fprintf(stderr, "malloccx: prv %p, cur: %p\n", buf,
	//	mp->cxblks[mp->cxind]);
	return buf;
}

void mmp_freeblk(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	if (!mp)
		//if (!mp || !mp->page || mp->page >= POOL_SIZE - 1)
		return;
	mp->blocks[++mp->page] = buf;
	//fprintf(stderr, "freeblk: prv %p, cur: %p -- ",
	//	mp->blocks[mp->page - 1], buf);
}

void mmp_freecx(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	//if (!mp || !mp->cxind)
	if (!mp)
		return;
	//fprintf(stderr, "cxtfree: fd %d, addr: %p\n", cxt->fd, cxt->buf);
	//uint8_t *prv = mp->cxblks[mp->cxind];
	mp->cxblks[++mp->cxind] = buf;
}

/*
void *mmp_pshblk(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	if (!mp)
		return (void *)NULL;
	mp->svblks[mp->svind--] = buf;
	return buf;
}
void mmp_popblk(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	if (!mp)
		return;
	mp->svblks[++mp->svind] = buf;
}
*/
