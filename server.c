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

void handleConn(int efd, int sfd, struct sockaddr_in *addr,
		struct epoll_event *ev)
{
	int conn_fd;
	socklen_t addrlen = sizeof(*addr);

	if ((conn_fd = accept(sfd, NULL, NULL)) == -1) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	printf("Adding a new connection: %d\n\n", conn_fd);
	setNonBlocking(conn_fd);
	ev->events = EPOLLIN | EPOLLET;
	ev->data.fd = conn_fd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, conn_fd, ev) == -1) {
		perror("epoll_ctl: conn_sock");
		exit(EXIT_FAILURE);
	}
}

void handleMessage(struct epoll_event *ev)
{
	ubyte buf[300];
	ubyte *input_ptr = buf;
	while (TRUE) {
		ssize_t bytes_read = read(ev->data.fd, buf, sizeof(buf) - 1);
		if (bytes_read == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			} else {
				perror("Error reading");
				close(ev->data.fd);
				break;
			}
		} else if (bytes_read == 0) {
			printf("Client disconnected.\n");
			close(ev->data.fd);
			break;
		} else {
			buf[bytes_read] = '\0';
			printf("%ld bytes from fd: %d\n%s\n", bytes_read,
			       ev->data.fd, buf);
		}
	}
}

void startPolling(int server_fd, struct sockaddr_in *addr)
{
	struct epoll_event conn_ev, conn_evs[100];
	int nfds, epollfd, n;
	socklen_t addrlen = sizeof(*addr);

	if ((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	conn_ev.events = EPOLLIN;
	conn_ev.data.fd = server_fd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &conn_ev) == -1) {
		perror("epoll_ctl: listen_sock (server_fd)");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		signal(SIGINT, intHandler);
		nfds = epoll_wait(epollfd, conn_evs, MAX_CONNECTIONS, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; n++) {
			printf("Got new fd: %u\n", conn_evs[n].data.fd);
			if (conn_evs[n].data.fd == server_fd) {
				handleConn(epollfd, server_fd, addr,
					   &conn_evs[n]);
			} else {
				handleMessage(&conn_evs[n]);
			}
		}
	}
}

void recvSync(int *fd_socket)
{
	ubyte input_buffer[500];
	ubyte *input_ptr = input_buffer;

	int fd_conn = accept(*fd_socket, NULL, NULL);

	if (fd_conn == -1) {
		printf("Failure accepting: %d\n", errno);
		close(*fd_socket);
		exit(EXIT_FAILURE);
	}

	ssize_t bytes_wr = read(fd_conn, (ubyte *)input_buffer, 500);

	if (bytes_wr == -1) {
		printf("Failure receiving: ERROR -> %d\n", errno);
		_exit(EXIT_FAILURE);
	} else if (bytes_wr > 0) {
		// poll stuff
		printf("we at least got the bytes\n");
		char hworld[14];
		memcpy(hworld, input_buffer, 13);
		hworld[13] = '\0';

		printf("We did catch the bytes: %s\n", hworld);
		/*
                char incoming[bytes_wr * sizeof(char) + 1];
                char *iter = incoming;

                ubyte *c = input_ptr + ((bytes_wr - 1) * sizeof(byte));
                do {
                        *iter = *c;
                        c--;
                        ++iter;
                } while (c != input_ptr);
                *iter = '\0';
                printf("I got something: %s", incoming);
                printf("I got something\n");
                */
	} else if (shutdown(fd_conn, SHUT_RDWR) == -1) {
		printf("Failure shutting down\n");
		close(*fd_socket);
		close(fd_conn);
		exit(EXIT_FAILURE);
	} else {
		//close(fd_socket);
		printf("Something happend on client side. idk");
		close(fd_conn);
	}
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

	startPolling(sockfd, &addr);
	exit(EXIT_SUCCESS);
}
