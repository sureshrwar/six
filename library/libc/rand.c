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

unsigned int arc4random_uniform(unsigned int upper_bound)
{
	if (upper_bound == 0)
		return 0;
	return (unsigned int)(rand() % upper_bound);
}
