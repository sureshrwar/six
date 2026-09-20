/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 25 "awkgram.y"

#include <stdio.h>
#include <string.h>
#include "awk.h"

void checkdup(Node *list, Cell *item);
int yywrap(void) { return(1); }

Node	*beginloc = 0;
Node	*endloc = 0;
int	infunc	= 0;	/* = 1 if in arglist or body of func */
int	inloop	= 0;	/* = 1 if in while, for, do */
char	*curfname = 0;	/* current function name */
Node	*arglist = 0;	/* list of args for current function */

#line 87 "y.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Use api.header.include to #include this header
   instead of duplicating it here.  */
#ifndef YY_YY_Y_TAB_H_INCLUDED
# define YY_YY_Y_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    FIRSTTOKEN = 258,              /* FIRSTTOKEN  */
    PROGRAM = 259,                 /* PROGRAM  */
    PASTAT = 260,                  /* PASTAT  */
    PASTAT2 = 261,                 /* PASTAT2  */
    XBEGIN = 262,                  /* XBEGIN  */
    XEND = 263,                    /* XEND  */
    NL = 264,                      /* NL  */
    ARRAY = 265,                   /* ARRAY  */
    MATCH = 266,                   /* MATCH  */
    NOTMATCH = 267,                /* NOTMATCH  */
    MATCHOP = 268,                 /* MATCHOP  */
    FINAL = 269,                   /* FINAL  */
    DOT = 270,                     /* DOT  */
    ALL = 271,                     /* ALL  */
    CCL = 272,                     /* CCL  */
    NCCL = 273,                    /* NCCL  */
    CHAR = 274,                    /* CHAR  */
    OR = 275,                      /* OR  */
    STAR = 276,                    /* STAR  */
    QUEST = 277,                   /* QUEST  */
    PLUS = 278,                    /* PLUS  */
    EMPTYRE = 279,                 /* EMPTYRE  */
    AND = 280,                     /* AND  */
    BOR = 281,                     /* BOR  */
    APPEND = 282,                  /* APPEND  */
    EQ = 283,                      /* EQ  */
    GE = 284,                      /* GE  */
    GT = 285,                      /* GT  */
    LE = 286,                      /* LE  */
    LT = 287,                      /* LT  */
    NE = 288,                      /* NE  */
    IN = 289,                      /* IN  */
    ARG = 290,                     /* ARG  */
    BLTIN = 291,                   /* BLTIN  */
    BREAK = 292,                   /* BREAK  */
    CLOSE = 293,                   /* CLOSE  */
    CONTINUE = 294,                /* CONTINUE  */
    DELETE = 295,                  /* DELETE  */
    DO = 296,                      /* DO  */
    EXIT = 297,                    /* EXIT  */
    FOR = 298,                     /* FOR  */
    FUNC = 299,                    /* FUNC  */
    SUB = 300,                     /* SUB  */
    GSUB = 301,                    /* GSUB  */
    IF = 302,                      /* IF  */
    INDEX = 303,                   /* INDEX  */
    LSUBSTR = 304,                 /* LSUBSTR  */
    MATCHFCN = 305,                /* MATCHFCN  */
    NEXT = 306,                    /* NEXT  */
    NEXTFILE = 307,                /* NEXTFILE  */
    ADD = 308,                     /* ADD  */
    MINUS = 309,                   /* MINUS  */
    MULT = 310,                    /* MULT  */
    DIVIDE = 311,                  /* DIVIDE  */
    MOD = 312,                     /* MOD  */
    ASSIGN = 313,                  /* ASSIGN  */
    ASGNOP = 314,                  /* ASGNOP  */
    ADDEQ = 315,                   /* ADDEQ  */
    SUBEQ = 316,                   /* SUBEQ  */
    MULTEQ = 317,                  /* MULTEQ  */
    DIVEQ = 318,                   /* DIVEQ  */
    MODEQ = 319,                   /* MODEQ  */
    POWEQ = 320,                   /* POWEQ  */
    PRINT = 321,                   /* PRINT  */
    PRINTF = 322,                  /* PRINTF  */
    SPRINTF = 323,                 /* SPRINTF  */
    ELSE = 324,                    /* ELSE  */
    INTEST = 325,                  /* INTEST  */
    CONDEXPR = 326,                /* CONDEXPR  */
    POSTINCR = 327,                /* POSTINCR  */
    PREINCR = 328,                 /* PREINCR  */
    POSTDECR = 329,                /* POSTDECR  */
    PREDECR = 330,                 /* PREDECR  */
    VAR = 331,                     /* VAR  */
    IVAR = 332,                    /* IVAR  */
    VARNF = 333,                   /* VARNF  */
    CALL = 334,                    /* CALL  */
    NUMBER = 335,                  /* NUMBER  */
    STRING = 336,                  /* STRING  */
    REGEXPR = 337,                 /* REGEXPR  */
    GETLINE = 338,                 /* GETLINE  */
    RETURN = 339,                  /* RETURN  */
    SPLIT = 340,                   /* SPLIT  */
    SUBSTR = 341,                  /* SUBSTR  */
    WHILE = 342,                   /* WHILE  */
    CAT = 343,                     /* CAT  */
    NOT = 344,                     /* NOT  */
    UMINUS = 345,                  /* UMINUS  */
    UPLUS = 346,                   /* UPLUS  */
    POWER = 347,                   /* POWER  */
    DECR = 348,                    /* DECR  */
    INCR = 349,                    /* INCR  */
    INDIRECT = 350,                /* INDIRECT  */
    LASTTOKEN = 351                /* LASTTOKEN  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define FIRSTTOKEN 258
#define PROGRAM 259
#define PASTAT 260
#define PASTAT2 261
#define XBEGIN 262
#define XEND 263
#define NL 264
#define ARRAY 265
#define MATCH 266
#define NOTMATCH 267
#define MATCHOP 268
#define FINAL 269
#define DOT 270
#define ALL 271
#define CCL 272
#define NCCL 273
#define CHAR 274
#define OR 275
#define STAR 276
#define QUEST 277
#define PLUS 278
#define EMPTYRE 279
#define AND 280
#define BOR 281
#define APPEND 282
#define EQ 283
#define GE 284
#define GT 285
#define LE 286
#define LT 287
#define NE 288
#define IN 289
#define ARG 290
#define BLTIN 291
#define BREAK 292
#define CLOSE 293
#define CONTINUE 294
#define DELETE 295
#define DO 296
#define EXIT 297
#define FOR 298
#define FUNC 299
#define SUB 300
#define GSUB 301
#define IF 302
#define INDEX 303
#define LSUBSTR 304
#define MATCHFCN 305
#define NEXT 306
#define NEXTFILE 307
#define ADD 308
#define MINUS 309
#define MULT 310
#define DIVIDE 311
#define MOD 312
#define ASSIGN 313
#define ASGNOP 314
#define ADDEQ 315
#define SUBEQ 316
#define MULTEQ 317
#define DIVEQ 318
#define MODEQ 319
#define POWEQ 320
#define PRINT 321
#define PRINTF 322
#define SPRINTF 323
#define ELSE 324
#define INTEST 325
#define CONDEXPR 326
#define POSTINCR 327
#define PREINCR 328
#define POSTDECR 329
#define PREDECR 330
#define VAR 331
#define IVAR 332
#define VARNF 333
#define CALL 334
#define NUMBER 335
#define STRING 336
#define REGEXPR 337
#define GETLINE 338
#define RETURN 339
#define SPLIT 340
#define SUBSTR 341
#define WHILE 342
#define CAT 343
#define NOT 344
#define UMINUS 345
#define UPLUS 346
#define POWER 347
#define DECR 348
#define INCR 349
#define INDIRECT 350
#define LASTTOKEN 351

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 41 "awkgram.y"

	Node	*p;
	Cell	*cp;
	int	i;
	char	*s;

#line 339 "y.tab.c"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_Y_TAB_H_INCLUDED  */
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_FIRSTTOKEN = 3,                 /* FIRSTTOKEN  */
  YYSYMBOL_PROGRAM = 4,                    /* PROGRAM  */
  YYSYMBOL_PASTAT = 5,                     /* PASTAT  */
  YYSYMBOL_PASTAT2 = 6,                    /* PASTAT2  */
  YYSYMBOL_XBEGIN = 7,                     /* XBEGIN  */
  YYSYMBOL_XEND = 8,                       /* XEND  */
  YYSYMBOL_NL = 9,                         /* NL  */
  YYSYMBOL_10_ = 10,                       /* ','  */
  YYSYMBOL_11_ = 11,                       /* '{'  */
  YYSYMBOL_12_ = 12,                       /* '('  */
  YYSYMBOL_13_ = 13,                       /* '|'  */
  YYSYMBOL_14_ = 14,                       /* ';'  */
  YYSYMBOL_15_ = 15,                       /* '/'  */
  YYSYMBOL_16_ = 16,                       /* ')'  */
  YYSYMBOL_17_ = 17,                       /* '}'  */
  YYSYMBOL_18_ = 18,                       /* '['  */
  YYSYMBOL_19_ = 19,                       /* ']'  */
  YYSYMBOL_ARRAY = 20,                     /* ARRAY  */
  YYSYMBOL_MATCH = 21,                     /* MATCH  */
  YYSYMBOL_NOTMATCH = 22,                  /* NOTMATCH  */
  YYSYMBOL_MATCHOP = 23,                   /* MATCHOP  */
  YYSYMBOL_FINAL = 24,                     /* FINAL  */
  YYSYMBOL_DOT = 25,                       /* DOT  */
  YYSYMBOL_ALL = 26,                       /* ALL  */
  YYSYMBOL_CCL = 27,                       /* CCL  */
  YYSYMBOL_NCCL = 28,                      /* NCCL  */
  YYSYMBOL_CHAR = 29,                      /* CHAR  */
  YYSYMBOL_OR = 30,                        /* OR  */
  YYSYMBOL_STAR = 31,                      /* STAR  */
  YYSYMBOL_QUEST = 32,                     /* QUEST  */
  YYSYMBOL_PLUS = 33,                      /* PLUS  */
  YYSYMBOL_EMPTYRE = 34,                   /* EMPTYRE  */
  YYSYMBOL_AND = 35,                       /* AND  */
  YYSYMBOL_BOR = 36,                       /* BOR  */
  YYSYMBOL_APPEND = 37,                    /* APPEND  */
  YYSYMBOL_EQ = 38,                        /* EQ  */
  YYSYMBOL_GE = 39,                        /* GE  */
  YYSYMBOL_GT = 40,                        /* GT  */
  YYSYMBOL_LE = 41,                        /* LE  */
  YYSYMBOL_LT = 42,                        /* LT  */
  YYSYMBOL_NE = 43,                        /* NE  */
  YYSYMBOL_IN = 44,                        /* IN  */
  YYSYMBOL_ARG = 45,                       /* ARG  */
  YYSYMBOL_BLTIN = 46,                     /* BLTIN  */
  YYSYMBOL_BREAK = 47,                     /* BREAK  */
  YYSYMBOL_CLOSE = 48,                     /* CLOSE  */
  YYSYMBOL_CONTINUE = 49,                  /* CONTINUE  */
  YYSYMBOL_DELETE = 50,                    /* DELETE  */
  YYSYMBOL_DO = 51,                        /* DO  */
  YYSYMBOL_EXIT = 52,                      /* EXIT  */
  YYSYMBOL_FOR = 53,                       /* FOR  */
  YYSYMBOL_FUNC = 54,                      /* FUNC  */
  YYSYMBOL_SUB = 55,                       /* SUB  */
  YYSYMBOL_GSUB = 56,                      /* GSUB  */
  YYSYMBOL_IF = 57,                        /* IF  */
  YYSYMBOL_INDEX = 58,                     /* INDEX  */
  YYSYMBOL_LSUBSTR = 59,                   /* LSUBSTR  */
  YYSYMBOL_MATCHFCN = 60,                  /* MATCHFCN  */
  YYSYMBOL_NEXT = 61,                      /* NEXT  */
  YYSYMBOL_NEXTFILE = 62,                  /* NEXTFILE  */
  YYSYMBOL_ADD = 63,                       /* ADD  */
  YYSYMBOL_MINUS = 64,                     /* MINUS  */
  YYSYMBOL_MULT = 65,                      /* MULT  */
  YYSYMBOL_DIVIDE = 66,                    /* DIVIDE  */
  YYSYMBOL_MOD = 67,                       /* MOD  */
  YYSYMBOL_ASSIGN = 68,                    /* ASSIGN  */
  YYSYMBOL_ASGNOP = 69,                    /* ASGNOP  */
  YYSYMBOL_ADDEQ = 70,                     /* ADDEQ  */
  YYSYMBOL_SUBEQ = 71,                     /* SUBEQ  */
  YYSYMBOL_MULTEQ = 72,                    /* MULTEQ  */
  YYSYMBOL_DIVEQ = 73,                     /* DIVEQ  */
  YYSYMBOL_MODEQ = 74,                     /* MODEQ  */
  YYSYMBOL_POWEQ = 75,                     /* POWEQ  */
  YYSYMBOL_PRINT = 76,                     /* PRINT  */
  YYSYMBOL_PRINTF = 77,                    /* PRINTF  */
  YYSYMBOL_SPRINTF = 78,                   /* SPRINTF  */
  YYSYMBOL_ELSE = 79,                      /* ELSE  */
  YYSYMBOL_INTEST = 80,                    /* INTEST  */
  YYSYMBOL_CONDEXPR = 81,                  /* CONDEXPR  */
  YYSYMBOL_POSTINCR = 82,                  /* POSTINCR  */
  YYSYMBOL_PREINCR = 83,                   /* PREINCR  */
  YYSYMBOL_POSTDECR = 84,                  /* POSTDECR  */
  YYSYMBOL_PREDECR = 85,                   /* PREDECR  */
  YYSYMBOL_VAR = 86,                       /* VAR  */
  YYSYMBOL_IVAR = 87,                      /* IVAR  */
  YYSYMBOL_VARNF = 88,                     /* VARNF  */
  YYSYMBOL_CALL = 89,                      /* CALL  */
  YYSYMBOL_NUMBER = 90,                    /* NUMBER  */
  YYSYMBOL_STRING = 91,                    /* STRING  */
  YYSYMBOL_REGEXPR = 92,                   /* REGEXPR  */
  YYSYMBOL_93_ = 93,                       /* '?'  */
  YYSYMBOL_94_ = 94,                       /* ':'  */
  YYSYMBOL_GETLINE = 95,                   /* GETLINE  */
  YYSYMBOL_RETURN = 96,                    /* RETURN  */
  YYSYMBOL_SPLIT = 97,                     /* SPLIT  */
  YYSYMBOL_SUBSTR = 98,                    /* SUBSTR  */
  YYSYMBOL_WHILE = 99,                     /* WHILE  */
  YYSYMBOL_CAT = 100,                      /* CAT  */
  YYSYMBOL_101_ = 101,                     /* '+'  */
  YYSYMBOL_102_ = 102,                     /* '-'  */
  YYSYMBOL_103_ = 103,                     /* '*'  */
  YYSYMBOL_104_ = 104,                     /* '%'  */
  YYSYMBOL_NOT = 105,                      /* NOT  */
  YYSYMBOL_UMINUS = 106,                   /* UMINUS  */
  YYSYMBOL_UPLUS = 107,                    /* UPLUS  */
  YYSYMBOL_POWER = 108,                    /* POWER  */
  YYSYMBOL_DECR = 109,                     /* DECR  */
  YYSYMBOL_INCR = 110,                     /* INCR  */
  YYSYMBOL_INDIRECT = 111,                 /* INDIRECT  */
  YYSYMBOL_LASTTOKEN = 112,                /* LASTTOKEN  */
  YYSYMBOL_YYACCEPT = 113,                 /* $accept  */
  YYSYMBOL_program = 114,                  /* program  */
  YYSYMBOL_and = 115,                      /* and  */
  YYSYMBOL_bor = 116,                      /* bor  */
  YYSYMBOL_comma = 117,                    /* comma  */
  YYSYMBOL_do = 118,                       /* do  */
  YYSYMBOL_else = 119,                     /* else  */
  YYSYMBOL_for = 120,                      /* for  */
  YYSYMBOL_121_1 = 121,                    /* $@1  */
  YYSYMBOL_122_2 = 122,                    /* $@2  */
  YYSYMBOL_123_3 = 123,                    /* $@3  */
  YYSYMBOL_funcname = 124,                 /* funcname  */
  YYSYMBOL_if = 125,                       /* if  */
  YYSYMBOL_lbrace = 126,                   /* lbrace  */
  YYSYMBOL_nl = 127,                       /* nl  */
  YYSYMBOL_opt_nl = 128,                   /* opt_nl  */
  YYSYMBOL_opt_pst = 129,                  /* opt_pst  */
  YYSYMBOL_opt_simple_stmt = 130,          /* opt_simple_stmt  */
  YYSYMBOL_pas = 131,                      /* pas  */
  YYSYMBOL_pa_pat = 132,                   /* pa_pat  */
  YYSYMBOL_pa_stat = 133,                  /* pa_stat  */
  YYSYMBOL_134_4 = 134,                    /* $@4  */
  YYSYMBOL_pa_stats = 135,                 /* pa_stats  */
  YYSYMBOL_patlist = 136,                  /* patlist  */
  YYSYMBOL_ppattern = 137,                 /* ppattern  */
  YYSYMBOL_pattern = 138,                  /* pattern  */
  YYSYMBOL_plist = 139,                    /* plist  */
  YYSYMBOL_pplist = 140,                   /* pplist  */
  YYSYMBOL_prarg = 141,                    /* prarg  */
  YYSYMBOL_print = 142,                    /* print  */
  YYSYMBOL_pst = 143,                      /* pst  */
  YYSYMBOL_rbrace = 144,                   /* rbrace  */
  YYSYMBOL_re = 145,                       /* re  */
  YYSYMBOL_reg_expr = 146,                 /* reg_expr  */
  YYSYMBOL_147_5 = 147,                    /* $@5  */
  YYSYMBOL_rparen = 148,                   /* rparen  */
  YYSYMBOL_simple_stmt = 149,              /* simple_stmt  */
  YYSYMBOL_st = 150,                       /* st  */
  YYSYMBOL_stmt = 151,                     /* stmt  */
  YYSYMBOL_152_6 = 152,                    /* $@6  */
  YYSYMBOL_153_7 = 153,                    /* $@7  */
  YYSYMBOL_154_8 = 154,                    /* $@8  */
  YYSYMBOL_stmtlist = 155,                 /* stmtlist  */
  YYSYMBOL_subop = 156,                    /* subop  */
  YYSYMBOL_term = 157,                     /* term  */
  YYSYMBOL_var = 158,                      /* var  */
  YYSYMBOL_varlist = 159,                  /* varlist  */
  YYSYMBOL_varname = 160,                  /* varname  */
  YYSYMBOL_while = 161                     /* while  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  8
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   4699

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  113
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  185
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  368

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   351


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,   104,     2,     2,
      12,    16,   103,   101,    10,   102,     2,    15,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    94,    14,
       2,     2,     2,    93,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    18,     2,    19,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    11,    13,    17,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    95,    96,
      97,    98,    99,   100,   105,   106,   107,   108,   109,   110,
     111,   112
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    98,    98,   100,   104,   104,   108,   108,   112,   112,
     116,   116,   120,   120,   124,   124,   126,   126,   128,   128,
     133,   134,   138,   142,   142,   146,   146,   150,   151,   155,
     156,   161,   162,   166,   167,   171,   175,   176,   177,   178,
     179,   180,   182,   184,   184,   189,   190,   194,   195,   199,
     200,   202,   204,   206,   207,   212,   213,   214,   215,   216,
     220,   221,   223,   225,   227,   228,   229,   230,   231,   232,
     233,   234,   239,   240,   241,   244,   247,   248,   249,   253,
     254,   258,   259,   263,   264,   265,   269,   269,   273,   273,
     273,   273,   277,   277,   281,   283,   287,   287,   291,   291,
     295,   298,   301,   304,   305,   306,   307,   308,   312,   313,
     317,   319,   321,   321,   321,   323,   324,   325,   326,   327,
     328,   329,   332,   335,   336,   337,   338,   338,   339,   343,
     344,   348,   348,   352,   353,   354,   355,   356,   357,   358,
     359,   360,   361,   362,   363,   364,   365,   366,   367,   368,
     369,   370,   371,   372,   373,   374,   375,   376,   378,   381,
     382,   384,   389,   390,   392,   394,   396,   397,   398,   400,
     405,   407,   412,   414,   416,   420,   421,   422,   423,   427,
     428,   429,   435,   436,   437,   442
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "FIRSTTOKEN",
  "PROGRAM", "PASTAT", "PASTAT2", "XBEGIN", "XEND", "NL", "','", "'{'",
  "'('", "'|'", "';'", "'/'", "')'", "'}'", "'['", "']'", "ARRAY", "MATCH",
  "NOTMATCH", "MATCHOP", "FINAL", "DOT", "ALL", "CCL", "NCCL", "CHAR",
  "OR", "STAR", "QUEST", "PLUS", "EMPTYRE", "AND", "BOR", "APPEND", "EQ",
  "GE", "GT", "LE", "LT", "NE", "IN", "ARG", "BLTIN", "BREAK", "CLOSE",
  "CONTINUE", "DELETE", "DO", "EXIT", "FOR", "FUNC", "SUB", "GSUB", "IF",
  "INDEX", "LSUBSTR", "MATCHFCN", "NEXT", "NEXTFILE", "ADD", "MINUS",
  "MULT", "DIVIDE", "MOD", "ASSIGN", "ASGNOP", "ADDEQ", "SUBEQ", "MULTEQ",
  "DIVEQ", "MODEQ", "POWEQ", "PRINT", "PRINTF", "SPRINTF", "ELSE",
  "INTEST", "CONDEXPR", "POSTINCR", "PREINCR", "POSTDECR", "PREDECR",
  "VAR", "IVAR", "VARNF", "CALL", "NUMBER", "STRING", "REGEXPR", "'?'",
  "':'", "GETLINE", "RETURN", "SPLIT", "SUBSTR", "WHILE", "CAT", "'+'",
  "'-'", "'*'", "'%'", "NOT", "UMINUS", "UPLUS", "POWER", "DECR", "INCR",
  "INDIRECT", "LASTTOKEN", "$accept", "program", "and", "bor", "comma",
  "do", "else", "for", "$@1", "$@2", "$@3", "funcname", "if", "lbrace",
  "nl", "opt_nl", "opt_pst", "opt_simple_stmt", "pas", "pa_pat", "pa_stat",
  "$@4", "pa_stats", "patlist", "ppattern", "pattern", "plist", "pplist",
  "prarg", "print", "pst", "rbrace", "re", "reg_expr", "$@5", "rparen",
  "simple_stmt", "st", "stmt", "$@6", "$@7", "$@8", "stmtlist", "subop",
  "term", "var", "varlist", "varname", "while", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-314)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-32)

#define yytable_value_is_error(Yyn) \
  ((Yyn) == YYTABLE_NINF)

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     681,  -314,  -314,  -314,    29,  1600,  -314,   142,  -314,    40,
      40,  -314,  4229,  -314,  -314,    50,  4588,   -42,  -314,  -314,
      53,    76,    78,  -314,  -314,  -314,    92,  -314,  -314,   -12,
     149,   160,  4588,  4588,  4287,    19,    19,  4588,   812,    90,
    -314,   143,  3515,  -314,  -314,   165,    -6,    11,    36,  -314,
    -314,   812,   812,  2202,    45,   -19,  4044,  4229,  4588,    -6,
     102,  -314,  -314,   172,  4229,  4229,  4229,  4102,  4588,   121,
    4229,  4229,    80,    80,  -314,    80,  -314,  -314,  -314,  -314,
    -314,   177,   144,   144,    -9,  -314,  1752,   179,   182,   144,
     144,  -314,  -314,  1752,   184,   205,  -314,  1426,   812,  3515,
    4345,   144,  -314,   880,  -314,   177,   812,  1600,   122,  4229,
    -314,  -314,  4229,  4229,  4229,  4229,  4229,  4229,    -9,  4229,
    1810,  1868,    -6,  4229,  4403,  4588,  4588,  4588,  4588,  4588,
    4229,  -314,  -314,  4229,   948,  1016,  -314,  -314,  1926,   174,
    1926,   209,  -314,    62,  3515,  2693,   134,  2602,  2602,   107,
    -314,   108,    -6,  4588,  2602,  2602,  -314,   218,  -314,   177,
     218,  -314,  -314,   214,  1694,  -314,  1484,  4229,  -314,  -314,
    1694,  -314,  4229,  -314,  1426,   154,  1084,  4229,  3917,   224,
      30,  -314,    -6,    16,  -314,  -314,  -314,  1426,  4229,  1152,
    -314,    19,  3766,  -314,  3766,  3766,  3766,  3766,  3766,  3766,
    -314,  2784,  -314,  3686,  -314,  3606,  2602,   224,  4588,    80,
      42,    42,    80,    80,    80,  3515,    15,  -314,  -314,  -314,
    3515,    -9,  3515,  -314,  -314,  1926,  -314,   117,  1926,  1926,
    -314,  -314,    -6,     1,  1926,  -314,  -314,  4229,  -314,   222,
    -314,     4,  2875,  -314,  2875,  -314,  -314,  1222,  -314,   239,
     125,  4461,    -9,  4461,  1984,  2042,    -6,  2100,  4588,  4588,
    4588,  4461,  -314,    40,  -314,  -314,  4229,  1926,  1926,    -6,
    -314,  -314,  3515,  -314,    -5,   240,  2966,   234,  3057,   235,
     126,  2304,    21,   151,    -9,   240,   240,   153,  -314,  -314,
    -314,   210,  4229,  4530,  -314,  -314,  3837,  4171,  3986,  3917,
      -6,    -6,    -6,  3917,   812,  3515,  2406,  2508,  -314,  -314,
      40,  -314,  -314,  -314,  -314,  -314,  1926,  -314,  1926,  -314,
     177,  4229,   241,   243,    -9,   139,  4461,  1290,  -314,     5,
    -314,     5,   812,  3148,   242,  3239,  1542,  3333,   240,  4229,
    -314,   210,  3917,  -314,   247,   250,  1358,  -314,  -314,  -314,
     241,   177,  1426,  3424,  -314,  -314,  -314,   240,  1542,  -314,
     144,  1426,   241,  -314,  -314,   240,  1426,  -314
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     3,    88,    89,     0,    33,     2,    30,     1,     0,
       0,    23,     0,    96,   183,   145,     0,     0,   131,   132,
       0,     0,     0,   182,   177,   184,     0,   162,   167,   156,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    36,
      45,    29,    35,    77,    94,     0,    78,   174,   175,    90,
      91,     0,     0,     0,     0,     0,     0,     0,     0,   148,
     174,    20,    21,     0,     0,     0,     0,     0,     0,   155,
       0,     0,   141,   140,    95,   142,   149,   150,   178,   107,
      24,    27,     0,     0,     0,    10,     0,     0,     0,     0,
       0,    86,    87,     0,     0,   112,   117,     0,     0,   106,
      83,     0,   129,     0,   126,    27,     0,    34,     0,     0,
       4,     6,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    76,     0,     0,     0,     0,     0,     0,     0,
       0,   151,   152,     0,     0,     0,     8,   159,     0,     0,
       0,     0,   143,     0,    47,     0,   179,     0,     0,     0,
     146,     0,   154,     0,     0,     0,    25,    28,   128,    27,
     108,   110,   111,   105,     0,   116,     0,     0,   121,   122,
       0,   124,     0,    11,     0,   119,     0,     0,    81,    84,
     103,    58,    59,   174,   125,    40,   130,     0,     0,     0,
      46,    75,    71,    70,    64,    65,    66,    67,    68,    69,
      72,     0,     5,    63,     7,    62,     0,    94,     0,   137,
     134,   135,   136,   138,   139,    60,     0,    41,    42,     9,
      79,     0,    80,    97,   144,     0,   180,     0,     0,     0,
     166,   147,   153,     0,     0,    26,   109,     0,   115,     0,
      32,   175,     0,   123,     0,   113,    12,     0,    92,   120,
       0,     0,     0,     0,     0,     0,    57,     0,     0,     0,
       0,     0,   127,    38,    37,    74,     0,     0,     0,   133,
     176,    73,    48,    98,     0,    43,     0,    94,     0,    94,
       0,     0,     0,    27,     0,    22,   185,     0,    13,   118,
      93,    85,     0,    54,    53,    55,     0,    52,    51,    82,
     100,   101,   102,    49,     0,    61,     0,     0,   181,    99,
       0,   157,   158,   161,   160,   165,     0,   173,     0,   104,
      27,     0,     0,     0,     0,     0,     0,     0,   169,     0,
     168,     0,     0,     0,    94,     0,     0,     0,    18,     0,
      56,     0,    50,    39,     0,     0,     0,   163,   164,   172,
       0,    27,     0,     0,   171,   170,    44,    16,     0,    19,
       0,     0,     0,   114,    17,    14,     0,    15
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -314,  -314,  -129,   -96,    61,  -314,  -314,  -314,  -314,  -314,
    -314,  -314,  -314,    -4,   -73,   -90,   229,  -313,  -314,    86,
     169,  -314,  -314,   -39,    18,   527,  -170,  -314,  -314,  -314,
    -314,  -314,   -32,   -85,  -314,  -203,  -163,   -30,   310,  -314,
    -314,  -314,   -40,  -314,   270,   -16,  -314,    87,  -314
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     4,   120,   121,   225,    95,   247,    96,   366,   361,
     352,    63,    97,    98,   160,   158,     5,   239,     6,    39,
      40,   310,    41,   143,   178,    99,    54,   179,   180,   100,
       7,   249,    43,    44,    55,   275,   101,   161,   102,   174,
     287,   187,   103,    45,    46,    47,   227,    48,   104
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      60,    38,    74,   240,   219,    51,    52,   250,   157,   124,
     219,   134,   135,    69,   219,   188,    60,    60,    60,    76,
      77,    60,   133,   350,   193,   136,    60,   149,   151,     8,
      68,   136,   157,    14,   270,   106,    14,    60,   207,   285,
     319,   286,    60,   258,    61,   362,    14,    62,   284,   254,
      14,    11,    60,   162,   133,   136,   165,   124,   176,   168,
     169,   139,    56,   171,    14,    64,   189,   259,   181,   236,
     260,   184,   136,   141,    23,    24,    25,    23,   224,    25,
     130,   308,   255,    60,   183,   261,   157,    23,    65,    25,
      66,    23,    24,    25,   216,   125,   126,   127,   128,    37,
     105,    11,   129,    38,    67,    23,    24,    25,    60,    60,
      60,    60,    60,    60,   138,   140,    37,   136,   136,   338,
     131,   132,   325,   230,   231,   131,   132,   136,    60,    60,
      37,    60,    60,   273,   238,   136,   136,    60,    60,    60,
     243,   291,   315,   277,   279,   127,   128,   357,    60,   136,
     129,    49,     2,   156,    60,   341,    50,     3,   159,   365,
     156,    70,    60,   153,   254,   320,   294,   254,   254,   254,
     254,   163,    71,   240,   254,   265,    60,   123,    60,    60,
      60,    60,    60,    60,   146,    60,   156,    60,   129,    60,
      60,   166,    60,   321,   167,   240,   172,   255,   282,    60,
     255,   255,   255,   255,    60,   200,    60,   255,   228,   229,
     157,   131,   132,   254,   173,   233,   234,   191,   221,   181,
     226,   181,   181,   181,   223,   181,    60,   235,    60,   181,
     336,   334,   237,   246,   136,   183,   283,   183,   183,   183,
     257,   183,    60,    60,    60,   183,   255,   157,   290,   309,
     312,   314,   323,   241,   324,   339,    60,   273,   348,   304,
      60,   358,    60,   354,   327,    60,   355,   267,   268,   293,
     107,   296,   297,   298,   263,   299,   190,    60,   157,   303,
      60,    60,    60,    60,     0,     0,    59,    60,   274,    60,
      60,    60,   346,     0,   181,     0,     0,     0,     0,     0,
       0,     0,    72,    73,    75,     0,   332,    78,   271,     0,
     183,   140,   122,   344,     0,   345,     0,    60,     0,    60,
     280,    60,     0,   122,     0,     0,    60,     0,    75,     0,
     363,     0,     0,     0,     0,     0,     0,    60,   152,   295,
       0,   316,   318,     0,   342,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   329,   331,   122,
     182,   322,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   140,     0,     0,     0,
       0,     0,     0,     0,   209,   210,   211,   212,   213,   214,
       0,     0,     0,     0,     0,     0,     0,   175,     0,     0,
       0,   340,     0,   186,   122,   122,     0,   122,   122,     0,
       0,     0,     0,   232,   122,   122,     0,     0,     0,     0,
       0,     0,     0,     0,   122,     0,     0,     0,     0,     0,
     122,     0,     0,     0,   186,   186,     0,     0,   256,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   122,     0,   122,   122,   122,   122,   122,   122,
       0,   122,     0,   122,     0,   122,   122,     0,   269,     0,
       0,     0,     0,     0,   245,   122,   186,     0,     0,     0,
     122,     0,   122,     0,     0,     0,     0,   262,     0,   186,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   122,     0,   122,     0,     0,     0,     0,     0,
       0,   182,     0,   182,   182,   182,     0,   182,   300,   301,
     302,   182,    42,     0,     0,     0,     0,     0,     0,    53,
       0,     0,   122,     0,     0,     0,   122,     0,   122,     0,
       0,   122,     0,     0,     0,     0,     0,   289,     0,     0,
       0,     0,     0,   256,     0,     0,   256,   256,   256,   256,
       0,     0,     0,   256,     0,   122,   122,   122,     0,     0,
       0,     0,     0,   144,   145,     0,     0,     0,     0,     0,
       0,   147,   148,   144,   144,     0,   182,   154,   155,     0,
       0,     0,     0,   122,     0,   122,     0,   122,     0,     0,
       0,     0,   256,   164,     0,     0,     0,     0,     0,     0,
     170,     0,     0,   122,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    42,     0,   192,   186,     0,   194,
     195,   196,   197,   198,   199,     0,   201,   203,   205,     0,
     206,     0,     0,     0,     0,     0,   186,   215,     0,     0,
     144,     0,   359,     0,     0,   220,     0,   222,     0,     0,
       0,   364,     0,     0,     0,     0,   367,     0,     0,     0,
       0,   -29,     1,     0,     0,     0,     0,     0,   -29,   -29,
       2,     0,   -29,   -29,   242,     3,   -29,     0,     0,   244,
       0,     0,     0,     0,    53,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    42,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   -29,   -29,     0,   -29,
       0,     0,     0,     0,     0,   -29,   -29,   -29,     0,   -29,
       0,   -29,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   272,     0,     0,   276,   278,     0,     0,   -29,
       0,   281,     0,     0,   144,     0,     0,   -29,   -29,   -29,
     -29,   -29,   -29,     0,     0,     0,   -29,     0,   -29,   -29,
       0,     0,   -29,   -29,     0,     0,   -29,     0,     0,     0,
     -29,   -29,   -29,   305,   306,   307,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    79,     0,     0,     0,     0,     0,    53,
       0,    80,     0,    11,    12,     0,    81,    13,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   333,     0,   335,     0,     0,   337,     0,
       0,     0,     0,     0,     0,     0,     0,    14,    15,    82,
      16,    83,    84,    85,    86,    87,   353,    18,    19,    88,
      20,     0,    21,    89,    90,     0,     0,     0,     0,     0,
       0,    79,     0,     0,     0,     0,     0,     0,    91,    92,
      22,    11,    12,     0,    81,    13,     0,   185,    23,    24,
      25,    26,    27,    28,     0,     0,     0,    29,    93,    30,
      31,    94,     0,    32,    33,     0,     0,    34,     0,     0,
       0,    35,    36,    37,     0,    14,    15,    82,    16,    83,
      84,    85,    86,    87,     0,    18,    19,    88,    20,     0,
      21,    89,    90,     0,     0,     0,     0,     0,     0,    79,
       0,     0,     0,     0,     0,     0,    91,    92,    22,    11,
      12,     0,    81,    13,     0,   217,    23,    24,    25,    26,
      27,    28,     0,     0,     0,    29,    93,    30,    31,    94,
       0,    32,    33,     0,     0,    34,     0,     0,     0,    35,
      36,    37,     0,    14,    15,    82,    16,    83,    84,    85,
      86,    87,     0,    18,    19,    88,    20,     0,    21,    89,
      90,     0,     0,     0,     0,     0,     0,    79,     0,     0,
       0,     0,     0,     0,    91,    92,    22,    11,    12,     0,
      81,    13,     0,   218,    23,    24,    25,    26,    27,    28,
       0,     0,     0,    29,    93,    30,    31,    94,     0,    32,
      33,     0,     0,    34,     0,     0,     0,    35,    36,    37,
       0,    14,    15,    82,    16,    83,    84,    85,    86,    87,
       0,    18,    19,    88,    20,     0,    21,    89,    90,     0,
       0,     0,     0,     0,     0,    79,     0,     0,     0,     0,
       0,     0,    91,    92,    22,    11,    12,     0,    81,    13,
       0,   248,    23,    24,    25,    26,    27,    28,     0,     0,
       0,    29,    93,    30,    31,    94,     0,    32,    33,     0,
       0,    34,     0,     0,     0,    35,    36,    37,     0,    14,
      15,    82,    16,    83,    84,    85,    86,    87,     0,    18,
      19,    88,    20,     0,    21,    89,    90,     0,     0,     0,
       0,     0,     0,    79,     0,     0,     0,     0,     0,     0,
      91,    92,    22,    11,    12,     0,    81,    13,     0,   264,
      23,    24,    25,    26,    27,    28,     0,     0,     0,    29,
      93,    30,    31,    94,     0,    32,    33,     0,     0,    34,
       0,     0,     0,    35,    36,    37,     0,    14,    15,    82,
      16,    83,    84,    85,    86,    87,     0,    18,    19,    88,
      20,     0,    21,    89,    90,     0,     0,     0,     0,     0,
       0,     0,     0,    79,     0,     0,     0,     0,    91,    92,
      22,   288,     0,    11,    12,     0,    81,    13,    23,    24,
      25,    26,    27,    28,     0,     0,     0,    29,    93,    30,
      31,    94,     0,    32,    33,     0,     0,    34,     0,     0,
       0,    35,    36,    37,     0,     0,     0,    14,    15,    82,
      16,    83,    84,    85,    86,    87,     0,    18,    19,    88,
      20,     0,    21,    89,    90,     0,     0,     0,     0,     0,
       0,    79,     0,     0,     0,     0,     0,     0,    91,    92,
      22,    11,    12,     0,    81,    13,     0,   343,    23,    24,
      25,    26,    27,    28,     0,     0,     0,    29,    93,    30,
      31,    94,     0,    32,    33,     0,     0,    34,     0,     0,
       0,    35,    36,    37,     0,    14,    15,    82,    16,    83,
      84,    85,    86,    87,     0,    18,    19,    88,    20,     0,
      21,    89,    90,     0,     0,     0,     0,     0,     0,    79,
       0,     0,     0,     0,     0,     0,    91,    92,    22,    11,
      12,     0,    81,    13,     0,   356,    23,    24,    25,    26,
      27,    28,     0,     0,     0,    29,    93,    30,    31,    94,
       0,    32,    33,     0,     0,    34,     0,     0,     0,    35,
      36,    37,     0,    14,    15,    82,    16,    83,    84,    85,
      86,    87,     0,    18,    19,    88,    20,     0,    21,    89,
      90,     0,     0,     0,     0,     0,     0,    79,     0,     0,
       0,     0,     0,     0,    91,    92,    22,    11,    12,     0,
      81,    13,     0,     0,    23,    24,    25,    26,    27,    28,
       0,     0,     0,    29,    93,    30,    31,    94,     0,    32,
      33,     0,     0,    34,     0,     0,     0,    35,    36,    37,
       0,    14,    15,    82,    16,    83,    84,    85,    86,    87,
       0,    18,    19,    88,    20,    79,    21,    89,    90,     0,
       0,     0,     0,     0,     0,     0,    12,     0,   -31,    13,
       0,     0,    91,    92,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,     0,     0,
       0,    29,    93,    30,    31,    94,     0,    32,    33,    14,
      15,    34,    16,     0,    84,    35,    36,    37,     0,    18,
      19,     0,    20,    79,    21,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    12,     0,     0,    13,   -31,     0,
      91,    92,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,     0,     0,    29,
       0,    30,    31,     0,     0,    32,    33,    14,    15,    34,
      16,     0,    84,    35,    36,    37,     0,    18,    19,     0,
      20,     0,    21,     0,     0,     0,     0,     9,    10,     0,
       0,    11,    12,     0,     0,    13,     0,     0,    91,    92,
      22,     0,     0,     0,     0,     0,     0,     0,    23,    24,
      25,    26,    27,    28,     0,     0,     0,    29,     0,    30,
      31,     0,     0,    32,    33,    14,    15,    34,    16,     0,
       0,    35,    36,    37,    17,    18,    19,     0,    20,     0,
      21,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    22,     0,
       0,     0,     0,     0,     0,     0,    23,    24,    25,    26,
      27,    28,     0,     0,     0,    29,     0,    30,    31,     0,
       0,    32,    33,   156,     0,    34,    57,   108,   159,    35,
      36,    37,     0,     0,     0,     0,     0,   109,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   110,
     111,     0,   112,   113,   114,   115,   116,   117,   118,    14,
      15,     0,    16,     0,     0,     0,     0,     0,     0,    18,
      19,     0,    20,     0,    21,     0,     0,     0,     0,     0,
       0,   156,     0,     0,    12,     0,   159,    13,     0,     0,
       0,     0,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,   119,     0,    29,
       0,    30,    31,     0,     0,    32,    33,    14,    15,    58,
      16,     0,     0,    35,    36,    37,     0,    18,    19,     0,
      20,     0,    21,     0,     0,     0,     0,     0,     0,   202,
       0,     0,    12,     0,     0,    13,     0,     0,     0,     0,
      22,     0,     0,     0,     0,     0,     0,     0,    23,    24,
      25,    26,    27,    28,     0,     0,     0,    29,     0,    30,
      31,     0,     0,    32,    33,    14,    15,    34,    16,     0,
       0,    35,    36,    37,     0,    18,    19,     0,    20,     0,
      21,     0,     0,     0,     0,     0,     0,   204,     0,     0,
      12,     0,     0,    13,     0,     0,     0,     0,    22,     0,
       0,     0,     0,     0,     0,     0,    23,    24,    25,    26,
      27,    28,     0,     0,     0,    29,     0,    30,    31,     0,
       0,    32,    33,    14,    15,    34,    16,     0,     0,    35,
      36,    37,     0,    18,    19,     0,    20,     0,    21,     0,
       0,     0,     0,     0,     0,   219,     0,     0,    12,     0,
       0,    13,     0,     0,     0,     0,    22,     0,     0,     0,
       0,     0,     0,     0,    23,    24,    25,    26,    27,    28,
       0,     0,     0,    29,     0,    30,    31,     0,     0,    32,
      33,    14,    15,    34,    16,     0,     0,    35,    36,    37,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,   202,     0,     0,   292,     0,     0,    13,
       0,     0,     0,     0,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,     0,     0,
       0,    29,     0,    30,    31,     0,     0,    32,    33,    14,
      15,    34,    16,     0,     0,    35,    36,    37,     0,    18,
      19,     0,    20,     0,    21,     0,     0,     0,     0,     0,
       0,   204,     0,     0,   292,     0,     0,    13,     0,     0,
       0,     0,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,     0,     0,    29,
       0,    30,    31,     0,     0,    32,    33,    14,    15,    34,
      16,     0,     0,    35,    36,    37,     0,    18,    19,     0,
      20,     0,    21,     0,     0,     0,     0,     0,     0,   219,
       0,     0,   292,     0,     0,    13,     0,     0,     0,     0,
      22,     0,     0,     0,     0,     0,     0,     0,    23,    24,
      25,    26,    27,    28,     0,     0,     0,    29,     0,    30,
      31,     0,     0,    32,    33,    14,    15,    34,    16,     0,
       0,    35,    36,    37,     0,    18,    19,     0,    20,     0,
      21,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    22,     0,
       0,     0,     0,     0,     0,     0,    23,    24,    25,    26,
      27,    28,     0,     0,     0,    29,     0,    30,    31,     0,
       0,    32,    33,     0,     0,    34,     0,     0,     0,    35,
      36,    37,   136,     0,    57,   108,     0,     0,   137,     0,
       0,     0,     0,     0,     0,   109,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   110,   111,     0,
     112,   113,   114,   115,   116,   117,   118,    14,    15,     0,
      16,     0,     0,     0,     0,     0,     0,    18,    19,     0,
      20,     0,    21,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      22,     0,     0,     0,     0,     0,     0,     0,    23,    24,
      25,    26,    27,    28,     0,   119,     0,    29,     0,    30,
      31,     0,     0,    32,    33,     0,     0,    58,     0,     0,
       0,    35,    36,    37,   136,     0,    57,   108,     0,     0,
     317,     0,     0,     0,     0,     0,     0,   109,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   110,
     111,     0,   112,   113,   114,   115,   116,   117,   118,    14,
      15,     0,    16,     0,     0,     0,     0,     0,     0,    18,
      19,     0,    20,     0,    21,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,   119,     0,    29,
       0,    30,    31,     0,     0,    32,    33,     0,     0,    58,
       0,     0,     0,    35,    36,    37,   136,     0,    57,   108,
       0,     0,   328,     0,     0,     0,     0,     0,     0,   109,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   110,   111,     0,   112,   113,   114,   115,   116,   117,
     118,    14,    15,     0,    16,     0,     0,     0,     0,     0,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,     0,   119,
       0,    29,     0,    30,    31,     0,     0,    32,    33,     0,
       0,    58,     0,     0,     0,    35,    36,    37,   136,     0,
      57,   108,     0,     0,   330,     0,     0,     0,     0,     0,
       0,   109,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   110,   111,     0,   112,   113,   114,   115,
     116,   117,   118,    14,    15,     0,    16,     0,     0,     0,
       0,     0,     0,    18,    19,     0,    20,     0,    21,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    22,     0,     0,     0,
       0,     0,     0,     0,    23,    24,    25,    26,    27,    28,
       0,   119,     0,    29,     0,    30,    31,     0,     0,    32,
      33,     0,   136,    58,    57,   108,     0,    35,    36,    37,
       0,     0,     0,     0,     0,   109,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   110,   111,     0,
     112,   113,   114,   115,   116,   117,   118,    14,    15,     0,
      16,     0,     0,     0,     0,     0,     0,    18,    19,     0,
      20,     0,    21,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      22,     0,     0,     0,     0,     0,     0,     0,    23,    24,
      25,    26,    27,    28,     0,   119,     0,    29,     0,    30,
      31,     0,     0,    32,    33,    57,   108,    58,     0,   137,
       0,    35,    36,    37,     0,     0,   109,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   110,   111,
       0,   112,   113,   114,   115,   116,   117,   118,    14,    15,
       0,    16,     0,     0,     0,     0,     0,     0,    18,    19,
       0,    20,     0,    21,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    22,     0,     0,     0,     0,     0,     0,     0,    23,
      24,    25,    26,    27,    28,     0,   119,     0,    29,     0,
      30,    31,     0,     0,    32,    33,    57,   108,    58,     0,
       0,     0,    35,    36,    37,     0,     0,   109,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   110,
     111,     0,   112,   113,   114,   115,   116,   117,   118,    14,
      15,     0,    16,     0,     0,     0,     0,     0,     0,    18,
      19,     0,    20,     0,    21,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,   119,   266,    29,
       0,    30,    31,     0,     0,    32,    33,    57,   108,    58,
       0,   273,     0,    35,    36,    37,     0,     0,   109,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     110,   111,     0,   112,   113,   114,   115,   116,   117,   118,
      14,    15,     0,    16,     0,     0,     0,     0,     0,     0,
      18,    19,     0,    20,     0,    21,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    22,     0,     0,     0,     0,     0,     0,
       0,    23,    24,    25,    26,    27,    28,     0,   119,     0,
      29,     0,    30,    31,     0,     0,    32,    33,    57,   108,
      58,     0,   311,     0,    35,    36,    37,     0,     0,   109,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   110,   111,     0,   112,   113,   114,   115,   116,   117,
     118,    14,    15,     0,    16,     0,     0,     0,     0,     0,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,     0,   119,
       0,    29,     0,    30,    31,     0,     0,    32,    33,    57,
     108,    58,     0,   313,     0,    35,    36,    37,     0,     0,
     109,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   110,   111,     0,   112,   113,   114,   115,   116,
     117,   118,    14,    15,     0,    16,     0,     0,     0,     0,
       0,     0,    18,    19,     0,    20,     0,    21,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    22,     0,     0,     0,     0,
       0,     0,     0,    23,    24,    25,    26,    27,    28,     0,
     119,     0,    29,     0,    30,    31,     0,     0,    32,    33,
      57,   108,    58,     0,   347,     0,    35,    36,    37,     0,
       0,   109,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   110,   111,     0,   112,   113,   114,   115,
     116,   117,   118,    14,    15,     0,    16,     0,     0,     0,
       0,     0,     0,    18,    19,     0,    20,     0,    21,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    22,     0,     0,     0,
       0,     0,     0,     0,    23,    24,    25,    26,    27,    28,
       0,   119,     0,    29,     0,    30,    31,     0,     0,    32,
      33,    57,   108,    58,     0,   349,     0,    35,    36,    37,
       0,     0,   109,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   110,   111,     0,   112,   113,   114,
     115,   116,   117,   118,    14,    15,     0,    16,     0,     0,
       0,     0,     0,     0,    18,    19,     0,    20,     0,    21,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    22,     0,     0,
       0,     0,     0,     0,     0,    23,    24,    25,    26,    27,
      28,     0,   119,     0,    29,     0,    30,    31,     0,     0,
      32,    33,     0,     0,    58,    57,   108,   351,    35,    36,
      37,     0,     0,     0,     0,     0,   109,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   110,   111,
       0,   112,   113,   114,   115,   116,   117,   118,    14,    15,
       0,    16,     0,     0,     0,     0,     0,     0,    18,    19,
       0,    20,     0,    21,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    22,     0,     0,     0,     0,     0,     0,     0,    23,
      24,    25,    26,    27,    28,     0,   119,     0,    29,     0,
      30,    31,     0,     0,    32,    33,    57,   108,    58,     0,
     360,     0,    35,    36,    37,     0,     0,   109,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   110,
     111,     0,   112,   113,   114,   115,   116,   117,   118,    14,
      15,     0,    16,     0,     0,     0,     0,     0,     0,    18,
      19,     0,    20,     0,    21,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,   119,     0,    29,
       0,    30,    31,     0,     0,    32,    33,    57,   108,    58,
       0,     0,     0,    35,    36,    37,     0,     0,   109,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     110,   111,     0,   112,   113,   114,   115,   116,   117,   118,
      14,    15,     0,    16,     0,     0,     0,     0,     0,     0,
      18,    19,     0,    20,     0,    21,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    22,     0,     0,     0,     0,     0,     0,
       0,    23,    24,    25,    26,    27,    28,     0,   119,     0,
      29,     0,    30,    31,     0,     0,    32,    33,    57,   108,
      58,     0,     0,     0,    35,    36,    37,     0,     0,   109,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   110,     0,     0,   112,   113,   114,   115,   116,   117,
     118,    14,    15,     0,    16,     0,     0,     0,     0,     0,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,    57,   108,
       0,    29,     0,    30,    31,     0,     0,    32,    33,   109,
       0,    58,     0,     0,     0,    35,    36,    37,     0,     0,
       0,     0,     0,     0,   112,   113,   114,   115,   116,   117,
     118,    14,    15,     0,    16,     0,     0,     0,     0,     0,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,    57,   -32,
       0,    29,     0,    30,    31,     0,     0,    32,    33,   -32,
       0,    58,     0,     0,     0,    35,    36,    37,     0,     0,
       0,     0,     0,     0,   -32,   -32,   -32,   -32,   -32,   -32,
     -32,    14,    15,     0,    16,     0,     0,     0,     0,     0,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    22,     0,     0,     0,     0,    57,
       0,     0,    23,    24,    25,    26,    27,    28,     0,     0,
     251,     0,     0,    30,    31,     0,     0,    32,    33,     0,
       0,    58,   110,   111,     0,    35,    36,    37,     0,     0,
       0,   252,    14,    15,     0,    16,     0,     0,     0,     0,
       0,     0,    18,    19,     0,    20,     0,    21,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    22,     0,     0,     0,     0,
       0,     0,     0,    23,    24,    25,    26,    27,    28,    57,
     253,   326,    29,     0,    30,    31,     0,     0,    32,    33,
     251,     0,    58,     0,     0,     0,    35,    36,    37,     0,
       0,     0,   110,   111,     0,     0,     0,     0,     0,     0,
       0,   252,    14,    15,     0,    16,     0,     0,     0,     0,
       0,     0,    18,    19,     0,    20,     0,    21,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    22,     0,     0,    57,     0,
       0,     0,     0,    23,    24,    25,    26,    27,    28,   251,
     253,     0,    29,     0,    30,    31,     0,     0,    32,    33,
       0,   110,    58,     0,     0,     0,    35,    36,    37,     0,
     252,    14,    15,     0,    16,     0,     0,     0,     0,     0,
       0,    18,    19,     0,    20,     0,    21,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    12,     0,     0,    13,
     142,     0,     0,     0,    22,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    25,    26,    27,    28,     0,     0,
       0,    29,     0,    30,    31,     0,     0,    32,    33,    14,
      15,    58,    16,     0,     0,    35,    36,    37,     0,    18,
      19,     0,    20,     0,    21,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    12,     0,     0,    13,   150,     0,
       0,     0,    22,     0,     0,     0,     0,     0,     0,     0,
      23,    24,    25,    26,    27,    28,     0,     0,     0,    29,
       0,    30,    31,     0,     0,    32,    33,    14,    15,    34,
      16,     0,     0,    35,    36,    37,     0,    18,    19,     0,
      20,     0,    21,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      22,     0,     0,    57,     0,     0,     0,     0,    23,    24,
      25,    26,    27,    28,   251,     0,     0,    29,     0,    30,
      31,     0,     0,    32,    33,     0,     0,    34,     0,     0,
       0,    35,    36,    37,     0,   252,    14,    15,     0,    16,
       0,     0,     0,     0,     0,     0,    18,    19,     0,    20,
       0,    21,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    12,     0,     0,    13,     0,     0,     0,     0,    22,
       0,     0,     0,     0,     0,     0,     0,    23,    24,    25,
      26,    27,    28,     0,     0,     0,    29,     0,    30,    31,
       0,     0,    32,    33,    14,    15,    58,    16,     0,     0,
      35,    36,    37,     0,    18,    19,     0,    20,     0,    21,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    57,
       0,     0,    13,     0,     0,     0,     0,    22,     0,     0,
       0,     0,     0,     0,     0,    23,    24,    25,    26,    27,
      28,     0,     0,     0,    29,     0,    30,    31,     0,     0,
      32,    33,    14,    15,    34,    16,     0,     0,    35,    36,
      37,     0,    18,    19,     0,    20,     0,    21,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   177,     0,     0,
      13,     0,     0,     0,     0,    22,     0,     0,     0,     0,
       0,     0,     0,    23,    24,    25,    26,    27,    28,     0,
       0,     0,    29,     0,    30,    31,     0,     0,    32,    33,
      14,    15,    34,    16,     0,     0,    35,    36,    37,     0,
      18,    19,     0,    20,     0,    21,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    57,     0,     0,     0,     0,
       0,     0,     0,    22,     0,     0,     0,     0,     0,     0,
       0,    23,    24,    25,    26,    27,    28,     0,     0,     0,
      29,     0,    30,    31,     0,     0,    32,    33,    14,    15,
      34,    16,     0,     0,    35,    36,    37,     0,    18,    19,
       0,    20,     0,    21,     0,     0,     0,     0,     0,     0,
       0,     0,   208,   292,     0,     0,    13,     0,     0,     0,
       0,    22,     0,     0,     0,     0,     0,     0,     0,    23,
      24,    25,    26,    27,    28,     0,     0,     0,    29,     0,
      30,    31,     0,     0,    32,    33,    14,    15,    58,    16,
       0,     0,    35,    36,    37,     0,    18,    19,     0,    20,
       0,    21,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    22,
       0,     0,    57,     0,     0,     0,     0,    23,    24,    25,
      26,    27,    28,   -32,     0,     0,    29,     0,    30,    31,
       0,     0,    32,    33,     0,     0,    34,     0,     0,     0,
      35,    36,    37,     0,   -32,    14,    15,     0,    16,     0,
       0,     0,     0,     0,     0,    18,    19,     0,    20,     0,
      21,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      57,     0,     0,     0,     0,     0,     0,     0,    22,     0,
       0,     0,     0,     0,     0,     0,    23,    24,    25,    26,
      27,    28,     0,     0,     0,     0,     0,    30,    31,     0,
       0,    32,    33,    14,    15,    58,    16,     0,     0,    35,
      36,    37,     0,    18,    19,     0,    20,     0,    21,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    22,     0,     0,     0,
       0,     0,     0,     0,    23,    24,    25,    26,    27,    28,
       0,     0,     0,    29,     0,    30,    31,     0,     0,    32,
      33,     0,     0,    58,     0,     0,     0,    35,    36,    37
};

static const yytype_int16 yycheck[] =
{
      16,     5,    34,   166,     9,     9,    10,   177,    81,    15,
       9,    51,    52,    29,     9,   105,    32,    33,    34,    35,
      36,    37,    18,   336,   109,    10,    42,    66,    67,     0,
      42,    10,   105,    45,    19,    39,    45,    53,   123,   242,
      19,   244,    58,    13,    86,   358,    45,    89,    44,   178,
      45,    11,    68,    83,    18,    10,    86,    15,    98,    89,
      90,    16,    12,    93,    45,    12,   106,    37,   100,   159,
      40,   101,    10,    92,    86,    87,    88,    86,    16,    88,
      69,    86,   178,    99,   100,    69,   159,    86,    12,    88,
      12,    86,    87,    88,   133,   101,   102,   103,   104,   111,
      10,    11,   108,   107,    12,    86,    87,    88,   124,   125,
     126,   127,   128,   129,    53,    54,   111,    10,    10,   322,
     109,   110,   292,    16,    16,   109,   110,    10,   144,   145,
     111,   147,   148,    16,   164,    10,    10,   153,   154,   155,
     170,    16,    16,   228,   229,   103,   104,   350,   164,    10,
     108,     9,     9,     9,   170,    16,    14,    14,    14,   362,
       9,    12,   178,    42,   293,    14,   251,   296,   297,   298,
     299,    84,    12,   336,   303,   191,   192,    12,   194,   195,
     196,   197,   198,   199,    12,   201,     9,   203,   108,   205,
     206,    12,   208,   283,    12,   358,    12,   293,   237,   215,
     296,   297,   298,   299,   220,   118,   222,   303,   147,   148,
     283,   109,   110,   342,     9,   154,   155,    95,    44,   251,
      86,   253,   254,   255,    15,   257,   242,     9,   244,   261,
     320,   316,    18,    79,    10,   251,    14,   253,   254,   255,
     179,   257,   258,   259,   260,   261,   342,   320,     9,     9,
      16,    16,    99,   166,    44,    12,   272,    16,    16,   263,
     276,   351,   278,    16,   304,   281,    16,   206,   207,   251,
      41,   253,   254,   255,   188,   257,   107,   293,   351,   261,
     296,   297,   298,   299,    -1,    -1,    16,   303,   227,   305,
     306,   307,   332,    -1,   326,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    32,    33,    34,    -1,   310,    37,   221,    -1,
     326,   250,    42,   329,    -1,   331,    -1,   333,    -1,   335,
     233,   337,    -1,    53,    -1,    -1,   342,    -1,    58,    -1,
     360,    -1,    -1,    -1,    -1,    -1,    -1,   353,    68,   252,
      -1,   280,   281,    -1,   326,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   306,   307,    99,
     100,   284,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   325,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   124,   125,   126,   127,   128,   129,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    97,    -1,    -1,
      -1,   324,    -1,   103,   144,   145,    -1,   147,   148,    -1,
      -1,    -1,    -1,   153,   154,   155,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   164,    -1,    -1,    -1,    -1,    -1,
     170,    -1,    -1,    -1,   134,   135,    -1,    -1,   178,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   192,    -1,   194,   195,   196,   197,   198,   199,
      -1,   201,    -1,   203,    -1,   205,   206,    -1,   208,    -1,
      -1,    -1,    -1,    -1,   174,   215,   176,    -1,    -1,    -1,
     220,    -1,   222,    -1,    -1,    -1,    -1,   187,    -1,   189,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   242,    -1,   244,    -1,    -1,    -1,    -1,    -1,
      -1,   251,    -1,   253,   254,   255,    -1,   257,   258,   259,
     260,   261,     5,    -1,    -1,    -1,    -1,    -1,    -1,    12,
      -1,    -1,   272,    -1,    -1,    -1,   276,    -1,   278,    -1,
      -1,   281,    -1,    -1,    -1,    -1,    -1,   247,    -1,    -1,
      -1,    -1,    -1,   293,    -1,    -1,   296,   297,   298,   299,
      -1,    -1,    -1,   303,    -1,   305,   306,   307,    -1,    -1,
      -1,    -1,    -1,    56,    57,    -1,    -1,    -1,    -1,    -1,
      -1,    64,    65,    66,    67,    -1,   326,    70,    71,    -1,
      -1,    -1,    -1,   333,    -1,   335,    -1,   337,    -1,    -1,
      -1,    -1,   342,    86,    -1,    -1,    -1,    -1,    -1,    -1,
      93,    -1,    -1,   353,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   107,    -1,   109,   327,    -1,   112,
     113,   114,   115,   116,   117,    -1,   119,   120,   121,    -1,
     123,    -1,    -1,    -1,    -1,    -1,   346,   130,    -1,    -1,
     133,    -1,   352,    -1,    -1,   138,    -1,   140,    -1,    -1,
      -1,   361,    -1,    -1,    -1,    -1,   366,    -1,    -1,    -1,
      -1,     0,     1,    -1,    -1,    -1,    -1,    -1,     7,     8,
       9,    -1,    11,    12,   167,    14,    15,    -1,    -1,   172,
      -1,    -1,    -1,    -1,   177,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   188,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    45,    46,    -1,    48,
      -1,    -1,    -1,    -1,    -1,    54,    55,    56,    -1,    58,
      -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   225,    -1,    -1,   228,   229,    -1,    -1,    78,
      -1,   234,    -1,    -1,   237,    -1,    -1,    86,    87,    88,
      89,    90,    91,    -1,    -1,    -1,    95,    -1,    97,    98,
      -1,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,    -1,
     109,   110,   111,   266,   267,   268,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     1,    -1,    -1,    -1,    -1,    -1,   292,
      -1,     9,    -1,    11,    12,    -1,    14,    15,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   316,    -1,   318,    -1,    -1,   321,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    45,    46,    47,
      48,    49,    50,    51,    52,    53,   339,    55,    56,    57,
      58,    -1,    60,    61,    62,    -1,    -1,    -1,    -1,    -1,
      -1,     1,    -1,    -1,    -1,    -1,    -1,    -1,    76,    77,
      78,    11,    12,    -1,    14,    15,    -1,    17,    86,    87,
      88,    89,    90,    91,    -1,    -1,    -1,    95,    96,    97,
      98,    99,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,
      -1,   109,   110,   111,    -1,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    -1,    55,    56,    57,    58,    -1,
      60,    61,    62,    -1,    -1,    -1,    -1,    -1,    -1,     1,
      -1,    -1,    -1,    -1,    -1,    -1,    76,    77,    78,    11,
      12,    -1,    14,    15,    -1,    17,    86,    87,    88,    89,
      90,    91,    -1,    -1,    -1,    95,    96,    97,    98,    99,
      -1,   101,   102,    -1,    -1,   105,    -1,    -1,    -1,   109,
     110,   111,    -1,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    -1,    55,    56,    57,    58,    -1,    60,    61,
      62,    -1,    -1,    -1,    -1,    -1,    -1,     1,    -1,    -1,
      -1,    -1,    -1,    -1,    76,    77,    78,    11,    12,    -1,
      14,    15,    -1,    17,    86,    87,    88,    89,    90,    91,
      -1,    -1,    -1,    95,    96,    97,    98,    99,    -1,   101,
     102,    -1,    -1,   105,    -1,    -1,    -1,   109,   110,   111,
      -1,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      -1,    55,    56,    57,    58,    -1,    60,    61,    62,    -1,
      -1,    -1,    -1,    -1,    -1,     1,    -1,    -1,    -1,    -1,
      -1,    -1,    76,    77,    78,    11,    12,    -1,    14,    15,
      -1,    17,    86,    87,    88,    89,    90,    91,    -1,    -1,
      -1,    95,    96,    97,    98,    99,    -1,   101,   102,    -1,
      -1,   105,    -1,    -1,    -1,   109,   110,   111,    -1,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    -1,    55,
      56,    57,    58,    -1,    60,    61,    62,    -1,    -1,    -1,
      -1,    -1,    -1,     1,    -1,    -1,    -1,    -1,    -1,    -1,
      76,    77,    78,    11,    12,    -1,    14,    15,    -1,    17,
      86,    87,    88,    89,    90,    91,    -1,    -1,    -1,    95,
      96,    97,    98,    99,    -1,   101,   102,    -1,    -1,   105,
      -1,    -1,    -1,   109,   110,   111,    -1,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    -1,    55,    56,    57,
      58,    -1,    60,    61,    62,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     1,    -1,    -1,    -1,    -1,    76,    77,
      78,     9,    -1,    11,    12,    -1,    14,    15,    86,    87,
      88,    89,    90,    91,    -1,    -1,    -1,    95,    96,    97,
      98,    99,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,
      -1,   109,   110,   111,    -1,    -1,    -1,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    -1,    55,    56,    57,
      58,    -1,    60,    61,    62,    -1,    -1,    -1,    -1,    -1,
      -1,     1,    -1,    -1,    -1,    -1,    -1,    -1,    76,    77,
      78,    11,    12,    -1,    14,    15,    -1,    17,    86,    87,
      88,    89,    90,    91,    -1,    -1,    -1,    95,    96,    97,
      98,    99,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,
      -1,   109,   110,   111,    -1,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    -1,    55,    56,    57,    58,    -1,
      60,    61,    62,    -1,    -1,    -1,    -1,    -1,    -1,     1,
      -1,    -1,    -1,    -1,    -1,    -1,    76,    77,    78,    11,
      12,    -1,    14,    15,    -1,    17,    86,    87,    88,    89,
      90,    91,    -1,    -1,    -1,    95,    96,    97,    98,    99,
      -1,   101,   102,    -1,    -1,   105,    -1,    -1,    -1,   109,
     110,   111,    -1,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    -1,    55,    56,    57,    58,    -1,    60,    61,
      62,    -1,    -1,    -1,    -1,    -1,    -1,     1,    -1,    -1,
      -1,    -1,    -1,    -1,    76,    77,    78,    11,    12,    -1,
      14,    15,    -1,    -1,    86,    87,    88,    89,    90,    91,
      -1,    -1,    -1,    95,    96,    97,    98,    99,    -1,   101,
     102,    -1,    -1,   105,    -1,    -1,    -1,   109,   110,   111,
      -1,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      -1,    55,    56,    57,    58,     1,    60,    61,    62,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    12,    -1,    14,    15,
      -1,    -1,    76,    77,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    -1,    -1,
      -1,    95,    96,    97,    98,    99,    -1,   101,   102,    45,
      46,   105,    48,    -1,    50,   109,   110,   111,    -1,    55,
      56,    -1,    58,     1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    12,    -1,    -1,    15,    16,    -1,
      76,    77,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    -1,    -1,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    45,    46,   105,
      48,    -1,    50,   109,   110,   111,    -1,    55,    56,    -1,
      58,    -1,    60,    -1,    -1,    -1,    -1,     7,     8,    -1,
      -1,    11,    12,    -1,    -1,    15,    -1,    -1,    76,    77,
      78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,    87,
      88,    89,    90,    91,    -1,    -1,    -1,    95,    -1,    97,
      98,    -1,    -1,   101,   102,    45,    46,   105,    48,    -1,
      -1,   109,   110,   111,    54,    55,    56,    -1,    58,    -1,
      60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    86,    87,    88,    89,
      90,    91,    -1,    -1,    -1,    95,    -1,    97,    98,    -1,
      -1,   101,   102,     9,    -1,   105,    12,    13,    14,   109,
     110,   111,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,
      36,    -1,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    55,
      56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,     9,    -1,    -1,    12,    -1,    14,    15,    -1,    -1,
      -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    93,    -1,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    45,    46,   105,
      48,    -1,    -1,   109,   110,   111,    -1,    55,    56,    -1,
      58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,     9,
      -1,    -1,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,    87,
      88,    89,    90,    91,    -1,    -1,    -1,    95,    -1,    97,
      98,    -1,    -1,   101,   102,    45,    46,   105,    48,    -1,
      -1,   109,   110,   111,    -1,    55,    56,    -1,    58,    -1,
      60,    -1,    -1,    -1,    -1,    -1,    -1,     9,    -1,    -1,
      12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    78,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    86,    87,    88,    89,
      90,    91,    -1,    -1,    -1,    95,    -1,    97,    98,    -1,
      -1,   101,   102,    45,    46,   105,    48,    -1,    -1,   109,
     110,   111,    -1,    55,    56,    -1,    58,    -1,    60,    -1,
      -1,    -1,    -1,    -1,    -1,     9,    -1,    -1,    12,    -1,
      -1,    15,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    86,    87,    88,    89,    90,    91,
      -1,    -1,    -1,    95,    -1,    97,    98,    -1,    -1,   101,
     102,    45,    46,   105,    48,    -1,    -1,   109,   110,   111,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,     9,    -1,    -1,    12,    -1,    -1,    15,
      -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    -1,    -1,
      -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,    45,
      46,   105,    48,    -1,    -1,   109,   110,   111,    -1,    55,
      56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,     9,    -1,    -1,    12,    -1,    -1,    15,    -1,    -1,
      -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    -1,    -1,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    45,    46,   105,
      48,    -1,    -1,   109,   110,   111,    -1,    55,    56,    -1,
      58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,     9,
      -1,    -1,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,    87,
      88,    89,    90,    91,    -1,    -1,    -1,    95,    -1,    97,
      98,    -1,    -1,   101,   102,    45,    46,   105,    48,    -1,
      -1,   109,   110,   111,    -1,    55,    56,    -1,    58,    -1,
      60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    86,    87,    88,    89,
      90,    91,    -1,    -1,    -1,    95,    -1,    97,    98,    -1,
      -1,   101,   102,    -1,    -1,   105,    -1,    -1,    -1,   109,
     110,   111,    10,    -1,    12,    13,    -1,    -1,    16,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    36,    -1,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    -1,    -1,    -1,    -1,    -1,    -1,    55,    56,    -1,
      58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,    87,
      88,    89,    90,    91,    -1,    93,    -1,    95,    -1,    97,
      98,    -1,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,
      -1,   109,   110,   111,    10,    -1,    12,    13,    -1,    -1,
      16,    -1,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,
      36,    -1,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    55,
      56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    93,    -1,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    -1,    -1,   105,
      -1,    -1,    -1,   109,   110,   111,    10,    -1,    12,    13,
      -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    35,    36,    -1,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    -1,    93,
      -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,    -1,
      -1,   105,    -1,    -1,    -1,   109,   110,   111,    10,    -1,
      12,    13,    -1,    -1,    16,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    35,    36,    -1,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    -1,    -1,    -1,
      -1,    -1,    -1,    55,    56,    -1,    58,    -1,    60,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    86,    87,    88,    89,    90,    91,
      -1,    93,    -1,    95,    -1,    97,    98,    -1,    -1,   101,
     102,    -1,    10,   105,    12,    13,    -1,   109,   110,   111,
      -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    36,    -1,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    -1,
      48,    -1,    -1,    -1,    -1,    -1,    -1,    55,    56,    -1,
      58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,    87,
      88,    89,    90,    91,    -1,    93,    -1,    95,    -1,    97,
      98,    -1,    -1,   101,   102,    12,    13,   105,    -1,    16,
      -1,   109,   110,   111,    -1,    -1,    23,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    36,
      -1,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    55,    56,
      -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,
      87,    88,    89,    90,    91,    -1,    93,    -1,    95,    -1,
      97,    98,    -1,    -1,   101,   102,    12,    13,   105,    -1,
      -1,    -1,   109,   110,   111,    -1,    -1,    23,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,
      36,    -1,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    55,
      56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    93,    94,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    12,    13,   105,
      -1,    16,    -1,   109,   110,   111,    -1,    -1,    23,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      35,    36,    -1,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,
      55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    86,    87,    88,    89,    90,    91,    -1,    93,    -1,
      95,    -1,    97,    98,    -1,    -1,   101,   102,    12,    13,
     105,    -1,    16,    -1,   109,   110,   111,    -1,    -1,    23,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    35,    36,    -1,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    -1,    93,
      -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,    12,
      13,   105,    -1,    16,    -1,   109,   110,   111,    -1,    -1,
      23,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    35,    36,    -1,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,
      -1,    -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    86,    87,    88,    89,    90,    91,    -1,
      93,    -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,
      12,    13,   105,    -1,    16,    -1,   109,   110,   111,    -1,
      -1,    23,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    35,    36,    -1,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    -1,    48,    -1,    -1,    -1,
      -1,    -1,    -1,    55,    56,    -1,    58,    -1,    60,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    86,    87,    88,    89,    90,    91,
      -1,    93,    -1,    95,    -1,    97,    98,    -1,    -1,   101,
     102,    12,    13,   105,    -1,    16,    -1,   109,   110,   111,
      -1,    -1,    23,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    35,    36,    -1,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    -1,    48,    -1,    -1,
      -1,    -1,    -1,    -1,    55,    56,    -1,    58,    -1,    60,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    86,    87,    88,    89,    90,
      91,    -1,    93,    -1,    95,    -1,    97,    98,    -1,    -1,
     101,   102,    -1,    -1,   105,    12,    13,    14,   109,   110,
     111,    -1,    -1,    -1,    -1,    -1,    23,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,    36,
      -1,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    55,    56,
      -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,
      87,    88,    89,    90,    91,    -1,    93,    -1,    95,    -1,
      97,    98,    -1,    -1,   101,   102,    12,    13,   105,    -1,
      16,    -1,   109,   110,   111,    -1,    -1,    23,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    35,
      36,    -1,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,    55,
      56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    93,    -1,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    12,    13,   105,
      -1,    -1,    -1,   109,   110,   111,    -1,    -1,    23,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      35,    36,    -1,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,    -1,
      55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    86,    87,    88,    89,    90,    91,    -1,    93,    -1,
      95,    -1,    97,    98,    -1,    -1,   101,   102,    12,    13,
     105,    -1,    -1,    -1,   109,   110,   111,    -1,    -1,    23,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    35,    -1,    -1,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    12,    13,
      -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,    23,
      -1,   105,    -1,    -1,    -1,   109,   110,   111,    -1,    -1,
      -1,    -1,    -1,    -1,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    12,    13,
      -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,    23,
      -1,   105,    -1,    -1,    -1,   109,   110,   111,    -1,    -1,
      -1,    -1,    -1,    -1,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    12,
      -1,    -1,    86,    87,    88,    89,    90,    91,    -1,    -1,
      23,    -1,    -1,    97,    98,    -1,    -1,   101,   102,    -1,
      -1,   105,    35,    36,    -1,   109,   110,   111,    -1,    -1,
      -1,    44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,
      -1,    -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    86,    87,    88,    89,    90,    91,    12,
      93,    94,    95,    -1,    97,    98,    -1,    -1,   101,   102,
      23,    -1,   105,    -1,    -1,    -1,   109,   110,   111,    -1,
      -1,    -1,    35,    36,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,
      -1,    -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,    12,    -1,
      -1,    -1,    -1,    86,    87,    88,    89,    90,    91,    23,
      93,    -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,
      -1,    35,   105,    -1,    -1,    -1,   109,   110,   111,    -1,
      44,    45,    46,    -1,    48,    -1,    -1,    -1,    -1,    -1,
      -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    12,    -1,    -1,    15,
      16,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    86,    87,    88,    89,    90,    91,    -1,    -1,
      -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,    45,
      46,   105,    48,    -1,    -1,   109,   110,   111,    -1,    55,
      56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    12,    -1,    -1,    15,    16,    -1,
      -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      86,    87,    88,    89,    90,    91,    -1,    -1,    -1,    95,
      -1,    97,    98,    -1,    -1,   101,   102,    45,    46,   105,
      48,    -1,    -1,   109,   110,   111,    -1,    55,    56,    -1,
      58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      78,    -1,    -1,    12,    -1,    -1,    -1,    -1,    86,    87,
      88,    89,    90,    91,    23,    -1,    -1,    95,    -1,    97,
      98,    -1,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,
      -1,   109,   110,   111,    -1,    44,    45,    46,    -1,    48,
      -1,    -1,    -1,    -1,    -1,    -1,    55,    56,    -1,    58,
      -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    12,    -1,    -1,    15,    -1,    -1,    -1,    -1,    78,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,    87,    88,
      89,    90,    91,    -1,    -1,    -1,    95,    -1,    97,    98,
      -1,    -1,   101,   102,    45,    46,   105,    48,    -1,    -1,
     109,   110,   111,    -1,    55,    56,    -1,    58,    -1,    60,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    12,
      -1,    -1,    15,    -1,    -1,    -1,    -1,    78,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    86,    87,    88,    89,    90,
      91,    -1,    -1,    -1,    95,    -1,    97,    98,    -1,    -1,
     101,   102,    45,    46,   105,    48,    -1,    -1,   109,   110,
     111,    -1,    55,    56,    -1,    58,    -1,    60,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    12,    -1,    -1,
      15,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    86,    87,    88,    89,    90,    91,    -1,
      -1,    -1,    95,    -1,    97,    98,    -1,    -1,   101,   102,
      45,    46,   105,    48,    -1,    -1,   109,   110,   111,    -1,
      55,    56,    -1,    58,    -1,    60,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    12,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    86,    87,    88,    89,    90,    91,    -1,    -1,    -1,
      95,    -1,    97,    98,    -1,    -1,   101,   102,    45,    46,
     105,    48,    -1,    -1,   109,   110,   111,    -1,    55,    56,
      -1,    58,    -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    69,    12,    -1,    -1,    15,    -1,    -1,    -1,
      -1,    78,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    86,
      87,    88,    89,    90,    91,    -1,    -1,    -1,    95,    -1,
      97,    98,    -1,    -1,   101,   102,    45,    46,   105,    48,
      -1,    -1,   109,   110,   111,    -1,    55,    56,    -1,    58,
      -1,    60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,
      -1,    -1,    12,    -1,    -1,    -1,    -1,    86,    87,    88,
      89,    90,    91,    23,    -1,    -1,    95,    -1,    97,    98,
      -1,    -1,   101,   102,    -1,    -1,   105,    -1,    -1,    -1,
     109,   110,   111,    -1,    44,    45,    46,    -1,    48,    -1,
      -1,    -1,    -1,    -1,    -1,    55,    56,    -1,    58,    -1,
      60,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      12,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    86,    87,    88,    89,
      90,    91,    -1,    -1,    -1,    -1,    -1,    97,    98,    -1,
      -1,   101,   102,    45,    46,   105,    48,    -1,    -1,   109,
     110,   111,    -1,    55,    56,    -1,    58,    -1,    60,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    78,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    86,    87,    88,    89,    90,    91,
      -1,    -1,    -1,    95,    -1,    97,    98,    -1,    -1,   101,
     102,    -1,    -1,   105,    -1,    -1,    -1,   109,   110,   111
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     1,     9,    14,   114,   129,   131,   143,     0,     7,
       8,    11,    12,    15,    45,    46,    48,    54,    55,    56,
      58,    60,    78,    86,    87,    88,    89,    90,    91,    95,
      97,    98,   101,   102,   105,   109,   110,   111,   126,   132,
     133,   135,   138,   145,   146,   156,   157,   158,   160,     9,
      14,   126,   126,   138,   139,   147,    12,    12,   105,   157,
     158,    86,    89,   124,    12,    12,    12,    12,    42,   158,
      12,    12,   157,   157,   145,   157,   158,   158,   157,     1,
       9,    14,    47,    49,    50,    51,    52,    53,    57,    61,
      62,    76,    77,    96,    99,   118,   120,   125,   126,   138,
     142,   149,   151,   155,   161,    10,   126,   129,    13,    23,
      35,    36,    38,    39,    40,    41,    42,    43,    44,    93,
     115,   116,   157,    12,    15,   101,   102,   103,   104,   108,
      69,   109,   110,    18,   155,   155,    10,    16,   117,    16,
     117,    92,    16,   136,   138,   138,    12,   138,   138,   136,
      16,   136,   157,    42,   138,   138,     9,   127,   128,    14,
     127,   150,   150,   160,   138,   150,    12,    12,   150,   150,
     138,   150,    12,     9,   152,   151,   155,    12,   137,   140,
     141,   145,   157,   158,   150,    17,   151,   154,   128,   155,
     133,    95,   138,   146,   138,   138,   138,   138,   138,   138,
     160,   138,     9,   138,     9,   138,   138,   146,    69,   157,
     157,   157,   157,   157,   157,   138,   136,    17,    17,     9,
     138,    44,   138,    15,    16,   117,    86,   159,   117,   117,
      16,    16,   157,   117,   117,     9,   128,    18,   150,   130,
     149,   160,   138,   150,   138,   151,    79,   119,    17,   144,
     139,    23,    44,    93,   115,   116,   157,   117,    13,    37,
      40,    69,   151,   132,    17,   158,    94,   117,   117,   157,
      19,   160,   138,    16,   117,   148,   138,   146,   138,   146,
     160,   138,   136,    14,    44,   148,   148,   153,     9,   151,
       9,    16,    12,   137,   146,   160,   137,   137,   137,   137,
     157,   157,   157,   137,   126,   138,   138,   138,    86,     9,
     134,    16,    16,    16,    16,    16,   117,    16,   117,    19,
      14,   128,   160,    99,    44,   139,    94,   155,    16,   117,
      16,   117,   126,   138,   146,   138,   128,   138,   148,    12,
     160,    16,   137,    17,   158,   158,   155,    16,    16,    16,
     130,    14,   123,   138,    16,    16,    17,   148,   128,   151,
      16,   122,   130,   150,   151,   148,   121,   151
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,   113,   114,   114,   115,   115,   116,   116,   117,   117,
     118,   118,   119,   119,   121,   120,   122,   120,   123,   120,
     124,   124,   125,   126,   126,   127,   127,   128,   128,   129,
     129,   130,   130,   131,   131,   132,   133,   133,   133,   133,
     133,   133,   133,   134,   133,   135,   135,   136,   136,   137,
     137,   137,   137,   137,   137,   137,   137,   137,   137,   137,
     138,   138,   138,   138,   138,   138,   138,   138,   138,   138,
     138,   138,   138,   138,   138,   138,   138,   138,   138,   139,
     139,   140,   140,   141,   141,   141,   142,   142,   143,   143,
     143,   143,   144,   144,   145,   145,   147,   146,   148,   148,
     149,   149,   149,   149,   149,   149,   149,   149,   150,   150,
     151,   151,   152,   153,   151,   151,   151,   151,   151,   151,
     151,   151,   151,   151,   151,   151,   154,   151,   151,   155,
     155,   156,   156,   157,   157,   157,   157,   157,   157,   157,
     157,   157,   157,   157,   157,   157,   157,   157,   157,   157,
     157,   157,   157,   157,   157,   157,   157,   157,   157,   157,
     157,   157,   157,   157,   157,   157,   157,   157,   157,   157,
     157,   157,   157,   157,   157,   158,   158,   158,   158,   159,
     159,   159,   160,   160,   160,   161
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     1,     1,     2,     1,     2,     1,     2,
       1,     2,     1,     2,     0,    12,     0,    10,     0,     8,
       1,     1,     4,     1,     2,     1,     2,     0,     1,     0,
       1,     0,     1,     1,     3,     1,     1,     4,     4,     7,
       3,     4,     4,     0,     9,     1,     3,     1,     3,     3,
       5,     3,     3,     3,     3,     3,     5,     2,     1,     1,
       3,     5,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     5,     4,     3,     2,     1,     1,     3,
       3,     1,     3,     0,     1,     3,     1,     1,     1,     1,
       2,     2,     1,     2,     1,     2,     0,     4,     1,     2,
       4,     4,     4,     2,     5,     2,     1,     1,     1,     2,
       2,     2,     0,     0,     9,     3,     2,     1,     4,     2,
       3,     2,     2,     3,     2,     2,     0,     3,     2,     1,
       2,     1,     1,     4,     3,     3,     3,     3,     3,     3,
       2,     2,     2,     3,     4,     1,     3,     4,     2,     2,
       2,     2,     2,     4,     3,     2,     1,     6,     6,     3,
       6,     6,     1,     8,     8,     6,     4,     1,     6,     6,
       8,     8,     8,     6,     1,     1,     4,     1,     2,     0,
       1,     3,     1,     1,     1,     4
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: pas  */
#line 98 "awkgram.y"
                { if (errorflag==0)
			winner = (Node *)stat3(PROGRAM, beginloc, (yyvsp[0].p), endloc); }
