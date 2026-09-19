/*
 * basic.c - Classic Tiny BASIC Interpreter for SIX (/bin/basic)
 *
 * Supports line-numbered programming, immediate mode, variables A-Z,
 * expressions (+, -, *, /, %, parens), PRINT, INPUT, LET, IF/THEN,
 * FOR/NEXT/STEP, GOTO, GOSUB/RETURN, REM, END/STOP, LIST, RUN, NEW,
 * LOAD, SAVE, DEMO, and RND(n).
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>

#define MAX_LINES   1000
#define MAX_LINE_LEN 128
#define GOSUB_STACK 32
#define FOR_STACK   32

struct line {
	int num;
	char text[MAX_LINE_LEN];
};

static struct line program[MAX_LINES];
static int line_count = 0;

static long vars[26];
static unsigned long rng_state = 1;

/* Execution state */
static int cur_line_idx = 0;
static const char *cur_ptr = NULL;
static int running = 0;

/* GOSUB stack */
struct gosub_entry {
	int line_idx;
	const char *ptr;
};
static struct gosub_entry gosub_stk[GOSUB_STACK];
static int gosub_sp = 0;

/* FOR stack */
struct for_entry {
	int var_idx;
	long target;
	long step;
	int line_idx;
	const char *loop_start;
};
static struct for_entry for_stk[FOR_STACK];
static int for_sp = 0;

static int next_rand(void)
{
	rng_state = rng_state * 1103515245UL + 12345UL;
	return (int)((rng_state >> 16) & 0x7fff);
}

static void skip_ws(const char **p)
{
	while (**p == ' ' || **p == '\t')
		(*p)++;
}

static int is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static char to_upper(char c)
{
	if (c >= 'a' && c <= 'z')
		return c - ('a' - 'A');
	return c;
}

static int match_kw(const char **p, const char *kw)
{
	const char *s = *p;
	skip_ws(&s);
	while (*kw) {
		if (to_upper(*s) != to_upper(*kw))
			return 0;
		s++;
		kw++;
	}
	if (is_alpha(*s) || is_digit(*s))
		return 0;
	*p = s;
	return 1;
}

/* Forward declaration for expression parser */
static long expr(const char **p);

static long factor(const char **p)
{
	long val = 0;
	skip_ws(p);

	if (**p == '(') {
		(*p)++;
		val = expr(p);
		skip_ws(p);
		if (**p == ')') (*p)++;
		return val;
	}

	if (match_kw(p, "RND")) {
		skip_ws(p);
		if (**p == '(') {
			long bound;
			(*p)++;
			bound = expr(p);
			skip_ws(p);
			if (**p == ')') (*p)++;
			if (bound <= 1) return 1;
			return 1 + (next_rand() % bound);
		}
		return next_rand();
	}

	if (match_kw(p, "ABS")) {
		skip_ws(p);
		if (**p == '(') {
			long n;
			(*p)++;
			n = expr(p);
			skip_ws(p);
			if (**p == ')') (*p)++;
			return (n < 0) ? -n : n;
		}
	}

	if (**p == '-') {
		(*p)++;
		return -factor(p);
	}
	if (**p == '+') {
		(*p)++;
		return factor(p);
	}

	if (is_alpha(**p)) {
		int v = to_upper(**p) - 'A';
		(*p)++;
		return vars[v];
	}

	if (is_digit(**p)) {
		while (is_digit(**p)) {
			val = val * 10 + (**p - '0');
			(*p)++;
		}
		return val;
	}

	return 0;
}

static long term(const char **p)
{
	long val = factor(p);
	for (;;) {
		skip_ws(p);
		if (**p == '*') {
			(*p)++;
			val *= factor(p);
		} else if (**p == '/') {
			long d;
			(*p)++;
			d = factor(p);
			val = (d != 0) ? (val / d) : 0;
		} else if (**p == '%') {
			long d;
			(*p)++;
			d = factor(p);
			val = (d != 0) ? (val % d) : 0;
		} else {
			break;
		}
	}
	return val;
}

