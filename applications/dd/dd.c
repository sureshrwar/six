/*
 * dd.c - Copy blocks of a file or device for SIX (/bin/dd)
 *
 * A deliberately small dd.  It exists mainly so that /dev/hda is reachable
 * from userland: until now nothing in the guest could open the raw disk,
 * because there was no way to seek to an arbitrary offset and read a
 * bounded number of bytes.
 *
 *   dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N] [hex]
 *
 *   if=     input file, default standard input
 *   of=     output file, default standard output
 *   bs=     block size in bytes, default 512
 *   count=  number of blocks to copy, default "until end of input"
 *   skip=   blocks to skip at the start of the input
 *   seek=   blocks to skip at the start of the output
 *   hex     dump to standard output as hex + ASCII instead of copying
 *           bytes; this is a SIX extension, there being no od(1) here
 *
 * N may carry a suffix: 'b' multiplies by 512, 'k' by 1024, 'm' by 1048576.
 *
 * Two deliberate omissions, both because the underlying kernel is 2.0:
 *
 *   - No conv= of any kind.
 *   - skip=/seek= are implemented with lseek(..., SEEK_SET), never
 *     SEEK_END.  hd_fops has no lseek method, so sys_lseek() falls through
 *     to its default handler, and that computes SEEK_END from
 *     inode->i_size -- which is zero for a block device.  Absolute seeks
 *     are the only ones that mean anything on /dev/hda.
 *
 * As elsewhere in the guest, the stubs in library/sys/ return the kernel's
 * negative error code directly and never touch errno, so we set it
 * ourselves before calling perror().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#define DEFAULT_BS	512
#define HEX_COLUMNS	16

static char *progname = "dd";

/* Report a failed syscall whose negative return code is in rc. */
static void fail(const char *what, int rc)
{
	errno = (rc < 0) ? -rc : rc;
	perror(what);
	exit(1);
}

/*
 * Parse a non-negative operand with an optional b/k/m suffix.  Returns -1
 * on anything malformed; the caller turns that into a usage error rather
 * than silently treating it as zero, which is how a mistyped count= ends
 * up overwriting a disk.
 */
static long parse_num(const char *s)
{
	char *end;
	long v;

	if (*s == '\0')
		return -1;

	v = strtol(s, &end, 10);
	if (v < 0 || end == s)
		return -1;

	switch (*end) {
	case '\0':			  break;
	case 'b': v *= 512;	  end++; break;
	case 'k':
	case 'K': v *= 1024;	  end++; break;
	case 'm':
	case 'M': v *= 1024 * 1024; end++; break;
	default:  return -1;
	}

	return (*end == '\0') ? v : -1;
}

/* True if arg starts with "key=", and stores the value part in *val. */
static int operand(const char *arg, const char *key, const char **val)
{
	int n = 0;

	while (key[n] != '\0') {
		if (arg[n] != key[n])
			return 0;
		n++;
	}
	if (arg[n] != '=')
		return 0;

	*val = arg + n + 1;
	return 1;
}

/*
 * One line of a hex dump: the offset, up to HEX_COLUMNS bytes in hex, then
 * the same bytes as ASCII with unprintables shown as '.'.  A short final
 * line is padded so the ASCII column stays put.
 */
static void hex_line(unsigned long offset, const unsigned char *p, int n)
{
	int i;

	printf("%08lx  ", offset);

	for (i = 0; i < HEX_COLUMNS; i++) {
		if (i < n)
			printf("%02x ", p[i]);
		else
			printf("   ");
		if (i == (HEX_COLUMNS / 2) - 1)
			printf(" ");
	}

	printf(" |");
	for (i = 0; i < n; i++)
		printf("%c", (p[i] >= 0x20 && p[i] < 0x7f) ? p[i] : '.');
	printf("|\n");
}

