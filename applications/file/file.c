#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stat.h>

static int opt_brief = 0;
static int opt_deref = 0;

static unsigned short
read_u16(const unsigned char *p, int msb)
{
	if (msb)
		return (unsigned short)(((unsigned int)p[0] << 8) | p[1]);
	return (unsigned short)(((unsigned int)p[1] << 8) | p[0]);
}

static unsigned int
read_u32(const unsigned char *p, int msb)
{
	if (msb)
		return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
		       ((unsigned int)p[2] << 8) | (unsigned int)p[3];
	return ((unsigned int)p[3] << 24) | ((unsigned int)p[2] << 16) |
	       ((unsigned int)p[1] << 8) | (unsigned int)p[0];
}

static void
describe_elf(const unsigned char *buf, int n, char *out, int outsz)
{
	int cls = buf[4];
	int data = buf[5];
	int ver = buf[6];
	int msb = (data == 2);
	unsigned short e_type = 0, e_machine = 0, e_phentsize = 0, e_phnum = 0;
	unsigned int e_phoff = 0;
	const char *cls_str = (cls == 1) ? "32-bit" : (cls == 2) ? "64-bit" : "unknown-class";
	const char *end_str = (data == 1) ? "LSB" : (data == 2) ? "MSB" : "unknown-endian";
	const char *type_str = "object";
	const char *mach_str = "unknown architecture";
	const char *link_str = "";

	if (n >= 20) {
		e_type = read_u16(buf + 16, msb);
		e_machine = read_u16(buf + 18, msb);
	}
	switch (e_type) {
	case 1: type_str = "relocatable"; break;
	case 2: type_str = "executable"; break;
	case 3: type_str = "shared object"; break;
	case 4: type_str = "core file"; break;
	}
	switch (e_machine) {
	case 2:   mach_str = "SPARC"; break;
	case 3:   mach_str = "Intel 80386"; break;
	case 6:   mach_str = "Intel 80486"; break;
	case 8:   mach_str = "MIPS"; break;
	case 20:  mach_str = "PowerPC"; break;
	case 40:  mach_str = "ARM"; break;
	case 62:  mach_str = "x86-64"; break;
	case 183: mach_str = "ARM aarch64"; break;
	case 243: mach_str = "RISC-V"; break;
	}

	if (cls == 1 && e_type == 2 && n >= 44) {
		int dynamic = 0;
		unsigned int i;
		e_phoff = read_u32(buf + 28, msb);
		e_phentsize = read_u16(buf + 42, msb);
		e_phnum = read_u16(buf + 44, msb);
		if (e_phentsize >= 32 && e_phnum > 0 && e_phnum < 32) {
			for (i = 0; i < e_phnum; i++) {
				unsigned int off = e_phoff + i * e_phentsize;
				if (off + 4 <= (unsigned int)n) {
					unsigned int p_type = read_u32(buf + off, msb);
					if (p_type == 2 || p_type == 3) { /* PT_DYNAMIC or PT_INTERP */
						dynamic = 1;
						break;
					}
				}
			}
		}
		link_str = dynamic ? ", dynamically linked" : ", statically linked";
	}

	snprintf(out, outsz, "ELF %s %s %s, %s, version %d (SYSV)%s",
	         cls_str, end_str, type_str, mach_str, ver ? ver : 1, link_str);
}

static int
has_prefix(const char *s, int slen, const char *pfx)
{
	int plen = strlen(pfx);
	return (slen >= plen && memcmp(s, pfx, plen) == 0);
}

