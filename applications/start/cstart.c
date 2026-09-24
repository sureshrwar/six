char **envlist = 0;
char **environ = 0;
extern void main(int, char **, char **);

typedef void (*init_fn_t)(void);
extern init_fn_t __init_array_start[] __attribute__((weak));
extern init_fn_t __init_array_end[] __attribute__((weak));

void cstart(int argc, char *argv[], char *envp[])
{
	init_fn_t *fn;

	environ = envlist = envp;
	if (__init_array_start && __init_array_end) {
		for (fn = __init_array_start; fn < __init_array_end; fn++) {
			if (*fn)
				(*fn)();
		}
	}
	main(argc, argv, envp);
	exit(0);
}

void _start(int argc, char *argv[], char *envp[])
{
	cstart(argc, argv, envp);
}
