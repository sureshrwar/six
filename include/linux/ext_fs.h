
#ifndef _LINUX_EXT_FS_H
#define _LINUX_EXT_FS_H

/*
 * The ext filesystem constants/structures
 */

#define EXT_NAME_LEN 255
#define EXT_ROOT_INO 1

#define EXT_SUPER_MAGIC 0x137D

#define EXT_INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct ext_inode)))

struct ext_inode {
        unsigned short i_mode;
        unsigned short i_uid;
        unsigned long i_size;
        unsigned long i_time;
        unsigned short i_gid;
        unsigned short i_nlinks;
        unsigned long i_zone[12];
};

struct ext_free_inode {
        unsigned long count;
        unsigned long free[14];
        unsigned long next;
};

struct ext_free_block {
        unsigned long count;
        unsigned long free[254];
        unsigned long next;
};

struct ext_super_block {
        unsigned long s_ninodes;
        unsigned long s_nzones;
        unsigned long s_firstfreeblock;
        unsigned long s_freeblockscount;
        unsigned long s_firstfreeinode;
        unsigned long s_freeinodescount;
        unsigned long s_firstdatazone;
        unsigned long s_log_zone_size;
        unsigned long s_max_size;
        unsigned long s_reserved1;
        unsigned long s_reserved2;
        unsigned long s_reserved3;
        unsigned long s_reserved4;
        unsigned long s_reserved5;
        unsigned short s_magic;
};

struct ext_dir_entry {
        unsigned long inode;
        unsigned short rec_len;
        unsigned short name_len;
        char name[EXT_NAME_LEN];
};

#endif
