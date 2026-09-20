/*
 * cowsay.c - Classic Unix Cowsay for SIX (/bin/cowsay)
 *
 * Supports input from command line arguments or stdin (e.g. fortune | cowsay),
 * custom eyes (-b Borg, -d dead, -g greedy, -p paranoid, -s stoned, -t tired, -w wired),
 * and thought bubble mode (cowthink).
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>

#define MAX_LINES 128
#define MAX_LINE_LEN 80

static char lines[MAX_LINES][MAX_LINE_LEN];
static int num_lines = 0;
static int max_len = 0;

static const char *eyes = "oo";
static const char *tongue = "  ";
static char bubble_stem = '\\';

static void add_line(const char *s)
{
	if (num_lines >= MAX_LINES) return;
	strncpy(lines[num_lines], s, MAX_LINE_LEN - 1);
	lines[num_lines][MAX_LINE_LEN - 1] = '\0';
	int l = strlen(lines[num_lines]);
	if (l > max_len) max_len = l;
	num_lines++;
}

static void wrap_and_add(const char *text)
{
	char word[MAX_LINE_LEN];
	char cur_line[MAX_LINE_LEN];
	int cur_len = 0;
	const char *p = text;

	cur_line[0] = '\0';

	while (*p) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p) break;

		int wp = 0;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n' && wp < MAX_LINE_LEN - 1)
			word[wp++] = *p++;
		word[wp] = '\0';

		if (cur_len + wp + (cur_len > 0 ? 1 : 0) > 40) {
			add_line(cur_line);
			strcpy(cur_line, word);
			cur_len = wp;
		} else {
			if (cur_len > 0) {
				strcat(cur_line, " ");
				cur_len++;
			}
			strcat(cur_line, word);
			cur_len += wp;
		}
		if (*p == '\n') {
			p++;
			add_line(cur_line);
			cur_line[0] = '\0';
			cur_len = 0;
		}
	}
	if (cur_len > 0)
		add_line(cur_line);
}

static void print_bubble(void)
{
	int i, j;

	/* Top border */
	putchar(' ');
	putchar('_');
	for (j = 0; j < max_len + 2; j++) putchar('_');
	putchar('\n');

	if (num_lines == 1) {
		printf("< %s >\n", lines[0]);
	} else {
		for (i = 0; i < num_lines; i++) {
			char left = '|', right = '|';
			if (i == 0) { left = '/'; right = '\\'; }
			else if (i == num_lines - 1) { left = '\\'; right = '/'; }

			printf("%c %-*s %c\n", left, max_len, lines[i], right);
		}
	}

	/* Bottom border */
	putchar(' ');
	putchar('-');
	for (j = 0; j < max_len + 2; j++) putchar('-');
	putchar('\n');
}

static void print_cow(void)
{
	char b = bubble_stem;
	printf("        %c   ^__^\n", b);
	printf("         %c  (%s)\\_______\n", b, eyes);
	printf("            (__)\\       )\\/\\\n");
	printf("             %s ||----w |\n", tongue);
	printf("                ||     ||\n");
}

static const char *my_strstr(const char *h, const char *n)
{
	size_t i;
	if (!*n) return h;
	while (*h) {
		for (i = 0; n[i] && h[i] == n[i]; i++);
		if (!n[i]) return h;
		h++;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	int i;
	char full_arg[1024];

	/* Check if invoked as cowthink */
	if (my_strstr(argv[0], "think"))
		bubble_stem = 'o';

	full_arg[0] = '\0';

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0) eyes = "==";
		else if (strcmp(argv[i], "-d") == 0) { eyes = "xx"; tongue = "U "; }
		else if (strcmp(argv[i], "-g") == 0) eyes = "$$";
		else if (strcmp(argv[i], "-p") == 0) eyes = "@@";
		else if (strcmp(argv[i], "-s") == 0) { eyes = "**"; tongue = "U "; }
		else if (strcmp(argv[i], "-t") == 0) eyes = "--";
		else if (strcmp(argv[i], "-w") == 0) eyes = "OO";
		else if (strcmp(argv[i], "-think") == 0) bubble_stem = 'o';
		else {
			if (full_arg[0] != '\0') strcat(full_arg, " ");
			strcat(full_arg, argv[i]);
		}
	}

	if (full_arg[0] != '\0') {
		wrap_and_add(full_arg);
	} else {
		/* Read from stdin */
		char buf[MAX_LINE_LEN];
		while (fgets(buf, sizeof(buf), stdin)) {
			int l = strlen(buf);
			while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
				buf[--l] = '\0';
			wrap_and_add(buf);
		}
	}

	if (num_lines == 0)
		wrap_and_add("Moo!");

	print_bubble();
	print_cow();
	return 0;
}
