#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include "fdcxt.h"
#include <pthread.h>
#include <stdatomic.h>

#define NUM_THREADS 3

atomic_int snt = 0;
atomic_int err = 0;

int send_all(int sfd, const void *buf, size_t len)
{
	const uint8_t *ptr = buf;
	size_t remaining = len;
	while (remaining > 0) {
		ssize_t sent = send(sfd, ptr, remaining, 0);
		if (sent <= 0) {
			perror("we not sending dog\n");
			return -1; // Error or Connection Closed
		}
		ptr += sent;
		remaining -= sent;
	}
	return 0;
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
	int sfd = stsck();
	if (sfd < 0) {
		atomic_fetch_add(&err, 1);
		return (void *)NULL;
	}

	int l = 100;
	//int l = 2048;
	char ms[l];
	memset(ms, 'c', 100);
	memset(&ms[89], 'b', 10);
	//memset(ms, 'c', 2047);
	//ms[2047] = '\n';
	//ms[2047] = '\n';
	ms[l - 1] = '\n';

	struct packethd hd = {
		.magic = MAGIC,
		.type = 1,
		.byts = l - sizeof(struct packethd),
	};
	memcpy(ms, &hd, sizeof(struct packethd));

	for (;;) {
		if (send_all(sfd, ms, l) == -1) {
			//MSG_DONTWAIT);
			perror("Failed sending message\n");
			atomic_fetch_add(&err, 1);
			break;
			//fprintf(stderr, "sent fail\n");
		} else {
			atomic_fetch_add(&snt, 1);
			//fprintf(stderr, "sent successful\n");
		}
	}
	close(sfd);

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
