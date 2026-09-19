#include <linux/signal.h>

extern int (*_clean)(void);

void abort(void)
{
        if (_clean) _clean();           /* flush all output files */
        raise(SIGABRT);
}

