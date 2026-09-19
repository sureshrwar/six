#include <linux/types.h>
#include <ttyent.h>

int ttyslot()
{
	int slot;

	slot= fttyslot(0);
	if (slot == 0) slot= fttyslot(1);
	if (slot == 0) slot= fttyslot(2);
	return slot;
}

int fttyslot(fd)
int fd;
{
	char *tname;
	int lineno;
	struct ttyent *ttyp;

	tname= ttyname(fd);
	if (tname == NULL) return 0;

	/* Assume that tty devices are in /dev */
	if (strncmp(tname, "/dev/", 5) != 0)
		return 0;	/* Malformed tty name. */
	tname += 5;

	/* Scan /etc/ttytab. */
	lineno= 1;
	while ((ttyp= getttyent()) != NULL)
	{
		if (strcmp(tname, ttyp->ty_name) == 0)
		{
			endttyent();
			return lineno;
		}
		lineno++;
	}
	/* No match */
	endttyent();
	return 0;
}

