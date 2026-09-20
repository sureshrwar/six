#include <stdio.h>

int main(int argc, char **argv)
{
	int i;
	printf("\n========================================\n");
	printf("   TCC Self-Hosting Verification on SIX\n");
	printf("========================================\n");
	printf("Hello from native C compiled inside SIX!\n");
	for (i = 1; i <= 3; i++) {
		printf("  [Pass %d] In-guest C execution successful.\n", i);
	}
	printf("========================================\n\n");
	return 0;
}
