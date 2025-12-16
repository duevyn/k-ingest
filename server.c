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
#include <stdint.h>

#define MAX_CONN 200
#define PORT 1100
//#define KFK_BTCH 65536
//#define KFK_BTCH 32768
#define KFK_BTCH 16384
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
	//fprintf(stderr, "Adding new connection: %d\n", conn_fd);

	struct fdcxt *cx = cxinit(conn_fd, mp);
	setNonBlocking(cx->fd);
	ev->events = EPOLLIN | EPOLLET;
	ev->data.ptr = cx;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, cx->fd, ev) == -1) {
		perror("epoll_ctl: conn_sock");
		exit(EXIT_FAILURE);
	}
}

void hello(uint8_t *b, size_t n)
{
	char hello[n + 1];
	hello[n] = '\0';
	memcpy(hello, b, n);
	//fprintf(stderr, "bytes: %lu, drain buf has string: %s\n", n, hello);
}

void btchkfk(struct rbuf *bf, struct kafka *kf, mempool *mp)
{
	if (!bf || !kf)
		return;

	size_t frn = bf->hd >= bf->tl ? bf->hd : bf->tl;

	//size_t len = MIN();
	packethd hd;
	uint32_t itr = bf->tl;
	uint32_t ti = 0;
	rd_kafka_resp_err_t err;
	size_t tot = 0;
	if (bf->tl < bf->hd) {
		uint8_t *b = bf->slb + bf->tl;
		tot = (intptr_t)(bf->hd - bf->tl);
		if ((err = kfk_produce(kf, b, hd.byts, kf->tpc) == 0)) {
			rbf_rdfr(bf, NULL, hd.byts);
		}
	}
	do {
		rbf_unwr(bf, &hd, sizeof(struct packethd));
		if (!hd.magic || hd.magic != MAGIC) {
			fprintf(stderr,
				"ERROR drain_rbuf: Invalid magic %x, iter %d\nCRITICAL rbuf should never have invalid entries. Store invalidated\n",
				hd.magic);
			exit(EXIT_FAILURE);
		}
		ti += sizeof(hd) + hd.byts;

	} while (true);

	memset(&hd, 0, sizeof(struct packethd));
	//rbf_unwr(bf, );
	//length();
}

void drain_rbuf(struct rbuf *buf, struct kafka *kf)
{
	packethd hdr;
	memset(&hdr, 0, sizeof(struct packethd));
	while (!rbf_isempty(buf)) {
		rbf_unwr(buf, &hdr, sizeof(struct packethd));
		if (!hdr.magic || hdr.magic != MAGIC) {
			fprintf(stderr,
				"ERROR drain_rbuf: Invalid magic %x\nCRITICAL rbuf should never have invalid entries. Store invalidated\n",
				hdr.magic);
			exit(EXIT_FAILURE);
		}
		rbf_rdfr(buf, NULL, sizeof(struct packethd));
		uint8_t *b = buf->slb + buf->tl;
		bool clean = false;
		if ((rbf_nfrmwrp(buf, 0)) < hdr.byts) {
			b = malloc(hdr.byts);
			clean = true;
			rbf_unwr(buf, b, hdr.byts);
		}

		// TODO: we lose this message. we currenly advance to keep rbf in sync
		kfk_produce(kf, b, hdr.byts, kf->tpc);
		rbf_rdfr(buf, NULL, hdr.byts);

		if (clean)
			free(b);
		//kfk_poll(kf);
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

ssize_t hndlev(mempool *mp, rbuf *buf, fdcxt *cxt)
{
	if (cxt->blk == NULL)
		cxgetblk(cxt, mp);
	ssize_t bytes, tot, cur;
	bytes = cur = 0;
	tot = MIN(rbf_capac(buf), MAX_MESSAGE);

	if (rbf_capac(buf) < MAX_MESSAGE * 2)
		return 0;

	bytes = cxreadfd(cxt, tot);
	procfdcxt(cxt, buf, memcprng);

	if (bytes > 0 ||
	    (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
		//cxresetblk(cxt);
		if (cxt->pnd == 0)
			cxfreeblk(cxt, mp);
		return MAX(bytes, 1);
	}

	cxt->que = false;
	return -1;
}

int main(int argc, char *argv[])
{
	struct kafka *kf = NULL;
	if (argc == 3)
		kf = kafka_init(argv[1], argv[2]);
	else
		fprintf(stderr, "%% Usage: %s <broker> <topic>\n", argv[0]);

	int sockfd = initsocket();
	struct fdcxt cxt = { .fd = sockfd };
	int sigfd;
	int epollfd = initepoll(sockfd, &sigfd, &cxt);

	struct epoll_event conn_evs[MAX_CONN];
	struct rbuf *buf = rbufinit();
	struct fdcxt *cnns[POOL_SIZE], *tmp[POOL_SIZE];
	struct mempool *mp =
		mmp_init(MAX_MESSAGE, sizeof(struct fdcxt), MAX_CONN);

	uint8_t cnncnt = 0;
	int nfds, n;
	while (run) {
		int8_t to = cnncnt ? 0 : -1;
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONN, to);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}
		for (int i = 0; i < cnncnt; i++)
			tmp[i] = cnns[i];

		for (n = 0; n < nfds; n++) {
			struct fdcxt *cx = conn_evs[n].data.ptr;
			if (cx->fd == sigfd)
				goto destroy;

			if (cx->fd == sockfd)
				hndlcn(epollfd, sockfd, &conn_evs[n], mp);
			else if (!cx->que) {
				tmp[cnncnt++] = cx;
				cx->que = true;
				//fprintf(stderr,
				//	"queued fd %d. cx %p, blk %p cnt %u\n",
				//	cx->fd, cx, cx->blk, cnncnt);
			}
		}

		int ncnt = 0;
		int res;
		for (n = 0; n < cnncnt; n++) {
			if ((res = hndlev(mp, buf, tmp[n])) > 0)
				cnns[ncnt++] = tmp[n];
			else {
				shutdown(tmp[n]->fd, SHUT_RDWR);
				close(tmp[n]->fd);
				cxdestroy(tmp[n], mp);
			}
		}
		cnncnt = ncnt;
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

	fprintf(stdout, "\n\nGoodbye!\n");
	exit(EXIT_SUCCESS);
}