static void
classify_buffer(const unsigned char *buf, int n, char *out, int outsz)
{
	int i, printable = 0, high = 0, nul = 0, has_nl = 0;

	if (n >= 4 && buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
		describe_elf(buf, n, out, outsz);
		return;
	}
	if (n >= 8 && memcmp(buf, "!<arch>\n", 8) == 0) {
		snprintf(out, outsz, "current ar archive");
		return;
	}
	if (n >= 2 && buf[0] == 0x1f && buf[1] == 0x8b) {
		snprintf(out, outsz, "gzip compressed data");
		return;
	}
	if (n >= 2 && buf[0] == 0x1f && buf[1] == 0x9d) {
		snprintf(out, outsz, "compress'd data");
		return;
	}
	if (n >= 3 && buf[0] == 'B' && buf[1] == 'Z' && buf[2] == 'h') {
		snprintf(out, outsz, "bzip2 compressed data");
		return;
	}
	if (n >= 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 3 && buf[3] == 4) {
		snprintf(out, outsz, "Zip archive data");
		return;
	}
	if (n >= 262 && memcmp(buf + 257, "ustar", 5) == 0) {
		snprintf(out, outsz, "POSIX tar archive");
		return;
	}
	if (n >= 11 && memcmp(buf + 3, "NTFS    ", 8) == 0) {
		snprintf(out, outsz, "DOS/MBR boot sector, NTFS filesystem");
		return;
	}
	if (n >= 1082 && buf[1080] == 0x53 && buf[1081] == 0xef) {
		snprintf(out, outsz, "Linux rev 1.0 ext2/ext3/ext4 filesystem data");
		return;
	}
	if (n >= 4 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') {
		snprintf(out, outsz, "PNG image data");
		return;
	}
	if (n >= 4 && memcmp(buf, "GIF8", 4) == 0) {
		snprintf(out, outsz, "GIF image data");
		return;
	}
	if (n >= 3 && buf[0] == 0xff && buf[1] == 0xd8 && buf[2] == 0xff) {
		snprintf(out, outsz, "JPEG image data");
		return;
	}
	if (n >= 4 && memcmp(buf, "%PDF", 4) == 0) {
		snprintf(out, outsz, "PDF document");
		return;
	}
	if (n >= 2 && buf[0] == '#' && buf[1] == '!') {
		char interp[64];
		int p = 2, k = 0;
		while (p < n && (buf[p] == ' ' || buf[p] == '\t'))
			p++;
		while (p < n && buf[p] != '\n' && buf[p] != '\r' && k < (int)sizeof(interp) - 1)
			interp[k++] = (char)buf[p++];
		interp[k] = '\0';
		if (strstr(interp, "sh") != NULL)
			snprintf(out, outsz, "POSIX shell script, ASCII text executable (%s)", interp);
		else if (strstr(interp, "python") != NULL)
			snprintf(out, outsz, "Python script, ASCII text executable (%s)", interp);
		else if (strstr(interp, "awk") != NULL)
			snprintf(out, outsz, "awk script, ASCII text executable (%s)", interp);
		else
			snprintf(out, outsz, "a %s script, ASCII text executable", interp[0] ? interp : "shell");
		return;
	}

	for (i = 0; i < n; i++) {
		unsigned char c = buf[i];
		if (c == 0) {
			nul++;
		} else if (c == '\n') {
			has_nl = 1;
			printable++;
		} else if (c == '\r' || c == '\t' || c == '\b' || c == 0x1b || (c >= 0x20 && c <= 0x7e)) {
			printable++;
		} else if (c >= 0x80) {
			high++;
		}
	}

	if (nul == 0 && (printable + high) * 10 >= n * 9) {
		const char *s = (const char *)buf;
		if (has_prefix(s, n, "#include") || has_prefix(s, n, "/*") ||
		    strstr(s, "#include <") != NULL || strstr(s, "#include \"") != NULL ||
		    strstr(s, "int main(") != NULL) {
			snprintf(out, outsz, "C source, ASCII text");
		} else if (has_prefix(s, n, "<!DOCTYPE html") || has_prefix(s, n, "<html") ||
		           has_prefix(s, n, "<HTML") || strstr(s, "<body") != NULL) {
			snprintf(out, outsz, "HTML document, ASCII text");
		} else if (high > 0) {
			snprintf(out, outsz, "UTF-8 Unicode text");
		} else if (has_nl) {
			snprintf(out, outsz, "ASCII text");
		} else {
			snprintf(out, outsz, "ASCII text, with no line terminators");
		}
		return;
	}

	snprintf(out, outsz, "data");
}

static int
inspect_file(const char *path)
{
	struct stat st;
	unsigned char buf[1088];
	char desc[256];
	int rc, fd, n;

	rc = opt_deref ? stat((char *)path, &st) : lstat((char *)path, &st);
	if (rc < 0) {
		if (!opt_brief)
			printf("%s: ", path);
		printf("cannot open `%s' (No such file or directory)\n", path);
		return 1;
	}

	if (!opt_deref && S_ISLNK(st.st_mode)) {
		char lbuf[256];
		int llen = readlink(path, lbuf, sizeof(lbuf) - 1);
		if (llen >= 0) {
			lbuf[llen] = '\0';
			snprintf(desc, sizeof(desc), "symbolic link to %s", lbuf);
		} else {
			snprintf(desc, sizeof(desc), "symbolic link");
		}
	} else if (S_ISDIR(st.st_mode)) {
		snprintf(desc, sizeof(desc), "directory");
	} else if (S_ISCHR(st.st_mode)) {
		snprintf(desc, sizeof(desc), "character special (%d/%d)",
		         (int)((st.st_rdev >> 8) & 0xff), (int)(st.st_rdev & 0xff));
	} else if (S_ISBLK(st.st_mode)) {
		snprintf(desc, sizeof(desc), "block special (%d/%d)",
		         (int)((st.st_rdev >> 8) & 0xff), (int)(st.st_rdev & 0xff));
	} else if (S_ISFIFO(st.st_mode)) {
		snprintf(desc, sizeof(desc), "fifo (named pipe)");
	} else if (S_ISSOCK(st.st_mode)) {
		snprintf(desc, sizeof(desc), "socket");
	} else {
		memset(buf, 0, sizeof(buf));
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			if (st.st_size == 0)
				snprintf(desc, sizeof(desc), "empty");
			else
				snprintf(desc, sizeof(desc), "regular file, no read permission");
		} else {
			n = read(fd, (char *)buf, sizeof(buf) - 1);
			close(fd);
			if (n <= 0)
				snprintf(desc, sizeof(desc), "empty");
			else
				classify_buffer(buf, n, desc, sizeof(desc));
		}
	}

	if (opt_brief)
		printf("%s\n", desc);
	else
		printf("%s: %s\n", path, desc);
	return 0;
}

int
main(int argc, char **argv)
{
	int i = 1, rc = 0;

	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		const char *p = argv[i] + 1;
		if (strcmp(p, "-") == 0) {
			i++;
			break;
		}
		while (*p) {
			if (*p == 'b')
				opt_brief = 1;
			else if (*p == 'L')
				opt_deref = 1;
			else if (*p == 'h')
				opt_deref = 0;
			else {
				printf("Usage: file [-bLh] file ...\n");
				return 1;
			}
			p++;
		}
		i++;
	}

	if (i >= argc) {
		printf("Usage: file [-bLh] file ...\n");
		return 1;
	}

	for (; i < argc; i++) {
		if (inspect_file(argv[i]) != 0)
			rc = 1;
	}
	return rc;
}
