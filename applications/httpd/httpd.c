#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stat.h>
#include <linux/fcntl.h>

extern int close(int fd);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern int fork(void);
extern int open(const char *pathname, int flags, ...);

static const char *DEFAULT_INDEX =
"<!DOCTYPE html>\n"
"<html>\n"
"<head><title>Welcome to SIX</title></head>\n"
"<body>\n"
"<h1>SIX Operating System</h1>\n"
"<p>Welcome to the <b>Solaris Interface eXtension</b> (SIX) running Linux 2.0.11.</p>\n"
"<hr>\n"
"<h2>Documentation & Navigation</h2>\n"
"<ul>\n"
"<li><a href=\"/tcpip.html\">TCP/IP Stack Architecture & Debugging Guide</a></li>\n"
"<li><a href=\"/about.html\">About SIX & Kernel Features</a></li>\n"
"<li><a href=\"/etc/motd\">View System Message of the Day</a></li>\n"
"<li><a href=\"/etc/hosts\">View Host Table (/etc/hosts)</a></li>\n"
"</ul>\n"
"<hr>\n"
"<p><i>Served by SIX built-in httpd over the Linux 2.0.11 TCP/IP stack.</i></p>\n"
"</body>\n"
"</html>\n";

static const char *TCPIP_PAGE =
"<!DOCTYPE html>\n"
"<html>\n"
"<head><title>SIX TCP/IP Debugging</title></head>\n"
"<body>\n"
"<h1>Debugging the Linux 2.0.11 TCP/IP Stack</h1>\n"
"<p>You are viewing this web page through the <b>in-kernel Linux 2.0.11 TCP/IP stack</b> running in user mode!</p>\n"
"<hr>\n"
"<h2>Key Kernel Breakpoints for GDB</h2>\n"
"<pre>\n"
"1. tcp_connect      - Outgoing 3-way handshake initiation\n"
"2. tcp_v4_rcv       - Incoming TCP segment demultiplexing\n"
"3. tcp_data         - TCP sliding window & data processing\n"
"4. tcp_sendmsg      - User send() buffer copying & packetization\n"
"5. ip_output        - IP header generation & routing lookup\n"
"6. loopback_xmit    - Loopback device packet delivery\n"
"7. netif_rx         - Network interface packet receive backlog\n"
"8. net_bh           - Bottom-half packet queue processing\n"
"</pre>\n"
"<hr>\n"
"<p><a href=\"/\">Back to Home</a></p>\n"
"</body>\n"
"</html>\n";

static const char *ABOUT_PAGE =
"<!DOCTYPE html>\n"
"<html>\n"
"<head><title>About SIX</title></head>\n"
"<body>\n"
"<h1>About SIX</h1>\n"
"<p>SIX is an early-2000s port of the Linux 2.0.11 kernel to run as a user-mode process.</p>\n"
"<p>Key subsystems restored and operational:</p>\n"
"<ul>\n"
"<li>Full 32-bit x86 architecture execution layer</li>\n"
"<li>Ext2 filesystem on 50MB root image</li>\n"
"<li>Linux 2.0.11 TCP/IP networking (IPv4, TCP, UDP, ICMP, ARP, Loopback)</li>\n"
"<li>Interactive Bourne shell with command completion and history</li>\n"
"<li>Rich userland: vi, rogue, advent, tetris, basic, and lynx browser</li>\n"
"</ul>\n"
"<hr>\n"
"<p><a href=\"/\">Back to Home</a></p>\n"
"</body>\n"
"</html>\n";

