/*
 * sort.c - Sort lines of text files for SIX
 *
 * Options:
 *   -r          Reverse sort order
 *   -n          Compare according to numerical value
 *   -u          Output only unique lines
 *   -f          Fold lower case to upper case (case-insensitive)
 *   -o outfile  Write output to outfile instead of stdout
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

static int opt_reverse = 0;
static int opt_numeric = 0;
static int opt_unique = 0;
static int opt_fold = 0;
static const char *opt_outfile = NULL;

static int compare_lines(const void *a, const void *b)
{
	const char *s1 = *(const char **)a;
	const char *s2 = *(const char **)b;
	int res = 0;

	if (opt_numeric) {
		/* Skip leading whitespace */
		const char *p1 = s1;
		const char *p2 = s2;
		while (*p1 && isspace((unsigned char)*p1)) p1++;
		while (*p2 && isspace((unsigned char)*p2)) p2++;

		double v1 = strtod(p1, NULL);
		double v2 = strtod(p2, NULL);
		if (v1 < v2) res = -1;
		else if (v1 > v2) res = 1;
		else res = 0;

		/* If numeric values match, fall back to lexicographical tie-break */
		if (res == 0) {
			res = opt_fold ? strcasecmp(s1, s2) : strcmp(s1, s2);
		}
	} else if (opt_fold) {
		res = strcasecmp(s1, s2);
	} else {
		res = strcmp(s1, s2);
	}

	return opt_reverse ? -res : res;
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
			/* Found newline, strip it */
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
	int i;
	int line_cap = 1024;
	int line_count = 0;
	char **lines = malloc(line_cap * sizeof(char *));
	if (!lines) {
		fprintf(stderr, "sort: out of memory\n");
		return 1;
	}

	int arg_idx = 1;
	while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
		char *p = argv[arg_idx] + 1;
		if (strcmp(p, "-") == 0) {
			/* Stop options */
			arg_idx++;
			break;
		}
		while (*p) {
			if (*p == 'r') opt_reverse = 1;
			else if (*p == 'n') opt_numeric = 1;
			else if (*p == 'u') opt_unique = 1;
			else if (*p == 'f') opt_fold = 1;
			else if (*p == 'o') {
				if (*(p + 1)) {
					opt_outfile = p + 1;
					break;
				} else if (arg_idx + 1 < argc) {
					opt_outfile = argv[++arg_idx];
					break;
				} else {
					fprintf(stderr, "sort: option requires an argument -- o\n");
					return 1;
				}
			} else {
				fprintf(stderr, "sort: unrecognized option '-%c'\n", *p);
				return 1;
			}
			p++;
		}
		arg_idx++;
	}

	int files_processed = 0;
	for (i = (arg_idx < argc ? arg_idx : argc - 1); i < argc; i++) {
		FILE *fp = NULL;
		if (arg_idx >= argc || strcmp(argv[i], "-") == 0) {
			fp = stdin;
			files_processed++;
		} else {
			fp = fopen(argv[i], "r");
			if (!fp) {
				fprintf(stderr, "sort: cannot open %s\n", argv[i]);
				continue;
			}
			files_processed++;
		}

		char *line;
		while ((line = read_line(fp)) != NULL) {
			if (line_count >= line_cap) {
				line_cap *= 2;
				char **nl = realloc(lines, line_cap * sizeof(char *));
				if (!nl) {
					fprintf(stderr, "sort: out of memory\n");
					return 1;
				}
				lines = nl;
			}
			lines[line_count++] = line;
		}

		if (fp != stdin)
			fclose(fp);
		if (arg_idx >= argc)
			break;
	}

	if (line_count > 1) {
		qsort(lines, line_count, sizeof(char *), compare_lines);
	}

	FILE *out_fp = stdout;
	if (opt_outfile) {
		out_fp = fopen(opt_outfile, "w");
		if (!out_fp) {
			fprintf(stderr, "sort: cannot open output file %s\n", opt_outfile);
			return 1;
		}
	}

	for (i = 0; i < line_count; i++) {
		if (opt_unique && i > 0) {
			/* Check if current line equals previous line */
			if (opt_numeric) {
				double v1 = strtod(lines[i - 1], NULL);
				double v2 = strtod(lines[i], NULL);
				if (v1 == v2 && (opt_fold ? (strcasecmp(lines[i - 1], lines[i]) == 0) : (strcmp(lines[i - 1], lines[i]) == 0)))
					continue;
			} else if (opt_fold) {
				if (strcasecmp(lines[i - 1], lines[i]) == 0)
					continue;
			} else {
				if (strcmp(lines[i - 1], lines[i]) == 0)
					continue;
			}
		}
		fprintf(out_fp, "%s\n", lines[i]);
	}

	if (out_fp != stdout)
		fclose(out_fp);

	for (i = 0; i < line_count; i++) {
		free(lines[i]);
	}
	free(lines);

	return 0;
}
