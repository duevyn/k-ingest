#include "fdcxt.h"
#include "rbuf.h"
#include "mempool.h"
#include "kafka.h"

#include <stddef.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#define FALSE 0
#define TRUE 1
#define MAX_CONN 100
#define PORT 1100
typedef char byte;
typedef unsigned char ubyte;

void intHandler()
{
	printf("\nGoodbye!\n");
	exit(0);
}

int wrkafka()
{
	return 0;
}

void setNonBlocking(int fd)
{
	int prev = fcntl(fd, F_GETFL, 0);
	if (prev == -1) {
		perror("error setting non blocking");
		_exit(EXIT_FAILURE);
	}
	fcntl(fd, F_SETFL, prev | O_NONBLOCK);
}

void printSocketPort(int sock, struct sockaddr_in *addr)
{
	socklen_t len = sizeof(*addr);
	if (getsockname(sock, (struct sockaddr *)addr, &len)) {
		perror("getting sock name");
		exit(EXIT_FAILURE);
	}

	uint32_t ip = ntohl(addr->sin_addr.s_addr);
	printf("socket fd (%d) listening on %d:%d\n", sock, ip, PORT);
}

void hndlcn(int efd, int sfd, struct epoll_event *ev, struct mempool *mp)
{
	int conn_fd;
	if ((conn_fd = accept(sfd, NULL, NULL)) == -1) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "Adding new connection: %d\n", conn_fd);

	struct fdcxt *cx = cxinit(conn_fd, mp);
	setNonBlocking(cx->fd);
	ev->events = EPOLLIN | EPOLLET;
	ev->data.ptr = cx;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, cx->fd, ev) == -1) {
		perror("epoll_ctl: conn_sock");
		exit(EXIT_FAILURE);
	}
}

void hndlev(struct mempool *mp, struct rbuf *buf, struct fdcxt *cxt)
{
	ssize_t bytes;

	fprintf(stderr, "\n\n==========================\nHandling event\n\n");
	if (cxt->blk == NULL)
		cxgetblk(cxt, mp);

	while ((bytes = cxreadfd(cxt, MAX_MESSAGE)) > 0) {
		int res;
		/*
		fprintf(stderr, "hndlev read %lu bytes\n", bytes);
		if (rbfcapac(buf) < bytes) {
			fprintf(stderr,
				"ERROR hndlev: buffer overflow. bytes %lu, capac %ld, addr %p\n",
				bytes, rbfcapac(buf), buf);
			return;
		}
                */
		if ((res = procfdcxt(cxt, buf, memcprng)) == -1)
			goto destroy;
	}

	if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		// TODO: How do we know they will come back to release mem
		if (cxt->pnd)
			cxresetblk(cxt);
		else
			cxfreeblk(cxt, mp);
		return;
	}

destroy:
	cxdestroy(cxt, mp);
	close(cxt->fd);
	fprintf(stderr, "closing conn: %d\n", cxt->fd);
}
// main.c

uint8_t *rbf_unwr(struct rbuf *bf, size_t n)
{
	packethd hdr;
	uint8_t *b = malloc(n);
	memcpy(&hdr, bf, sizeof(struct packethd));

	size_t off = (bf->tl + n) % SIZE;
	memcpy(b, bf + sizeof(struct packethd), n - off);
	memcpy(b + n - off, bf, off);
	return b;
}

void drain_rbuf(struct rbuf *buf, struct kafka *kf)
{
	packethd hdr;

	// Process all available messages
	while (buf->tl != buf->hd) {
		memcpy(&hdr, buf, 0);
		uint8_t *b = NULL;
		int rs;

		char *c;
		b = hdr.byts > (buf->sz - buf->tl) ? rbf_unwr(buf, hdr.byts) :
						     buf->slb;
		//if ((rs = kfk_produce(kf, b + sizeof(struct packethd), hdr.byts,
		//		      c))) {
		if (1) {
			fprintf(stderr, "ERR\n");
			return;
		}

		// update tail
		rbf_rdfr(buf, NULL, hdr.byts);
	}

	/*
	while (buf->cnt >= sizeof(packethd)) {
		// 1. PEEK SIZE (We must do this to know how much to send)
		// We use peek because we might wrap inside the header itself
		rbuf_peek(buf, &hdr, sizeof(packethd));

		uint32_t payload_len = hdr.byts;
		uint32_t total_packet_len = sizeof(packethd) + payload_len;

		// Wait if full packet isn't here yet
		if (buf->cnt < total_packet_len) {
			return;
		}

		// 2. TRY DIRECT POINTER (Optimization)
		void *direct_ptr = rbuf_get_linear_ptr(buf, total_packet_len);

		if (direct_ptr) {
			// [CASE A: LINEAR]
			// Fast path! No scratch buffer.
			// We strip the header by adding sizeof(packethd) to the pointer.

			int res = kfk_produce(
				kf, (uint8_t *)direct_ptr + sizeof(packethd),
				payload_len);

			if (res == 0) {
				// Success. Manually advance tail since we accessed directly.
				buf->tail =
					(buf->tail + total_packet_len) % SIZE;
				buf->count -= total_packet_len;
			} else {
				// Backpressure (Kafka full). Stop draining.
				return;
			}
		} else {
			// [CASE B: WRAPPED]
			// Slow path. The message wraps around the end of the buffer.
			// We MUST linearize it into scratch_pad to send it.

			// Unroll the wrap into scratch
			rbuf_read(buf, scratch_pad, total_packet_len);

			// Send scratch (Skipping header bytes)
			int res = kfk_produce(kf,
					      scratch_pad + sizeof(packethd),
					      payload_len);

			if (res == -1) {
				// CRITICAL: We already advanced tail in rbuf_read!
				// If Kafka fails here, we effectively dropped the packet.
				// Ideally: Implement a "rollback" or infinite retry here.
				fprintf(stderr,
					"Buffer Wrap + Kafka Full = Dropped Packet\n");
				return;
			}
		}
	}
        */
}

