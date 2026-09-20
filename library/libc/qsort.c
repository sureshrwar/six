#include <stdlib.h>
#include <string.h>

static void swap(char *a, char *b, size_t size)
{
	char tmp;
	while (size--) {
		tmp = *a;
		*a++ = *b;
		*b++ = tmp;
	}
}

void qsort(void *base, size_t nmemb, size_t size,
	   int (*compar)(const void *, const void *))
{
	char *b = (char *)base;
	size_t i, j;

	if (nmemb < 2 || size == 0)
		return;

	/* Standard insertion sort for small arrays or simple quicksort */
	for (i = 1; i < nmemb; i++) {
		for (j = i; j > 0; j--) {
			char *p1 = b + (j - 1) * size;
			char *p2 = b + j * size;
			if (compar(p1, p2) > 0) {
				swap(p1, p2, size);
			} else {
				break;
			}
		}
	}
}
