
/*
 * Thanks to all the minix guys - Like many other stuff, i borrowed this from the
 * minix code base. Thanks once again. All rights/credits to them.
 */

#include <linux/types.h>

#define	ptrint		long

#define BRKSIZE		8192
#define	PTRSIZE		((int) sizeof(double))

#define Align(x,a)	(((x) + (a - 1)) & ~(a - 1))
#define NextSlot(p)	(* (void **) ((p) - PTRSIZE))
#define NextFree(p)	(* (void **) (p))

extern unsigned long brk(unsigned long);
static void *_bottom = 0, *_top = 0, *_empty = 0;

static int grow(long len)
{
  register char *p;

  if ((char *) _top + len < (char *) _top
      || (p = (char *)Align((ptrint)_top + len, BRKSIZE)) < (char *) _top 
      || brk(p) != 0)
	return(0);
  NextSlot((char *)_top) = p;
  NextSlot(p) = 0;
  free(_top);
  _top = p;
  return 1;
}

void *
malloc(long size)
{
  register char *prev, *p, *next, *new;
  register unsigned len, ntries;

  if (size == 0) return 0;
  for (ntries = 0; ntries < 2; ntries++) {
	if ((len = Align(size, PTRSIZE) + PTRSIZE) < 2 * PTRSIZE)
		return 0;
	if (_bottom == 0) {
		if ((p = (char *)sbrk(2 * PTRSIZE)) == (char *) -1)
			return 0;
		p = (char *) Align((ptrint)p, PTRSIZE);
		p += PTRSIZE;
		_top = _bottom = p;
		NextSlot(p) = 0;
	}
	for (prev = 0, p = _empty; p != 0; prev = p, p = NextFree(p)) {
		next = NextSlot(p);
		new = p + len;	/* easily overflows!! */
		if (new > next || new <= p)
			continue;		/* too small */
		if (new + PTRSIZE < next) {	/* too big, so split */
			/* + PTRSIZE avoids tiny slots on free list */
			NextSlot(new) = next;
			NextSlot(p) = new;
			NextFree(new) = NextFree(p);
			NextFree(p) = new;
		}
		if (prev)
			NextFree(prev) = NextFree(p);
		else
			_empty = NextFree(p);
		return p;
	}
	if (grow(len) == 0)
		break;
  }
  return 0;
}

void *
realloc(void *oldp, long size)
{
  register char *prev, *p, *next, *new;
  char *old = oldp;
  register long len, n;

  if (!old) return malloc(size);
  else if (!size) {
	free(oldp);
	return 0;
  }
  len = Align(size, PTRSIZE) + PTRSIZE;
  next = NextSlot(old);
  n = (int)(next - old);			/* old length */
  /*
   * extend old if there is any free space just behind it
   */
  for (prev = 0, p = _empty; p != 0; prev = p, p = NextFree(p)) {
	if (p > next)
		break;
	if (p == next) {	/* 'next' is a free slot: merge */
		NextSlot(old) = NextSlot(p);
		if (prev)
			NextFree(prev) = NextFree(p);
		else
			_empty = NextFree(p);
		next = NextSlot(old);
		break;
	}
  }
  new = old + len;
  /*
   * Can we use the old, possibly extended slot?
   */
  if (new <= next && new >= old) {		/* it does fit */
	if (new + PTRSIZE < next) {		/* too big, so split */
		/* + PTRSIZE avoids tiny slots on free list */
		NextSlot(new) = next;
		NextSlot(old) = new;
		free(new);
	}
	return old;
  }
  if ((new = malloc(size)) == 0)		/* it didn't fit */
	return 0;
  memcpy(new, old, n);				/* n < size */
  free(old);
  return new;
}

void free(void *ptr)
{
  register char *prev, *next;
  char *p = ptr;

  if (!p) return;

  for (prev = 0, next = _empty; next != 0; prev = next, next = NextFree(next))
	if (p < next)
		break;
  NextFree(p) = next;
  if (prev)
	NextFree(prev) = p;
  else
	_empty = p;
  if (next) {
	if (NextSlot(p) == next) {		/* merge p and next */
		NextSlot(p) = NextSlot(next);
		NextFree(p) = NextFree(next);
	}
  }
  if (prev) {
	if (NextSlot(prev) == p) {		/* merge prev and p */
		NextSlot(prev) = NextSlot(p);
		NextFree(prev) = NextFree(p);
	}
  }
}

#define ALIGN(x)        (((x) + (sizeof(size_t) - 1)) & ~(sizeof(size_t) - 1))

void *
calloc(size_t nelem, size_t elsize)
{
        register char *p;
        register size_t *q;
        size_t size = ALIGN(nelem * elsize);

        p = malloc(size);
        if (p == NULL) return NULL;
        q = (size_t *) (p + size);
        while ((char *) q > p) *--q = 0;
        return p;
}

