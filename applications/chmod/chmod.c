#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stat.h>

extern int chmod(const char *path, mode_t mode);

static int
parse_mode(const char *spec, mode_t cur, mode_t *out)
{
	if (spec[0] >= '0' && spec[0] <= '7') {
		mode_t v = 0;
		const char *p = spec;
		while (*p) {
			if (*p < '0' || *p > '7')
				return -1;
			v = (v << 3) | (mode_t)(*p - '0');
			p++;
		}
		*out = v;
		return 0;
	}

	{
		mode_t m = cur & 07777;
		const char *p = spec;
		while (*p) {
			int who = 0;
			char op;
			mode_t perm = 0;
			while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
				if (*p == 'u') who |= 0700;
				else if (*p == 'g') who |= 0070;
				else if (*p == 'o') who |= 0007;
				else if (*p == 'a') who |= 0777;
				p++;
			}
			if (who == 0)
				who = 0777;
			if (*p != '+' && *p != '-' && *p != '=')
				return -1;
			op = *p++;
			while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 's' || *p == 't') {
				if (*p == 'r') perm |= (0444 & who);
				else if (*p == 'w') perm |= (0222 & who);
				else if (*p == 'x') perm |= (0111 & who);
				else if (*p == 's') perm |= ((who & 0700 ? 04000 : 0) | (who & 0070 ? 02000 : 0));
				else if (*p == 't') perm |= 01000;
				p++;
			}
			if (op == '+') m |= perm;
			else if (op == '-') m &= ~perm;
			else if (op == '=') m = (m & ~who) | perm;
			if (*p == ',')
				p++;
		}
		*out = m;
		return 0;
	}
}

int
main(int argc, char **argv)
{
	int i = 1, rc = 0;
	const char *spec;

	if (argc > 1 && strcmp(argv[1], "-R") == 0)
		i++;

	if (argc - i < 2) {
		printf("Usage: chmod MODE FILE...\n");
		return 1;
	}
	spec = argv[i++];

	for (; i < argc; i++) {
		struct stat st;
		mode_t new_mode = 0;
		if (stat(argv[i], &st) < 0) {
			perror(argv[i]);
			rc = 1;
			continue;
		}
		if (parse_mode(spec, st.st_mode, &new_mode) < 0) {
			printf("chmod: invalid mode '%s'\n", spec);
			return 1;
		}
		if (chmod(argv[i], new_mode) < 0) {
			perror(argv[i]);
			rc = 1;
		}
	}
	return rc;
}
