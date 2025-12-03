#ifndef FDCXT_H
#define FDCXT_H
#include <stdint.h>
#include <stdlib.h>

#define MAGIC 0xDEADBEEF

typedef void *(*getmem_fn)(void *);
typedef void (*freemem_fn)(void *, void *);
typedef void *memcpy_fn(void *, const void *, size_t);

typedef struct fdcxt {
	int fd;
	uint8_t *blk;
	enum {
		EAGAIN,
		READING_HEADER,
		READING_DATA,
		READING,
	} state;
	// can i just use head, tail,
	// (pend is head - tail)
	// (free = BL_SZ - head + tail)
	// (cntig = BL_SZ - hea )

	uint16_t head;
	uint16_t tail;
	uint16_t pnd;
	getmem_fn getmem;
	freemem_fn freemem;
	void *memsrc;

} fdcxt;

typedef struct packethd {
	uint32_t magic;
	uint8_t type;
	uint16_t byts;

} packethd;

typedef struct packet {
	struct packethd head;
	uint8_t *data;
} packet;

struct fdcxt *cxinit(int fd, void *src, getmem_fn gm, freemem_fn fm);
int procfdcxt(struct fdcxt *cx, void *dest, memcpy_fn memcopy);
int cxreadfd(struct fdcxt *cx, size_t n);
void *cxgetblk(struct fdcxt *cx);
void cxfreeblk(struct fdcxt *cx);
void destroycx(struct fdcxt *cx);
void cxwrite(struct fdcxt *cx, void *dest, size_t n, memcpy_fn memcopy);

#endif