static void usage(void)
{
	fprintf(stderr,
		"Usage: dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N] [hex]\n"
		"       N may end in b (x512), k (x1024) or m (x1048576)\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *infile = NULL, *outfile = NULL, *val;
	long bs = DEFAULT_BS;
	long count = 0, skip = 0, seek = 0;
	int have_count = 0, hex = 0;
	int ifd = 0, ofd = 1;
	long full_in = 0, part_in = 0, full_out = 0, part_out = 0;
	unsigned long offset = 0;
	char *buf;
	int i, rc;

	if (argv[0] != NULL && argv[0][0] != '\0')
		progname = argv[0];

	for (i = 1; i < argc; i++) {
		long v;

		if (operand(argv[i], "if", &val)) {
			infile = val;
			continue;
		}
		if (operand(argv[i], "of", &val)) {
			outfile = val;
			continue;
		}
		if (!strcmp(argv[i], "hex")) {
			hex = 1;
			continue;
		}

		if (operand(argv[i], "bs", &val)) {
			v = parse_num(val);
			if (v <= 0) {
				fprintf(stderr, "%s: bad block size '%s'\n", progname, val);
				usage();
			}
			bs = v;
			continue;
		}
		if (operand(argv[i], "count", &val)) {
			v = parse_num(val);
			if (v < 0) {
				fprintf(stderr, "%s: bad count '%s'\n", progname, val);
				usage();
			}
			count = v;
			have_count = 1;
			continue;
		}
		if (operand(argv[i], "skip", &val)) {
			v = parse_num(val);
			if (v < 0) {
				fprintf(stderr, "%s: bad skip '%s'\n", progname, val);
				usage();
			}
			skip = v;
			continue;
		}
		if (operand(argv[i], "seek", &val)) {
			v = parse_num(val);
			if (v < 0) {
				fprintf(stderr, "%s: bad seek '%s'\n", progname, val);
				usage();
			}
			seek = v;
			continue;
		}

		fprintf(stderr, "%s: unknown operand '%s'\n", progname, argv[i]);
		usage();
	}

	buf = malloc((size_t) bs);
	if (buf == NULL) {
		fprintf(stderr, "%s: cannot allocate a %ld byte buffer\n", progname, bs);
		return 1;
	}

	if (infile != NULL) {
		ifd = open((char *)infile, O_RDONLY);
		if (ifd < 0)
			fail(infile, ifd);
	}

	/*
	 * Only open the output when there is one.  In hex mode we are the
	 * ones producing the text, so of= would be meaningless and a stray
	 * of=/dev/hda alongside hex should not truncate anything.
	 */
	if (hex) {
		if (outfile != NULL) {
			fprintf(stderr, "%s: of= is ignored in hex mode\n", progname);
			outfile = NULL;
		}
	} else if (outfile != NULL) {
		ofd = open((char *)outfile, O_WRONLY | O_CREAT, 0644);
		if (ofd < 0)
			fail(outfile, ofd);
	}

	if (skip > 0) {
		rc = lseek(ifd, skip * bs, 0);
		if (rc < 0)
			fail("lseek on input", rc);
		offset = (unsigned long)(skip * bs);
	}
	if (seek > 0 && !hex) {
		rc = lseek(ofd, seek * bs, 0);
		if (rc < 0)
			fail("lseek on output", rc);
	}

	while (!have_count || (full_in + part_in) < count) {
		int n = read(ifd, buf, (size_t) bs);

		if (n < 0)
			fail("read", n);
		if (n == 0)
			break;			/* end of input */

		if (n == bs)
			full_in++;
		else
			part_in++;

		if (hex) {
			int off;

			for (off = 0; off < n; off += HEX_COLUMNS) {
				int chunk = n - off;

				if (chunk > HEX_COLUMNS)
					chunk = HEX_COLUMNS;
				hex_line(offset + (unsigned long) off,
					 (unsigned char *) buf + off, chunk);
			}
		} else {
			int w = write(ofd, buf, (size_t) n);

			if (w < 0)
				fail("write", w);
			if (w != n) {
				fprintf(stderr, "%s: short write (%d of %d bytes)\n",
					progname, w, n);
				return 1;
			}
			if (w == bs)
				full_out++;
			else
				part_out++;
		}

		offset += (unsigned long) n;
	}

	if (hex)
		fflush(stdout);

	if (infile != NULL)
		close(ifd);
	if (outfile != NULL)
		close(ofd);

	/*
	 * dd's record counts go to stderr so that they do not corrupt a
	 * pipeline.  In hex mode there is nothing written, so only report
	 * what came in.
	 */
	fprintf(stderr, "%ld+%ld records in\n", full_in, part_in);
	if (!hex)
		fprintf(stderr, "%ld+%ld records out\n", full_out, part_out);

	return 0;
}