#line 2603 "y.tab.c"
    break;

  case 3: /* program: error  */
#line 100 "awkgram.y"
                { yyclearin; bracecheck(); SYNTAX("bailing out"); }
#line 2609 "y.tab.c"
    break;

  case 14: /* $@1: %empty  */
#line 124 "awkgram.y"
                                                                                       {inloop++;}
#line 2615 "y.tab.c"
    break;

  case 15: /* for: FOR '(' opt_simple_stmt ';' opt_nl pattern ';' opt_nl opt_simple_stmt rparen $@1 stmt  */
#line 125 "awkgram.y"
                { --inloop; (yyval.p) = stat4(FOR, (yyvsp[-9].p), notnull((yyvsp[-6].p)), (yyvsp[-3].p), (yyvsp[0].p)); }
#line 2621 "y.tab.c"
    break;

  case 16: /* $@2: %empty  */
#line 126 "awkgram.y"
                                                                         {inloop++;}
#line 2627 "y.tab.c"
    break;

  case 17: /* for: FOR '(' opt_simple_stmt ';' ';' opt_nl opt_simple_stmt rparen $@2 stmt  */
#line 127 "awkgram.y"
                { --inloop; (yyval.p) = stat4(FOR, (yyvsp[-7].p), NIL, (yyvsp[-3].p), (yyvsp[0].p)); }
