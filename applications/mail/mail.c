/*
 * mail.c -- Classic UNIX Mail Client (POSIX / BSD mailx compatible)
 *
 * Supports:
 *   - Reading local mbox files (/var/mail/$USER)
 *   - Interactive shell (?, p, n, d, u, s, r, m, h, q, x)
 *   - Composing and sending mail locally to users or root
 *   - Remote delivery via direct SMTP (RFC 821)
 *   - Saving messages to file, replying with Re:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAIL_DIR "/var/mail"
#define DEFAULT_USER "root"
#define MAX_MSGS 512

struct Message {
	char *from_line;  /* "From sender date" mbox envelope */
	char *sender;     /* "From: ..." header */
	char *date;       /* "Date: ..." header */
	char *subject;    /* "Subject: ..." header */
	char *to;         /* "To: ..." header */
	char *raw_headers;
	char *body;
	size_t body_len;
	int is_deleted;
	int is_read;
};

static struct Message msgs[MAX_MSGS];
static int msg_count = 0;
static int cur_msg = 0;
static char current_user[64];
static char mbox_path[256];

/* -------------------------------------------------------------------------
 * Utility Functions
 * ------------------------------------------------------------------------- */

static void get_current_username(char *buf, size_t size)
{
	char *user = getenv("USER");
	if (!user || !user[0])
		user = getenv("LOGNAME");
	if (!user || !user[0]) {
		struct passwd *pw = getpwuid(getuid());
		if (pw && pw->pw_name)
			user = pw->pw_name;
	}
	if (!user || !user[0])
		user = DEFAULT_USER;
	snprintf(buf, size, "%s", user);
}

static char *trim_whitespace(char *str)
{
	char *end;
	while (isspace((unsigned char)*str)) str++;
	if (*str == 0) return str;
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) end--;
	*(end + 1) = '\0';
	return str;
}

static void rfc822_date(char *buf, size_t size)
{
	time_t now;
	struct tm *tm;
	static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

	time(&now);
	tm = localtime(&now);
	if (tm) {
		snprintf(buf, size, "%s, %d %s %d %02d:%02d:%02d +0000",
		         days[tm->tm_wday], tm->tm_mday, months[tm->tm_mon],
		         1900 + tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec);
	} else {
		snprintf(buf, size, "Sun, 20 Sep 2026 09:00:00 +0000");
	}
}

/* -------------------------------------------------------------------------
 * Mbox Parser & Loader
 * ------------------------------------------------------------------------- */

static void free_messages(void)
{
	int i;
	for (i = 0; i < msg_count; i++) {
		if (msgs[i].from_line) free(msgs[i].from_line);
		if (msgs[i].sender) free(msgs[i].sender);
		if (msgs[i].date) free(msgs[i].date);
		if (msgs[i].subject) free(msgs[i].subject);
		if (msgs[i].to) free(msgs[i].to);
		if (msgs[i].raw_headers) free(msgs[i].raw_headers);
		if (msgs[i].body) free(msgs[i].body);
	}
	memset(msgs, 0, sizeof(msgs));
	msg_count = 0;
	cur_msg = 0;
}

static int load_mbox(const char *path)
{
	FILE *fp;
	char line[2048];
	int in_header = 0;
	char header_buf[8192];
	size_t header_len = 0;
	char body_buf[65536];
	size_t body_len = 0;

	free_messages();

	fp = fopen(path, "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "From ", 5) == 0) {
			/* Finish prior message if any */
			if (in_header || body_len > 0) {
				if (msg_count < MAX_MSGS) {
					header_buf[header_len] = '\0';
					body_buf[body_len] = '\0';
					msgs[msg_count].raw_headers = strdup(header_buf);
					msgs[msg_count].body = strdup(body_buf);
					msgs[msg_count].body_len = body_len;
					msgs[msg_count].is_read = 0;
					msgs[msg_count].is_deleted = 0;
					msg_count++;
				}
				header_len = 0;
				body_len = 0;
			}
			if (msg_count < MAX_MSGS) {
				msgs[msg_count].from_line = strdup(trim_whitespace(line));
			}
			in_header = 1;
			continue;
		}

		if (in_header) {
			if (line[0] == '\n' || (line[0] == '\r' && line[1] == '\n')) {
				in_header = 0; /* blank line marks end of headers */
				continue;
			}
			if (header_len + strlen(line) < sizeof(header_buf) - 1) {
				strcpy(header_buf + header_len, line);
				header_len += strlen(line);
			}

			if (strncasecmp(line, "From:", 5) == 0 && msg_count < MAX_MSGS) {
				msgs[msg_count].sender = strdup(trim_whitespace(line + 5));
			} else if (strncasecmp(line, "Subject:", 8) == 0 && msg_count < MAX_MSGS) {
				msgs[msg_count].subject = strdup(trim_whitespace(line + 8));
			} else if (strncasecmp(line, "Date:", 5) == 0 && msg_count < MAX_MSGS) {
				msgs[msg_count].date = strdup(trim_whitespace(line + 5));
			} else if (strncasecmp(line, "To:", 3) == 0 && msg_count < MAX_MSGS) {
				msgs[msg_count].to = strdup(trim_whitespace(line + 3));
			}
		} else {
			if (body_len + strlen(line) < sizeof(body_buf) - 1) {
				strcpy(body_buf + body_len, line);
				body_len += strlen(line);
			}
		}
	}

	if (in_header || body_len > 0) {
		if (msg_count < MAX_MSGS) {
			header_buf[header_len] = '\0';
			body_buf[body_len] = '\0';
			msgs[msg_count].raw_headers = strdup(header_buf);
			msgs[msg_count].body = strdup(body_buf);
			msgs[msg_count].body_len = body_len;
			msgs[msg_count].is_read = 0;
			msgs[msg_count].is_deleted = 0;
			msg_count++;
		}
	}

	fclose(fp);
	return msg_count;
}

