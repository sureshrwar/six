#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <mntent.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <wchar.h>

ssize_t pread(int fd, void *buf, size_t count, long long offset)
{
	if (lseek(fd, (off_t)offset, SEEK_SET) == (off_t)-1)
		return -1;
	return read(fd, buf, count);
}

ssize_t pwrite(int fd, const void *buf, size_t count, long long offset)
{
	if (lseek(fd, (off_t)offset, SEEK_SET) == (off_t)-1)
		return -1;
	return write(fd, buf, count);
}

int fdatasync(int fd)
{
	return fsync(fd);
}

int getpagesize(void)
{
	return 4096;
}

int ffs(int i)
{
	return __builtin_ffs(i);
}

int execle(const char *path, const char *arg, ...)
{
	char *argv[32];
	char * const *envp;
	va_list ap;
	int i = 0;

	argv[i++] = (char *)arg;
	va_start(ap, arg);
	while (i < 31 && (argv[i] = va_arg(ap, char *)) != NULL)
		i++;
	argv[i] = NULL;
	envp = va_arg(ap, char * const *);
	va_end(ap);
	return execve((char *)path, argv, (char **)envp);
}

long sysconf(int name)
{
	if (name == _SC_PAGESIZE)
		return 4096;
	return -1;
}

int daemon(int nochdir, int noclose)
{
	int pid = fork();
	if (pid < 0)
		return -1;
	if (pid > 0)
		_exit(0);
	setsid();
	if (!nochdir)
		chdir("/");
	if (!noclose) {
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
	}
	return 0;
}

char *strsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return NULL;
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return tok;
			}
		} while (sc != 0);
	}
}

char *stpcpy(char *dest, const char *src)
{
	while ((*dest = *src++) != '\0')
		dest++;
	return dest;
}

char *realpath(const char *path, char *resolved_path)
{
	char *out;
	if (!path) {
		errno = EINVAL;
		return NULL;
	}
	out = resolved_path ? resolved_path : (char *)malloc(PATH_MAX);
	if (!out)
		return NULL;
	if (path[0] == '/') {
		strncpy(out, path, PATH_MAX - 1);
		out[PATH_MAX - 1] = '\0';
	} else {
		if (!getcwd(out, PATH_MAX / 2)) {
			strcpy(out, "/");
		}
		if (strcmp(out, "/") != 0)
			strcat(out, "/");
		strncat(out, path, PATH_MAX - strlen(out) - 1);
	}
	return out;
}

char *mkdtemp(char *template)
{
	size_t len;
	int i;
	if (!template) {
		errno = EINVAL;
		return NULL;
	}
	len = strlen(template);
	if (len < 6 || strcmp(template + len - 6, "XXXXXX") != 0) {
		errno = EINVAL;
		return NULL;
	}
	for (i = 0; i < 100; i++) {
		snprintf(template + len - 6, 7, "%06d", (getpid() * 100 + i) % 1000000);
		if (mkdir(template, 0700) == 0)
			return template;
	}
	return NULL;
}

long random(void)
{
	return rand();
}

void srandom(unsigned int seed)
{
	srand(seed);
}

int umount2(const char *target, int flags)
{
	(void)flags;
	return umount((char *)target);
}

int clock_gettime(int clk_id, struct timespec *tp)
{
	struct timeval tv;
	(void)clk_id;
	if (!tp) {
		errno = EINVAL;
		return -1;
	}
	if (gettimeofday(&tv, NULL) < 0)
		return -1;
	tp->tv_sec = tv.tv_sec;
	tp->tv_nsec = tv.tv_usec * 1000;
	return 0;
}

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	if (ruid) *ruid = getuid();
	if (euid) *euid = geteuid();
	if (suid) *suid = geteuid();
	return 0;
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	if (rgid) *rgid = getgid();
	if (egid) *egid = getegid();
	if (sgid) *sgid = getegid();
	return 0;
}

int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	(void)ruid; (void)suid;
	if (euid != (uid_t)-1)
		return setuid(euid);
	return 0;
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	(void)rgid; (void)sgid;
	if (egid != (gid_t)-1)
		return setgid(egid);
	return 0;
}

int setfsuid(uid_t fsuid) { (void)fsuid; return 0; }
int setfsgid(gid_t fsgid) { (void)fsgid; return 0; }

