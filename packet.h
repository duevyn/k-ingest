#ifndef PACKET_H
#define PACKET_H
#define MAGIC 0xDEADBEEF

#include <stdint.h>

typedef struct packethd {
	uint32_t magic;
	uint8_t type;
	uint16_t databytes;

} packethd;

typedef struct packet {
	struct packethd head;
	uint8_t *data;
} packet;

typedef struct context {
	int fd;

	struct packethd head;
	uint8_t buf[16384];
	enum { READING_HEADER, READING_DATA } state;

	uint16_t prcbyt;
	uint16_t totbyt;
} context;

#endif
