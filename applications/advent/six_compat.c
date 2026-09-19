#include <stdlib.h>

static unsigned long next_rand = 1;

void srand(unsigned int seed)
{
	next_rand = seed ? seed : 1;
}

int rand(void)
{
	next_rand = next_rand * 1103515245UL + 12345UL;
	return (int)((next_rand >> 16) & 0x7fff);
}

int abs(int x)
{
	return x < 0 ? -x : x;
}

int sscanf(const char *str, const char *fmt, int *out)
{
	if (!str || !out)
		return 0;
	*out = atoi((char *)str);
	return 1;
}