static long expr(const char **p)
{
	long val = term(p);
	for (;;) {
		skip_ws(p);
		if (**p == '+') {
			(*p)++;
			val += term(p);
		} else if (**p == '-') {
			(*p)++;
			val -= term(p);
		} else {
			break;
		}
	}
	return val;
}

static int find_line_idx(int num)
{
	int i;
	for (i = 0; i < line_count; i++) {
		if (program[i].num == num)
			return i;
	}
	return -1;
}

static void insert_or_delete_line(int num, const char *text)
{
	int i, pos = 0;
	skip_ws(&text);

	/* Check if line already exists */
	for (i = 0; i < line_count; i++) {
		if (program[i].num == num) {
			if (*text == '\0') {
				/* Delete line */
				for (; i < line_count - 1; i++)
					program[i] = program[i + 1];
				line_count--;
				return;
			}
			/* Replace line */
			strncpy(program[i].text, text, MAX_LINE_LEN - 1);
			program[i].text[MAX_LINE_LEN - 1] = '\0';
			return;
		}
	}

	if (*text == '\0')
		return;

	if (line_count >= MAX_LINES) {
		printf("Error: Program memory full!\n");
		return;
	}

	/* Find insertion index to keep lines sorted */
	while (pos < line_count && program[pos].num < num)
		pos++;

	for (i = line_count; i > pos; i--)
		program[i] = program[i - 1];

	program[pos].num = num;
	strncpy(program[pos].text, text, MAX_LINE_LEN - 1);
	program[pos].text[MAX_LINE_LEN - 1] = '\0';
	line_count++;
}

static void cmd_list(const char *arg)
{
	int start = 0, end = 999999;
	int i;

	skip_ws(&arg);
	if (is_digit(*arg)) {
		start = (int)expr(&arg);
		skip_ws(&arg);
		if (*arg == '-' || *arg == ',') {
			arg++;
			skip_ws(&arg);
			if (is_digit(*arg))
				end = (int)expr(&arg);
		} else {
			end = start;
		}
	}

	for (i = 0; i < line_count; i++) {
		if (program[i].num >= start && program[i].num <= end)
			printf("%d %s\n", program[i].num, program[i].text);
	}
}

static void cmd_new(void)
{
	line_count = 0;
	memset(vars, 0, sizeof(vars));
	gosub_sp = 0;
	for_sp = 0;
}

static void exec_stmt(const char *stmt);

static void stmt_print(const char **p)
{
	int need_nl = 1;
	skip_ws(p);

	if (**p == '\0') {
		putchar('\n');
		return;
	}

	while (**p != '\0') {
		skip_ws(p);
		if (**p == '"') {
			(*p)++;
			while (**p && **p != '"') {
				putchar(**p);
				(*p)++;
			}
			if (**p == '"') (*p)++;
			need_nl = 1;
		} else if (**p == ';' || **p == ',') {
			if (**p == ',') putchar('\t');
			(*p)++;
			need_nl = 0;
		} else {
			long val = expr(p);
			printf("%ld", val);
			need_nl = 1;
		}
		skip_ws(p);
		if (**p == ';' || **p == ',') {
			if (**p == ',') putchar('\t');
			(*p)++;
			need_nl = 0;
		} else if (**p != '\0') {
			/* next argument */
		}
	}
	if (need_nl)
		putchar('\n');
}

static void stmt_input(const char **p)
{
	char prompt[64] = "? ";
	char buf[64];
	int v;
	skip_ws(p);

	if (**p == '"') {
		int i = 0;
		(*p)++;
		while (**p && **p != '"' && i < 60)
			prompt[i++] = *(*p)++;
		if (**p == '"') (*p)++;
		prompt[i] = '\0';
		skip_ws(p);
		if (**p == ';' || **p == ',') (*p)++;
	}

	skip_ws(p);
	if (!is_alpha(**p)) {
		printf("Error: Variable expected in INPUT\n");
		return;
	}
	v = to_upper(**p) - 'A';
	(*p)++;

	printf("%s", prompt);
	fflush(stdout);

	if (fgets(buf, sizeof(buf), stdin)) {
		const char *bp = buf;
		vars[v] = expr(&bp);
	}
}

