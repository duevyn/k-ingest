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

char *hwstr = "hello world\n";
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

void drain_rbuf2(struct rbuf *buf, struct kafka *kf)
{
	packethd hdr;
	memset(&hdr, 0, sizeof(struct packethd));
	size_t rmn, mscnt;

	rmn = MIN(SIZE - rbf_capac(buf), KFK_BTCH);
	mscnt = 0;

	rd_kafka_message_t msgs[rmn / 10];

	while (rmn > sizeof(hdr) && !rbf_isempty(buf)) {
		rbf_unwr(buf, &hdr, sizeof(struct packethd));
		if (!hdr.magic || hdr.magic != MAGIC) {
			fprintf(stderr,
				"ERROR drain_rbuf: Invalid magic %x\nCRITICAL rbuf should never have invalid entries. Store invalidated\n",
				hdr.magic);
			exit(EXIT_FAILURE);
		}
		rbf_rdfr(buf, NULL, sizeof(struct packethd));
		uint8_t *b = buf->slb + buf->tl;
		uint8_t *clean = NULL;
		if ((rbf_nfrmwrp(buf, 0)) < hdr.byts) {
			b = malloc(hdr.byts);
			clean = b;
			rbf_unwr(buf, b, hdr.byts);
		}
		msgs[mscnt++] = (rd_kafka_message_t){
			.payload = b,
			.len = hdr.byts,
			.rkt = kf->rkt,
			._private = clean,
		};

		rbf_rdfr(buf, NULL, hdr.byts);

		rmn -= hdr.byts;
	}
	if (mscnt) {
		rd_kafka_produce_batch(kf->rkt, RD_KAFKA_PARTITION_UA, 0, msgs,
				       mscnt);
		kfk_poll(kf);
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

	if (rbf_capac(buf) < MAX_MESSAGE) {
		fprintf(stderr, "ALERT hndlev: buffer overflow: cpc %lu cnt %u",
			rbf_capac(buf), buf->cnt);
		return 0;
	}

	int res = cxreadfd(cxt, MAX_MESSAGE);
	procfdcxt(cxt, buf, memcprng);
	// TODO: We should not compare compare bytes here. Any change in cx read can lead to unexpected behavior.
	// we need to check bytes immediately after sys call.

	if (cxt->pnd > 0)
		cxresetblk(cxt);
	else

		cxfreeblk(cxt, mp);
	return res;
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
	//int to;
	while (run) {
		//to = (cnncnt > 0) ? 0 : -1;
		//nfds = epoll_wait(epollfd, conn_evs, MAX_CONN, to);
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONN, 0);
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
			drain_rbuf2(buf, kf);
			kfk_poll(kf);
		}
	}
destroy:
	if (kf) {
		kfk_poll(kf);
		kfk_destroy(kf);
	}
	for (int i = 0; i < cnncnt; i++) {
		if (!cnns[i]->que) {
			continue;
		}
		fprintf(stderr, "Closing fd %d\n", cnns[i]->fd);
		shutdown(cnns[i]->fd, SHUT_RDWR);
		close(cnns[i]->fd);
	}
	mmp_destroy(mp);
	rbf_destroy(buf);
	close(epollfd);
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);

	fprintf(stdout, "\n\nGoodbye!\n");
	exit(EXIT_SUCCESS);
}
