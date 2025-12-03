#include "mempool.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

struct mempool *mempoolinit()
{
	struct mempool *mp = (struct mempool *)malloc(sizeof(*mp));
	mp->slab = (uint8_t *)mmap(NULL, BLOCK_SIZE * POOL_SIZE,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	mp->page = -1;
	fprintf(stderr, "inital page: %ld, %p\n", mp->page, mp->slab);
	for (int i = 0; i < POOL_SIZE; i++) {
		mp->blocks[++mp->page] = mp->slab + (i * BLOCK_SIZE);
	}
	fprintf(stderr, "inital page: %ld, %p\n", mp->page, mp->blocks[99]);
	return mp;
}

uint8_t *cxtmalloc(struct mempool *mp, struct context *cxt)
{
	if (mp->page < 0)
		return NULL;
	cxt->buf = mp->blocks[mp->page--];
	fprintf(stderr, "cxtmalloc: fd %d, addr: %p\n", cxt->fd, cxt->buf);
	return cxt->buf;
}

void cxtfree(struct mempool *mp, struct context *cxt)
{
	if (mp->page >= POOL_SIZE - 1)
		return;
	fprintf(stderr, "cxtfree: fd %d, addr: %p\n", cxt->fd, cxt->buf);
	mp->blocks[++mp->page] = cxt->buf;
	cxt->buf = NULL;
}

void mpdestroy(struct mempool *mp)
{
	if (!mp)
		return;
	if (mp->slab)
		munmap(mp->slab, BLOCK_SIZE * POOL_SIZE);
	free(mp);
	fprintf(stderr, "Destroyed mempool\n");
}

int valdpkt(struct context *cxt)
{
	if (cxt->pnd < sizeof(packethd)) {
		fprintf(stderr, "ALERT processcxt: %u bytes rd\n", cxt->pnd);
		return 0;
	}

	struct packethd pckhd;
	memcpy(&pckhd, cxt->buf, sizeof(packethd));

	//TODO: we need to close the socket if it fails magic. but we want all
	// mem management done in hndlev
	if (pckhd.magic != MAGIC) {
		fprintf(stderr, "ERROR: processcxt inval magic\n");
		return -1;
	}

	if ((pckhd.byts + sizeof(packethd)) > cxt->pnd) {
		fprintf(stderr,
			"ALERT processcxt:byts avail %u,  byts needed %lu\n",
			cxt->pnd, pckhd.byts + sizeof(packethd));
		return 0;
	}

	int tot = pckhd.byts + sizeof(packethd);
	fprintf(stderr, "valdpkt: %d byts\n", tot);
	return tot;
}

void helloworld(struct context *cx, int byts)
{
	size_t strsz = byts - sizeof(packethd) + 1;
	char str[strsz];
	bzero(str, strsz);
	memcpy(str, cx->buf + cx->offrd + sizeof(packethd), strsz - 1);

	fprintf(stderr, "processcxt found string\n%s\n", str);
}

int processcxt(struct context *cx, struct rbuf *buf)
{
	//TODO: if data remains, do we shift it now.
	// if not, then we might have to just because we dont have contiguous
	// mem left -- unless we wrap around
	//
	// if we do, logic is simplified, but we pay cost of mem move
	//
	// start with automatic shifting and upgrade if necessary. I doubt the
	// complexity is worth it
	int totbyts, valdbyts;
	totbyts = valdbyts = 0;

	for (;;) {
		if ((valdbyts = valdpkt(cx)) <= 0) {
			fprintf(stderr, "processcxt: incmp msg. %d of \n",
				cx->pnd);
			return valdbyts;
		}

		fprintf(stderr, "\n*******************Processing Context\n\n");
		helloworld(cx, valdbyts);

		memcpyrbuf(buf, cx->buf + cx->offrd, valdbyts);
		totbyts += valdbyts;
		cx->base = cx->offrd;
		cx->offrd += valdbyts;
		cx->pnd -= valdbyts;
		fprintf(stderr, "cxt offrd %u offwr %u pnd %u\n", cx->offrd,
			cx->offwr, cx->pnd);
		fprintf(stderr, "rbuf tail %lu head %lu\n", buf->tail,
			buf->head);
	};

	return totbyts;
}

struct context *cxtinit(int fd)
{
	// only way to keep contexts on the stack is to keep them in an array
	// array has to be size of MAX_FD bc fd range is unpredictable
	struct context *cx = (struct context *)malloc(sizeof(*cx));
	cx->fd = fd;
	cx->buf = NULL;
	cx->offwr = cx->offrd = cx->pnd = cx->base = 0;

	return cx;
}

void hndlev(struct mempool *mp, struct rbuf *buf, struct context *cxt)
{
	ssize_t bytes, cmt;
	cmt = 0;

	fprintf(stderr, "\n\n==========================\nHandling event\n\n");
	if (cxt->buf == NULL) {
		//cxt->buf = palloc(mp);
		cxtmalloc(mp, cxt);
	}

	//TODO: use base as basis for return logic (pun intended)
	uint16_t base = cxt->offrd;
	while ((bytes = read(cxt->fd, cxt->buf, BLOCK_SIZE)) > 0) {
		fprintf(stderr, "hndlev read %lu bytes\n", bytes);
		cxt->pnd += bytes;

		// can i just use offwr, offrd,
		// (pend is wr - rd)
		// (free = BL_SZ - wr + rd)
		// (cntig = BL_SZ - offwr )
		cxt->offwr += bytes;
		processcxt(cxt, buf);
	}

	if (bytes == 0) {
		close(cxt->fd);
		cxtfree(mp, cxt);
		fprintf(stderr, "hndlev: usr closed conn %p\n", cxt->buf);
		return;
	} else if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
		perror("ERROR reading bytes");
		cxtfree(mp, cxt);
		close(cxt->fd);
		return;
	} else if (cxt->pnd == 0) {
		fprintf(stderr, "hndlev: read: %d, releasing%p\n",
			cxt->offrd - base, cxt->buf);
		cxtfree(mp, cxt);
		cxt->buf = NULL;
		return;
	}

	fprintf(stderr,
		"hndlev: non empty queue when EAGAIN. keeping mem block %p\n",
		cxt->buf);
}