static void stmt_let(const char **p)
{
	int v;
	skip_ws(p);
	if (!is_alpha(**p)) {
		printf("Error: Variable expected in LET\n");
		return;
	}
	v = to_upper(**p) - 'A';
	(*p)++;
	skip_ws(p);
	if (**p == '=') (*p)++;
	vars[v] = expr(p);
}

static void stmt_if(const char **p)
{
	long lhs = expr(p);
	char op[3] = { 0, 0, 0 };
	long rhs;
	int cond = 0;

	skip_ws(p);
	if (**p == '=' || **p == '<' || **p == '>') {
		op[0] = *(*p)++;
		if (**p == '=' || **p == '>')
			op[1] = *(*p)++;
	}

	rhs = expr(p);

	if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) cond = (lhs == rhs);
	else if (strcmp(op, "<>") == 0 || strcmp(op, "!=") == 0) cond = (lhs != rhs);
	else if (strcmp(op, "<") == 0) cond = (lhs < rhs);
	else if (strcmp(op, ">") == 0) cond = (lhs > rhs);
	else if (strcmp(op, "<=") == 0) cond = (lhs <= rhs);
	else if (strcmp(op, ">=") == 0) cond = (lhs >= rhs);

	skip_ws(p);
	if (match_kw(p, "THEN")) {
		skip_ws(p);
		if (cond) {
			if (is_digit(**p)) {
				long target = expr(p);
				int idx = find_line_idx((int)target);
				if (idx >= 0) {
					cur_line_idx = idx;
					cur_ptr = NULL;
				} else {
					printf("Error: Line %ld not found\n", target);
					running = 0;
				}
			} else {
				exec_stmt(*p);
			}
		}
	}
}

