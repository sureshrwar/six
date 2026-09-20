#include <ctype.h>

#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef isascii
#undef toascii
#undef tolower
#undef toupper

int isalnum(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_U|_L|_D)); }
int isalpha(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_U|_L)); }
int iscntrl(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_C)); }
int isdigit(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_D)); }
int isgraph(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_P|_U|_L|_D)); }
int islower(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_L)); }
int isprint(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_P|_U|_L|_D|_SP)); }
int ispunct(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_P)); }
int isspace(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_S)); }
int isupper(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_U)); }
int isxdigit(int c) { return ((_ctype_ + 1)[(unsigned char)c] & (_D|_X)); }
int isascii(int c) { return (((unsigned int)c) <= 0x7f); }
int toascii(int c) { return (((unsigned int)c) & 0x7f); }
int tolower(int c) { return isupper(c) ? c - ('A' - 'a') : c; }
int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }
