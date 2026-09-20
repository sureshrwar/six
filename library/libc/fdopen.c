#include <stdio.h>
#include <stdlib.h>

FILE *fdopen(int fd, const char *mode)
{
	int i;
	int flags = 0;
	FILE *stream;

	if (fd < 0 || !mode)
		return NULL;

	for (i = 0; iotab[i] != NULL; i++) {
		if (i >= FOPEN_MAX - 1)
			return NULL;
	}

	if (*mode == 'r')
		flags |= _IOREAD | _IOREADING;
	else if (*mode == 'w' || *mode == 'a')
		flags |= _IOWRITE | _IOWRITING;
	else
		return NULL;

	stream = (FILE *)malloc(sizeof(FILE));
	if (!stream)
		return NULL;

	stream->count = 0;
	stream->fd = fd;
	stream->flags = flags;
	stream->buf = NULL;
	stream->ptr = NULL;
	iotab[i] = stream;
	return stream;
}
