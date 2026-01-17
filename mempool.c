#include "mempool.h"
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#define SYS_PAGE 4096

struct mempool *mmp_init(size_t blksz, size_t cxsz, size_t cxcnt)
{
	size_t mpstructsz = sizeof(struct mempool);
	size_t cxptrsz = cxcnt * sizeof(uint8_t *);
	size_t cxdatasz = cxcnt * cxsz;
	size_t blkptrsz = POOL_SIZE * sizeof(uint8_t *);
	size_t blkdatasz = POOL_SIZE * blksz;
	size_t totsz = cxptrsz + cxdatasz + blkptrsz + blkdatasz + mpstructsz;

	struct mempool *mp =
		(struct mempool *)mmap(NULL, totsz, PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mp->sz = totsz;

	uint8_t *itr = ((uint8_t *)mp) + mpstructsz;
	mp->cxblks = (uint8_t **)itr;

	itr += cxptrsz;
	mp->blocks = (uint8_t **)itr;

	itr += blkptrsz;
	mp->cxslb = itr;

	itr += cxdatasz;
	mp->slab = itr;

	mp->cxind = -1;
	for (int i = 0; i < cxcnt; i++)
		mp->cxblks[++mp->cxind] = mp->cxslb + (i * cxsz);

	mp->page = -1;
	for (int i = 0; i < POOL_SIZE; i++)
		mp->blocks[++mp->page] = mp->slab + (i * blksz);
	fprintf(stderr,
		"mp %p sizeof %lu\ncxblk[0] %p, [1] %p, [99] %p slb %p\nblk[0] %p, [1] %p, [99] %p, slb %p\n",

		mp, sizeof(*mp), mp->cxblks[0], mp->cxblks[1],
		mp->cxblks[cxcnt - 1], mp->cxslb, mp->blocks[0], mp->blocks[1],
		mp->blocks[POOL_SIZE - 1], mp->slab);
	fprintf(stderr, "cxblks %p %p, blocks %p %p\n", &mp->cxblks, mp->cxblks,
		&mp->blocks, mp->blocks);
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
