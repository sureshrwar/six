#ifndef _VAR_H
#define _VAR_H

struct  var {   
        char    *value;         
        char    *name;          
        struct  var     *next;  
        char    status; 
};
#define COPYV   1       /* flag to setval, suggesting copy */
#define RONLY   01      /* variable is read-only */
#define EXPORT  02      /* variable is to be exported */
#define GETCELL 04      /* name & value space was got with getcell */

extern  struct  var     *vlist;         /* dictionary */
        
extern  struct  var     *homedir;       /* home directory */
extern  struct  var     *prompt;        /* main prompt */
extern  struct  var     *cprompt;       /* continuation prompt */
extern  struct  var     *path;          /* search path for commands */
extern  struct  var     *shell;         /* shell to interpret command files */
extern  struct  var     *ifs;           /* field separators */

_PROTOTYPE(int yyparse , (void));
_PROTOTYPE(struct var *lookup , (char *n )); 
_PROTOTYPE(void setval , (struct var *vp , char *val ));
_PROTOTYPE(void nameval , (struct var *vp , char *val , char *name ));
_PROTOTYPE(void export , (struct var *vp ));
_PROTOTYPE(void ronly , (struct var *vp ));
_PROTOTYPE(int isassign , (char *s ));
_PROTOTYPE(int checkname , (char *cp ));
_PROTOTYPE(int assign , (char *s , int cf ));
_PROTOTYPE(void putvlist , (int f , int out ));
_PROTOTYPE(int eqname , (char *n1 , char *n2 ));
 
_PROTOTYPE(int execute , (struct op *t , int *pin , int *pout , int act ));

#endif // _VAR_H

