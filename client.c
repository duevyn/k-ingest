#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

int main()
{
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		exit(EXIT_FAILURE);
	} else {
		printf("Socket successfully created..\n");
	}
	struct sockaddr_in sa = {
		.sin_port = htons(1100),
		.sin_family = AF_INET,
	};
	int res = inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

	if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		fprintf(stderr, "Failed to establish connection!\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	printf("connected %d:%d\n", sa.sin_addr.s_addr, 1100);
	char *msg = "\nHello World\n";
	int sent = send(sockfd, msg, strlen(msg), MSG_DONTWAIT);
	if (sent == -1) {
		printf("ERROR sending message: %d", errno);
		close(sockfd);
		exit(EXIT_FAILURE);
	} else {
		close(sockfd);
	}
	close(sockfd);
	return EXIT_SUCCESS;
}
