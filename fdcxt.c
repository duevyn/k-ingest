#include "fdcxt.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

void *cxgetblk(struct fdcxt *cx)
{
	cx->blk = (uint8_t *)cx->getmem(cx->memsrc);
	return cx->blk;
}
void cxfreeblk(struct fdcxt *cx)
{
	cx->freemem(cx->memsrc, cx->blk);
	cx->blk = NULL;
	cx->head = cx->tail = cx->pnd = 0;
}

struct fdcxt *cxinit(int fd, void *src, getmem_fn getmem, freemem_fn freemem)
{
	// only way to keep contexts on the stack is to keep them in an array
	// array has to be size of MAX_FD bc fd range is unpredictable
	//
	// TODO: statement above is wrong. we can allocted these in mempool
	struct fdcxt *cx = (struct fdcxt *)malloc(sizeof(*cx));
	cx->fd = fd;
	cx->blk = NULL;
	cx->getmem = getmem;
	cx->freemem = freemem;
	cx->memsrc = src;
	cx->head = cx->tail = cx->pnd = 0;

	return cx;
}

int cxreadfd(struct fdcxt *cx, size_t n)
{
	ssize_t byts = read(cx->fd, cx->blk, n);
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
		fprintf(stderr, "ALERT processcxt: %u bytes rd\n", cxt->pnd);
		return 0;
	}

	struct packethd pckhd;
	memcpy(&pckhd, cxt->blk, sizeof(packethd));

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

void helloworld(struct fdcxt *cx, int byts)
{
	size_t strsz = byts - sizeof(packethd) + 1;
	char str[strsz];
	bzero(str, strsz);
	memcpy(str, cx->blk + cx->tail + sizeof(packethd), strsz - 1);

	fprintf(stderr, "processcxt found string\n%s\n", str);
}

int procfdcxt(struct fdcxt *cx, void *dest, memcpy_fn memcopy)
{
	int totbyts, valdbyts;
	totbyts = valdbyts = 0;

	for (;;) {
		if ((valdbyts = validpkt(cx)) <= 0) {
			fprintf(stderr, "proccxt: incmp msg. %d\n", cx->pnd);
			return valdbyts;
		}

		fprintf(stderr, "\n*****************\n-> Processing Context\n");
		helloworld(cx, valdbyts);
		cxwrite(cx, dest, valdbyts, memcopy);
		totbyts += valdbyts;
		fprintf(stderr, "cxt offrd %u offwr %u pnd %u\n", cx->tail,
			cx->head, cx->pnd);
	};

	return totbyts;
	return 0;
}
