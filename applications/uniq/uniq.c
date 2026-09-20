/*
 * uniq.c - Report or omit repeated lines for SIX
 *
 * Options:
 *   -c  Prefix lines by the number of occurrences
 *   -d  Only print duplicate lines
 *   -u  Only print unique lines
 *   -i  Ignore differences in case when comparing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int opt_count = 0;
static int opt_repeated = 0;
static int opt_unique = 0;
static int opt_ignorecase = 0;

static void output_line(FILE *out, const char *line, long count)
{
	if (opt_repeated && count < 2)
		return;
	if (opt_unique && count > 1)
		return;

	if (opt_count)
		fprintf(out, "%7ld %s\n", count, line);
	else
		fprintf(out, "%s\n", line);
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

int main(int argc, char **argv)
{
	int arg_idx = 1;
	const char *infile = NULL;
	const char *outfile = NULL;

	while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
		char *p = argv[arg_idx] + 1;
		if (strcmp(p, "-") == 0) {
			arg_idx++;
			break;
		}
		while (*p) {
			if (*p == 'c') opt_count = 1;
			else if (*p == 'd') opt_repeated = 1;
			else if (*p == 'u') opt_unique = 1;
			else if (*p == 'i') opt_ignorecase = 1;
			else {
				fprintf(stderr, "uniq: unrecognized option '-%c'\n", *p);
				return 1;
			}
			p++;
		}
		arg_idx++;
	}

	if (arg_idx < argc) {
		infile = argv[arg_idx++];
	}
	if (arg_idx < argc) {
		outfile = argv[arg_idx++];
	}

	FILE *fin = stdin;
	if (infile && strcmp(infile, "-") != 0) {
		fin = fopen(infile, "r");
		if (!fin) {
			fprintf(stderr, "uniq: cannot open %s\n", infile);
			return 1;
		}
	}

	FILE *fout = stdout;
	if (outfile && strcmp(outfile, "-") != 0) {
		fout = fopen(outfile, "w");
		if (!fout) {
			fprintf(stderr, "uniq: cannot open %s\n", outfile);
			if (fin != stdin) fclose(fin);
			return 1;
		}
	}

	char *prev_line = NULL;
	char *curr_line = NULL;
	long count = 0;

	while ((curr_line = read_line(fin)) != NULL) {
		if (prev_line == NULL) {
			prev_line = curr_line;
			count = 1;
		} else {
			int matches = 0;
			if (opt_ignorecase)
				matches = (strcasecmp(prev_line, curr_line) == 0);
			else
				matches = (strcmp(prev_line, curr_line) == 0);

			if (matches) {
				count++;
				free(curr_line);
			} else {
				output_line(fout, prev_line, count);
				free(prev_line);
				prev_line = curr_line;
				count = 1;
			}
		}
	}

	if (prev_line != NULL) {
		output_line(fout, prev_line, count);
		free(prev_line);
	}

	if (fin != stdin) fclose(fin);
	if (fout != stdout) fclose(fout);

	return 0;
}
