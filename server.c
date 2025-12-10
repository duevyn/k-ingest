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
#include <sys/param.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#define MAX_CONN 100
#define PORT 1100
typedef char byte;
typedef unsigned char ubyte;

static volatile sig_atomic_t run = 1;

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

void hello(uint8_t *b, size_t n)
{
	char hello[n + 1];
	hello[n] = '\0';
	memcpy(hello, b, n);
	fprintf(stderr, "bytes: %lu, drain buf has string: %s\n", n, hello);
}

void drain_rbuf(struct rbuf *buf, struct kafka *kf)
{
	packethd hdr;

	// Process all available messages
	fprintf(stderr, "before --> hd %lu, tl %lu\n", buf->hd, buf->tl);
	while (!rbf_isempty(buf)) {
		if (rbf_nfrmwrp(buf, 0) < sizeof(struct packethd)) {
			rbf_unwr(buf, &hdr, sizeof(struct packethd));
			rbf_rdfr(buf, NULL, sizeof(struct packethd));
			fprintf(stderr, " Did i read hdr? %u", hdr.byts);
		} else {
			rbf_rdfr(buf, (uint8_t *)&hdr, sizeof(struct packethd));
		}
		fprintf(stderr, "after head--> hd %lu, tl %lu\n", buf->hd,
			buf->tl);
		rd_kafka_resp_err_t err;
		uint8_t *b;

		if (rbf_nfrmwrp(buf, 0) < hdr.byts) {
			uint8_t m[hdr.byts];
			rbf_unwr(buf, m, hdr.byts);
			hello(m, hdr.byts);
			err = kfk_produce(kf, m, hdr.byts, kf->tpc);
		} else {
			hello(buf->slb + buf->tl, hdr.byts);
			err = kfk_produce(kf, buf->slb + buf->tl, hdr.byts,
					  kf->tpc);
		}

		rbf_rdfr(buf, NULL, hdr.byts);
		fprintf(stderr, "after --> hd %lu, tl %lu\n", buf->hd, buf->tl);
	}
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

int initepoll(int sfd, int *sigfd, fdcxt *cxt)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	// Block signals in this thread to prevent direct delivery
	// Need since call to rd_kafka_produceev is async
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	*sigfd = signalfd(-1, &mask, 0);
	int epollfd;

	if ((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	fdcxt *cxsig = malloc(sizeof(struct fdcxt));
	cxsig->fd = *sigfd;

	struct epoll_event conn_ev;
	conn_ev.events = EPOLLIN;
	conn_ev.data.ptr = cxsig;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, *sigfd, &conn_ev) == -1) {
		perror("epoll_ctl: signalfd ");
		exit(EXIT_FAILURE);
	}

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
	int sigfd;

	struct kafka *kf = NULL;
	if (argc == 3)
		kf = kafka_init(argv[1], argv[2]);
	else
		fprintf(stderr, "%% Usage: %s <broker> <topic>\n", argv[0]);

	int sockfd = initsocket();
	struct fdcxt cxt = { .fd = sockfd };
	int epollfd = initepoll(sockfd, &sigfd, &cxt);

	struct epoll_event conn_evs[MAX_CONN];
	struct rbuf *buf = rbufinit();
	struct mempool *mp =
		mmp_init(MAX_MESSAGE, sizeof(struct fdcxt), MAX_CONN);

	int nfds, n;
	while (run) {
		// 3. DRAIN RING BUFFER
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONN, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; n++) {
			struct fdcxt *cx = conn_evs[n].data.ptr;
			if (cx->fd == sigfd) {
				fprintf(stderr, "\n\nGoodbye!\n");
				goto destroy;
			}

			if (cx->fd == sockfd)
				hndlcn(epollfd, sockfd, &conn_evs[n], mp);
			else
				hndlev(mp, buf, cx);
		}

		if (kf) {
			drain_rbuf(buf, kf);
			kfk_poll(kf);
		}
	}
destroy:
	if (kf) {
		kfk_poll(kf);
		kfk_destroy(kf);
	}
	mmp_destroy(mp);
	rbf_destroy(buf);

	exit(EXIT_SUCCESS);
}
