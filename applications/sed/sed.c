/*
 * sed.c - Stream editor for SIX
 *
 * Supports:
 *   -n               Suppress default output
 *   -e script        Add script commands
 *
 * Commands:
 *   s/pat/repl/[g][p] Substitute pattern with replacement
 *   d                 Delete pattern space
 *   p                 Print pattern space
 *   q                 Quit
 *   y/src/dst/        Transliterate characters
 *
 * Addressing:
 *   N                 Line number N
 *   $                 Last line
 *   /pattern/         Lines matching pattern
 *   addr1,addr2       Address range
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <regexp.h>

void regerror(char *s)
{
	/* Suppress or log internal regexp errors */
}

typedef enum {
	ADDR_NONE,
	ADDR_NUM,
	ADDR_LAST,
	ADDR_RE
} addr_type_t;

typedef struct {
	addr_type_t type;
	long num;
	regexp *re;
} addr_t;

typedef enum {
	CMD_SUBST,
	CMD_DELETE,
	CMD_PRINT,
	CMD_QUIT,
	CMD_Y
} cmd_type_t;

typedef struct sed_cmd {
	addr_t a1;
	addr_t a2;
	int has_a2;
	int in_range;

	cmd_type_t cmd;

	/* For CMD_SUBST */
	regexp *sub_re;
	char *sub_repl;
	int sub_global;
	int sub_print;

	/* For CMD_Y */
	unsigned char y_map[256];

	struct sed_cmd *next;
} sed_cmd_t;

static sed_cmd_t *cmd_list = NULL;
static sed_cmd_t **cmd_tail = &cmd_list;
static int opt_n = 0;

static const char *parse_addr(const char *p, addr_t *a)
{
	while (*p && isspace((unsigned char)*p)) p++;
	if (!*p) {
		a->type = ADDR_NONE;
		return p;
	}

	if (isdigit((unsigned char)*p)) {
		a->type = ADDR_NUM;
		a->num = strtol(p, (char **)&p, 10);
		return p;
	}

	if (*p == '$') {
		a->type = ADDR_LAST;
		return p + 1;
	}

	if (*p == '/') {
		p++;
		const char *start = p;
		while (*p && *p != '/') {
			if (*p == '\\' && *(p + 1)) p += 2;
			else p++;
		}
		int len = p - start;
		char *pat = malloc(len + 1);
		memcpy(pat, start, len);
		pat[len] = '\0';
		if (*p == '/') p++;

		a->type = ADDR_RE;
		a->re = regcomp(pat);
		free(pat);
		return p;
	}

	a->type = ADDR_NONE;
	return p;
}

static void add_cmd(sed_cmd_t *c)
{
	*cmd_tail = c;
	cmd_tail = &c->next;
}

static int parse_script(const char *script)
{
	const char *p = script;

	while (*p) {
		while (*p && (isspace((unsigned char)*p) || *p == ';')) p++;
		if (!*p) break;

		sed_cmd_t *c = calloc(1, sizeof(sed_cmd_t));

		p = parse_addr(p, &c->a1);
		while (*p && isspace((unsigned char)*p)) p++;

		if (*p == ',') {
			p++;
			c->has_a2 = 1;
			p = parse_addr(p, &c->a2);
			while (*p && isspace((unsigned char)*p)) p++;
		}

		if (!*p) {
			free(c);
			break;
		}

		char op = *p++;
		if (op == 's') {
			c->cmd = CMD_SUBST;
			char delim = *p++;
			if (!delim) {
				fprintf(stderr, "sed: missing delimiter for s command\n");
				return -1;
			}
			/* Parse pattern */
			const char *pstart = p;
			while (*p && *p != delim) {
				if (*p == '\\' && *(p + 1)) p += 2;
				else p++;
			}
			int plen = p - pstart;
			char *pat = malloc(plen + 1);
			memcpy(pat, pstart, plen);
			pat[plen] = '\0';
			c->sub_re = regcomp(pat);
			free(pat);

			if (*p == delim) p++;

			/* Parse replacement */
			const char *rstart = p;
			while (*p && *p != delim) {
				if (*p == '\\' && *(p + 1)) p += 2;
				else p++;
			}
			int rlen = p - rstart;
			c->sub_repl = malloc(rlen + 1);
			memcpy(c->sub_repl, rstart, rlen);
			c->sub_repl[rlen] = '\0';

			if (*p == delim) p++;

			/* Flags */
			while (*p && *p != ';' && *p != '\n' && !isspace((unsigned char)*p)) {
				if (*p == 'g') c->sub_global = 1;
				else if (*p == 'p') c->sub_print = 1;
				p++;
			}
		} else if (op == 'd') {
			c->cmd = CMD_DELETE;
		} else if (op == 'p') {
			c->cmd = CMD_PRINT;
		} else if (op == 'q') {
			c->cmd = CMD_QUIT;
		} else if (op == 'y') {
			c->cmd = CMD_Y;
			char delim = *p++;
			int i;
			for (i = 0; i < 256; i++) c->y_map[i] = (unsigned char)i;

			const char *s1 = p;
			while (*p && *p != delim) p++;
			int len1 = p - s1;
			if (*p == delim) p++;
			const char *s2 = p;
			while (*p && *p != delim) p++;
			int len2 = p - s2;
			if (*p == delim) p++;

			for (i = 0; i < len1 && i < len2; i++) {
				c->y_map[(unsigned char)s1[i]] = (unsigned char)s2[i];
			}
		} else {
			fprintf(stderr, "sed: unknown command '%c'\n", op);
			free(c);
			return -1;
		}

		add_cmd(c);
	}
	return 0;
}

