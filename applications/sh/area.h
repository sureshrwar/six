#ifndef _AREA_H
#define _AREA_H
/*
 * storage allocation
 */
_PROTOTYPE(char *getcell , (unsigned nbytes ));
_PROTOTYPE(void garbage , (void));
_PROTOTYPE(void setarea , (char *cp , int a ));
_PROTOTYPE(int getarea , (char *cp ));
_PROTOTYPE(void freearea , (int a ));
_PROTOTYPE(void freecell , (char *cp ));

extern  int     areanum;        /* current allocation area */

#define NEW(type) (type *)getcell(sizeof(type))
#define DELETE(obj)     freecell((char *)obj)

#define DIRBLKSIZ       512     /* size of directory block */

#ifndef DIRSIZ
#define DIRSIZ  14
#endif

struct direct {
  ino_t d_ino;
  char d_name[DIRSIZ];
};
#endif