char *basename(char *path)
{
	char *p;
	if (!path || !*path)
		return ".";
	p = strrchr(path, '/');
	return p ? p + 1 : path;
}

char *dirname(char *path)
{
	static char dot[] = ".";
	char *p;
	if (!path || !*path)
		return dot;
	p = strrchr(path, '/');
	if (!p)
		return dot;
	if (p == path) {
		path[1] = '\0';
		return path;
	}
	*p = '\0';
	return path;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	nfds_t i;
	(void)timeout;
	for (i = 0; i < nfds; i++)
		fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
	return (int)nfds;
}

int statvfs(const char *path, struct statvfs *buf)
{
	(void)path;
	if (!buf) {
		errno = EINVAL;
		return -1;
	}
	memset(buf, 0, sizeof(*buf));
	buf->f_bsize = 4096;
	buf->f_frsize = 4096;
	buf->f_namemax = 255;
	return 0;
}

int fstatvfs(int fd, struct statvfs *buf)
{
	(void)fd;
	return statvfs("/", buf);
}

/* mntent helpers */
FILE *setmntent(const char *filename, const char *type)
{
	return fopen(filename, type);
}

struct mntent *getmntent(FILE *stream)
{
	static struct mntent m;
	static char line[512];
	char *p;
	if (!stream)
		return NULL;
	while (fgets(line, sizeof(line), stream)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		p = line;
		m.mnt_fsname = strsep(&p, " \t\n");
		m.mnt_dir    = strsep(&p, " \t\n");
		m.mnt_type   = strsep(&p, " \t\n");
		m.mnt_opts   = strsep(&p, " \t\n");
		m.mnt_freq   = 0;
		m.mnt_passno = 0;
		if (m.mnt_fsname && m.mnt_dir && m.mnt_type)
			return &m;
	}
	return NULL;
}

int addmntent(FILE *stream, const struct mntent *mnt)
{
	if (!stream || !mnt)
		return 1;
	fprintf(stream, "%s %s %s %s %d %d\n",
		mnt->mnt_fsname ? mnt->mnt_fsname : "none",
		mnt->mnt_dir ? mnt->mnt_dir : "/",
		mnt->mnt_type ? mnt->mnt_type : "fuse",
		mnt->mnt_opts ? mnt->mnt_opts : "rw",
		mnt->mnt_freq, mnt->mnt_passno);
	return 0;
}

int endmntent(FILE *streamp)
{
	if (streamp)
		fclose(streamp);
	return 1;
}

char *hasmntopt(const struct mntent *mnt, const char *opt)
{
	if (!mnt || !mnt->mnt_opts || !opt)
		return NULL;
	return strstr(mnt->mnt_opts, opt);
}

/* getopt_long */
int getopt_long(int argc, char * const argv[], const char *optstring,
		const struct option *longopts, int *longindex)
{
	int return_in_order = (optstring && optstring[0] == '-');
	const char *short_opts = return_in_order ? (optstring + 1) : optstring;

	if (optind >= argc || !argv[optind])
		return -1;

	if (strcmp(argv[optind], "--") == 0) {
		optind++;
		return -1;
	}

	if (argv[optind][0] != '-' || argv[optind][1] == '\0') {
		if (return_in_order) {
			optarg = argv[optind++];
			return 1;
		}
		return -1;
	}

	if (argv[optind][0] == '-' && argv[optind][1] == '-' && argv[optind][2] != '\0') {
		const char *arg = argv[optind] + 2;
		const char *eq = strchr(arg, '=');
		size_t namelen = eq ? (size_t)(eq - arg) : strlen(arg);
		int i;
		if (longopts) {
			for (i = 0; longopts[i].name; i++) {
				if (strncmp(longopts[i].name, arg, namelen) == 0 &&
				    longopts[i].name[namelen] == '\0') {
					if (longindex)
						*longindex = i;
					optind++;
					if (longopts[i].has_arg == required_argument) {
						if (eq)
							optarg = (char *)(eq + 1);
						else if (optind < argc)
							optarg = argv[optind++];
					} else if (longopts[i].has_arg == optional_argument) {
						optarg = eq ? (char *)(eq + 1) : NULL;
					} else {
						optarg = NULL;
					}
					if (longopts[i].flag) {
						*longopts[i].flag = longopts[i].val;
						return 0;
					}
					return longopts[i].val;
				}
			}
		}
		optind++;
		return '?';
	}
	return getopt(argc, argv, (char *)short_opts);
}

