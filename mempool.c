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
	mp->slab = (uint8_t *)mmap(NULL, blksz * POOL_SIZE + cxcnt * cxsz,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	mp->cxslb = mp->slab + (blksz * POOL_SIZE);

	mp->page = -1;
	for (int i = 0; i < POOL_SIZE; i++)
		mp->blocks[++mp->page] = mp->slab + (i * BLOCK_SIZE);

	mp->cxind = -1;
	mp->cxblks = malloc(sizeof(size_t) * cxcnt);
	for (int i = 0; i < cxcnt; i++)
		mp->cxblks[++mp->cxind] = mp->cxslb + (i * cxsz);

	fprintf(stderr,
		"\nmempool: %d, %p -> %p, cxsz %lu cxcnt %lu cxslb %p %p\n\n",
		mp->page, mp->blocks[0], mp->blocks[99], cxsz, cxcnt,
		mp->cxblks, mp->cxblks[cxcnt - 1]);

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
	if (!mp || mp->page <= 0) {
		fprintf(stderr, "WARN: invalid  malloblk. page %d\n", mp->page);
		return (void *)0;
	}

	uint8_t *buf = mp->blocks[mp->page--];
	memset(buf, 0, BLOCK_SIZE);
	//fprintf(stderr, "mmp_mallocblk addr: %p\n", buf);
	fprintf(stderr, "malloblk: prv %p, cur: %p\n", buf,
		mp->blocks[mp->page]);
	return buf;
}

void *mmp_malloccx(void *mpool)
{
	struct mempool *mp = mpool;
	if (!mp || mp->cxind <= 0) {
		fprintf(stderr, "WARN: invalid  malloccx. cxind %d\n",
			mp->cxind);
		return (void *)0;
	}

	uint8_t *buf = mp->cxblks[mp->cxind--];
	fprintf(stderr, "malloccx: prv %p, cur: %p\n", buf,
		mp->cxblks[mp->cxind]);
	return buf;
}

void mmp_freeblk(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	if (!mp || !mp->page || mp->page >= POOL_SIZE - 1)
		return;
	mp->blocks[++mp->page] = buf;
	fprintf(stderr, "freeblk: prv %p, cur: %p\n", mp->blocks[mp->page - 1],
		buf);
}

void mmp_freecx(void *mpool, void *buf)
{
	struct mempool *mp = mpool;
	if (!mp || !mp->cxind)
		return;
	//fprintf(stderr, "cxtfree: fd %d, addr: %p\n", cxt->fd, cxt->buf);
	uint8_t *prv = mp->cxblks[mp->cxind];
	mp->cxblks[++mp->cxind] = buf;
	fprintf(stderr, "freecx: prv %p, cur: %p\n", mp->cxblks[mp->cxind - 1],
		buf);
}
