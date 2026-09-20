/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

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

#line 266 "y.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_Y_TAB_H_INCLUDED  */
