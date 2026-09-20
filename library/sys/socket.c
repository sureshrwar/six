#include <sys/socket.h>

extern int socketcall(int call, unsigned long *args);

int socket(int domain, int type, int protocol)
{
	unsigned long args[3];
	args[0] = (unsigned long)domain;
	args[1] = (unsigned long)type;
	args[2] = (unsigned long)protocol;
	return socketcall(SYS_SOCKET, args);
}

int bind(int sockfd, const struct sockaddr *addr, int addrlen)
{
	unsigned long args[3];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)addr;
	args[2] = (unsigned long)addrlen;
	return socketcall(SYS_BIND, args);
}

int connect(int sockfd, const struct sockaddr *addr, int addrlen)
{
	unsigned long args[3];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)addr;
	args[2] = (unsigned long)addrlen;
	return socketcall(SYS_CONNECT, args);
}

int listen(int sockfd, int backlog)
{
	unsigned long args[2];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)backlog;
	return socketcall(SYS_LISTEN, args);
}

int accept(int sockfd, struct sockaddr *addr, int *addrlen)
{
	unsigned long args[3];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)addr;
	args[2] = (unsigned long)addrlen;
	return socketcall(SYS_ACCEPT, args);
}

int getsockname(int sockfd, struct sockaddr *addr, int *addrlen)
{
	unsigned long args[3];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)addr;
	args[2] = (unsigned long)addrlen;
	return socketcall(SYS_GETSOCKNAME, args);
}

int getpeername(int sockfd, struct sockaddr *addr, int *addrlen)
{
	unsigned long args[3];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)addr;
	args[2] = (unsigned long)addrlen;
	return socketcall(SYS_GETPEERNAME, args);
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
	unsigned long args[4];
	args[0] = (unsigned long)domain;
	args[1] = (unsigned long)type;
	args[2] = (unsigned long)protocol;
	args[3] = (unsigned long)sv;
	return socketcall(SYS_SOCKETPAIR, args);
}

int send(int sockfd, const void *buf, int len, int flags)
{
	unsigned long args[4];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)buf;
	args[2] = (unsigned long)len;
	args[3] = (unsigned long)flags;
	return socketcall(SYS_SEND, args);
}

int recv(int sockfd, void *buf, int len, int flags)
{
	unsigned long args[4];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)buf;
	args[2] = (unsigned long)len;
	args[3] = (unsigned long)flags;
	return socketcall(SYS_RECV, args);
}

int sendto(int sockfd, const void *buf, int len, int flags, const struct sockaddr *dest_addr, int addrlen)
{
	unsigned long args[6];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)buf;
	args[2] = (unsigned long)len;
	args[3] = (unsigned long)flags;
	args[4] = (unsigned long)dest_addr;
	args[5] = (unsigned long)addrlen;
	return socketcall(SYS_SENDTO, args);
}

int recvfrom(int sockfd, void *buf, int len, int flags, struct sockaddr *src_addr, int *addrlen)
{
	unsigned long args[6];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)buf;
	args[2] = (unsigned long)len;
	args[3] = (unsigned long)flags;
	args[4] = (unsigned long)src_addr;
	args[5] = (unsigned long)addrlen;
	return socketcall(SYS_RECVFROM, args);
}

int shutdown(int sockfd, int how)
{
	unsigned long args[2];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)how;
	return socketcall(SYS_SHUTDOWN, args);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, int optlen)
{
	unsigned long args[5];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)level;
	args[2] = (unsigned long)optname;
	args[3] = (unsigned long)optval;
	args[4] = (unsigned long)optlen;
	return socketcall(SYS_SETSOCKOPT, args);
}

int getsockopt(int sockfd, int level, int optname, void *optval, int *optlen)
{
	unsigned long args[5];
	args[0] = (unsigned long)sockfd;
	args[1] = (unsigned long)level;
	args[2] = (unsigned long)optname;
	args[3] = (unsigned long)optval;
	args[4] = (unsigned long)optlen;
	return socketcall(SYS_GETSOCKOPT, args);
}
