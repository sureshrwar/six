
#ifndef _I386_STAT_H
#define _I386_STAT_H

struct old_stat {
        unsigned short st_dev;
        unsigned long  st_ino;  
        unsigned short st_mode;
        unsigned short st_nlink;
        unsigned short st_uid; 
        unsigned short st_gid;
        unsigned short st_rdev;
        unsigned long  st_size;
        unsigned long  st_atime; 
        unsigned long  st_mtime;
        unsigned long  st_ctime;
        unsigned long  st_blksize;
        unsigned long  st_blocks;
};      
        
struct new_stat {
    	unsigned long int st_dev;
    	long st_filler1[3];
    	unsigned long st_ino;             /* File serial number.          */
   	unsigned long int st_mode;  /* File mode.  */
  	unsigned long int st_nlink; /* Link count.  */
    	unsigned short st_uid;             /* User ID of the file's owner. */
   	unsigned short st_gid;             /* Group ID of the file's group.*/
  	unsigned long int st_rdev;  /* Device number, if device.  */ 
    	long st_filler2[2];
    	unsigned long st_size;            /* Size of file, in bytes.  */
    	long st_filler3;           
    	unsigned long st_atime;          /* Time of last access.  */
    	unsigned long int st_atime_usec;
    	unsigned long st_mtime;          /* Time of last modification.  */
    	unsigned long int st_mtime_usec;
    	unsigned long st_ctime;          /* Time of last status change.  */
    	unsigned long int st_ctime_usec;
    	long st_blksize;            /* Optimal block size for I/O.  */
    	long st_blocks;             /* Number of 512-byte blocks allocated.  */
    	char st_fstype[16];
    	long st_filler4[8];        
};      
        
#endif