int getopt_long_only(int argc, char * const argv[], const char *optstring,
		     const struct option *longopts, int *longindex)
{
	return getopt_long(argc, argv, optstring, longopts, longindex);
}

/* Multibyte / UTF-8 helpers for libntfs-3g/unistr.c */
int mbsinit(const mbstate_t *ps)
{
	(void)ps;
	return 1;
}

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)
{
	unsigned char c;
	(void)ps;
	if (!s)
		return 0;
	if (n == 0)
		return (size_t)-2;
	c = (unsigned char)s[0];
	if (c == 0) {
		if (pwc) *pwc = 0;
		return 0;
	}
	if (c < 0x80) {
		if (pwc) *pwc = (wchar_t)c;
		return 1;
	}
	if ((c & 0xe0) == 0xc0 && n >= 2) {
		if (pwc) *pwc = ((c & 0x1f) << 6) | ((unsigned char)s[1] & 0x3f);
		return 2;
	}
	if ((c & 0xf0) == 0xe0 && n >= 3) {
		if (pwc) *pwc = ((c & 0x0f) << 12) | (((unsigned char)s[1] & 0x3f) << 6) | ((unsigned char)s[2] & 0x3f);
		return 3;
	}
	if (pwc) *pwc = (wchar_t)c;
	return 1;
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
	unsigned int u = (unsigned int)wc;
	(void)ps;
	if (!s)
		return 1;
	if (u < 0x80) {
		s[0] = (char)u;
		return 1;
	}
	if (u < 0x800) {
		s[0] = (char)(0xc0 | (u >> 6));
		s[1] = (char)(0x80 | (u & 0x3f));
		return 2;
	}
	s[0] = (char)(0xe0 | (u >> 12));
	s[1] = (char)(0x80 | ((u >> 6) & 0x3f));
	s[2] = (char)(0x80 | (u & 0x3f));
	return 3;
}

size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps)
{
	const char *s = *src;
	size_t count = 0;
	while (!dst || count < len) {
		wchar_t wc;
		size_t r = mbrtowc(&wc, s, 4, ps);
		if (r == (size_t)-1 || r == (size_t)-2)
			return (size_t)-1;
		if (dst)
			dst[count] = wc;
		if (r == 0) {
			s = NULL;
			break;
		}
		s += r;
		count++;
	}
	*src = s;
	return count;
}

/* Syslog stubs */
void openlog(const char *ident, int option, int facility)
{
	(void)ident; (void)option; (void)facility;
}

void vsyslog(int priority, const char *format, va_list ap)
{
	(void)priority; (void)format; (void)ap;
}

void closelog(void)
{
}

/* Single-threaded pthread no-op stubs */
static void *tls_slots[8];
static int tls_next_key = 0;

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)m; (void)a; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t *a) { (void)a; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { (void)a; (void)t; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }

int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *a) { (void)l; (void)a; return 0; }
int pthread_rwlock_destroy(pthread_rwlock_t *l) { (void)l; return 0; }
int pthread_rwlock_rdlock(pthread_rwlock_t *l) { (void)l; return 0; }
int pthread_rwlock_wrlock(pthread_rwlock_t *l) { (void)l; return 0; }
int pthread_rwlock_unlock(pthread_rwlock_t *l) { (void)l; return 0; }

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)c; (void)a; return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) { (void)c; (void)m; (void)t; return 0; }

int pthread_key_create(pthread_key_t *key, void (*destr)(void *))
{
	(void)destr;
	if (tls_next_key >= 8)
		return EAGAIN;
	*key = tls_next_key++;
	tls_slots[*key] = NULL;
	return 0;
}

int pthread_key_delete(pthread_key_t key)
{
	if (key >= 0 && key < 8)
		tls_slots[key] = NULL;
	return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
	if (key >= 0 && key < 8)
		return tls_slots[key];
	return NULL;
}

int pthread_setspecific(pthread_key_t key, const void *val)
{
	if (key >= 0 && key < 8)
		tls_slots[key] = (void *)val;
	return 0;
}

pthread_t pthread_self(void) { return 1; }
int pthread_kill(pthread_t thread, int sig) { (void)thread; return kill(getpid(), sig); }
