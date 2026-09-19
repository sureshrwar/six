#ifndef _WORD_H
#define _WORD_H

struct  wdblock {
        short   w_bsize;
        short   w_nword;
        /* bounds are arbitrary */
        char    *w_words[1];
};

_PROTOTYPE(struct wdblock *addword , (char *wd , struct wdblock *wb ));
_PROTOTYPE(struct wdblock *newword , (int nw ));
_PROTOTYPE(char **getwords , (struct wdblock *wb ));

#endif