static void exec_stmt(const char *stmt)
{
	const char *p = stmt;
	skip_ws(&p);

	if (*p == '\0') return;

	if (match_kw(&p, "REM")) return;

	if (match_kw(&p, "PRINT") || *p == '?') {
		if (*p == '?') p++;
		stmt_print(&p);
		return;
	}

	if (match_kw(&p, "INPUT")) {
		stmt_input(&p);
		return;
	}

	if (match_kw(&p, "LET")) {
		stmt_let(&p);
		return;
	}

	if (match_kw(&p, "IF")) {
		stmt_if(&p);
		return;
	}

	if (match_kw(&p, "GOTO")) {
		long target = expr(&p);
		int idx = find_line_idx((int)target);
		if (idx >= 0) {
			cur_line_idx = idx;
			cur_ptr = NULL;
		} else {
			printf("Error: Line %ld not found in GOTO\n", target);
			running = 0;
		}
		return;
	}

	if (match_kw(&p, "GOSUB")) {
		long target = expr(&p);
		int idx = find_line_idx((int)target);
		if (idx >= 0) {
			if (gosub_sp < GOSUB_STACK) {
				gosub_stk[gosub_sp].line_idx = cur_line_idx + 1;
				gosub_sp++;
				cur_line_idx = idx;
				cur_ptr = NULL;
			} else {
				printf("Error: GOSUB stack overflow\n");
				running = 0;
			}
		} else {
			printf("Error: Line %ld not found in GOSUB\n", target);
			running = 0;
		}
		return;
	}

	if (match_kw(&p, "RETURN")) {
		if (gosub_sp > 0) {
			gosub_sp--;
			cur_line_idx = gosub_stk[gosub_sp].line_idx;
			cur_ptr = NULL;
		} else {
			printf("Error: RETURN without GOSUB\n");
			running = 0;
		}
		return;
	}

	if (match_kw(&p, "FOR")) {
		int v;
		long start, target, step = 1;
		skip_ws(&p);
		if (!is_alpha(*p)) {
			printf("Error: Variable expected in FOR\n");
			return;
		}
		v = to_upper(*p) - 'A';
		p++;
		skip_ws(&p);
		if (*p == '=') p++;
		start = expr(&p);
		vars[v] = start;
		skip_ws(&p);
		if (match_kw(&p, "TO")) {
			target = expr(&p);
			skip_ws(&p);
			if (match_kw(&p, "STEP"))
				step = expr(&p);
			if (for_sp < FOR_STACK) {
				for_stk[for_sp].var_idx = v;
				for_stk[for_sp].target = target;
				for_stk[for_sp].step = step;
				for_stk[for_sp].line_idx = cur_line_idx;
				for_sp++;
			}
		}
		return;
	}

	if (match_kw(&p, "NEXT")) {
		if (for_sp > 0) {
			struct for_entry *f = &for_stk[for_sp - 1];
			skip_ws(&p);
			if (is_alpha(*p)) {
				int v = to_upper(*p) - 'A';
				p++;
				if (v != f->var_idx) {
					printf("Error: NEXT variable mismatch\n");
					running = 0;
					return;
				}
			}
			vars[f->var_idx] += f->step;
			if ((f->step > 0 && vars[f->var_idx] <= f->target) ||
			    (f->step < 0 && vars[f->var_idx] >= f->target)) {
				cur_line_idx = f->line_idx + 1;
				cur_ptr = NULL;
			} else {
				for_sp--;
			}
		} else {
			printf("Error: NEXT without FOR\n");
			running = 0;
		}
		return;
	}

	if (match_kw(&p, "END") || match_kw(&p, "STOP")) {
		running = 0;
		return;
	}

	/* Implicit LET: X = 5 */
	if (is_alpha(*p)) {
		const char *look = p + 1;
		skip_ws(&look);
		if (*look == '=') {
			stmt_let(&p);
			return;
		}
	}

	printf("Syntax error: %s\n", stmt);
	running = 0;
}

static void cmd_run(void)
{
	if (line_count == 0) return;
	memset(vars, 0, sizeof(vars));
	gosub_sp = 0;
	for_sp = 0;
	cur_line_idx = 0;
	running = 1;

	while (running && cur_line_idx < line_count) {
		const char *line = program[cur_line_idx].text;
		cur_ptr = line;
		exec_stmt(line);
		if (running && cur_ptr != NULL)
			cur_line_idx++;
	}
	running = 0;
}

static void cmd_demo(void)
{
	cmd_new();
	insert_or_delete_line(10, "PRINT \"=== NUMBER GUESSING GAME ===\"");
	insert_or_delete_line(20, "PRINT \"I'm thinking of a number from 1 to 100!\"");
	insert_or_delete_line(30, "S = RND(100)");
	insert_or_delete_line(40, "T = 0");
	insert_or_delete_line(50, "INPUT \"Your guess\"; G");
	insert_or_delete_line(60, "T = T + 1");
	insert_or_delete_line(70, "IF G = S THEN GOTO 120");
	insert_or_delete_line(80, "IF G < S THEN PRINT \"Too low! Try higher.\"");
	insert_or_delete_line(90, "IF G > S THEN PRINT \"Too high! Try lower.\"");
	insert_or_delete_line(100, "GOTO 50");
	insert_or_delete_line(120, "PRINT \"CORRECT! You won in \"; T; \" tries!\"");
	insert_or_delete_line(130, "END");
	printf("Loaded demo: Number Guessing Game. Type RUN to play, or LIST to view code.\n");
}

