#include "fdcxt.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/param.h>
#include <string.h>

void *cxgetblk(struct fdcxt *cx, void *src)
//void *cxgetblk(struct fdcxt *cx)
{
	if (!cx)
		return (void *)0;
	cx->blk = (uint8_t *)mmp_mallocblk(src);
	return cx->blk;
}
void cxfreeblk(struct fdcxt *cx, void *src)
{
	if (!cx)
		return;

	mmp_freeblk(src, cx->blk);
	cx->blk = NULL;
	cx->head = cx->tail = cx->pnd = 0;
}

void cxdestroy(struct fdcxt *cx, void *src)
{
	if (!cx)
		return;
	if (cx->blk)
		cxfreeblk(cx, src);
	mmp_freecx(src, cx);
}

struct fdcxt *cxinit(int fd, void *src)
{
	struct fdcxt *cx = mmp_malloccx(src);
	cx->fd = fd;
	cx->blk = NULL;
	cx->head = cx->tail = cx->pnd = 0;

	return cx;
}

int cxreadfd(struct fdcxt *cx, size_t n)
{
	ssize_t byts = read(cx->fd, cx->blk + cx->head, n);
	if (byts > 0) {
		cx->head += byts;
		cx->pnd += byts;
	}
	return byts;
}

void cxwrite(struct fdcxt *cx, void *dest, size_t n, memcpy_fn memcopy)
{
	memcopy(dest, cx->blk + cx->tail, n);
	cx->tail += n;
	cx->pnd -= n;
}

int validpkt(struct fdcxt *cxt)
{
	if (cxt->pnd < sizeof(packethd)) {
		//fprintf(stderr, "ALERT processcxt: %u bytes rd\n", cxt->pnd);
		return 0;
	}

	struct packethd pckhd;
	memcpy(&pckhd, cxt->blk + cxt->tail, sizeof(packethd));

	//TODO: we need to close the socket if it fails magic. but we want all
	// mem management done in hndlev
	if (pckhd.magic != MAGIC) {
		fprintf(stderr, "ERROR: processcxt inval magic\n");
		return -1;
	}

	if ((pckhd.byts + sizeof(packethd)) > cxt->pnd) {
		//TODO: This packet never has to finish. how do we ensure we
		// get mp->blk back from this cxt
		fprintf(stderr,
			"ALERT processcxt:byts avail %u,  byts needed %lu\n",
			cxt->pnd, pckhd.byts + sizeof(packethd));
		return 0;
	}

	int tot = pckhd.byts + sizeof(packethd);
	return tot;
}

void helloworld(struct fdcxt *cx, int byts)
{
	size_t strsz = byts - sizeof(packethd) + 1;
	char str[strsz];
	bzero(str, strsz);
	memcpy(str, cx->blk + cx->tail + sizeof(packethd), strsz - 1);

	fprintf(stderr, "%s\n", str);
}

void cxresetblk(struct fdcxt *cx)
{
	if (!cx || !cx->pnd)
		return;

	memmove(cx->blk, cx->blk + cx->tail, cx->pnd);
	cx->tail = 0;
	cx->head = cx->pnd;
}

int procfdcxt(struct fdcxt *cx, void *dest, memcpy_fn memcopy)
{
	int totbyts, valdbyts;
	totbyts = valdbyts = 0;

	for (;;) {
		if ((valdbyts = validpkt(cx)) <= 0)
			return valdbyts;

		fprintf(stderr, "\n*****************\n-> Processing Context\n");
		helloworld(cx, valdbyts);
		cxwrite(cx, dest, valdbyts, memcopy);
		totbyts += valdbyts;
		//fprintf(stderr, "cxt offrd %u offwr %u pnd %u\n", cx->tail,
		//cx->head, cx->pnd);
	};

	return totbyts;
	return 0;
}