static void print_headers(void)
{
	int i;
	int unread = 0;

	for (i = 0; i < msg_count; i++) {
		if (!msgs[i].is_deleted && !msgs[i].is_read)
			unread++;
	}

	printf("\"%s\": %d message%s", mbox_path, msg_count, msg_count == 1 ? "" : "s");
	if (unread > 0)
		printf(" [%d unread]", unread);
	printf("\n");

	for (i = 0; i < msg_count; i++) {
		char flag = ' ';
		char current_marker = (i == cur_msg) ? '>' : ' ';

		if (msgs[i].is_deleted)
			flag = 'D';
		else if (!msgs[i].is_read)
			flag = 'N';

		printf("%c%c %2d %-20.20s %-16.16s \"%-.34s\"\n",
		       current_marker,
		       flag,
		       i + 1,
		       msgs[i].sender ? msgs[i].sender : "(unknown)",
		       msgs[i].date ? msgs[i].date : "",
		       msgs[i].subject ? msgs[i].subject : "(no subject)");
	}
}

static void print_message(int idx)
{
	if (idx < 0 || idx >= msg_count || msgs[idx].is_deleted) {
		printf("Invalid message number.\n");
		return;
	}

	msgs[idx].is_read = 1;
	cur_msg = idx;

	printf("\nMessage %d:\n", idx + 1);
	if (msgs[idx].sender)  printf("From:    %s\n", msgs[idx].sender);
	if (msgs[idx].to)      printf("To:      %s\n", msgs[idx].to);
	if (msgs[idx].date)    printf("Date:    %s\n", msgs[idx].date);
	if (msgs[idx].subject) printf("Subject: %s\n", msgs[idx].subject);
	printf("----------------------------------------------------------------------\n");
	if (msgs[idx].body)
		printf("%s", msgs[idx].body);
	printf("----------------------------------------------------------------------\n\n");
}

/* -------------------------------------------------------------------------
 * SMTP Delivery (Remote RFC 821)
 * ------------------------------------------------------------------------- */

static int smtp_send(const char *to, const char *subject, const char *from, const char *body)
{
	char host[128];
	char *at;
	int fd, port = 25;
	struct hostent *he;
	struct sockaddr_in sa;
	char buf[1024];

	at = strchr(to, '@');
	if (!at) return -1;

	snprintf(host, sizeof(host), "%s", at + 1);
	printf("Connecting to mail server %s on port %d...\n", host, port);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	he = gethostbyname(host);
	if (!he) {
		fprintf(stderr, "Could not resolve mail server for %s\n", host);
		close(fd);
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	memcpy(&sa.sin_addr, he->h_addr, he->h_length);

	alarm(5);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		alarm(0);
		perror("connect");
		close(fd);
		return -1;
	}
	alarm(0);

	/* Read greeting */
	recv(fd, buf, sizeof(buf) - 1, 0);

	/* HELO */
	snprintf(buf, sizeof(buf), "HELO six.local\r\n");
	send(fd, buf, strlen(buf), 0);
	recv(fd, buf, sizeof(buf) - 1, 0);

	/* MAIL FROM */
	snprintf(buf, sizeof(buf), "MAIL FROM:<%s>\r\n", from);
	send(fd, buf, strlen(buf), 0);
	recv(fd, buf, sizeof(buf) - 1, 0);

	/* RCPT TO */
	snprintf(buf, sizeof(buf), "RCPT TO:<%s>\r\n", to);
	send(fd, buf, strlen(buf), 0);
	recv(fd, buf, sizeof(buf) - 1, 0);

	/* DATA */
	snprintf(buf, sizeof(buf), "DATA\r\n");
	send(fd, buf, strlen(buf), 0);
	recv(fd, buf, sizeof(buf) - 1, 0);

	/* Headers + Body */
	snprintf(buf, sizeof(buf), "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n", from, to, subject);
	send(fd, buf, strlen(buf), 0);
	send(fd, body, strlen(body), 0);
	send(fd, "\r\n.\r\n", 5, 0);
	recv(fd, buf, sizeof(buf) - 1, 0);

	/* QUIT */
	send(fd, "QUIT\r\n", 6, 0);
	close(fd);
	printf("Email delivered via SMTP to %s!\n", host);
	return 0;
}

