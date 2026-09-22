#include <stdio.h>
#include <stdlib.h>
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
nm_file(const char *path, int show_file)
{
	unsigned char eh[52];
	unsigned char shtab[64 * 40];
	int fd, msb, i;
	unsigned int e_shoff, e_shentsize, e_shnum;
	int sym_sh = -1, str_sh = -1;
	unsigned int sym_off = 0, sym_sz = 0, str_off = 0, str_sz = 0;

	fd = open(path, 0);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	if (read(fd, (char *)eh, 52) != 52 || eh[0] != 0x7f || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F') {
		printf("nm: %s: file format not recognized\n", path);
		close(fd);
		return 1;
	}
	msb = (eh[5] == 2);
	e_shoff = r32(eh + 32, msb);
	e_shentsize = r16(eh + 46, msb);
	e_shnum = r16(eh + 48, msb);

	if (e_shoff == 0 || e_shentsize < 40 || e_shnum == 0 || e_shnum > 64) {
		printf("nm: %s: no symbols\n", path);
		close(fd);
		return 0;
	}

	for (i = 0; i < (int)e_shnum; i++) {
		unsigned char *sh = shtab + i * 40;
		if (lseek(fd, (off_t)(e_shoff + (unsigned int)i * e_shentsize), SEEK_SET) < 0 ||
		    read(fd, (char *)sh, 40) != 40) {
			close(fd);
			return 1;
		}
		if (r32(sh + 4, msb) == 2) { /* SHT_SYMTAB */
			sym_sh = i;
			sym_off = r32(sh + 16, msb);
			sym_sz = r32(sh + 20, msb);
			str_sh = (int)r32(sh + 24, msb);
		}
	}

	if (sym_sh < 0 || str_sh < 0 || str_sh >= (int)e_shnum) {
		printf("nm: %s: no symbols\n", path);
		close(fd);
		return 0;
	}

	str_off = r32(shtab + str_sh * 40 + 16, msb);
	str_sz = r32(shtab + str_sh * 40 + 20, msb);

	if (show_file)
		printf("\n%s:\n", path);

	for (i = 0; (unsigned int)(i * 16) < sym_sz; i++) {
		unsigned char sym[16];
		char name[128];
		unsigned int st_name, st_value, st_info, st_shndx;
		int bind, type;
		char tchar = '?';

		if (lseek(fd, (off_t)(sym_off + (unsigned int)i * 16), SEEK_SET) < 0 ||
		    read(fd, (char *)sym, 16) != 16)
			break;
		st_name = r32(sym, msb);
		st_value = r32(sym + 4, msb);
		st_info = sym[12];
		st_shndx = r16(sym + 14, msb);
		bind = st_info >> 4;
		type = st_info & 0x0f;

		if (st_name == 0 || type == 3 || type == 4) /* skip empty, SECTION, FILE */
			continue;
		if (st_name >= str_sz)
			continue;
		if (lseek(fd, (off_t)(str_off + st_name), SEEK_SET) < 0)
			continue;
		{
			int nr = read(fd, name, sizeof(name) - 1);
			if (nr <= 0)
				continue;
			name[nr] = '\0';
		}
		if (!name[0])
			continue;

		if (st_shndx == 0) {
			tchar = 'U';
		} else if (st_shndx == 0xfff1) {
			tchar = 'A';
		} else if (st_shndx == 0xfff2) {
			tchar = 'C';
		} else if (st_shndx < e_shnum) {
			unsigned char *sh = shtab + st_shndx * 40;
			unsigned int sh_type = r32(sh + 4, msb);
			unsigned int sh_flags = r32(sh + 8, msb);
			if (sh_type == 8) /* SHT_NOBITS (.bss) */
				tchar = 'B';
			else if (sh_flags & 0x4) /* SHF_EXECINSTR (.text) */
				tchar = 'T';
			else if (sh_flags & 0x1) /* SHF_WRITE (.data) */
				tchar = 'D';
			else
				tchar = 'R';
		}
		if (bind == 0 && tchar >= 'A' && tchar <= 'Z' && tchar != 'U')
			tchar = (char)(tchar + ('a' - 'A'));

		if (tchar == 'U')
			printf("         %c %s\n", tchar, name);
		else
			printf("%08x %c %s\n", st_value, tchar, name);
	}

	close(fd);
	return 0;
}

int
main(int argc, char **argv)
{
	int i = 1, rc = 0;

	while (i < argc && argv[i][0] == '-')
		i++;

	if (i >= argc) {
		return nm_file("a.out", 0);
	}
	for (; i < argc; i++) {
		if (nm_file(argv[i], argc > 2) != 0)
			rc = 1;
	}
	return rc;
}
