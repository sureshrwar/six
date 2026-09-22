#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static unsigned short
r16(const unsigned char *p, int msb)
{
	return msb ? (unsigned short)(((unsigned int)p[0] << 8) | p[1])
	           : (unsigned short)(((unsigned int)p[1] << 8) | p[0]);
}

static unsigned int
r32(const unsigned char *p, int msb)
{
	return msb ? (((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3])
	           : (((unsigned int)p[3] << 24) | ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0]);
}

static int
size_file(const char *path)
{
	unsigned char eh[52], sh[40];
	int fd, msb, i;
	unsigned int e_shoff, e_shentsize, e_shnum;
	unsigned long text = 0, data = 0, bss = 0, total;

	fd = open(path, 0);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	if (read(fd, (char *)eh, 52) != 52 || eh[0] != 0x7f || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F') {
		printf("size: %s: file format not recognized\n", path);
		close(fd);
		return 1;
	}
	msb = (eh[5] == 2);
	e_shoff = r32(eh + 32, msb);
	e_shentsize = r16(eh + 46, msb);
	e_shnum = r16(eh + 48, msb);

	for (i = 0; i < (int)e_shnum; i++) {
		unsigned int sh_type, sh_flags, sh_size;
		if (lseek(fd, (off_t)(e_shoff + (unsigned int)i * e_shentsize), SEEK_SET) < 0 ||
		    read(fd, (char *)sh, 40) != 40)
			break;
		sh_type  = r32(sh + 4, msb);
		sh_flags = r32(sh + 8, msb);
		sh_size  = r32(sh + 20, msb);
		if (!(sh_flags & 0x2)) /* not SHF_ALLOC */
			continue;
		if (sh_type == 8) /* SHT_NOBITS (.bss) */
			bss += sh_size;
		else if ((sh_flags & 0x4) || !(sh_flags & 0x1)) /* SHF_EXECINSTR or RO */
			text += sh_size;
		else
			data += sh_size;
	}
	close(fd);
	total = text + data + bss;
	printf("%7lu %7lu %7lu %7lu %7lx %s\n", text, data, bss, total, total, path);
	return 0;
}

int
main(int argc, char **argv)
{
	int i = 1, rc = 0;

	printf("   text    data     bss     dec     hex filename\n");
	if (argc < 2)
		return size_file("a.out");
	for (; i < argc; i++) {
		if (size_file(argv[i]) != 0)
			rc = 1;
	}
	return rc;
}
