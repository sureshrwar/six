#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_LINES 256
#define MAX_LEN   160

static char lines_a[MAX_LINES][MAX_LEN];
static char lines_b[MAX_LINES][MAX_LEN];

static int
load_lines(const char *path, char arr[MAX_LINES][MAX_LEN])
{
	char fbuf[512];
	int fd = open(path, 0);
	int n, i, row = 0, col = 0;

	if (fd < 0) {
		perror(path);
		return -1;
	}
	while ((n = read(fd, fbuf, sizeof(fbuf))) > 0) {
		for (i = 0; i < n; i++) {
			if (fbuf[i] == '\n') {
				arr[row][col] = '\0';
				row++;
				col = 0;
				if (row >= MAX_LINES) {
					close(fd);
					return row;
				}
			} else if (fbuf[i] != '\r' && col < MAX_LEN - 1) {
				arr[row][col++] = fbuf[i];
			}
		}
	}
	if (col > 0 && row < MAX_LINES) {
		arr[row][col] = '\0';
		row++;
	}
	close(fd);
	return row;
}

int
main(int argc, char **argv)
{
	int unified = 0, argi = 1;
	int na, nb, ia = 0, ib = 0, diff_found = 0;

	while (argi < argc && argv[argi][0] == '-') {
		if (strcmp(argv[argi], "-u") == 0)
			unified = 1;
		argi++;
	}
	if (argc - argi != 2) {
		printf("Usage: diff [-u] FILE1 FILE2\n");
		return 2;
	}

	na = load_lines(argv[argi], lines_a);
	nb = load_lines(argv[argi + 1], lines_b);
	if (na < 0 || nb < 0)
		return 2;

	while (ia < na || ib < nb) {
		if (ia < na && ib < nb && strcmp(lines_a[ia], lines_b[ib]) == 0) {
			if (unified && diff_found)
				printf(" %s\n", lines_a[ia]);
			ia++;
			ib++;
			continue;
		}
		if (!diff_found) {
			diff_found = 1;
			if (unified) {
				printf("--- %s\n", argv[argi]);
				printf("+++ %s\n", argv[argi + 1]);
				printf("@@ -%d,%d +%d,%d @@\n", ia + 1, na - ia, ib + 1, nb - ib);
			}
		}
		/* Look ahead up to 8 lines to resync */
		{
			int da, db, synced = 0;
			for (da = 0; da <= 8 && !synced; da++) {
				for (db = 0; db <= 8; db++) {
					if ((da > 0 || db > 0) &&
					    ia + da < na && ib + db < nb &&
					    strcmp(lines_a[ia + da], lines_b[ib + db]) == 0) {
						while (da-- > 0) {
							printf("%s%s\n", unified ? "-" : "< ", lines_a[ia++]);
						}
						while (db-- > 0) {
							printf("%s%s\n", unified ? "+" : "> ", lines_b[ib++]);
						}
						synced = 1;
						break;
					}
				}
			}
			if (!synced) {
				if (ia < na)
					printf("%s%s\n", unified ? "-" : "< ", lines_a[ia++]);
				if (ib < nb)
					printf("%s%s\n", unified ? "+" : "> ", lines_b[ib++]);
			}
		}
	}
	return diff_found ? 1 : 0;
}
