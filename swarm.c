#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <errno.h>
#include "fdcxt.h"
#include <pthread.h>
#include <stdatomic.h>

#define NUM_THREADS 100 // Concurrent "Functions" running
#define REQS_PER_THREAD 1000
//#define NUM_THREADS 75 // Concurrent "Functions" running
//#define REQS_PER_THREAD 1500
//#define NUM_THREADS 10 // Concurrent "Functions" running
//#define REQS_PER_THREAD 100

atomic_int snt = 0;
atomic_int err = 0;

char *msg = "hello world from b blient\n";

uint8_t *crbuf()
{
	size_t sz = sizeof(struct packethd);
	struct packethd hd = {
		.magic = MAGIC,
		.type = 1,
		.byts = strlen(msg),
	};

	struct packet pk = { .head = hd, .data = (uint8_t *)msg };

	//fprintf(stderr,
	//	"packethd byt: %u, packet size: %ld, calc size: %ld, %s\n",
	//	hd.byts, sizeof(pk), sz + strlen(bf), bf);

	uint8_t *buff = malloc(sz + strlen(msg) + 1);
	memset(buff, '\0', sz + strlen(msg) + 1);
	memcpy(buff, &pk.head, sz);
	memcpy(&buff[sz], msg, strlen(msg));
	//fprintf(stderr, "malloced buff to %p: %s\n", buff,
	//	buff + sizeof(struct packethd));
	return buff;
}

int stsck()
{
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		fprintf(stderr, "socket creation failed...\n");
		return -1;
	}
	struct sockaddr_in sa = {
		.sin_port = htons(1100),
		.sin_family = AF_INET,
	};
	inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

	if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		fprintf(stderr, "Failed to establish connection!\n");
		return -1;
	} else {
		//fprintf(stderr, "connected %d:%d\n", sa.sin_addr.s_addr, 1100);
	}
	return sockfd;
}

void *worker(void *arg)
{
	uint8_t *b = crbuf();
	struct packethd hd;
	memcpy(&hd, b, sizeof(struct packethd));
	//fprintf(stderr, "init b address %p, %s\n", b, b);
	struct sockaddr_in sa = {
		.sin_port = htons(1100),
		.sin_family = AF_INET,
	};
	inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

	for (int i = 0; i < REQS_PER_THREAD; i++) {
		//int sfd = socket(AF_INET, SOCK_STREAM, 0);
		int sfd = stsck();
		if (sfd < 0) {
			atomic_fetch_add(&err, 1);
			continue;
		}

		int sent = send(sfd, b, sizeof(struct packethd) + hd.byts, 0);
		//int sent = send(sfd, b, sizeof(struct packethd) + hd.byts, MSG_DONTWAIT);
		if (sent == -1) {
			atomic_fetch_add(&err, 1);
			//fprintf(stderr, "sent fail\n");
		} else {
			atomic_fetch_add(&snt, 1);
			//fprintf(stderr, "sent successful\n");
		}

		close(sfd);
	}
	free(b);

	return NULL;
}

int main(void)
{
	pthread_t ths[NUM_THREADS];

	struct timespec s, e;
	clock_gettime(CLOCK_MONOTONIC, &s);

	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_create(&ths[i], NULL, worker, NULL);
	}
	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(ths[i], NULL);
	}

	clock_gettime(CLOCK_MONOTONIC, &e);

	double elapsed = (e.tv_sec - s.tv_sec) + (e.tv_nsec - s.tv_nsec) / 1e9;

	fprintf(stderr, "Done\n");
	fprintf(stderr, "Total Success: %d\n", snt);
	fprintf(stderr, "Total Fail: %d\n", err);
	printf("Elapsed real: %.2f seconds\n", elapsed);

	return EXIT_SUCCESS;
}
