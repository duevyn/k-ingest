#include "rbuf.h"
#include <string.h>
#include <sys/mman.h>

//void *memcpyrng(struct rbuf *bf, uint16_t *src, size_t n)
void *memcpyrng(void *dest, const void *src, size_t n)
{
	struct rbuf *buf = (struct rbuf *)dest;
	memcpy(buf->mem + buf->head, src, n);
	buf->head += n;
	buf->count += n;
	return (void *)0;
}

struct rbuf *rbufinit()
{
	struct rbuf *bf = (struct rbuf *)malloc(sizeof(*bf));
	bf->slb = (uint8_t *)mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	bf->head = bf->tail = 0;
	return bf;
}

void rbufdestroy(struct rbuf *bf)
{
	if (!bf)
		return;
	if (bf->slb)
		munmap(bf, SIZE);
	free(bf);
}
