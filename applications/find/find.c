/*
 * find.c - Search for files in a directory hierarchy for SIX
 *
 * Usage:
 *   find [path...] [predicates]
 *
 * Predicates:
 *   -name <pattern>   Match file basename against shell glob pattern
 *   -iname <pattern>  Case-insensitive pattern match
 *   -type <c>         Match file type (f=file, d=dir, l=link, c=char, b=block, p=fifo, s=sock)
 *   -size [+-]n[ck]   Match file size (c=bytes, k=kilobytes, default=512-byte blocks)
 *   -maxdepth <n>     Descend at most n levels of directories
 *   -mindepth <n>     Do not evaluate tests/actions at levels less than n
 *   -print            Print matching path (default action)
 *   -exec cmd ... {} \; Execute command on each match
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>

#define MAX_DEPTH 64

/* Glob matching */
static int glob_match(const char *pat, const char *str, int icase)
{
	while (*pat) {
		if (*pat == '*') {
			pat++;
			if (!*pat) return 1; /* trailing * matches everything */
			while (*str) {
				if (glob_match(pat, str, icase))
					return 1;
				str++;
			}
			return 0;
		} else if (*pat == '?') {
			if (!*str) return 0;
			pat++;
			str++;
		} else if (*pat == '[') {
			pat++;
			int negate = (*pat == '!' || *pat == '^');
			if (negate) pat++;
			int match = 0;
			while (*pat && *pat != ']') {
				if (*(pat + 1) == '-' && *(pat + 2) && *(pat + 2) != ']') {
					char c1 = *pat;
					char c2 = *(pat + 2);
					char cur = *str;
					if (icase) {
						c1 = tolower((unsigned char)c1);
						c2 = tolower((unsigned char)c2);
						cur = tolower((unsigned char)cur);
					}
					if (cur >= c1 && cur <= c2) match = 1;
					pat += 3;
				} else {
					char c1 = *pat++;
					char cur = *str;
					if (icase) {
						c1 = tolower((unsigned char)c1);
						cur = tolower((unsigned char)cur);
					}
					if (cur == c1) match = 1;
				}
			}
			if (*pat == ']') pat++;
			if (negate) match = !match;
			if (!match || !*str) return 0;
			str++;
		} else {
			char c1 = *pat++;
			char c2 = *str++;
			if (icase) {
				c1 = tolower((unsigned char)c1);
				c2 = tolower((unsigned char)c2);
			}
			if (c1 != c2) return 0;
		}
	}
	return (*str == '\0');
}

/* Criteria options */
static const char *opt_name = NULL;
static int opt_iname = 0;
static char opt_type = 0;
static int opt_maxdepth = 999999;
static int opt_mindepth = 0;

static int opt_size_mode = 0; /* 0: none, 1: exact, 2: greater (+), 3: less (-) */
static off_t opt_size_val = 0;

static char **exec_argv = NULL;
static int exec_argc = 0;
static int action_specified = 0;

static int evaluate_file(const char *path, const char *name, const struct stat *st, int depth)
{
	if (depth < opt_mindepth)
		return 0;

	/* -name / -iname */
	if (opt_name) {
		if (!glob_match(opt_name, name, opt_iname))
			return 0;
	}

	/* -type */
	if (opt_type) {
		switch (opt_type) {
		case 'f': if (!S_ISREG(st->st_mode)) return 0; break;
		case 'd': if (!S_ISDIR(st->st_mode)) return 0; break;
		case 'l': if (!S_ISLNK(st->st_mode)) return 0; break;
		case 'c': if (!S_ISCHR(st->st_mode)) return 0; break;
		case 'b': if (!S_ISBLK(st->st_mode)) return 0; break;
		case 'p': if (!S_ISFIFO(st->st_mode)) return 0; break;
		case 's': if (!S_ISSOCK(st->st_mode)) return 0; break;
		default: return 0;
		}
	}

	/* -size */
	if (opt_size_mode) {
		off_t sz = st->st_size;
		if (opt_size_mode == 1 && sz != opt_size_val) return 0;
		if (opt_size_mode == 2 && sz <= opt_size_val) return 0;
		if (opt_size_mode == 3 && sz >= opt_size_val) return 0;
	}

	return 1;
}

