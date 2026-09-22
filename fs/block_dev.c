
/*
 *  linux/fs/block_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */


#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/system.h>

extern int *blk_size[];
extern int *blksize_size[];

#define MAX_BUF_PER_PAGE (PAGE_SIZE / 512)
#define NBUF 64

int block_write(struct inode * inode, struct file * filp, const char * buf, int count)
{
        int blocksize, blocksize_bits, i, j, buffercount,write_error;
        int block, blocks;
        loff_t offset;
        int chars;
        int written = 0;
        int cluster_list[MAX_BUF_PER_PAGE];
        struct buffer_head * bhlist[NBUF];
        int blocks_per_cluster;
        unsigned int size;
        kdev_t dev;
        struct buffer_head * bh, *bufferlist[NBUF];
        register char * p;
        int excess;

        write_error = buffercount = 0;
        dev = inode->i_rdev;
        if ( is_read_only( inode->i_rdev ))
                return -EPERM;
        blocksize = BLOCK_SIZE;
        if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
                blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];

        i = blocksize;
        blocksize_bits = 0;
        while(i != 1) {
                blocksize_bits++;
                i >>= 1;
        }
        blocks_per_cluster = PAGE_SIZE / blocksize;

        block = filp->f_pos >> blocksize_bits;
        offset = filp->f_pos & (blocksize-1);

        if (blk_size[MAJOR(dev)])
                size = ((loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS) >> blocksize_bits;
        else
                size = INT_MAX;
        while (count>0) {
                if (block >= size)
                        return written ? written : -ENOSPC;
                chars = blocksize - offset;
                if (chars > count)
                        chars=count;

#if 0
                if (chars == blocksize)
                        bh = getblk(dev, block, blocksize);
                else
                        bh = breada(dev,block,block+1,block+2,-1);

#else
                for(i=0; i<blocks_per_cluster; i++) cluster_list[i] = block+i;
                if((block % blocks_per_cluster) == 0)
                  generate_cluster(dev, cluster_list, blocksize);
                bh = getblk(dev, block, blocksize);

                if (chars != blocksize && !buffer_uptodate(bh)) {
                  if(!filp->f_reada ||
                     !read_ahead[MAJOR(dev)]) {
                    /* We do this to force the read of a single buffer */
                    brelse(bh);
                    bh = bread(dev,block,blocksize);
                  } else {
                    /* Read-ahead before write */
                    blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9) / 2;
                    if (block + blocks > size) blocks = size - block;
                    if (blocks > NBUF) blocks=NBUF;
                    excess = (block + blocks) % blocks_per_cluster;
                    if ( blocks > excess )
                        blocks -= excess;
                    bhlist[0] = bh;
                    for(i=1; i<blocks; i++){
                      if(((i+block) % blocks_per_cluster) == 0) {
                        for(j=0; j<blocks_per_cluster; j++) cluster_list[j] = block+i+j;
                        generate_cluster(dev, cluster_list, blocksize);
                      };
                      bhlist[i] = getblk (dev, block+i, blocksize);
                      if(!bhlist[i]){
                        while(i >= 0) brelse(bhlist[i--]);
                        return written ? written : -EIO;
                      };
                    };
                    ll_rw_block(READ, blocks, bhlist);
                    for(i=1; i<blocks; i++) brelse(bhlist[i]);
                    wait_on_buffer(bh);

                  };
                };
#endif
                block++;
                if (!bh)
                        return written ? written : -EIO;
                p = offset + bh->b_data;
                offset = 0;
                filp->f_pos += chars;
                written += chars;
                count -= chars;
                memcpy_fromfs(p,buf,chars);
                p += chars;
                buf += chars;
                mark_buffer_uptodate(bh, 1);
                mark_buffer_dirty(bh, 0);
                if (filp->f_flags & O_SYNC)
                        bufferlist[buffercount++] = bh;
                else
                        brelse(bh);
                if (buffercount == NBUF){
                        ll_rw_block(WRITE, buffercount, bufferlist);
                        for(i=0; i<buffercount; i++){
                                wait_on_buffer(bufferlist[i]);
                                if (!buffer_uptodate(bufferlist[i]))
                                        write_error=1;
                                brelse(bufferlist[i]);
                        }
                        buffercount=0;
                }
                if(write_error)
                        break;
        }
        if ( buffercount ){
                ll_rw_block(WRITE, buffercount, bufferlist);
                for(i=0; i<buffercount; i++){
                        wait_on_buffer(bufferlist[i]);
                        if (!buffer_uptodate(bufferlist[i]))
                                write_error=1;
                        brelse(bufferlist[i]);
                }
        }
        /*
         * Missing in the original tree, exactly as in block_read() below:
         * the function computed write_error and written and then fell off
         * the end without returning either.
         */
        filp->f_reada = 0;
        if (write_error)
                return -EIO;
        return written;
}