static int addr_match(addr_t *a, long lineno, int is_last, char *line)
{
	switch (a->type) {
	case ADDR_NONE: return 1;
	case ADDR_NUM: return (lineno == a->num);
	case ADDR_LAST: return is_last;
	case ADDR_RE:
		if (!a->re) return 0;
		return regexec(a->re, line, 1);
	}
	return 0;
}

static int cmd_matches(sed_cmd_t *c, long lineno, int is_last, char *line)
{
	if (c->a1.type == ADDR_NONE)
		return 1;

	if (!c->has_a2) {
		return addr_match(&c->a1, lineno, is_last, line);
	}

	/* Range addressing */
	if (!c->in_range) {
		if (addr_match(&c->a1, lineno, is_last, line)) {
			c->in_range = 1;
			return 1;
		}
		return 0;
	} else {
		if (addr_match(&c->a2, lineno, is_last, line)) {
			c->in_range = 0;
		}
		return 1;
	}
}

static char *apply_subst(sed_cmd_t *c, char *line, int *did_sub)
{
	if (!c->sub_re) return line;

	char *src = line;
	int cap = strlen(line) * 2 + 256;
	char *out = malloc(cap);
	int out_len = 0;
	int matched = 0;

	while (*src) {
		if (!regexec(c->sub_re, src, (src == line))) {
			break;
		}

		matched = 1;
		char *mstart = c->sub_re->startp[0];
		char *mend = c->sub_re->endp[0];

		/* Copy prefix before match */
		int prefix_len = mstart - src;
		while (out_len + prefix_len + 1 >= cap) {
			cap *= 2;
			out = realloc(out, cap);
		}
		memcpy(out + out_len, src, prefix_len);
		out_len += prefix_len;

		/* Expand replacement */
		const char *rp = c->sub_repl;
		while (*rp) {
			if (*rp == '&') {
				int mlen = mend - mstart;
				while (out_len + mlen + 1 >= cap) {
					cap *= 2;
					out = realloc(out, cap);
				}
				memcpy(out + out_len, mstart, mlen);
				out_len += mlen;
				rp++;
			} else if (*rp == '\\' && *(rp + 1) >= '1' && *(rp + 1) <= '9') {
				int subidx = *(rp + 1) - '0';
				rp += 2;
				if (c->sub_re->startp[subidx] && c->sub_re->endp[subidx]) {
					int slen = c->sub_re->endp[subidx] - c->sub_re->startp[subidx];
					while (out_len + slen + 1 >= cap) {
						cap *= 2;
						out = realloc(out, cap);
					}
					memcpy(out + out_len, c->sub_re->startp[subidx], slen);
					out_len += slen;
				}
			} else if (*rp == '\\' && *(rp + 1)) {
				rp++;
				if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
				out[out_len++] = *rp++;
			} else {
				if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
				out[out_len++] = *rp++;
			}
		}

		if (mend == src) {
			/* Empty match, advance one char to avoid infinite loop */
			if (*src) {
				if (out_len + 2 >= cap) { cap *= 2; out = realloc(out, cap); }
				out[out_len++] = *src++;
			}
		} else {
			src = mend;
		}

		if (!c->sub_global)
			break;
	}

	if (!matched) {
		free(out);
		*did_sub = 0;
		return line;
	}

	/* Copy remaining characters */
	int rest_len = strlen(src);
	while (out_len + rest_len + 1 >= cap) {
		cap *= 2;
		out = realloc(out, cap);
	}
	memcpy(out + out_len, src, rest_len);
	out_len += rest_len;
	out[out_len] = '\0';

	free(line);
	*did_sub = 1;
	return out;
}