static void execute_actions(const char *path)
{
	if (exec_argv) {
		/* Allocate argv replacement */
		char **argv_copy = malloc((exec_argc + 1) * sizeof(char *));
		int i;
		for (i = 0; i < exec_argc; i++) {
			if (strcmp(exec_argv[i], "{}") == 0) {
				argv_copy[i] = (char *)path;
			} else {
				argv_copy[i] = exec_argv[i];
			}
		}
		argv_copy[exec_argc] = NULL;

		pid_t pid = fork();
		if (pid == 0) {
			execvp(argv_copy[0], argv_copy);
			fprintf(stderr, "find: exec failed: %s\n", argv_copy[0]);
			exit(127);
		} else if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
		}
		free(argv_copy);
	} else {
		/* Default -print */
		printf("%s\n", path);
	}
}

static void traverse_dir(const char *path, int depth)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		fprintf(stderr, "find: cannot stat %s\n", path);
		return;
	}

	const char *base = strrchr(path, '/');
	base = base ? (base + 1) : path;
	if (!*base) base = path;

	if (evaluate_file(path, base, &st, depth)) {
		execute_actions(path);
	}

	if (!S_ISDIR(st.st_mode) || depth >= opt_maxdepth)
		return;

	DIR *dir = opendir(path);
	if (!dir) {
		fprintf(stderr, "find: cannot open directory %s\n", path);
		return;
	}

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		char subpath[1024];
		if (strcmp(path, "/") == 0)
			snprintf(subpath, sizeof(subpath), "/%s", ent->d_name);
		else
			snprintf(subpath, sizeof(subpath), "%s/%s", path, ent->d_name);

		traverse_dir(subpath, depth + 1);
	}

	closedir(dir);
}

int main(int argc, char **argv)
{
	char *roots[256];
	int num_roots = 0;

	int i = 1;
	while (i < argc && argv[i][0] != '-') {
		roots[num_roots++] = argv[i++];
	}

	if (num_roots == 0) {
		roots[num_roots++] = ".";
	}

	while (i < argc) {
		if (strcmp(argv[i], "-name") == 0 || strcmp(argv[i], "-iname") == 0) {
			opt_iname = (strcmp(argv[i], "-iname") == 0);
			if (i + 1 >= argc) {
				fprintf(stderr, "find: missing argument to %s\n", argv[i]);
				return 1;
			}
			opt_name = argv[++i];
		} else if (strcmp(argv[i], "-type") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "find: missing argument to -type\n", argv[i]);
				return 1;
			}
			opt_type = argv[++i][0];
		} else if (strcmp(argv[i], "-size") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "find: missing argument to -size\n");
				return 1;
			}
			const char *s = argv[++i];
			if (*s == '+') {
				opt_size_mode = 2;
				s++;
			} else if (*s == '-') {
				opt_size_mode = 3;
				s++;
			} else {
				opt_size_mode = 1;
			}
			char *endp;
			off_t val = strtol(s, &endp, 10);
			if (*endp == 'c') {
				opt_size_val = val;
			} else if (*endp == 'k') {
				opt_size_val = val * 1024;
			} else {
				opt_size_val = val * 512;
			}
		} else if (strcmp(argv[i], "-maxdepth") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "find: missing argument to -maxdepth\n");
				return 1;
			}
			opt_maxdepth = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-mindepth") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "find: missing argument to -mindepth\n");
				return 1;
			}
			opt_mindepth = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-print") == 0) {
			action_specified = 1;
		} else if (strcmp(argv[i], "-exec") == 0) {
			action_specified = 1;
			i++;
			int start_exec = i;
			while (i < argc && strcmp(argv[i], ";") != 0) {
				i++;
			}
			if (i >= argc) {
				fprintf(stderr, "find: missing ';' to -exec\n");
				return 1;
			}
			exec_argc = i - start_exec;
			exec_argv = malloc((exec_argc + 1) * sizeof(char *));
			int k;
			for (k = 0; k < exec_argc; k++) {
				exec_argv[k] = argv[start_exec + k];
			}
			exec_argv[exec_argc] = NULL;
		} else {
			fprintf(stderr, "find: unknown predicate '%s'\n", argv[i]);
			return 1;
		}
		i++;
	}

	for (i = 0; i < num_roots; i++) {
		traverse_dir(roots[i], 0);
	}

	return 0;
}
