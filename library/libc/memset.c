
#include <stdio.h>


void
memset(char *s, char ch, int len)
{
	int i;
	for(i=0; i<len; i++)
		s[i] = ch;
}       

