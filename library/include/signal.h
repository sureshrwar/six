#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <linux/types.h>
#include <linux/signal.h>

int kill(pid_t pid, int sig);
int raise(int sig);
__sighandler_t signal(int signum, __sighandler_t handler);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);

#endif
