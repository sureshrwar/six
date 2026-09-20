#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/";
	DIR *dir;
	struct dirent *de;
	int count1 = 0, count2 = 0;

	printf("--- Testing opendir on directory: %s ---\n", path);
	dir = opendir(path);
	if (!dir) {
		printf("FAIL: opendir(\"%s\") returned NULL, errno=%d\n", path, errno);
		return 1;
	}

	printf("Pass 1: reading directory entries:\n");
	while ((de = readdir(dir)) != NULL) {
		printf("  [%d] ino=%ld reclen=%d name='%s'\n",
		       count1 + 1, de->d_ino, (int)de->d_reclen, de->d_name);
		count1++;
	}
	printf("Pass 1 complete: read %d entries.\n", count1);
	if (count1 == 0) {
		printf("FAIL: readdir returned 0 entries for \"%s\"\n", path);
		closedir(dir);
		return 1;
	}

	printf("Testing rewinddir...\n");
	rewinddir(dir);

	printf("Pass 2: verifying rewinddir re-reads entries:\n");
	while ((de = readdir(dir)) != NULL) {
		count2++;
	}
	printf("Pass 2 complete: read %d entries.\n", count2);
	if (count1 != count2) {
		printf("FAIL: pass 1 count (%d) != pass 2 count (%d)\n", count1, count2);
		closedir(dir);
		return 1;
	}

	int cr = closedir(dir);
	if (cr != 0) {
		printf("FAIL: closedir returned %d, errno=%d!\n", cr, errno);
		return 1;
	}
	printf("closedir OK.\n");

	/* Test error cases */
	printf("Testing opendir on non-existent directory...\n");
	dir = opendir("/no_such_directory_exists_12345");
	if (dir != NULL) {
		printf("FAIL: opendir should have failed on non-existent directory!\n");
		closedir(dir);
		return 1;
	}
	printf("Failed as expected (errno=%d).\n", errno);

	printf("Testing opendir on regular file (/etc/passwd)...\n");
	dir = opendir("/etc/passwd");
	if (dir != NULL) {
		printf("FAIL: opendir should have failed on regular file!\n");
		closedir(dir);
		return 1;
	}
	if (errno != ENOTDIR) {
		printf("WARNING: expected errno %d (ENOTDIR), got %d\n", ENOTDIR, errno);
	} else {
		printf("Failed with ENOTDIR as expected.\n");
	}

	printf("All directory tests PASSED!\n");
	return 0;
}
