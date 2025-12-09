#include "rbuf.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/param.h>

void *memcprng(void *dest, const void *src, size_t n)
{
	struct rbuf *buf = (struct rbuf *)dest;
	size_t len = MIN(SIZE - buf->hd, n);
	memcpy(buf->slb + buf->hd, src, len);
	memcpy(buf->slb, (uint8_t *)src + len, n - len);

	buf->hd = (buf->hd + n) % SIZE;
	buf->cnt += n;
	//fprintf(stderr, "memcprng hd %lu tl %lu cnt %u, len %lu, addr %p\n",
	//	buf->hd, buf->tl, buf->cnt, len, buf);
	return dest;
}

struct rbuf *rbufinit()
{
	struct rbuf *bf = mmap(NULL, sizeof(*bf) + SIZE, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	bf->slb = (uint8_t *)(bf + 1);
	bf->hd = bf->tl = 0;
	return bf;
}

void rbf_rdfr(rbuf *buf, uint8_t *dest, size_t n)
{
	if (dest) {
		memcpy(dest, buf, n);
	}

	buf->tl = (buf->tl + n) % buf->sz;
}

void rbufdestroy(struct rbuf *bf)
{
	if (!bf)
		return;
	munmap(bf, SIZE + sizeof(*bf));
	fprintf(stderr, "DESTROYED RBUF\n");
}

size_t rbfcapac(struct rbuf *buf)
{
	return SIZE - buf->cnt;
}
