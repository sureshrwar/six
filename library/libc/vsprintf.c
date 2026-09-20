/*              
 *  linux/lib/vsprintf.c
 *                      
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */                             
                                
/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*              
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */     
            
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <linux/types.h>


char _ctmp;
unsigned char _ctype_[] = {0x00,                 /* EOF */
_C,_C,_C,_C,_C,_C,_C,_C,                        /* 0-7 */
_C,_C|_S,_C|_S,_C|_S,_C|_S,_C|_S,_C,_C,         /* 8-15 */
_C,_C,_C,_C,_C,_C,_C,_C,                        /* 16-23 */
_C,_C,_C,_C,_C,_C,_C,_C,                        /* 24-31 */
_S|_SP,_P,_P,_P,_P,_P,_P,_P,                    /* 32-39 */
_P,_P,_P,_P,_P,_P,_P,_P,                        /* 40-47 */
_D,_D,_D,_D,_D,_D,_D,_D,                        /* 48-55 */
_D,_D,_P,_P,_P,_P,_P,_P,                        /* 56-63 */
_P,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U,      /* 64-71 */
_U,_U,_U,_U,_U,_U,_U,_U,                        /* 72-79 */
_U,_U,_U,_U,_U,_U,_U,_U,                        /* 80-87 */
_U,_U,_U,_P,_P,_P,_P,_P,                        /* 88-95 */
_P,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L,      /* 96-103 */
_L,_L,_L,_L,_L,_L,_L,_L,                        /* 104-111 */
_L,_L,_L,_L,_L,_L,_L,_L,                        /* 112-119 */
_L,_L,_L,_P,_P,_P,_P,_C,                        /* 120-127 */
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                /* 128-143 */
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                /* 144-159 */
_S|_SP,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,   /* 160-175 */
_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,       /* 176-191 */
_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,       /* 192-207 */
_U,_U,_U,_U,_U,_U,_U,_P,_U,_U,_U,_U,_U,_U,_U,_L,       /* 208-223 */
_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,       /* 224-239 */
_L,_L,_L,_L,_L,_L,_L,_P,_L,_L,_L,_L,_L,_L,_L,_L};      /* 240-255 */


unsigned long simple_strtoul(const char *cp,char **endp,unsigned int base)
{
        unsigned long result = 0,value;

        if (!base) {    
                base = 10;
                if (*cp == '0') {
                        base = 8;
                        cp++;
                        if ((*cp == 'x') && isxdigit(cp[1])) {
                                cp++;
                                base = 16;
                        }
                }
        }
        while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp-'0' : (islower(*cp)
            ? toupper(*cp) : *cp)-'A'+10) < base) {
                result = (result*base) + value;
                cp++;
        }
        if (endp)
                *endp = (char *)cp;
        return result;
}

/* we use this so that we can do without the ctype library */
#define is_digit(c)     ((c) >= '0' && (c) <= '9') 

static int skip_atoi(const char **s)            
{
        int i=0;

        while (is_digit(**s))
                i = i*10 + *((*s)++) - '0';
        return i;
}

#define ZEROPAD 1               /* pad with zero */
#define SIGN    2               /* unsigned/signed long */
#define PLUS    4               /* show plus */
#define SPACE   8               /* space if plus */
#define LEFT    16              /* left justified */
#define SPECIAL 32              /* 0x */
#define LARGE   64              /* use 'ABCDEF' instead of 'abcdef' */

#define do_div(n,base) ({ \
int __res; \
__res = (((unsigned long) n % (unsigned) base)); \
n = (((unsigned long) n) / (unsigned) base); \
__res; }) 

static char * number(char *str, FILE *fp, long num, int base, int size, int precision
        ,int type)
{
        char c,sign,tmp[66];
        const char *digits="0123456789abcdefghijklmnopqrstuvwxyz";
        int i;


        if (type & LARGE)
                digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        if (type & LEFT)
                type &= ~ZEROPAD;
        if (base < 2 || base > 36)
                return 0;
        c = (type & ZEROPAD) ? '0' : ' ';
        sign = 0;
        if (type & SIGN) {
                if (num < 0) {
                        sign = '-';
                        num = -num;
                        size--;
                } else if (type & PLUS) {
                        sign = '+';
                        size--;
                } else if (type & SPACE) {
                        sign = ' ';
                        size--;
                }
        }
        if (type & SPECIAL) {
                if (base == 16)
                        size -= 2;
                else if (base == 8)
                        size--;
        }
        i = 0;
        if (num == 0)
                tmp[i++]='0';
        else while (num != 0)
                tmp[i++] = digits[do_div(num,base)];
        if (i > precision)
                precision = i;
        size -= precision;
        if (!(type&(ZEROPAD+LEFT)))
                while(size-->0)
		{
			if (str)
                        	*str++ = ' ';
			else
				fputc(' ', fp);
		}
        if (sign)
	{
		if (str)
                	*str++ = sign;
		else
			fputc(sign, fp);
	}
        if (type & SPECIAL)
                if (base==8)
		{
			if (str)
                        	*str++ = '0';
			else
				fputc('0', fp);
		}
                else if (base==16) {
			if (str)
			{
                        	*str++ = '0';
                        	*str++ = digits[33];
			}
			else
			{
				fputc('0', fp);
				fputc(digits[33], fp);
			}
                }
        if (!(type & LEFT))
                while (size-- > 0)
		{
			if (str)
                        	*str++ = c;
			else
				fputc(c, fp);
		}
        while (i < precision--)
	{
		if (str)
                	*str++ = '0';
		else
			fputc('0', fp);
	}
        while (i-- > 0)
	{
		if (str)
                	*str++ = tmp[i];
		else
			fputc(tmp[i], fp);
	}
        while (size-- > 0)
	{
		if (str)
                	*str++ = ' ';
		else
			fputc(' ', fp);
	}
        return str;
}



