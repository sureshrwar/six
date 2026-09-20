#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int close(int fd);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern int fork(void);
extern int sleep(unsigned int seconds);
extern int waitpid(int pid, int *status, int options);

static int run_server(int port)
{
	int sfd, cfd;
	struct sockaddr_in saddr, caddr;
	int clen = sizeof(caddr);
	char buf[128];
	int n;

	printf("[server] Creating TCP socket...\n");
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		printf("[server] socket() failed: %d\n", sfd);
		return 1;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((unsigned short)port);
	saddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	printf("[server] Binding to 127.0.0.1:%d...\n", port);
	if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		printf("[server] bind() failed\n");
		close(sfd);
		return 1;
	}

	printf("[server] Listening...\n");
	if (listen(sfd, 5) < 0) {
		printf("[server] listen() failed\n");
		close(sfd);
		return 1;
	}

	printf("[server] Waiting for client (accept)...\n");
	cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
	if (cfd < 0) {
		printf("[server] accept() failed: %d\n", cfd);
		close(sfd);
		return 1;
	}

	printf("[server] Client connected! Reading request...\n");
	n = read(cfd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		printf("[server] Received: '%s'\n", buf);
	}

	printf("[server] Sending reply...\n");
	write(cfd, "HELLO_FROM_TCP_SERVER\n", 22);

	close(cfd);
	close(sfd);
	printf("[server] Server finished successfully.\n");
	return 0;
}

static int run_client(const char *ip, int port)
{
	int sfd;
	struct sockaddr_in saddr;
	char buf[128];
	int n;

	printf("[client] Creating TCP socket...\n");
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		printf("[client] socket() failed: %d\n", sfd);
		return 1;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((unsigned short)port);
	saddr.sin_addr.s_addr = inet_addr(ip);

	printf("[client] Connecting to %s:%d...\n", ip, port);
	if (connect(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		printf("[client] connect() failed\n");
		close(sfd);
		return 1;
	}

	printf("[client] Connected! Sending message...\n");
	write(sfd, "PING_FROM_CLIENT\n", 17);

	printf("[client] Reading response...\n");
	n = read(sfd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		printf("[client] Received reply: '%s'\n", buf);
	}

	close(sfd);
	printf("[client] Client finished successfully.\n");
	return 0;
}

int main(int argc, char **argv)
{
	int port = 8080;
	if (argc >= 2 && strcmp(argv[1], "server") == 0) {
		if (argc >= 3) port = atoi(argv[2]);
		return run_server(port);
	} else if (argc >= 2 && strcmp(argv[1], "client") == 0) {
		const char *ip = "127.0.0.1";
		if (argc >= 3) ip = argv[2];
		if (argc >= 4) port = atoi(argv[3]);
		return run_client(ip, port);
	} else {
		int pid = fork();
		if (pid < 0) {
			printf("fork failed\n");
			return 1;
		}
		if (pid == 0) {
			return run_server(port);
		} else {
			int status;
			sleep(1);
			run_client("127.0.0.1", port);
			waitpid(pid, &status, 0);
			printf("[nettest] TCP LOOPBACK TEST SUCCESSFUL!\n");
			return 0;
		}
	}
}
