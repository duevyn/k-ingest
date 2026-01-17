#include "fdcxt.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/param.h>
#include <string.h>
#include <errno.h>

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
	if (!cx || !cx->blk)
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
	cx->blk = NULL;
	cx->que = false;
	cx->fd = -1;
	mmp_freecx(src, cx);
}

struct fdcxt *cxinit(int fd, void *src)
{
	struct fdcxt *cx = mmp_malloccx(src);
	//struct fdcxt *cx = malloc(sizeof(*cx));
	cx->fd = fd;
	cx->blk = NULL;
	cx->head = cx->tail = cx->pnd = 0;
	cx->que = false;

	return cx;
}

ssize_t cxreadfd(struct fdcxt *cx, size_t n)
{
	size_t locmax = MIN(MAX_MESSAGE, n);
	if ((MAX_MESSAGE  == cx->head)) {
		// TODO: this disconn fd if they are full. it should not reach full so maybe this is right.
		// other option is return 1 to keep them alive and rely on eviction policy when implemented
                cx->que = false;
		fprintf(stderr,
			"ALERT cxreadfd: cx buf is full hd %u, tl %u, n %lu, locmax %lu\n",
			cx->head, cx->tail, n, locmax);
		return 0;
	}
	size_t len = locmax - cx->head;
        if (cx->head + len > MAX_MESSAGE){
                cx->que = false;
		fprintf(stderr,
			"\nERROR. cx buffer overflow: hd %u tl %u pnd %u\n",
			cx->head, cx->tail, cx->pnd);
		return -1;
        }
	ssize_t bytes = read(cx->fd, cx->blk + cx->head, len);


	if (bytes > 0 ||
	    (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
	        cx->head += MAX(bytes, 0);
	        cx->pnd += MAX(bytes, 0);
                return MAX(bytes, 1);
	} else {
                cx->que = false;
                return -1;
        }
}

void cxwrite(struct fdcxt *cx, void *dest, size_t n, memcpy_fn memcopy)
{
	if (cx->tail + n > cx->head) {
		fprintf(stderr,
			"ALERT: cx cannot write %lu byts. only contain %u\n", n,
			cx->head - cx->tail);
		return;
	}
	memcopy(dest, cx->blk + cx->tail, n);
	cx->tail += n;
	cx->pnd -= n;
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
	if (!cx)
		return;

	if (cx->pnd > 0)
		memmove(cx->blk, cx->blk + cx->tail, cx->pnd);
	cx->tail = 0;
	cx->head = cx->pnd;
}

ssize_t fdx_validmsgs(struct fdcxt *cx)
{
	uint32_t it = cx->tail;

	struct packethd hd;
	while (it < cx->head) {
		if (!cx->pnd || cx->pnd < sizeof(hd)) {
			break;
		}
		memcpy(&hd, cx->blk + it, sizeof(hd));

		if (hd.magic != MAGIC) {
			fprintf(stderr, "ERROR: processcxt inval magic\n");
			//exit(EXIT_FAILURE);
			return -1;
		}
		if (it + hd.byts + sizeof(hd) > cx->head) {
			// TODO: this case needs more attention. should only happen if we receive partial message
                        // UPDATE: as suspected, we need to wait to finish this message
			//fprintf(stderr,
			//	"ALERT: partial message %d tl %u, hd %u, itr %u, hd.bytes %d-- ",
			//	cx->fd, cx->tail, cx->head, it, hd.byts);
			break;
		}
		it += sizeof(hd) + hd.byts;
	}
	return it - cx->tail;
}

ssize_t procfdcxt(struct fdcxt *cx, void *dest, memcpy_fn memcopy)
{
	ssize_t tot;

	if ((tot = fdx_validmsgs(cx)) <= 0)
		return tot;
	cxwrite(cx, dest, tot, memcopy);
	return tot;
}