static void handle_client(int cfd, struct sockaddr_in *caddr)
{
	char req[512];
	char method[16], path[128], proto[16];
	char header[256];
	int n = read(cfd, req, sizeof(req) - 1);
	if (n <= 0) {
		close(cfd);
		return;
	}
	req[n] = '\0';

	method[0] = path[0] = proto[0] = '\0';
	/* Parse GET /path HTTP/1.0 */
	{
		char *p = req;
		char *m = method, *pa = path, *pr = proto;
		while (*p && *p != ' ' && m < method + sizeof(method) - 1) *m++ = *p++;
		*m = '\0';
		while (*p == ' ') p++;
		while (*p && *p != ' ' && *p != '\r' && *p != '\n' && pa < path + sizeof(path) - 1) *pa++ = *p++;
		*pa = '\0';
		while (*p == ' ') p++;
		while (*p && *p != '\r' && *p != '\n' && pr < proto + sizeof(proto) - 1) *pr++ = *p++;
		*pr = '\0';
	}

	printf("[httpd] %s %s from %s:%d\n", method, path,
	       inet_ntoa(caddr->sin_addr), ntohs(caddr->sin_port));

	if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
		const char *err = "HTTP/1.0 501 Not Implemented\r\nContent-Type: text/plain\r\n\r\nMethod Not Implemented\n";
		write(cfd, err, strlen(err));
		close(cfd);
		return;
	}

	/* Check for built-in routes */
	if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
		int fd = open("/var/www/index.html", O_RDONLY);
		if (fd >= 0) {
			char fbuf[1024];
			int r;
			sprintf(header, "HTTP/1.0 200 OK\r\nServer: SIX-httpd/1.0\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
			write(cfd, header, strlen(header));
			while ((r = read(fd, fbuf, sizeof(fbuf))) > 0)
				write(cfd, fbuf, r);
			close(fd);
		} else {
			sprintf(header, "HTTP/1.0 200 OK\r\nServer: SIX-httpd/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", (int)strlen(DEFAULT_INDEX));
			write(cfd, header, strlen(header));
			write(cfd, DEFAULT_INDEX, strlen(DEFAULT_INDEX));
		}
	} else if (strcmp(path, "/tcpip.html") == 0) {
		sprintf(header, "HTTP/1.0 200 OK\r\nServer: SIX-httpd/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", (int)strlen(TCPIP_PAGE));
		write(cfd, header, strlen(header));
		write(cfd, TCPIP_PAGE, strlen(TCPIP_PAGE));
	} else if (strcmp(path, "/about.html") == 0) {
		sprintf(header, "HTTP/1.0 200 OK\r\nServer: SIX-httpd/1.0\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", (int)strlen(ABOUT_PAGE));
		write(cfd, header, strlen(header));
		write(cfd, ABOUT_PAGE, strlen(ABOUT_PAGE));
	} else {
		char filepath[160];
		int fd;
		if (path[0] == '/') {
			if (strncmp(path, "/var/www/", 9) == 0 || strncmp(path, "/etc/", 5) == 0)
				strncpy(filepath, path, sizeof(filepath) - 1);
			else
				sprintf(filepath, "/var/www%s", path);
		} else {
			sprintf(filepath, "/var/www/%s", path);
		}
		filepath[sizeof(filepath) - 1] = '\0';

		fd = open(filepath, O_RDONLY);
		if (fd >= 0) {
			char fbuf[1024];
			int r;
			const char *ctype = "text/plain";
			if (strstr(filepath, ".htm")) ctype = "text/html";
			sprintf(header, "HTTP/1.0 200 OK\r\nServer: SIX-httpd/1.0\r\nContent-Type: %s\r\nConnection: close\r\n\r\n", ctype);
			write(cfd, header, strlen(header));
			while ((r = read(fd, fbuf, sizeof(fbuf))) > 0)
				write(cfd, fbuf, r);
			close(fd);
		} else {
			const char *notfound = "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1><p>The requested URL was not found on this SIX server.</p><hr><p><a href=\"/\">Home</a></p>\n";
			write(cfd, notfound, strlen(notfound));
		}
	}
	close(cfd);
}

int main(int argc, char **argv)
{
	int port = 80;
	int sfd;
	struct sockaddr_in saddr;
	int opt = 1;

	if (argc >= 2) {
		port = atoi(argv[1]);
		if (port <= 0) port = 80;
	}

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		printf("httpd: socket() failed\n");
		return 1;
	}

	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((unsigned short)port);
	saddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
		if (port == 80) {
			port = 8080;
			saddr.sin_port = htons(8080);
			if (bind(sfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
				printf("httpd: bind() to port 8080 failed\n");
				close(sfd);
				return 1;
			}
		} else {
			printf("httpd: bind() to port %d failed\n", port);
			close(sfd);
			return 1;
		}
	}

	if (listen(sfd, 10) < 0) {
		printf("httpd: listen() failed\n");
		close(sfd);
		return 1;
	}

	printf("SIX-httpd 1.0 listening on port %d\n", port);
	fflush(stdout);

	for (;;) {
		struct sockaddr_in caddr;
		int clen = sizeof(caddr);
		int cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
		if (cfd < 0)
			continue;
		handle_client(cfd, &caddr);
	}
	close(sfd);
	return 0;
}