#line 2633 "y.tab.c"
    break;

  case 18: /* $@3: %empty  */
#line 128 "awkgram.y"
                                            {inloop++;}
#line 2639 "y.tab.c"
    break;

  case 19: /* for: FOR '(' varname IN varname rparen $@3 stmt  */
#line 129 "awkgram.y"
                { --inloop; (yyval.p) = stat3(IN, (yyvsp[-5].p), makearr((yyvsp[-3].p)), (yyvsp[0].p)); }
#line 2645 "y.tab.c"
    break;

  case 20: /* funcname: VAR  */
#line 133 "awkgram.y"
                { setfname((yyvsp[0].cp)); }
#line 2651 "y.tab.c"
    break;

  case 21: /* funcname: CALL  */
#line 134 "awkgram.y"
                { setfname((yyvsp[0].cp)); }
#line 2657 "y.tab.c"
    break;

  case 22: /* if: IF '(' pattern rparen  */
#line 138 "awkgram.y"
                                        { (yyval.p) = notnull((yyvsp[-1].p)); }
#line 2663 "y.tab.c"
    break;

  case 27: /* opt_nl: %empty  */
#line 150 "awkgram.y"
                        { (yyval.i) = 0; }
#line 2669 "y.tab.c"
    break;

  case 29: /* opt_pst: %empty  */
