
/*
 *  linux/fs/fifo.c
 *
 *  written by Paul H. Hargrove
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/mm.h>


/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the access mode of the file...
 */
static struct file_operations def_fifo_fops = {
        NULL,   
        NULL,           
        NULL,   
        NULL,           
        NULL,   
        NULL,
        NULL,
        NULL, //fifo_open,              /* will set read or write pipe_fops */
        NULL,
        NULL
};              

struct inode_operations fifo_inode_operations = {
        &def_fifo_fops,         /* default file operations */
        NULL,                   /* create */
        NULL,                   /* lookup */
        NULL,                   /* link */
        NULL,                   /* unlink */
        NULL,                   /* symlink */
        NULL,                   /* mkdir */
        NULL,                   /* rmdir */
        NULL,                   /* mknod */
        NULL,                   /* rename */
        NULL,                   /* readlink */ 
        NULL,                   /* follow_link */
        NULL,                   /* readpage */
        NULL,                   /* writepage */
        NULL,                   /* bmap */
        NULL,                   /* truncate */
        NULL                    /* permission */
};

void init_fifo(struct inode * inode)
{       
        inode->i_op = &fifo_inode_operations;
        inode->i_pipe = 1;
        PIPE_LOCK(*inode) = 0;
        PIPE_BASE(*inode) = NULL;
        PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
        PIPE_RD_OPENERS(*inode) = PIPE_WR_OPENERS(*inode) = 0;
        PIPE_WAIT(*inode) = NULL;
        PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
}