/* -------------------------------------------------------------------------
 * Local Delivery (Mbox Append)
 * ------------------------------------------------------------------------- */

static int local_deliver(const char *to_user, const char *subject, const char *from_user, const char *body)
{
	char dest_file[256];
	char date_str[64];
	time_t now;
	FILE *fp;

	mkdir(MAIL_DIR, 0777);
	snprintf(dest_file, sizeof(dest_file), "%s/%s", MAIL_DIR, to_user);

	fp = fopen(dest_file, "a");
	if (!fp) {
		perror("fopen mail file");
		return -1;
	}

	time(&now);
	rfc822_date(date_str, sizeof(date_str));

	/* Mbox separator line */
	fprintf(fp, "From %s %s", from_user, ctime(&now));
	fprintf(fp, "From: %s@six.local\n", from_user);
	fprintf(fp, "To: %s@six.local\n", to_user);
	fprintf(fp, "Date: %s\n", date_str);
	fprintf(fp, "Subject: %s\n\n", subject ? subject : "(no subject)");
	fprintf(fp, "%s", body);
	if (body[strlen(body) - 1] != '\n')
		fputc('\n', fp);
	fputc('\n', fp);

	fclose(fp);
	chmod(dest_file, 0600);
	printf("Message delivered to %s in %s\n", to_user, dest_file);
	return 0;
}

/* -------------------------------------------------------------------------
 * Compose Message
 * ------------------------------------------------------------------------- */

static void compose_mail(const char *recipient, const char *initial_subject)
{
	char to[128];
	char subject[256];
	char body[65536];
	char line[1024];
	size_t body_len = 0;

	if (recipient && recipient[0])
		snprintf(to, sizeof(to), "%s", recipient);
	else {
		printf("To: ");
		fflush(stdout);
		if (!fgets(to, sizeof(to), stdin)) return;
		to[strcspn(to, "\r\n")] = '\0';
	}
	if (!to[0]) return;

	if (initial_subject && initial_subject[0])
		snprintf(subject, sizeof(subject), "%s", initial_subject);
	else {
		printf("Subject: ");
		fflush(stdout);
		if (!fgets(subject, sizeof(subject), stdin)) return;
		subject[strcspn(subject, "\r\n")] = '\0';
	}

	printf("Enter mail, end with \".\" on a line by itself or Ctrl+D:\n");
	body[0] = '\0';

	while (fgets(line, sizeof(line), stdin)) {
		if (line[0] == '.' && (line[1] == '\n' || line[1] == '\r'))
			break;
		if (body_len + strlen(line) < sizeof(body) - 1) {
			strcpy(body + body_len, line);
			body_len += strlen(line);
		}
	}

	/* Deliver */
	if (strchr(to, '@')) {
		char from[128];
		snprintf(from, sizeof(from), "%s@six.local", current_user);
		smtp_send(to, subject, from, body);
	} else {
		local_deliver(to, subject, current_user, body);
	}
}

/* -------------------------------------------------------------------------
 * Interactive Shell Loop
 * ------------------------------------------------------------------------- */

static void save_mbox_changes(void)
{
	int i, remaining = 0;
	FILE *fp;

	for (i = 0; i < msg_count; i++) {
		if (!msgs[i].is_deleted)
			remaining++;
	}

	if (remaining == 0) {
		unlink(mbox_path);
		return;
	}

	fp = fopen(mbox_path, "w");
	if (!fp) return;

	for (i = 0; i < msg_count; i++) {
		if (!msgs[i].is_deleted) {
			if (msgs[i].from_line)
				fprintf(fp, "%s\n", msgs[i].from_line);
			else
				fprintf(fp, "From root Sun Sep 20 09:00:00 2026\n");
			if (msgs[i].raw_headers)
				fprintf(fp, "%s\n", msgs[i].raw_headers);
			if (msgs[i].body)
				fprintf(fp, "%s\n", msgs[i].body);
		}
	}
	fclose(fp);
}