int block_read(struct inode * inode, struct file * filp, char * buf, int count)
{
        unsigned int block;
        loff_t offset;
        int blocksize;
        int blocksize_bits, i;
        unsigned int blocks, rblocks, left;
        int bhrequest, uptodate;
        int cluster_list[MAX_BUF_PER_PAGE];
        int blocks_per_cluster;
        struct buffer_head ** bhb, ** bhe;
        struct buffer_head * buflist[NBUF];
        struct buffer_head * bhreq[NBUF];
        unsigned int chars;
        loff_t size;
        kdev_t dev;
        int read;
        int excess;

        dev = inode->i_rdev;
        blocksize = BLOCK_SIZE;
        if (blksize_size[MAJOR(dev)] && blksize_size[MAJOR(dev)][MINOR(dev)])
                blocksize = blksize_size[MAJOR(dev)][MINOR(dev)];
        i = blocksize;
        blocksize_bits = 0;
        while (i != 1) {
                blocksize_bits++;
                i >>= 1;
        }
        /*
         * There used to be a second, identical copy of the loop above
         * here.  It re-zeroed blocksize_bits and then did nothing, because
         * the first loop had already shifted i down to 1 -- so block_read()
         * always ran with a shift of 0.  That makes "block = offset >>
         * blocksize_bits" use the byte offset as a block number and
         * "offset & (blocksize-1)" select the wrong bytes within the
         * block, and the copy loop then returns whatever was already in
         * the user's buffer.
         *
         * The duplication dates from the original 2005 tree and never
         * mattered, because reading a block device requires a node under
         * /dev and there wasn't one until /dev/hda was added: mount_root()
         * reaches the disk through the buffer cache, not through
         * block_read().  block_write() below has always had just the one
         * copy.
         */

        offset = filp->f_pos;
        if (blk_size[MAJOR(dev)])
                size = (loff_t) blk_size[MAJOR(dev)][MINOR(dev)] << BLOCK_SIZE_BITS;
        else
                size = INT_MAX;

        blocks_per_cluster = PAGE_SIZE / blocksize;

        if (offset > size)
                left = 0;
        /* size - offset might not fit into left, so check explicitly. */
        else if (size - offset > INT_MAX)
                left = INT_MAX;
        else
                left = size - offset;
        if (left > count)
                left = count;
        if (left <= 0)
                return 0;
        read = 0;
        block = offset >> blocksize_bits;
        offset &= blocksize-1;
        size >>= blocksize_bits;
        rblocks = blocks = (left + offset + blocksize - 1) >> blocksize_bits;
        bhb = bhe = buflist;
        if (filp->f_reada) {
                if (blocks < read_ahead[MAJOR(dev)] / (blocksize >> 9))
                        blocks = read_ahead[MAJOR(dev)] / (blocksize >> 9);
                excess = (block + blocks) % blocks_per_cluster;
                if ( blocks > excess )
                        blocks -= excess;
                if (rblocks > blocks)
                        blocks = rblocks;

        }
        if (block + blocks > size)
                blocks = size - block;

        /* We do this in a two stage process.  We first try to request
           as many blocks as we can, then we wait for the first one to
           complete, and then we try to wrap up as many as are actually
           done.  This routine is rather generic, in that it can be used
           in a filesystem by substituting the appropriate function in
           for getblk.

           This routine is optimized to make maximum use of the various
           buffers and caches. */
        do {
                bhrequest = 0;
                uptodate = 1;
                while (blocks) {
                        --blocks;
#if 1
                        if((block % blocks_per_cluster) == 0) {
                          for(i=0; i<blocks_per_cluster; i++) cluster_list[i] = block+i;
                          generate_cluster(dev, cluster_list, blocksize);
                        }
#endif
                        *bhb = getblk(dev, block++, blocksize);
                        if (*bhb && !buffer_uptodate(*bhb)) {
                                uptodate = 0;
                                bhreq[bhrequest++] = *bhb;
                        }

                        if (++bhb == &buflist[NBUF])
                                bhb = buflist;

                        /* If the block we have on hand is uptodate, go ahead
                           and complete processing. */
                        if (uptodate)
                                break;
                        if (bhb == bhe)
                                break;
                }

                /* Now request them all */
                if (bhrequest) {
                        ll_rw_block(READ, bhrequest, bhreq);
                        refill_freelist(blocksize);
                }

                do { /* Finish off all I/O that has actually completed */
                        if (*bhe) {
                                wait_on_buffer(*bhe);
                                if (!buffer_uptodate(*bhe)) {   /* read error? */
                                        brelse(*bhe);
                                        if (++bhe == &buflist[NBUF])
                                          bhe = buflist;
                                        left = 0;
                                        break;
                                }
                        }
                        if (left < blocksize - offset)
                                chars = left;
                        else
                                chars = blocksize - offset;
                        filp->f_pos += chars;
                        left -= chars;
                        read += chars;
                        /*
                         * A second, identical copy of the wait-and-account
                         * sequence above used to sit here, between the
                         * accounting and the memcpy_tofs() that consumes
                         * chars.  It advanced f_pos, drained left and added
                         * to read a second time while only one block was
                         * ever copied, so the byte count this function
                         * reported ran at twice the truth and f_pos skipped
                         * every other block.  Compare fs/sysv/file.c:155-184
                         * and fs/ext/file.c, which carry the same loop
                         * undamaged.
                         */
                        if (*bhe) {
                                memcpy_tofs(buf,offset+(*bhe)->b_data,chars);
                                brelse(*bhe);
                                buf += chars;
                        } else {
                                while (chars-- > 0)
                                        put_user(0,buf++);
                        }
                        offset = 0;
                        if (++bhe == &buflist[NBUF])
                                bhe = buflist;
                } while (left > 0 && bhe != bhb && (!*bhe || !buffer_locked(*bhe)));
        } while (left > 0);

/*
 * Release the read-ahead blocks, then report how much was copied.
 *
 * Everything from here down was simply absent: the function ran its loop
 * and then fell off the end without a return statement, so callers got
 * whatever happened to be in the return register.  That is why the first
 * read() of /dev/hda appeared to succeed with an enormous byte count while
 * leaving the caller's buffer untouched.  It went unnoticed for twenty
 * years because nothing could reach block_read() -- there was no block
 * device node in the image, and the root filesystem talks to the buffer
 * cache directly.  The identical loop in fs/sysv/file.c:187-200 and
 * fs/ext/file.c:186-195 shows what belongs here.
 *
 * The read-ahead loop matters as much as the return: without it, every
 * buffer that was requested but not consumed keeps its reference forever
 * and is never returned to the free list.
 */
        while (bhe != bhb) {
                brelse(*bhe);
                if (++bhe == &buflist[NBUF])
                        bhe = buflist;
        };
        if (!read)
                return -EIO;
        filp->f_reada = 1;
        return read;
}


int block_fsync(struct inode *inode, struct file *filp)
{
        /*
         * Also empty in the original tree, and also missing its return.
         * Flushing the device is the whole point of the method: it is what
         * fsync(2) on /dev/hda, and BLKFLSBUF's fsync_dev() call, rely on.
         */
        return fsync_dev(inode->i_rdev);
}
