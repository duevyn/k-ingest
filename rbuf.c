#include "rbuf.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/param.h>

void *memcprng(void *dest, const void *src, size_t n)
{
	int stln = n - 8 + 1;
	char s[stln];
	s[stln - 1] = '\0';
	//memcpy(s, ((uint8_t *)src) + 8, n);
	struct rbuf *buf = (struct rbuf *)dest;
	size_t sthd = buf->hd + 8;
	size_t len = MIN(SIZE - buf->hd, n);
	size_t stlen = MIN(SIZE - sthd, n - 8);
	memcpy(buf->slb + buf->hd, src, len);
	memcpy(s, buf->slb + buf->hd + 8, stlen); // for hello
	memcpy(buf->slb, (uint8_t *)src + len, n - len);
	memcpy(&s[stlen], buf->slb, n - 8 - stlen); // for hello
	fprintf(stderr, "memcprng: cp %lu by : %s\n", n, s);

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
	bf->sz = SIZE;
	return bf;
}

void rbf_rdfr(rbuf *buf, uint8_t *dest, size_t n)
{
	if (dest) {
		memcpy(dest, buf->slb + buf->tl, n);
	}

	buf->tl = (buf->tl + n) % buf->sz;
	buf->cnt -= n;
}

void rbf_destroy(struct rbuf *bf)
{
	if (!bf)
		return;
	munmap(bf, SIZE + sizeof(*bf));
	fprintf(stderr, "DESTROYED RBUF\n");
}

size_t rbf_capac(struct rbuf *buf)
{
	return SIZE - buf->cnt;
}

bool rbf_isempty(struct rbuf *bf)
{
	return bf->hd == bf->tl && bf->cnt == 0;
}
bool rbf_isfull(struct rbuf *bf)
{
	return bf->hd == bf->tl && bf->cnt == SIZE;
}
