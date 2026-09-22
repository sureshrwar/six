/*
 * fs/ext4/bitmap.c
 */

#include <linux/fs.h>
#include <linux/ext4_fs.h>

static int nibblemap[] = {4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0};

unsigned long ext4_count_free(struct buffer_head *map, unsigned int numchars)
{
	unsigned int i;
	unsigned long sum = 0;

	if (!map)
		return 0;
	for (i = 0; i < numchars; i++)
		sum += nibblemap[map->b_data[i] & 0xf] +
		       nibblemap[(map->b_data[i] >> 4) & 0xf];
	return sum;
}
