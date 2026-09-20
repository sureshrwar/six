#ifndef _UNISTD_H
#define _UNISTD_H

#include <linux/types.h>

#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

#ifndef SEEK_SET
#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2
#endif

#ifndef F_OK
#define F_OK            0
#define X_OK            1
#define W_OK            2
#define R_OK            4
#endif

int access(const char *pathname, int mode);
int chdir(const char *path);
int close(int fd);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int execv(const char *path, char *const argv[]);
int execve(const char *filename, char *const argv[], char *const envp[]);
int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execvp(const char *file, char *const argv[]);
void _exit(int status);
pid_t fork(void);
char *getcwd(char *buf, size_t size);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int isatty(int fd);
int link(const char *oldpath, const char *newpath);
off_t lseek(int fd, off_t offset, int whence);
int pipe(int pipefd[2]);
ssize_t read(int fd, void *buf, size_t count);
int rmdir(const char *pathname);
unsigned int sleep(unsigned int seconds);
int unlink(const char *pathname);
ssize_t write(int fd, const void *buf, size_t count);

#endif
