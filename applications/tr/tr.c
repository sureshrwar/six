/*
 * tr.c - Translate, squeeze, and/or delete characters for SIX
 *
 * Usage:
 *   tr [-c|-C] [-d] [-s] string1 [string2]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096

static int opt_complement = 0;
static int opt_delete = 0;
static int opt_squeeze = 0;

static int parse_escape(const char **pp)
{
	const char *p = *pp;
	p++; /* skip '\\' */
	int val = 0;
	if (*p >= '0' && *p <= '7') {
		int digits = 0;
		while (digits < 3 && *p >= '0' && *p <= '7') {
			val = (val << 3) | (*p - '0');
			p++;
			digits++;
		}
		*pp = p;
		return val;
	}
	switch (*p) {
	case 'a': val = '\a'; break;
	case 'b': val = '\b'; break;
	case 'f': val = '\f'; break;
	case 'n': val = '\n'; break;
	case 'r': val = '\r'; break;
	case 't': val = '\t'; break;
	case 'v': val = '\v'; break;
	case '\\': val = '\\'; break;
	default: val = (unsigned char)*p; break;
	}
	if (*p) p++;
	*pp = p;
	return val;
}

static int expand_spec(const char *str, unsigned char *out, int max_len)
{
	int count = 0;
	const char *p = str;

	while (*p && count < max_len) {
		int c1;
		if (*p == '\\') {
			c1 = parse_escape(&p);
		} else {
			c1 = (unsigned char)*p++;
		}

		if (*p == '-' && *(p + 1) != '\0') {
			p++; /* skip '-' */
			int c2;
			if (*p == '\\') {
				c2 = parse_escape(&p);
			} else {
				c2 = (unsigned char)*p++;
			}
			if (c1 <= c2) {
				int ch;
				for (ch = c1; ch <= c2 && count < max_len; ch++) {
					out[count++] = (unsigned char)ch;
				}
			} else {
				out[count++] = (unsigned char)c1;
				out[count++] = (unsigned char)c2;
			}
		} else {
			out[count++] = (unsigned char)c1;
		}
	}
	return count;
}

int main(int argc, char **argv)
{
	int arg_idx = 1;
	while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
		char *p = argv[arg_idx] + 1;
		if (strcmp(p, "-") == 0) {
			arg_idx++;
			break;
		}
		while (*p) {
			if (*p == 'c' || *p == 'C') opt_complement = 1;
			else if (*p == 'd') opt_delete = 1;
			else if (*p == 's') opt_squeeze = 1;
			else {
				fprintf(stderr, "tr: unrecognized option '-%c'\n", *p);
				return 1;
			}
			p++;
		}
		arg_idx++;
	}

	if (arg_idx >= argc) {
		fprintf(stderr, "usage: tr [-c] [-d] [-s] string1 [string2]\n");
		return 1;
	}

	const char *s1_raw = argv[arg_idx++];
	const char *s2_raw = (arg_idx < argc) ? argv[arg_idx++] : NULL;

	if (!opt_delete && !opt_squeeze && !s2_raw) {
		fprintf(stderr, "tr: two strings must be given when translating\n");
		return 1;
	}

	unsigned char set1[512], set2[512];
	int len1 = expand_spec(s1_raw, set1, 512);
	int len2 = s2_raw ? expand_spec(s2_raw, set2, 512) : 0;

	/* In complement mode, invert set1 */
	unsigned char in_set1[256] = {0};
	int i;
	for (i = 0; i < len1; i++) {
		in_set1[set1[i]] = 1;
	}

	if (opt_complement) {
		int new_len = 0;
		for (i = 0; i < 256; i++) {
			if (!in_set1[i]) {
				set1[new_len++] = (unsigned char)i;
			}
		}
		len1 = new_len;
		memset(in_set1, 0, sizeof(in_set1));
		for (i = 0; i < len1; i++) {
			in_set1[set1[i]] = 1;
		}
	}

	/* Build translation and action tables */
	unsigned char map[256];
	unsigned char del_map[256] = {0};
	unsigned char sqz_map[256] = {0};

	for (i = 0; i < 256; i++) {
		map[i] = (unsigned char)i;
	}

	if (opt_delete) {
		for (i = 0; i < len1; i++) {
			del_map[set1[i]] = 1;
		}
		if (opt_squeeze && len2 > 0) {
			for (i = 0; i < len2; i++) {
				sqz_map[set2[i]] = 1;
			}
		}
	} else {
		/* Translation */
		if (len2 > 0) {
			unsigned char last_c2 = set2[len2 - 1];
			for (i = 0; i < len1; i++) {
				unsigned char src = set1[i];
				unsigned char dst = (i < len2) ? set2[i] : last_c2;
				map[src] = dst;
			}
		}
		if (opt_squeeze) {
			if (len2 > 0) {
				for (i = 0; i < len2; i++) {
					sqz_map[set2[i]] = 1;
				}
			} else {
				for (i = 0; i < len1; i++) {
					sqz_map[set1[i]] = 1;
				}
			}
		}
	}

	/* Process input from stdin */
	unsigned char in_buf[BUF_SIZE];
	unsigned char out_buf[BUF_SIZE];
	int out_pos = 0;
	int last_out = -1;
	ssize_t n;

	while ((n = read(0, in_buf, sizeof(in_buf))) > 0) {
		for (i = 0; i < n; i++) {
			unsigned char c = in_buf[i];
			if (del_map[c])
				continue;

			unsigned char out_c = map[c];
			if (opt_squeeze && sqz_map[out_c] && (int)out_c == last_out)
				continue;

			out_buf[out_pos++] = out_c;
			last_out = (int)out_c;

			if (out_pos >= BUF_SIZE) {
				write(1, out_buf, out_pos);
				out_pos = 0;
			}
		}
	}

	if (out_pos > 0) {
		write(1, out_buf, out_pos);
	}

	return 0;
}
