#define nil 0

#include <linux/types.h>
#include <linux/limits.h>
#include <stat.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>



static int addpath(const char *path, char **ap, const char *entry)
{               
        const char *e= entry;
        char *p= *ap;
                
        while (*e != 0) e++;
        
        while (e > entry && p > path) *--p = *--e;

        if (p == path) return -1;
        *--p = '/';
        *ap= p;
        return 0;
}       


static int recover(char *p)
{
        int e= errno, slash;
        char *p0;

        while (*p != 0) {
                p0= ++p;

                do p++; while (*p != 0 && *p != '/');
                slash= *p; *p= 0;

                if (chdir(p0) < 0) return -1;
                *p= slash;
        }
        errno= e;
        return 0;
}

char *getcwd(char *path, size_t size)
{
        struct stat above, current, tmp;
        struct dirent *entry;
        int d;
        char *p, *up, *dotdot;
        int cycle;
        
        if (path == nil || size <= 2) { errno= EINVAL; return nil; }
        
        p= path + size;
        *--p = 0;
        
        if (stat(".", &current) < 0) return nil;
        
        while (1) {
		int ret = 0, cur = 0;
		char temp[256];

                dotdot= "..";
                if (stat(dotdot, &above) < 0) { recover(p); return nil; }
                
                if (above.st_dev == current.st_dev
                                        && above.st_ino == current.st_ino)
                        break;  /* Root dir found */
                
                if ((d= open(dotdot, 0)) < 0) { recover(p); return nil; }
                
                /* Cycle is 0 for a simple inode nr search, or 1 for a search
                 * for inode *and* device nr.
                 */
                cycle= above.st_dev == current.st_dev ? 0 : 1;
                
                do {    
                        char name[3 + NAME_MAX + 1];
                        
                        tmp.st_ino= 0;
                        if (cur >= ret) {
                                cur = 0;
                                ret = getdents(d, (struct dirent *)temp, sizeof(temp));
                                if (ret <= 0) {
                                        ret = 0;
                                        switch (++cycle) {
                                        case 1: 
                                                lseek(d, 0, SEEK_SET);
                                                continue;
                                        case 2: 
                                                close(d);
                                                errno= ENOENT;
                                                recover(p);
                                                return nil;
                                        }
                                }
                        }
                        entry = (struct dirent *)(temp + cur);
                        if (entry->d_reclen <= 0) {
                                cur = ret;
                                continue;
                        }
                        cur += entry->d_reclen;
                        if (strcmp(entry->d_name, ".") == 0) continue;
                        if (strcmp(entry->d_name, "..") == 0) continue;

                        switch (cycle) {
                        case 0:
                                /* Simple test on inode nr. */
                                if (entry->d_ino != current.st_ino) continue;
                                /*FALL THROUGH*/

                        case 1:
                                /* Current is mounted. */
                                strcpy(name, "../");
                                strcpy(name+3, entry->d_name);
                                if (stat(name, &tmp) < 0) continue;
                                break;
                        }
                } while (tmp.st_ino != current.st_ino
                                        || tmp.st_dev != current.st_dev);

                up= p;
                if (addpath(path, &up, entry->d_name) < 0) {
                        close(d);
                        errno = ERANGE;
                        recover(p);
                        return nil;
                }
                close(d);

                if (chdir(dotdot) < 0) { recover(p); return nil; }
                p= up;

                current= above;
        }
        if (recover(p) < 0) return nil; /* Undo all those chdir("..")'s. */
        if (*p == 0) *--p = '/';        /* Cwd is "/" if nothing added */
        if (p > path) strcpy(path, p);  /* Move string to start of path. */
        return path;
}