#line 155 "awkgram.y"
                        { (yyval.i) = 0; }
#line 2675 "y.tab.c"
    break;

  case 31: /* opt_simple_stmt: %empty  */
#line 161 "awkgram.y"
                                        { (yyval.p) = 0; }
#line 2681 "y.tab.c"
    break;

  case 33: /* pas: opt_pst  */
#line 166 "awkgram.y"
                                        { (yyval.p) = 0; }
#line 2687 "y.tab.c"
    break;

  case 34: /* pas: opt_pst pa_stats opt_pst  */
#line 167 "awkgram.y"
                                        { (yyval.p) = (yyvsp[-1].p); }
#line 2693 "y.tab.c"
    break;

  case 35: /* pa_pat: pattern  */
#line 171 "awkgram.y"
                        { (yyval.p) = notnull((yyvsp[0].p)); }
#line 2699 "y.tab.c"
    break;

  case 36: /* pa_stat: pa_pat  */
#line 175 "awkgram.y"
                                        { (yyval.p) = stat2(PASTAT, (yyvsp[0].p), stat2(PRINT, rectonode(), NIL)); }
#line 2705 "y.tab.c"
    break;

  case 37: /* pa_stat: pa_pat lbrace stmtlist '}'  */
#line 176 "awkgram.y"
                                        { (yyval.p) = stat2(PASTAT, (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 2711 "y.tab.c"
    break;

  case 38: /* pa_stat: pa_pat ',' opt_nl pa_pat  */
#line 177 "awkgram.y"
                                                { (yyval.p) = pa2stat((yyvsp[-3].p), (yyvsp[0].p), stat2(PRINT, rectonode(), NIL)); }
#line 2717 "y.tab.c"
    break;

  case 39: /* pa_stat: pa_pat ',' opt_nl pa_pat lbrace stmtlist '}'  */
#line 178 "awkgram.y"
                                                        { (yyval.p) = pa2stat((yyvsp[-6].p), (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 2723 "y.tab.c"
    break;

  case 40: /* pa_stat: lbrace stmtlist '}'  */
#line 179 "awkgram.y"
                                        { (yyval.p) = stat2(PASTAT, NIL, (yyvsp[-1].p)); }
#line 2729 "y.tab.c"
    break;

  case 41: /* pa_stat: XBEGIN lbrace stmtlist '}'  */
#line 181 "awkgram.y"
                { beginloc = linkum(beginloc, (yyvsp[-1].p)); (yyval.p) = 0; }
#line 2735 "y.tab.c"
    break;

  case 42: /* pa_stat: XEND lbrace stmtlist '}'  */
#line 183 "awkgram.y"
                { endloc = linkum(endloc, (yyvsp[-1].p)); (yyval.p) = 0; }
#line 2741 "y.tab.c"
    break;

  case 43: /* $@4: %empty  */
#line 184 "awkgram.y"
                                           {infunc++;}
#line 2747 "y.tab.c"
    break;

  case 44: /* pa_stat: FUNC funcname '(' varlist rparen $@4 lbrace stmtlist '}'  */
#line 185 "awkgram.y"
                { infunc--; curfname=0; defn((Cell *)(yyvsp[-7].p), (yyvsp[-5].p), (yyvsp[-1].p)); (yyval.p) = 0; }
#line 2753 "y.tab.c"
    break;

  case 46: /* pa_stats: pa_stats opt_pst pa_stat  */
#line 190 "awkgram.y"
                                        { (yyval.p) = linkum((yyvsp[-2].p), (yyvsp[0].p)); }
#line 2759 "y.tab.c"
    break;

  case 48: /* patlist: patlist comma pattern  */
#line 195 "awkgram.y"
                                        { (yyval.p) = linkum((yyvsp[-2].p), (yyvsp[0].p)); }
#line 2765 "y.tab.c"
    break;

  case 49: /* ppattern: var ASGNOP ppattern  */
#line 199 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2771 "y.tab.c"
    break;

  case 50: /* ppattern: ppattern '?' ppattern ':' ppattern  */
#line 201 "awkgram.y"
                { (yyval.p) = op3(CONDEXPR, notnull((yyvsp[-4].p)), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2777 "y.tab.c"
    break;

  case 51: /* ppattern: ppattern bor ppattern  */
#line 203 "awkgram.y"
                { (yyval.p) = op2(BOR, notnull((yyvsp[-2].p)), notnull((yyvsp[0].p))); }
#line 2783 "y.tab.c"
    break;

  case 52: /* ppattern: ppattern and ppattern  */
#line 205 "awkgram.y"
                { (yyval.p) = op2(AND, notnull((yyvsp[-2].p)), notnull((yyvsp[0].p))); }
#line 2789 "y.tab.c"
    break;

  case 53: /* ppattern: ppattern MATCHOP reg_expr  */
#line 206 "awkgram.y"
                                        { (yyval.p) = op3((yyvsp[-1].i), NIL, (yyvsp[-2].p), (Node*)makedfa((yyvsp[0].s), 0)); }
#line 2795 "y.tab.c"
    break;

  case 54: /* ppattern: ppattern MATCHOP ppattern  */
#line 208 "awkgram.y"
                { if (constnode((yyvsp[0].p)))
			(yyval.p) = op3((yyvsp[-1].i), NIL, (yyvsp[-2].p), (Node*)makedfa(strnode((yyvsp[0].p)), 0));
		  else
			(yyval.p) = op3((yyvsp[-1].i), (Node *)1, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2804 "y.tab.c"
    break;

  case 55: /* ppattern: ppattern IN varname  */
#line 212 "awkgram.y"
                                        { (yyval.p) = op2(INTEST, (yyvsp[-2].p), makearr((yyvsp[0].p))); }
#line 2810 "y.tab.c"
    break;

  case 56: /* ppattern: '(' plist ')' IN varname  */
#line 213 "awkgram.y"
                                        { (yyval.p) = op2(INTEST, (yyvsp[-3].p), makearr((yyvsp[0].p))); }
#line 2816 "y.tab.c"
    break;

  case 57: /* ppattern: ppattern term  */
#line 214 "awkgram.y"
                                        { (yyval.p) = op2(CAT, (yyvsp[-1].p), (yyvsp[0].p)); }
#line 2822 "y.tab.c"
    break;

  case 60: /* pattern: var ASGNOP pattern  */
#line 220 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2828 "y.tab.c"
    break;

  case 61: /* pattern: pattern '?' pattern ':' pattern  */
#line 222 "awkgram.y"
                { (yyval.p) = op3(CONDEXPR, notnull((yyvsp[-4].p)), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2834 "y.tab.c"
    break;

  case 62: /* pattern: pattern bor pattern  */
#line 224 "awkgram.y"
                { (yyval.p) = op2(BOR, notnull((yyvsp[-2].p)), notnull((yyvsp[0].p))); }
#line 2840 "y.tab.c"
    break;

  case 63: /* pattern: pattern and pattern  */
#line 226 "awkgram.y"
                { (yyval.p) = op2(AND, notnull((yyvsp[-2].p)), notnull((yyvsp[0].p))); }
#line 2846 "y.tab.c"
    break;

  case 64: /* pattern: pattern EQ pattern  */
#line 227 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2852 "y.tab.c"
    break;

  case 65: /* pattern: pattern GE pattern  */
#line 228 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2858 "y.tab.c"
    break;

  case 66: /* pattern: pattern GT pattern  */
#line 229 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2864 "y.tab.c"
    break;

  case 67: /* pattern: pattern LE pattern  */
#line 230 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2870 "y.tab.c"
    break;

  case 68: /* pattern: pattern LT pattern  */
#line 231 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2876 "y.tab.c"
    break;

  case 69: /* pattern: pattern NE pattern  */
#line 232 "awkgram.y"
                                        { (yyval.p) = op2((yyvsp[-1].i), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2882 "y.tab.c"
    break;

  case 70: /* pattern: pattern MATCHOP reg_expr  */
#line 233 "awkgram.y"
                                        { (yyval.p) = op3((yyvsp[-1].i), NIL, (yyvsp[-2].p), (Node*)makedfa((yyvsp[0].s), 0)); }
#line 2888 "y.tab.c"
    break;

  case 71: /* pattern: pattern MATCHOP pattern  */
#line 235 "awkgram.y"
                { if (constnode((yyvsp[0].p)))
			(yyval.p) = op3((yyvsp[-1].i), NIL, (yyvsp[-2].p), (Node*)makedfa(strnode((yyvsp[0].p)), 0));
		  else
			(yyval.p) = op3((yyvsp[-1].i), (Node *)1, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 2897 "y.tab.c"
    break;

  case 72: /* pattern: pattern IN varname  */
#line 239 "awkgram.y"
                                        { (yyval.p) = op2(INTEST, (yyvsp[-2].p), makearr((yyvsp[0].p))); }
#line 2903 "y.tab.c"
    break;

  case 73: /* pattern: '(' plist ')' IN varname  */
#line 240 "awkgram.y"
                                        { (yyval.p) = op2(INTEST, (yyvsp[-3].p), makearr((yyvsp[0].p))); }
#line 2909 "y.tab.c"
    break;

  case 74: /* pattern: pattern '|' GETLINE var  */
#line 241 "awkgram.y"
                                        { 
			if (safe) SYNTAX("cmd | getline is unsafe");
			else (yyval.p) = op3(GETLINE, (yyvsp[0].p), itonp((yyvsp[-2].i)), (yyvsp[-3].p)); }
#line 2917 "y.tab.c"
    break;

  case 75: /* pattern: pattern '|' GETLINE  */
#line 244 "awkgram.y"
                                        { 
			if (safe) SYNTAX("cmd | getline is unsafe");
			else (yyval.p) = op3(GETLINE, (Node*)0, itonp((yyvsp[-1].i)), (yyvsp[-2].p)); }
#line 2925 "y.tab.c"
    break;

  case 76: /* pattern: pattern term  */
#line 247 "awkgram.y"
                                        { (yyval.p) = op2(CAT, (yyvsp[-1].p), (yyvsp[0].p)); }
#line 2931 "y.tab.c"
    break;

  case 79: /* plist: pattern comma pattern  */
#line 253 "awkgram.y"
                                        { (yyval.p) = linkum((yyvsp[-2].p), (yyvsp[0].p)); }
#line 2937 "y.tab.c"
    break;

  case 80: /* plist: plist comma pattern  */
#line 254 "awkgram.y"
                                        { (yyval.p) = linkum((yyvsp[-2].p), (yyvsp[0].p)); }
#line 2943 "y.tab.c"
    break;

  case 82: /* pplist: pplist comma ppattern  */
#line 259 "awkgram.y"
                                        { (yyval.p) = linkum((yyvsp[-2].p), (yyvsp[0].p)); }
#line 2949 "y.tab.c"
    break;

  case 83: /* prarg: %empty  */
#line 263 "awkgram.y"
                                        { (yyval.p) = rectonode(); }
#line 2955 "y.tab.c"
    break;

  case 85: /* prarg: '(' plist ')'  */
#line 265 "awkgram.y"
                                        { (yyval.p) = (yyvsp[-1].p); }
#line 2961 "y.tab.c"
    break;

  case 94: /* re: reg_expr  */
#line 282 "awkgram.y"
                { (yyval.p) = op3(MATCH, NIL, rectonode(), (Node*)makedfa((yyvsp[0].s), 0)); }
#line 2967 "y.tab.c"
    break;

  case 95: /* re: NOT re  */
#line 283 "awkgram.y"
                        { (yyval.p) = op1(NOT, notnull((yyvsp[0].p))); }
#line 2973 "y.tab.c"
    break;

  case 96: /* $@5: %empty  */
#line 287 "awkgram.y"
              {startreg();}
#line 2979 "y.tab.c"
    break;

  case 97: /* reg_expr: '/' $@5 REGEXPR '/'  */
#line 287 "awkgram.y"
                                                { (yyval.s) = (yyvsp[-1].s); }
#line 2985 "y.tab.c"
    break;

  case 100: /* simple_stmt: print prarg '|' term  */
#line 295 "awkgram.y"
                                        { 
			if (safe) SYNTAX("print | is unsafe");
			else (yyval.p) = stat3((yyvsp[-3].i), (yyvsp[-2].p), itonp((yyvsp[-1].i)), (yyvsp[0].p)); }
#line 2993 "y.tab.c"
    break;

  case 101: /* simple_stmt: print prarg APPEND term  */
#line 298 "awkgram.y"
                                        {
			if (safe) SYNTAX("print >> is unsafe");
			else (yyval.p) = stat3((yyvsp[-3].i), (yyvsp[-2].p), itonp((yyvsp[-1].i)), (yyvsp[0].p)); }
#line 3001 "y.tab.c"
    break;

  case 102: /* simple_stmt: print prarg GT term  */
#line 301 "awkgram.y"
                                        {
			if (safe) SYNTAX("print > is unsafe");
			else (yyval.p) = stat3((yyvsp[-3].i), (yyvsp[-2].p), itonp((yyvsp[-1].i)), (yyvsp[0].p)); }
#line 3009 "y.tab.c"
    break;

  case 103: /* simple_stmt: print prarg  */
#line 304 "awkgram.y"
                                        { (yyval.p) = stat3((yyvsp[-1].i), (yyvsp[0].p), NIL, NIL); }
#line 3015 "y.tab.c"
    break;

  case 104: /* simple_stmt: DELETE varname '[' patlist ']'  */
#line 305 "awkgram.y"
                                         { (yyval.p) = stat2(DELETE, makearr((yyvsp[-3].p)), (yyvsp[-1].p)); }
#line 3021 "y.tab.c"
    break;

  case 105: /* simple_stmt: DELETE varname  */
#line 306 "awkgram.y"
                                         { (yyval.p) = stat2(DELETE, makearr((yyvsp[0].p)), 0); }
#line 3027 "y.tab.c"
    break;

  case 106: /* simple_stmt: pattern  */
#line 307 "awkgram.y"
                                        { (yyval.p) = exptostat((yyvsp[0].p)); }
#line 3033 "y.tab.c"
    break;

  case 107: /* simple_stmt: error  */
#line 308 "awkgram.y"
                                        { yyclearin; SYNTAX("illegal statement"); }
#line 3039 "y.tab.c"
    break;

  case 110: /* stmt: BREAK st  */
#line 317 "awkgram.y"
                                { if (!inloop) SYNTAX("break illegal outside of loops");
				  (yyval.p) = stat1(BREAK, NIL); }
#line 3046 "y.tab.c"
    break;

  case 111: /* stmt: CONTINUE st  */
#line 319 "awkgram.y"
                                {  if (!inloop) SYNTAX("continue illegal outside of loops");
				  (yyval.p) = stat1(CONTINUE, NIL); }
#line 3053 "y.tab.c"
    break;

  case 112: /* $@6: %empty  */
#line 321 "awkgram.y"
             {inloop++;}
#line 3059 "y.tab.c"
    break;

  case 113: /* $@7: %empty  */
#line 321 "awkgram.y"
                              {--inloop;}
#line 3065 "y.tab.c"
    break;

  case 114: /* stmt: do $@6 stmt $@7 WHILE '(' pattern ')' st  */
#line 322 "awkgram.y"
                { (yyval.p) = stat2(DO, (yyvsp[-6].p), notnull((yyvsp[-2].p))); }
#line 3071 "y.tab.c"
    break;

  case 115: /* stmt: EXIT pattern st  */
#line 323 "awkgram.y"
                                { (yyval.p) = stat1(EXIT, (yyvsp[-1].p)); }
#line 3077 "y.tab.c"
    break;

  case 116: /* stmt: EXIT st  */
#line 324 "awkgram.y"
                                { (yyval.p) = stat1(EXIT, NIL); }
#line 3083 "y.tab.c"
    break;

  case 118: /* stmt: if stmt else stmt  */
#line 326 "awkgram.y"
                                { (yyval.p) = stat3(IF, (yyvsp[-3].p), (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3089 "y.tab.c"
    break;

  case 119: /* stmt: if stmt  */
#line 327 "awkgram.y"
                                { (yyval.p) = stat3(IF, (yyvsp[-1].p), (yyvsp[0].p), NIL); }
#line 3095 "y.tab.c"
    break;

  case 120: /* stmt: lbrace stmtlist rbrace  */
#line 328 "awkgram.y"
                                 { (yyval.p) = (yyvsp[-1].p); }
#line 3101 "y.tab.c"
    break;

  case 121: /* stmt: NEXT st  */
#line 329 "awkgram.y"
                        { if (infunc)
				SYNTAX("next is illegal inside a function");
			  (yyval.p) = stat1(NEXT, NIL); }
#line 3109 "y.tab.c"
    break;

  case 122: /* stmt: NEXTFILE st  */
#line 332 "awkgram.y"
                        { if (infunc)
				SYNTAX("nextfile is illegal inside a function");
			  (yyval.p) = stat1(NEXTFILE, NIL); }
#line 3117 "y.tab.c"
    break;

  case 123: /* stmt: RETURN pattern st  */
#line 335 "awkgram.y"
                                { (yyval.p) = stat1(RETURN, (yyvsp[-1].p)); }
#line 3123 "y.tab.c"
    break;

  case 124: /* stmt: RETURN st  */
#line 336 "awkgram.y"
                                { (yyval.p) = stat1(RETURN, NIL); }
#line 3129 "y.tab.c"
    break;

  case 126: /* $@8: %empty  */
#line 338 "awkgram.y"
                {inloop++;}
#line 3135 "y.tab.c"
    break;

  case 127: /* stmt: while $@8 stmt  */
#line 338 "awkgram.y"
                                        { --inloop; (yyval.p) = stat2(WHILE, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3141 "y.tab.c"
    break;

  case 128: /* stmt: ';' opt_nl  */
#line 339 "awkgram.y"
                                { (yyval.p) = 0; }
#line 3147 "y.tab.c"
    break;

  case 130: /* stmtlist: stmtlist stmt  */
#line 344 "awkgram.y"
                                { (yyval.p) = linkum((yyvsp[-1].p), (yyvsp[0].p)); }
#line 3153 "y.tab.c"
    break;

  case 133: /* term: term '/' ASGNOP term  */
#line 352 "awkgram.y"
                                        { (yyval.p) = op2(DIVEQ, (yyvsp[-3].p), (yyvsp[0].p)); }
#line 3159 "y.tab.c"
    break;

  case 134: /* term: term '+' term  */
#line 353 "awkgram.y"
                                        { (yyval.p) = op2(ADD, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3165 "y.tab.c"
    break;

  case 135: /* term: term '-' term  */
#line 354 "awkgram.y"
                                        { (yyval.p) = op2(MINUS, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3171 "y.tab.c"
    break;

  case 136: /* term: term '*' term  */
#line 355 "awkgram.y"
                                        { (yyval.p) = op2(MULT, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3177 "y.tab.c"
    break;

  case 137: /* term: term '/' term  */
#line 356 "awkgram.y"
                                        { (yyval.p) = op2(DIVIDE, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3183 "y.tab.c"
    break;

  case 138: /* term: term '%' term  */
#line 357 "awkgram.y"
                                        { (yyval.p) = op2(MOD, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3189 "y.tab.c"
    break;

  case 139: /* term: term POWER term  */
#line 358 "awkgram.y"
                                        { (yyval.p) = op2(POWER, (yyvsp[-2].p), (yyvsp[0].p)); }
#line 3195 "y.tab.c"
    break;

  case 140: /* term: '-' term  */
#line 359 "awkgram.y"
                                        { (yyval.p) = op1(UMINUS, (yyvsp[0].p)); }
#line 3201 "y.tab.c"
    break;

  case 141: /* term: '+' term  */
#line 360 "awkgram.y"
                                        { (yyval.p) = op1(UPLUS, (yyvsp[0].p)); }
#line 3207 "y.tab.c"
    break;

  case 142: /* term: NOT term  */
#line 361 "awkgram.y"
                                        { (yyval.p) = op1(NOT, notnull((yyvsp[0].p))); }
#line 3213 "y.tab.c"
    break;

  case 143: /* term: BLTIN '(' ')'  */
#line 362 "awkgram.y"
                                        { (yyval.p) = op2(BLTIN, itonp((yyvsp[-2].i)), rectonode()); }
#line 3219 "y.tab.c"
    break;

  case 144: /* term: BLTIN '(' patlist ')'  */
#line 363 "awkgram.y"
                                        { (yyval.p) = op2(BLTIN, itonp((yyvsp[-3].i)), (yyvsp[-1].p)); }
#line 3225 "y.tab.c"
    break;

  case 145: /* term: BLTIN  */
#line 364 "awkgram.y"
                                        { (yyval.p) = op2(BLTIN, itonp((yyvsp[0].i)), rectonode()); }
#line 3231 "y.tab.c"
    break;

  case 146: /* term: CALL '(' ')'  */
#line 365 "awkgram.y"
                                        { (yyval.p) = op2(CALL, celltonode((yyvsp[-2].cp),CVAR), NIL); }
#line 3237 "y.tab.c"
    break;

  case 147: /* term: CALL '(' patlist ')'  */
#line 366 "awkgram.y"
                                        { (yyval.p) = op2(CALL, celltonode((yyvsp[-3].cp),CVAR), (yyvsp[-1].p)); }
#line 3243 "y.tab.c"
    break;

  case 148: /* term: CLOSE term  */
#line 367 "awkgram.y"
                                        { (yyval.p) = op1(CLOSE, (yyvsp[0].p)); }
#line 3249 "y.tab.c"
    break;

  case 149: /* term: DECR var  */
#line 368 "awkgram.y"
                                        { (yyval.p) = op1(PREDECR, (yyvsp[0].p)); }
#line 3255 "y.tab.c"
    break;

  case 150: /* term: INCR var  */
#line 369 "awkgram.y"
                                        { (yyval.p) = op1(PREINCR, (yyvsp[0].p)); }
#line 3261 "y.tab.c"
    break;

  case 151: /* term: var DECR  */
#line 370 "awkgram.y"
                                        { (yyval.p) = op1(POSTDECR, (yyvsp[-1].p)); }
#line 3267 "y.tab.c"
    break;

  case 152: /* term: var INCR  */
#line 371 "awkgram.y"
                                        { (yyval.p) = op1(POSTINCR, (yyvsp[-1].p)); }
#line 3273 "y.tab.c"
    break;

  case 153: /* term: GETLINE var LT term  */
#line 372 "awkgram.y"
                                        { (yyval.p) = op3(GETLINE, (yyvsp[-2].p), itonp((yyvsp[-1].i)), (yyvsp[0].p)); }
#line 3279 "y.tab.c"
    break;

  case 154: /* term: GETLINE LT term  */
#line 373 "awkgram.y"
                                        { (yyval.p) = op3(GETLINE, NIL, itonp((yyvsp[-1].i)), (yyvsp[0].p)); }
#line 3285 "y.tab.c"
    break;

  case 155: /* term: GETLINE var  */
#line 374 "awkgram.y"
                                        { (yyval.p) = op3(GETLINE, (yyvsp[0].p), NIL, NIL); }
#line 3291 "y.tab.c"
    break;

  case 156: /* term: GETLINE  */
#line 375 "awkgram.y"
                                        { (yyval.p) = op3(GETLINE, NIL, NIL, NIL); }
#line 3297 "y.tab.c"
    break;

  case 157: /* term: INDEX '(' pattern comma pattern ')'  */
#line 377 "awkgram.y"
                { (yyval.p) = op2(INDEX, (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 3303 "y.tab.c"
    break;

  case 158: /* term: INDEX '(' pattern comma reg_expr ')'  */
#line 379 "awkgram.y"
                { SYNTAX("index() doesn't permit regular expressions");
		  (yyval.p) = op2(INDEX, (yyvsp[-3].p), (Node*)(yyvsp[-1].s)); }
#line 3310 "y.tab.c"
    break;

  case 159: /* term: '(' pattern ')'  */
#line 381 "awkgram.y"
                                        { (yyval.p) = (yyvsp[-1].p); }
#line 3316 "y.tab.c"
    break;

  case 160: /* term: MATCHFCN '(' pattern comma reg_expr ')'  */
#line 383 "awkgram.y"
                { (yyval.p) = op3(MATCHFCN, NIL, (yyvsp[-3].p), (Node*)makedfa((yyvsp[-1].s), 1)); }
#line 3322 "y.tab.c"
    break;

  case 161: /* term: MATCHFCN '(' pattern comma pattern ')'  */
#line 385 "awkgram.y"
                { if (constnode((yyvsp[-1].p)))
			(yyval.p) = op3(MATCHFCN, NIL, (yyvsp[-3].p), (Node*)makedfa(strnode((yyvsp[-1].p)), 1));
		  else
			(yyval.p) = op3(MATCHFCN, (Node *)1, (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 3331 "y.tab.c"
    break;

  case 162: /* term: NUMBER  */
#line 389 "awkgram.y"
                                        { (yyval.p) = celltonode((yyvsp[0].cp), CCON); }
#line 3337 "y.tab.c"
    break;

  case 163: /* term: SPLIT '(' pattern comma varname comma pattern ')'  */
#line 391 "awkgram.y"
                { (yyval.p) = op4(SPLIT, (yyvsp[-5].p), makearr((yyvsp[-3].p)), (yyvsp[-1].p), (Node*)STRING); }
#line 3343 "y.tab.c"
    break;

  case 164: /* term: SPLIT '(' pattern comma varname comma reg_expr ')'  */
#line 393 "awkgram.y"
                { (yyval.p) = op4(SPLIT, (yyvsp[-5].p), makearr((yyvsp[-3].p)), (Node*)makedfa((yyvsp[-1].s), 1), (Node *)REGEXPR); }
#line 3349 "y.tab.c"
    break;

  case 165: /* term: SPLIT '(' pattern comma varname ')'  */
#line 395 "awkgram.y"
                { (yyval.p) = op4(SPLIT, (yyvsp[-3].p), makearr((yyvsp[-1].p)), NIL, (Node*)STRING); }
#line 3355 "y.tab.c"
    break;

  case 166: /* term: SPRINTF '(' patlist ')'  */
#line 396 "awkgram.y"
                                        { (yyval.p) = op1((yyvsp[-3].i), (yyvsp[-1].p)); }
#line 3361 "y.tab.c"
    break;

  case 167: /* term: STRING  */
#line 397 "awkgram.y"
                                        { (yyval.p) = celltonode((yyvsp[0].cp), CCON); }
#line 3367 "y.tab.c"
    break;

  case 168: /* term: subop '(' reg_expr comma pattern ')'  */
#line 399 "awkgram.y"
                { (yyval.p) = op4((yyvsp[-5].i), NIL, (Node*)makedfa((yyvsp[-3].s), 1), (yyvsp[-1].p), rectonode()); }
#line 3373 "y.tab.c"
    break;

  case 169: /* term: subop '(' pattern comma pattern ')'  */
#line 401 "awkgram.y"
                { if (constnode((yyvsp[-3].p)))
			(yyval.p) = op4((yyvsp[-5].i), NIL, (Node*)makedfa(strnode((yyvsp[-3].p)), 1), (yyvsp[-1].p), rectonode());
		  else
			(yyval.p) = op4((yyvsp[-5].i), (Node *)1, (yyvsp[-3].p), (yyvsp[-1].p), rectonode()); }
#line 3382 "y.tab.c"
    break;

  case 170: /* term: subop '(' reg_expr comma pattern comma var ')'  */
#line 406 "awkgram.y"
                { (yyval.p) = op4((yyvsp[-7].i), NIL, (Node*)makedfa((yyvsp[-5].s), 1), (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 3388 "y.tab.c"
    break;

  case 171: /* term: subop '(' pattern comma pattern comma var ')'  */
#line 408 "awkgram.y"
                { if (constnode((yyvsp[-5].p)))
			(yyval.p) = op4((yyvsp[-7].i), NIL, (Node*)makedfa(strnode((yyvsp[-5].p)), 1), (yyvsp[-3].p), (yyvsp[-1].p));
		  else
			(yyval.p) = op4((yyvsp[-7].i), (Node *)1, (yyvsp[-5].p), (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 3397 "y.tab.c"
    break;

  case 172: /* term: SUBSTR '(' pattern comma pattern comma pattern ')'  */
#line 413 "awkgram.y"
                { (yyval.p) = op3(SUBSTR, (yyvsp[-5].p), (yyvsp[-3].p), (yyvsp[-1].p)); }
#line 3403 "y.tab.c"
    break;

  case 173: /* term: SUBSTR '(' pattern comma pattern ')'  */
#line 415 "awkgram.y"
                { (yyval.p) = op3(SUBSTR, (yyvsp[-3].p), (yyvsp[-1].p), NIL); }
#line 3409 "y.tab.c"
    break;

  case 176: /* var: varname '[' patlist ']'  */
#line 421 "awkgram.y"
                                        { (yyval.p) = op2(ARRAY, makearr((yyvsp[-3].p)), (yyvsp[-1].p)); }
#line 3415 "y.tab.c"
    break;

  case 177: /* var: IVAR  */
#line 422 "awkgram.y"
                                        { (yyval.p) = op1(INDIRECT, celltonode((yyvsp[0].cp), CVAR)); }
#line 3421 "y.tab.c"
    break;

  case 178: /* var: INDIRECT term  */
#line 423 "awkgram.y"
                                        { (yyval.p) = op1(INDIRECT, (yyvsp[0].p)); }
#line 3427 "y.tab.c"
    break;

  case 179: /* varlist: %empty  */
#line 427 "awkgram.y"
                                { arglist = (yyval.p) = 0; }
#line 3433 "y.tab.c"
    break;

  case 180: /* varlist: VAR  */
#line 428 "awkgram.y"
                                { arglist = (yyval.p) = celltonode((yyvsp[0].cp),CVAR); }
#line 3439 "y.tab.c"
    break;

  case 181: /* varlist: varlist comma VAR  */
#line 429 "awkgram.y"
                                {
			checkdup((yyvsp[-2].p), (yyvsp[0].cp));
			arglist = (yyval.p) = linkum((yyvsp[-2].p),celltonode((yyvsp[0].cp),CVAR)); }
#line 3447 "y.tab.c"
    break;

  case 182: /* varname: VAR  */
#line 435 "awkgram.y"
                                { (yyval.p) = celltonode((yyvsp[0].cp), CVAR); }
#line 3453 "y.tab.c"
    break;

  case 183: /* varname: ARG  */
#line 436 "awkgram.y"
                                { (yyval.p) = op1(ARG, itonp((yyvsp[0].i))); }
#line 3459 "y.tab.c"
    break;

  case 184: /* varname: VARNF  */
#line 437 "awkgram.y"
                                { (yyval.p) = op1(VARNF, (Node *) (yyvsp[0].cp)); }
#line 3465 "y.tab.c"
    break;

  case 185: /* while: WHILE '(' pattern rparen  */
#line 442 "awkgram.y"
                                        { (yyval.p) = notnull((yyvsp[-1].p)); }
#line 3471 "y.tab.c"
    break;


#line 3475 "y.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 445 "awkgram.y"


void setfname(Cell *p)
{
	if (isarr(p))
		SYNTAX("%s is an array, not a function", p->nval);
	else if (isfcn(p))
		SYNTAX("you can't define function %s more than once", p->nval);
	curfname = p->nval;
}

int constnode(Node *p)
{
	return isvalue(p) && ((Cell *) (p->narg[0]))->csub == CCON;
}

char *strnode(Node *p)
{
	return ((Cell *)(p->narg[0]))->sval;
}

Node *notnull(Node *n)
{
	switch (n->nobj) {
	case LE: case LT: case EQ: case NE: case GT: case GE:
	case BOR: case AND: case NOT:
		return n;
	default:
		return op2(NE, n, nullnode);
	}
}

void checkdup(Node *vl, Cell *cp)	/* check if name already in list */
{
	char *s = cp->nval;
	for ( ; vl; vl = vl->nnext) {
		if (strcmp(s, ((Cell *)(vl->narg[0]))->nval) == 0) {
			SYNTAX("duplicate argument %s", s);
			break;
		}
	}
}