static char *read_line(FILE *fp)
{
	int cap = 128;
	int len = 0;
	char *buf = malloc(cap);
	if (!buf) return NULL;

	while (fgets(buf + len, cap - len, fp)) {
		len += strlen(buf + len);
		if (len > 0 && buf[len - 1] == '\n') {
			buf[len - 1] = '\0';
			return buf;
		}
		if (len >= cap - 1) {
			cap *= 2;
			char *nb = realloc(buf, cap);
			if (!nb) {
				free(buf);
				return NULL;
			}
			buf = nb;
		}
	}

	if (len == 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

static void process_stream(FILE *fp)
{
	char *curr_line = read_line(fp);
	char *next_line = NULL;
	long lineno = 0;

	while (curr_line != NULL) {
		lineno++;
		next_line = read_line(fp);
		int is_last = (next_line == NULL);

		sed_cmd_t *c;
		int deleted = 0;
		int quit = 0;

		for (c = cmd_list; c != NULL; c = c->next) {
			if (!cmd_matches(c, lineno, is_last, curr_line))
				continue;

			if (c->cmd == CMD_SUBST) {
				int did_sub = 0;
				curr_line = apply_subst(c, curr_line, &did_sub);
				if (did_sub && c->sub_print) {
					printf("%s\n", curr_line);
				}
			} else if (c->cmd == CMD_DELETE) {
				deleted = 1;
				break;
			} else if (c->cmd == CMD_PRINT) {
				printf("%s\n", curr_line);
			} else if (c->cmd == CMD_QUIT) {
				quit = 1;
				break;
			} else if (c->cmd == CMD_Y) {
				int i;
				for (i = 0; curr_line[i]; i++) {
					curr_line[i] = c->y_map[(unsigned char)curr_line[i]];
				}
			}
		}

		if (!deleted && !opt_n) {
			printf("%s\n", curr_line);
		}

		free(curr_line);
		curr_line = next_line;

		if (quit) {
			if (next_line) free(next_line);
			exit(0);
		}
	}
}

int main(int argc, char **argv)
{
	int arg_idx = 1;
	int script_given = 0;

	while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
		if (strcmp(argv[arg_idx], "-n") == 0) {
			opt_n = 1;
		} else if (strcmp(argv[arg_idx], "-e") == 0) {
			if (arg_idx + 1 >= argc) {
				fprintf(stderr, "sed: missing argument to -e\n");
				return 1;
			}
			parse_script(argv[++arg_idx]);
			script_given = 1;
		} else {
			fprintf(stderr, "sed: unrecognized option '%s'\n", argv[arg_idx]);
			return 1;
		}
		arg_idx++;
	}

	if (!script_given) {
		if (arg_idx >= argc) {
			fprintf(stderr, "usage: sed [-n] [-e script] [script] [file...]\n");
			return 1;
		}
		parse_script(argv[arg_idx++]);
	}

	if (arg_idx >= argc) {
		process_stream(stdin);
	} else {
		int i;
		for (i = arg_idx; i < argc; i++) {
			if (strcmp(argv[i], "-") == 0) {
				process_stream(stdin);
			} else {
				FILE *fp = fopen(argv[i], "r");
				if (!fp) {
					fprintf(stderr, "sed: cannot open %s\n", argv[i]);
					continue;
				}
				process_stream(fp);
				fclose(fp);
			}
		}
	}

	return 0;
}
