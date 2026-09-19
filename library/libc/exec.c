#include <errno.h>
#include <stdarg.h>


int execl(const char *name, ...)
{
  va_list argp;
  int result; 

  va_start(argp, name);
  
  result = execve(name, (char **) argp, envlist);

  va_end(argp);
  return(result);
}

int execv(char *name, char *argv[])
{ 
  return(execve(name, argv, envlist));  
} 