static void mail_shell(void)
{
	char cmd[128];

	if (msg_count == 0) {
		printf("No mail for %s.\n", current_user);
		return;
	}

	print_headers();

	while (1) {
		printf("? ");
		fflush(stdout);

		if (!fgets(cmd, sizeof(cmd), stdin))
			break;

		cmd[strcspn(cmd, "\r\n")] = '\0';
		char *p = trim_whitespace(cmd);

		if (*p == '\0') {
			/* Read next message */
			if (cur_msg < msg_count) {
				print_message(cur_msg);
				if (cur_msg + 1 < msg_count) cur_msg++;
			} else {
				printf("At EOF\n");
			}
		} else if (isdigit((unsigned char)*p)) {
			int num = atoi(p) - 1;
			print_message(num);
		} else if (*p == 'p' || *p == 't') {
			int num = cur_msg;
			p++;
			while (isspace((unsigned char)*p)) p++;
			if (isdigit((unsigned char)*p)) num = atoi(p) - 1;
			print_message(num);
		} else if (*p == 'n') {
			if (cur_msg + 1 < msg_count) {
				cur_msg++;
				print_message(cur_msg);
			} else {
				printf("At EOF\n");
			}
		} else if (*p == 'd') {
			int num = cur_msg;
			p++;
			while (isspace((unsigned char)*p)) p++;
			if (isdigit((unsigned char)*p)) num = atoi(p) - 1;
			if (num >= 0 && num < msg_count) {
				msgs[num].is_deleted = 1;
				printf("Message %d deleted.\n", num + 1);
			}
		} else if (*p == 'u') {
			int num = cur_msg;
			p++;
			while (isspace((unsigned char)*p)) p++;
			if (isdigit((unsigned char)*p)) num = atoi(p) - 1;
			if (num >= 0 && num < msg_count) {
				msgs[num].is_deleted = 0;
				printf("Message %d undeleted.\n", num + 1);
			}
		} else if (*p == 'h') {
			print_headers();
		} else if (*p == 'r') {
			/* Reply */
			char reply_sub[300];
			char *to = msgs[cur_msg].sender ? msgs[cur_msg].sender : "root";
			snprintf(reply_sub, sizeof(reply_sub), "Re: %s",
			         msgs[cur_msg].subject ? msgs[cur_msg].subject : "");
			compose_mail(to, reply_sub);
		} else if (*p == 'm') {
			p++;
			while (isspace((unsigned char)*p)) p++;
			compose_mail(p, NULL);
		} else if (*p == 's') {
			char file_path[128] = "mbox";
			FILE *out;
			p++;
			while (isspace((unsigned char)*p)) p++;
			if (*p) snprintf(file_path, sizeof(file_path), "%s", p);
			out = fopen(file_path, "a");
			if (out && cur_msg < msg_count) {
				fprintf(out, "%s\n%s\n%s\n",
				        msgs[cur_msg].from_line ? msgs[cur_msg].from_line : "",
				        msgs[cur_msg].raw_headers ? msgs[cur_msg].raw_headers : "",
				        msgs[cur_msg].body ? msgs[cur_msg].body : "");
				fclose(out);
				printf("\"%s\" [Appended]\n", file_path);
			} else {
				printf("Error appending to %s\n", file_path);
			}
		} else if (*p == 'q') {
			save_mbox_changes();
			break;
		} else if (*p == 'x') {
			/* Exit without saving changes */
			break;
		} else if (*p == '?' || strcmp(p, "help") == 0) {
			printf("Mail commands:\n");
			printf("  <num>       Print message number <num>\n");
			printf("  p [num]     Print message\n");
			printf("  n           Next message\n");
			printf("  d [num]     Delete message\n");
			printf("  u [num]     Undelete message\n");
			printf("  s [file]    Save message to file (default: mbox)\n");
			printf("  r           Reply to current message\n");
			printf("  m <user>    Compose new message to <user>\n");
			printf("  h           Display headers\n");
			printf("  q           Quit, saving changes\n");
			printf("  x           Exit immediately without changes\n");
		} else {
			printf("Unknown command: %s. Type '?' for help.\n", p);
		}
	}
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	char *subject = NULL;
	char *target_user = NULL;
	int i;

	get_current_username(current_user, sizeof(current_user));
	snprintf(mbox_path, sizeof(mbox_path), "%s/%s", MAIL_DIR, current_user);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			subject = argv[++i];
		} else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
			snprintf(mbox_path, sizeof(mbox_path), "%s", argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("Usage: mail [-s subject] [recipient]\n");
			printf("       mail [-f mbox_file]\n");
			return 0;
		} else if (argv[i][0] != '-') {
			target_user = argv[i];
		}
	}

	if (target_user) {
		compose_mail(target_user, subject);
		return 0;
	}

	load_mbox(mbox_path);
	mail_shell();
	return 0;
}
