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
#endif
