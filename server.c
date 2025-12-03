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

#define FALSE 0
#define TRUE 1
#define MAX_CONNECTIONS 50
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

	struct fdcxt *cx =
		cxinit(conn_fd, mp, (getmem_fn)palloc, (freemem_fn)pfree);

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
	if (cxt->blk == NULL) {
		cxgetblk(cxt);
	}

	//TODO: use base as basis for return logic (pun intended)
	uint16_t base = cxt->tail;
	while ((bytes = cxreadfd(cxt, BLOCK_SIZE)) > 0) {
		fprintf(stderr, "hndlev read %lu bytes\n", bytes);

		// can i just use offwr, offrd,
		// (pend is wr - rd)
		// (free = BL_SZ - wr + rd)
		// (cntig = BL_SZ - offwr )
		procfdcxt(cxt, buf->mem, memcpyrng);
	}

	if (bytes == 0) {
		close(cxt->fd);
		cxfreeblk(cxt);
		fprintf(stderr, "hndlev: usr closed conn %p\n", cxt->blk);
		return;
	} else if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
		perror("ERROR reading bytes");
		close(cxt->fd);
		cxfreeblk(cxt);
		return;
	} else if (cxt->pnd == 0) {
		fprintf(stderr, "hndlev: read: %d, releasing%p\n",
			cxt->tail - base, cxt->blk);
		cxfreeblk(cxt);
		return;
	}

	fprintf(stderr,
		"hndlev: non empty queue when EAGAIN. keeping mem block %p\n",
		cxt->blk);
}

void startPolling(int server_fd)
{
	struct rbuf *buf = rbufinit();
	struct mempool *mp = mempoolinit();
	printf("buff size %lu, location %p\n", sizeof(buf->mem), buf->mem);
	struct epoll_event conn_ev, conn_evs[100];
	int nfds, epollfd, n;

	if ((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	struct fdcxt cxt = { .fd = server_fd };

	conn_ev.events = EPOLLIN;
	conn_ev.data.ptr = &cxt;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &conn_ev) == -1) {
		perror("epoll_ctl: listen_sock (server_fd)");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		//fprintf(stderr, "Do we even start loop\n");
		signal(SIGINT, intHandler);
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONNECTIONS, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; n++) {
			struct fdcxt *cx = conn_evs[n].data.ptr;
			//struct context *cx = conn_evs[n].data.ptr;
			if (cx->fd == server_fd) {
				hndlcn(epollfd, server_fd, &conn_evs[n], mp);
			} else {
				hndlev(mp, buf, cx);
			}
		}
	}
	mpdestroy(mp);
}

int main(void)
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

	startPolling(sockfd);
	exit(EXIT_SUCCESS);
}
