#include "rbuf.h"
#include "packet.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

size_t capac(struct rbuf *buf)
{
	if ((buf->head == buf->tail) && buf->count == 0) {
		return sizeof(buf->mem);
	}
	if (buf->head == buf->tail) {
		return 0;
	}
	return sizeof(buf->mem) - buf->count;
}

void dechd(struct rbuf *buf, uint16_t count)
{
	buf->tail += count;
	buf->count -= count;
}
void inchd(struct rbuf *buf, uint16_t count)
{
	buf->head += count;
	buf->count += count;
}

size_t readhd(struct rbuf *buf, int fd)
{
	size_t sz = sizeof(packethd);
	ssize_t bytes = read(fd, &buf->mem[buf->head], sz);
	if (bytes != sz) {
		perror("ERROR: Incorrect format\n");
		// TODO: is it my job to close the socket?
		close(fd);
	}

	inchd(buf, sz);
	struct packethd hd;
	memcpy(&hd, &buf->mem[(buf->head) - sz], sz);
	if (hd.magic != MAGIC) {
		perror("ERROR. Unverified\n");
		// TODO: is it my job to close the socket?
		close(fd);
	}

	fprintf(stderr, "$$$$$$$  %ld bytes from fd: %d\nhead %lu, capac %lu\n",
		bytes, fd, buf->head, capac(buf));
	fprintf(stderr, "hd.magic: %x, hd.type: %d, hd.databytes: %u\n",
		hd.magic, hd.type, hd.databytes);
	fprintf(stderr, "==============================================\n");
	return hd.databytes;
}

void readfd_nbl(struct rbuf *buf, int fd, uint16_t count)
{
	ssize_t datalen = readhd(buf, fd);
	if (datalen != sizeof(packethd)) {
		//handle error
	}
	while (1) {
		fprintf(stderr, "reading %lu bytes from from %x (%lu)\n",
			datalen, buf->mem[buf->head], buf->head);
		ssize_t bytes = read(fd, &buf->mem[buf->head], datalen);
		if (bytes == -1) {
			fprintf(stderr, "EAGAIN\n");
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			} else {
				perror("Error reading");
				close(fd);
				break;
			}
		} else if (bytes == 0) {
			printf("Client disconnected.\n");
			close(fd);
			break;
		} else {
			size_t tmp = buf->head;
			inchd(buf, bytes);
			buf->mem[buf->head] = '\0';
			inchd(buf, 1);
			printf("%ld bytes from fd: %d\n%s\nhead %lu, capac %lu, tmp: %lu\n",
			       bytes, fd, &buf->mem[tmp], buf->head, capac(buf),
			       tmp);
		}
	}
}
