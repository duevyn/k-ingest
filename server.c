#include "fdcxt.h"
#include "rbuf.h"
#include "mempool.h"

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
	ssize_t bytes, cmt;
	cmt = 0;

	fprintf(stderr, "\n\n==========================\nHandling event\n\n");
	if (cxt->blk == NULL)
		cxgetblk(cxt, mp);

	while ((bytes = cxreadfd(cxt, MAX_MESSAGE)) > 0) {
		/*
		fprintf(stderr, "hndlev read %lu bytes\n", bytes);
		if (rbfcapac(buf) < bytes) {
			fprintf(stderr,
				"ERROR hndlev: buffer overflow. bytes %lu, capac %ld, addr %p\n",
				bytes, rbfcapac(buf), buf);
			return;
		}
                */
		procfdcxt(cxt, buf, memcprng);
	}

	if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
		if (cxt->pnd)
			cxresetblk(cxt);
		else
			cxfreeblk(cxt, mp);
		return;
	}
	cxdestroy(cxt, mp);
	close(cxt->fd);
}

void startPolling(int server_fd, int epollfd, struct epoll_event *conn_evs)
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

int main(void)
{
	int sockfd = initsocket();
	int epollfd;

	if ((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	struct epoll_event conn_ev, conn_evs[MAX_CONN];
	conn_ev.events = EPOLLIN;
	struct fdcxt cxt = { .fd = sockfd };
	conn_ev.data.ptr = &cxt;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &conn_ev) == -1) {
		perror("epoll_ctl: listen_sock (server_fd)");
		exit(EXIT_FAILURE);
	}
	startPolling(sockfd, epollfd, conn_evs);
	exit(EXIT_SUCCESS);
}