void stpoll(int server_fd, int epollfd, struct epoll_event *conn_evs)
{
	struct rbuf *buf = rbufinit();
	struct mempool *mp = mmp_init(MAX_MESSAGE, sizeof(fdcxt), MAX_CONN);
	fprintf(stderr,
		"\n\n***********************\nMEMORY\nbuff %p, slab %p, cxslb %p\n",
		buf, mp->slab, mp->cxslb);
	fprintf(stderr,
		"buf.slb -> mp.slab %f (%f),  buf.slb -> mp.cxslb %f,  mp.slab -> mp.cxslb %lu\n",
		log10((size_t)((ptrdiff_t)(mp->slab - buf->slb))),
		log((size_t)((ptrdiff_t)(mp->slab - buf->slb))),
		log((size_t)(ptrdiff_t)(mp->cxslb - buf->slb)),
		(ptrdiff_t)(mp->cxslb - mp->slab));
	int nfds, n;
	for (;;) {
		signal(SIGINT, intHandler);

		// 2. KAFKA POLL (Callbacks)
		//kfk_poll(kf);

		// 3. DRAIN RING BUFFER
		//drain_rbuf(buf, kf);
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONN, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; n++) {
			struct fdcxt *cx = conn_evs[n].data.ptr;
			if (cx->fd == server_fd) {
				hndlcn(epollfd, server_fd, &conn_evs[n], mp);
			} else {
				hndlev(mp, buf, cx);
			}
		}
	}

	//TODO: we actually never call this. implement data cleanup
	mmp_destroy(mp);
}

int initsocket()
{
	int sockfd =
		socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port = htons(PORT),
	};
	int opt = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		printf("Failed binding socket\n");
		return EXIT_FAILURE;
	}

	printSocketPort(sockfd, &addr);
	if (listen(sockfd, 50) == -1) {
		printf("Failure listening: %d\n", errno);
		exit(EXIT_FAILURE);
	}

	return sockfd;
}

int initepoll(int sfd, fdcxt *cxt)
{
	int epollfd;

	if ((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	struct epoll_event conn_ev;
	conn_ev.events = EPOLLIN;
	conn_ev.data.ptr = cxt;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sfd, &conn_ev) == -1) {
		perror("epoll_ctl: listen_sock (server_fd)");
		exit(EXIT_FAILURE);
	}
	return epollfd;
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "%% Usage: %s <broker> <topic>\n", argv[0]);
		//return 1;
	}

	char *brokers;
	char *topic;

	int sockfd = initsocket();
	//struct fdcxt *cxt = malloc(sizeof(*cxt));
	struct fdcxt cxt = { .fd = sockfd };
	int epollfd = initepoll(sockfd, &cxt);

	struct epoll_event conn_evs[MAX_CONN];
	struct rbuf *buf = rbufinit();
	struct mempool *mp =
		mmp_init(MAX_MESSAGE, sizeof(struct fdcxt), MAX_CONN);
	//struct kafka *kf = kafka_init(NULL, NULL);
	struct kafka *kf;

	if (argc == 3) {
		brokers = argv[1];
		topic = argv[2];
		kf = kafka_init(brokers, topic);
		fprintf(stderr, "bro: %s, top %s\n", brokers, topic);

		// testing kafka setup
		char *payload = "hello world from C";
		kfk_produce(kf, payload, strlen(payload), kf->tpc);
		kfk_poll(kf);
		//cleanup
		rd_kafka_flush(kf->rk, 10 * 1000);
		if (rd_kafka_outq_len(kf->rk) > 0)
			fprintf(stderr, "%% %d message(s) were not delivered\n",
				rd_kafka_outq_len(kf->rk));

		rd_kafka_destroy(kf->rk);
		exit(0);
	}
	fprintf(stderr, "ERROR, too far\n");

	int nfds, n;
	for (;;) {
		signal(SIGINT, intHandler);

		// 3. DRAIN RING BUFFER
		//drain_rbuf(buf, kf);
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONN, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; n++) {
			struct fdcxt *cx = conn_evs[n].data.ptr;
			if (cx->fd == sockfd) {
				hndlcn(epollfd, sockfd, &conn_evs[n], mp);
			} else {
				hndlev(mp, buf, cx);
			}
		}

		// 2. KAFKA POLL (Callbacks)
		//kfk_poll(kf);
		if (kf) {
			kfk_poll(kf);
		}
	}
	if (kf) {
		rd_kafka_flush(kf->rk, 10 * 1000);
		if (rd_kafka_outq_len(kf->rk) > 0)
			fprintf(stderr, "%% %d message(s) were not delivered\n",
				rd_kafka_outq_len(kf->rk));

		rd_kafka_destroy(kf->rk);
	}

	//stpoll(sockfd, epollfd, conn_evs);
	exit(EXIT_SUCCESS);
}