static void cmd_save(const char *path)
{
	FILE *fp;
	int i;
	skip_ws(&path);
	if (*path == '"') {
		path++;
		char clean_path[64];
		int ci = 0;
		while (*path && *path != '"' && ci < 60)
			clean_path[ci++] = *path++;
		clean_path[ci] = '\0';
		fp = fopen(clean_path, "w");
	} else {
		fp = fopen(path, "w");
	}
	if (!fp) {
		printf("Error: Cannot open file for writing\n");
		return;
	}
	for (i = 0; i < line_count; i++)
		fprintf(fp, "%d %s\n", program[i].num, program[i].text);
	fclose(fp);
	printf("Saved %d lines.\n", line_count);
}

static void cmd_load(const char *path)
{
	FILE *fp;
	char buf[MAX_LINE_LEN];
	skip_ws(&path);
	if (*path == '"') {
		path++;
		char clean_path[64];
		int ci = 0;
		while (*path && *path != '"' && ci < 60)
			clean_path[ci++] = *path++;
		clean_path[ci] = '\0';
		fp = fopen(clean_path, "r");
	} else {
		fp = fopen(path, "r");
	}
	if (!fp) {
		printf("Error: Cannot open file for reading\n");
		return;
	}
	cmd_new();
	while (fgets(buf, sizeof(buf), fp)) {
		const char *p = buf;
		int num = 0;
		skip_ws(&p);
		if (is_digit(*p)) {
			while (is_digit(*p)) {
				num = num * 10 + (*p - '0');
				p++;
			}
			/* Remove trailing newline */
			char *nl = strchr(p, '\n');
			if (nl) *nl = '\0';
			insert_or_delete_line(num, p);
		}
	}
	fclose(fp);
	printf("Loaded %d lines.\n", line_count);
}

int main(int argc, char **argv)
{
	char line_buf[MAX_LINE_LEN];
	rng_state = (unsigned long)time(0) ^ (unsigned long)getpid();

	if (argc > 1) {
		cmd_load(argv[1]);
		cmd_run();
		return 0;
	}

	printf("SIX BASIC 1.0 (Dartmouth / Tiny BASIC)\n");
	printf("Ready. Type HELP for commands or DEMO to load a sample game.\n");

	for (;;) {
		printf("> ");
		fflush(stdout);

		if (!fgets(line_buf, sizeof(line_buf), stdin))
			break;

		/* Strip newline */
		char *nl = strchr(line_buf, '\n');
		if (nl) *nl = '\0';

		const char *p = line_buf;
		skip_ws(&p);
		if (*p == '\0') continue;

		/* Line-numbered input: 10 PRINT "HI" */
		if (is_digit(*p)) {
			int num = 0;
			while (is_digit(*p)) {
				num = num * 10 + (*p - '0');
				p++;
			}
			insert_or_delete_line(num, p);
			continue;
		}

		/* Immediate commands */
		if (match_kw(&p, "RUN")) {
			cmd_run();
		} else if (match_kw(&p, "LIST")) {
			cmd_list(p);
		} else if (match_kw(&p, "NEW") || match_kw(&p, "CLEAR")) {
			cmd_new();
			printf("Ready.\n");
		} else if (match_kw(&p, "DEMO")) {
			cmd_demo();
		} else if (match_kw(&p, "SAVE")) {
			cmd_save(p);
		} else if (match_kw(&p, "LOAD")) {
			cmd_load(p);
		} else if (match_kw(&p, "HELP")) {
			printf("Commands: RUN, LIST, NEW, DEMO, SAVE \"file\", LOAD \"file\", QUIT\n");
			printf("Statements: PRINT, INPUT, LET, IF/THEN, FOR/TO/STEP/NEXT, GOTO, GOSUB/RETURN, END\n");
			printf("Functions: RND(n), ABS(n), variables A-Z\n");
		} else if (match_kw(&p, "QUIT") || match_kw(&p, "EXIT") || match_kw(&p, "BYE")) {
			break;
		} else {
			/* Immediate statement execution */
			exec_stmt(line_buf);
		}
	}

	return 0;
}
