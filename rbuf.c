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
	size_t len = MIN(SIZE - buf->head, n);
	memcpy(buf->slb + buf->head, src, len);
	memcpy(buf->slb, (uint8_t *)src + len, n - len);

	buf->head = (buf->head + n) % SIZE;
	buf->count += n;
	//fprintf(stderr, "memcprng hd %lu tl %lu cnt %u, len %lu, addr %p\n",
	//	buf->head, buf->tail, buf->count, len, buf);
	return dest;
}

struct rbuf *rbufinit()
{
	struct rbuf *bf = mmap(NULL, sizeof(*bf) + SIZE, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	bf->slb = (uint8_t *)(bf + 1);
	bf->head = bf->tail = 0;
	return bf;
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
	return SIZE - buf->count;
}
