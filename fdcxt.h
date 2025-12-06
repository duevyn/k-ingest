#ifndef FDCXT_H
#define FDCXT_H
#include <stdint.h>
#include <stdlib.h>
#include "mempool.h"

#define MAGIC 0xDEADBEEF
#define MAX_MESSAGE 1024

typedef void *(*getmem_fn)(void *);
typedef void (*freemem_fn)(void *, void *);
typedef void *memcpy_fn(void *, const void *, size_t);

typedef struct fdcxt {
	int fd;
	uint8_t *blk;
	uint16_t head;
	uint16_t tail;
	uint16_t pnd;

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

struct fdcxt *cxinit(int fd, void *src);
int procfdcxt(struct fdcxt *cx, void *dest, memcpy_fn memcopy);
int cxreadfd(struct fdcxt *cx, size_t n);
void *cxgetblk(struct fdcxt *cx, void *src);
void cxfreeblk(struct fdcxt *cx, void *src);
void cxdestroy(struct fdcxt *cx, void *src);
void cxresetblk(struct fdcxt *cx);
void cxwrite(struct fdcxt *cx, void *dest, size_t n, memcpy_fn memcopy);

#endif