static int _vformat(char *buf, FILE *fp, const char *fmt, va_list args)
{
        int len;
        unsigned long num;
        int i, base;
        char * str;     
        const char *s;
	int count;
                        
        int flags;              /* flags to number() */
                        
        int field_width;        /* width of output field */
        int precision;          /* min. # of digits for integers; max
                                   number of chars for from string */
        int qualifier;          /* 'h', 'l', or 'L' for integer fields */

                
        for (str=buf ; *fmt ; ++fmt) {
                if (*fmt != '%') {
			if (buf)
                        	*str++ = *fmt;
			else
			{
				count ++;
				fputc(*fmt, fp);
			}
                        continue;
                }       
                        
                /* process flags */
                flags = 0;
                repeat:         
                        ++fmt;          /* this also skips first '%' */
                        switch (*fmt) {
                                case '-': flags |= LEFT; goto repeat;
                                case '+': flags |= PLUS; goto repeat;
                                case ' ': flags |= SPACE; goto repeat;
                                case '#': flags |= SPECIAL; goto repeat;
                                case '0': flags |= ZEROPAD; goto repeat;
                                }

                /* get field width */
                field_width = -1;
                if (is_digit(*fmt))
                        field_width = skip_atoi(&fmt);
                else if (*fmt == '*') {
                        ++fmt;
                        /* it's the next argument */
                        field_width = va_arg(args, int);
                        if (field_width < 0) {
                                field_width = -field_width;
                                flags |= LEFT;
                        }
                }

                /* get the precision */
                precision = -1;
                if (*fmt == '.') {
                        ++fmt;
                        if (is_digit(*fmt))
                                precision = skip_atoi(&fmt);
                        else if (*fmt == '*') {
                                ++fmt;
                                /* it's the next argument */
                                precision = va_arg(args, int);
                        }
                        if (precision < 0)
                                precision = 0;
                }               
                                
                /* get the conversion qualifier */
                qualifier = -1;
                if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
                        qualifier = *fmt;
                        ++fmt;
                }       
                
                /* default base */
                base = 10; 
                        
                switch (*fmt) {
                case 'c':       
                        if (!(flags & LEFT))
                                while (--field_width > 0)
				{
					if (buf)
                                        	*str++ = ' ';
					else 
					{
						fputc(' ', fp);
						count++;
					}
				}
			if (buf)
                        	*str++ = (unsigned char) va_arg(args, int);
			else
			{
				fputc((unsigned char) va_arg(args, int), fp);
				count++;
			}
                        while (--field_width > 0)
			{
				if (buf)
                                	*str++ = ' ';
				else
				{
					fputc(' ', fp);
					count++;
					
				}
			}
                        continue;
                        
                case 's':
                        s = va_arg(args, char *);
                        if (!s) 
                                s = "<NULL>";
                                
                        len = strnlen(s, precision);
                        
                        if (!(flags & LEFT))
                                while (len < field_width--)
				{
					if (buf)
                                        	*str++ = ' ';
					else
					{
						fputc(' ', fp);
						count++;
					}
				}
                        for (i = 0; i < len; ++i)
			{
				if (buf)
                                	*str++ = *s++;
				else
				{
					fputc(*s, fp);
					s++;
					count++;
				}
			}
                        while (len < field_width--)
			{
				if (buf)
                                	*str++ = ' ';
				else
				{
					fputc(' ', fp);
					count++;
				}
			}
                        continue;

                case 'p':
                        if (field_width == -1) {
                                field_width = 2*sizeof(void *);
                                flags |= ZEROPAD;
                        }
                        str = number(str, fp,
                                (unsigned long) va_arg(args, void *), 16,
                                field_width, precision, flags);
                        continue;


                case 'n':
                        if (qualifier == 'l') {
                                long * ip = va_arg(args, long *);
				if (buf)
                                	*ip = (str - buf);
				else
					*ip = count;
                        } else {
                                int * ip = va_arg(args, int *);
				if (buf)
                                	*ip = (str - buf);
				else
					*ip = count;
                        }
                        continue;

                /* integer number formats - set up the flags and "break" */
                case 'o':
                        base = 8;
                        break;          
                        
                case 'X':       
                        flags |= LARGE;
                case 'x':       
                        base = 16;
                        break;
                
                case 'd':
                case 'i':       
                        flags |= SIGN;
                case 'u':
                        break;
                                
                default:        
                        if (*fmt != '%')
			{
				if (buf)
                                	*str++ = '%';
				else
				{
					fputc('%', fp);
					count++;
				}
			}
                        if (*fmt)
			{
				if (buf)
                                	*str++ = *fmt;
				else
				{
					fputc(*fmt, fp);
					count++;
				}
			}
                        else
                                --fmt; 
                        continue;
                }       
                if (qualifier == 'l') 
                        num = va_arg(args, unsigned long);
                else if (qualifier == 'h')
                        if (flags & SIGN)
                                num = va_arg(args, short);
                        else
                                num = va_arg(args, unsigned short);
                else if (flags & SIGN)
                        num = va_arg(args, int);
                else
                        num = va_arg(args, unsigned int);
                str = number(str, fp, num, base, field_width, precision, flags);
        }
	if (buf)
        	*str = '\0';
	if (buf)
        	return str-buf;
	else
		return count;
}

int vsprintf(char *buf, const char *fmt, va_list args)
{
	return _vformat(buf, NULL, fmt, args);
}

int vfprintf(FILE *fp, const char *fmt, va_list args)
{
	return _vformat(NULL, fp, fmt, args);
}

