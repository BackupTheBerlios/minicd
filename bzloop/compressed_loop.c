/*
 *  Read-only compressed loop blockdevice
 *  hacked up by Rusty in 1999, extended and maintained by Klaus Knopper
 *
 *  cloop file looks like:
 *  [32-bit uncompressed block size: network order]
 *  [32-bit number of blocks (n_blocks): network order]
 *  [64-bit file offsets of start of blocks: native host byte order]
 *    * (n_blocks + 1).
 * n_blocks of:
 *   [compressed block]
 *
 *  Inspired by loop.c by Theodore Ts'o, 3/29/93.
 *
 * Copyright (C) 1999-2001, Paul `Rusty' Russell
 * Copyright (C) 1999-2003, Klaus Knopper <knopper@knopper.net>
 * Copyright (C) 2003,      Jaco Greeff <jaco@linuxminicd.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * CHANGELOG:
 * (see CHANGELOG file)
 *
 * $Id: compressed_loop.c,v 1.2 2003/09/25 09:39:11 jaco Exp $
 */

/* Define this if you are using Greenshoe Linux */
/* #define REDHAT_KERNEL */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/semaphore.h>
#include <asm/div64.h> /* do_div() for 64bit division */
#include <asm/uaccess.h>
#ifdef __BZLOOP
#  define _STDIO_H 1
#  ifndef BZ_NO_STDIO
#    define BZ_NO_STDIO
#  endif
#  include <bzlib.h>
#else
#  include <linux/zutil.h> /* Use zlib_inflate from lib/zlib_inflate */
#endif
#include <linux/loop.h>
#include "compressed_loop.h"

#ifdef __BZLOOP
#  define CLOOP_NAME    "bzloop"
#else
#  define CLOOP_NAME    "cloop"
#endif
#define CLOOP_MAX 8

EXPORT_NO_SYMBOLS;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9)
/* New License scheme */
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#endif

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

/* Use experimental major for now */
#define MAJOR_NR 240

#define DEVICE_NAME CLOOP_NAME
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)
#define DEVICE_NO_RANDOM
#define TIMEOUT_VALUE (6 * HZ)
#include <linux/blk.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, x...)
#endif

/* One file can be opened at module insertion time */
/* insmod cloop file=/path/to/file */
static char *file=NULL;
MODULE_PARM(file, "s");
static struct file *initial_file=NULL;

struct cloop_device
{
 /* Copied straight from the file */
 struct cloop_head head;

 /* An array of offsets of compressed blocks within the file */
 loff_t *offsets;

 /* We buffer one uncompressed `block' */
 int buffered_blocknum;
 void *buffer;
 void *compressed_buffer;

#ifdef __BZLOOP
 bz_stream zstream;
#else
 z_stream zstream;
#endif

 struct file   *backing_file;  /* associated file */
 struct inode  *backing_inode; /* for bmap */

 unsigned int underlying_blksize;
 int refcnt;
 int dev;
 int isblkdev;
 struct semaphore clo_lock;
};

static int cloop_sizes[CLOOP_MAX];
static int cloop_blksizes[CLOOP_MAX];

static struct cloop_device cloop_dev[CLOOP_MAX];
static char *cloop_name=CLOOP_NAME;
static const int max_cloop = CLOOP_MAX;
static devfs_handle_t devfs_handle;      /*  For the directory */

#ifndef __BZLOOP
#  ifndef CONFIG_ZLIB_INFLATE /* Not compiled in?! */
#    ifndef STATIC
#      define STATIC static
#    endif
#    include "/usr/src/linux/lib/inflate.c"
#  endif
#endif

#ifdef __BZLOOP
#  define BZLOOP_HI_SHIFT 16
#endif
static int uncompress(struct cloop_device *clo, char *dest, unsigned long *destLen,
                      char *source, unsigned long sourceLen)
{
 int err;
 DEBUGP("%s: uncompress: enter\n", cloop_name);
 /* Most of this code can be found in fs/cramfs/uncompress.c */
 clo->zstream.next_in = source;
 clo->zstream.avail_in = sourceLen;
 clo->zstream.next_out = dest;
 clo->zstream.avail_out = *destLen;
#ifdef __BZLOOP
 BZ2_bzDecompressEnd(&clo->zstream);
 BZ2_bzDecompressInit(&clo->zstream, 0, 0);
 err = BZ2_bzDecompress(&clo->zstream);
 *destLen = (((unsigned long)clo->zstream.total_out_hi32)<<BZLOOP_HI_SHIFT) + clo->zstream.total_out_lo32;
 if (err != BZ_STREAM_END) return err;
 return BZ_OK;
#else
 err = zlib_inflateReset(&clo->zstream);
 if (err != Z_OK)
  {
   printk(KERN_ERR "%s: zlib_inflateReset error %d\n", cloop_name, err);
   zlib_inflateEnd(&clo->zstream); zlib_inflateInit(&clo->zstream);
  }
 err = zlib_inflate(&clo->zstream, Z_FINISH);
 *destLen = clo->zstream.total_out;
 if (err != Z_STREAM_END) return err;
 return Z_OK;
#endif
}

/* Get blocksize of underlying device */
static unsigned int get_blksize(int dev)
{
 unsigned int bs = BLOCK_SIZE;
 DEBUGP("%s: get_blksize: enter\n", cloop_name);
 if (blksize_size[MAJOR(dev)])
  {
    bs = blksize_size[MAJOR(dev)][MINOR(dev)];
    if (!bs) bs = BLOCK_SIZE;
  }
 return bs;
}

/* This is more complicated than it looks. */
struct clo_read_data
{
 struct cloop_device *clo;
 char *data; /* We need to keep track of where we are in the buffer */
 int bsize;
};

/* We need this for do_generic_file_read() because the default function */
/* wants to read into user-space for an unknown reason. :-/ See loop.c. */
static int clo_read_actor(read_descriptor_t * desc, struct page *page,
                          unsigned long offset, unsigned long size)
{
 char *kaddr;
 struct clo_read_data *p = (struct clo_read_data*)desc->buf;
 unsigned long count = desc->count;
 DEBUGP("%s: clo_read_actor: enter\n", cloop_name);
 if (size > count) size = count;
 kaddr = kmap(page);
 memcpy(p->data, kaddr + offset, size);
 kunmap(page);
 desc->count = count - size;
 desc->written += size;
 p->data += size;
 return size;
}

static size_t clo_read_from_file(struct cloop_device *clo, struct file *f, char *buf,
  loff_t pos, size_t buf_len)
{
 size_t buf_done=0;
 DEBUGP("%s: clo_read_from_file: enter\n", cloop_name);
 while (buf_done < buf_len)
  {
   size_t size = buf_len - buf_done;
   struct clo_read_data cd={ /* do_generic_file_read() needs this. */
           clo,              /* struct cloop_device *clo */
           (char *)(buf + buf_done), /* char *data */
           size};            /* Actual data size */
   read_descriptor_t desc;
   desc.written = 0;
   desc.count   = size;
   desc.buf     = (char*)&cd;
   desc.error   = 0;
#ifdef REDHAT_KERNEL /* Greenshoe Linux */
   do_generic_file_read(f, &pos, &desc, clo_read_actor, 0);
#else /* Normal Kernel */
   do_generic_file_read(f, &pos, &desc, clo_read_actor);
#endif
   if(desc.error||desc.written<=0)
    {
     int left = size - desc.written;
     if(left<0) left = 0; /* better safe than sorry */
     printk(KERN_ERR "%s: Read error at pos %Lu in file %s, %d bytes lost.\n",
            cloop_name, pos, file, left);
     memset(buf + buf_len - left, 0, left);
     break;
    }
   buf_done+=desc.written;
  }
 return buf_done;
}

/* This looks more complicated than it is */
static int load_buffer(struct cloop_device *clo, int blocknum)
{
 unsigned int buf_done = 0;
 unsigned long buflen;
 unsigned int buf_length;
 int ret;

 DEBUGP("%s: load_buffer: enter\n", cloop_name);
 if(blocknum > ntohl(clo->head.num_blocks) || blocknum < 0)
  {
   printk(KERN_WARNING "%s: Invalid block number %d requested.\n",
                       cloop_name, blocknum);
   clo->buffered_blocknum = -1;
   return 0;
  }

 if (blocknum == clo->buffered_blocknum) return 1;

 /* Is there a ntohl for 64-bit values? */
 buf_length = clo->offsets[blocknum+1] - clo->offsets[blocknum];

/* Load one compressed block from the file. */
 clo_read_from_file(clo, clo->backing_file, (char *)clo->compressed_buffer,
                    clo->offsets[blocknum], buf_length);

 /* Do decompression into real buffer. */
 buflen = ntohl(clo->head.block_size);

 /* Do the uncompression */
 ret = uncompress(clo, clo->buffer, &buflen, clo->compressed_buffer,
                  buf_length);
 /* DEBUGP("cloop: buflen after uncompress: %ld\n",buflen); */
 if (ret != 0)
  {
   printk(KERN_ERR "%s: error %i uncompressing block %u %u/%lu/%u/%u "
          "%Lu-%Lu\n", cloop_name, ret, blocknum,
	  ntohl(clo->head.block_size), buflen, buf_length, buf_done,
	  clo->offsets[blocknum], clo->offsets[blocknum+1]);
   clo->buffered_blocknum = -1;
   return 0;
  }
 clo->buffered_blocknum = blocknum;
 return 1;
}

static int make_clo_request(request_queue_t *q, int rw, struct buffer_head *bh)
{
 struct cloop_device *cloop;
 int status = 0;
 int cloop_num;
 unsigned int len;
 loff_t offset;
 char *dest;

 DEBUGP("%s: make_clo_request: enter\n", cloop_name);
 /* (possible) high memory conversion */
 bh = blk_queue_bounce(q,rw,bh);

 /* quick sanity checks */
 if (rw != READ && rw != READA)
  {
   DEBUGP("%s: do_clo_request: bad command\n", cloop_name);
   goto out;
  }

 cloop_num = MINOR(bh->b_rdev);

 if (cloop_num >= max_cloop)
  {
   DEBUGP("%s: do_clo_request: invalid cloop minor\n", cloop_name);
   goto out;
  }

 cloop = &cloop_dev[cloop_num];

 if (!cloop->backing_file)
  {
   DEBUGP("%s: do_clo_request: not connected to a file\n", cloop_name);
   goto out;
  }

 if (bh->b_rsector == -1)
  {
   DEBUGP("%s: do_clo_request: bad sector requested\n", cloop_name);
   goto out;
  }

 len        = bh->b_size;
 offset     = (loff_t)bh->b_rsector << 9;
 dest       = bh->b_data;

 down(&cloop->clo_lock);

 while(len > 0)
  {
   u_int32_t length_in_buffer;
   loff_t block_offset=offset;

   /* do_div (div64.h) returns the 64bit division remainder and  */
   /* puts the result in the first argument, i.e. block_offset   */
   /* becomes the blocknumber to load, and offset_in_buffer the  */
   /* position in the buffer */
   u_int32_t offset_in_buffer;
   offset_in_buffer = do_div(block_offset, ntohl(cloop->head.block_size));

   status=load_buffer(cloop,block_offset);
   if(!status) break; /* invalid data, leave inner loop, goto next request */

   /* Now, at least part of what we want will be in the buffer. */
   length_in_buffer = ntohl(cloop->head.block_size) - offset_in_buffer;

   if(length_in_buffer > len)
    {
/*     DEBUGP("Warning: length_in_buffer=%u > len=%u\n",
                        length_in_buffer,len); */
     length_in_buffer = len;
    }

   memcpy(dest, cloop->buffer + offset_in_buffer, length_in_buffer);

   dest   += length_in_buffer;
   len    -= length_in_buffer;
   offset += length_in_buffer;
  } /* while inner loop */

 up(&cloop->clo_lock);

out:
 bh->b_end_io(bh,status);
 return 0;
}

/* Read header and offsets from already opened file */
static int clo_set_file(int cloop_num, struct file *file, char *filename)
{
 struct cloop_device *clo=&cloop_dev[cloop_num];
 struct inode *inode;
 char *bbuf=NULL;
 unsigned int i, offsets_read, total_offsets;
 unsigned long largest_block=0;
 int isblkdev, dev;
 int error = 0;

 DEBUGP("%s: clo_set_file: enter\n", cloop_name);
 inode = file->f_dentry->d_inode;
 isblkdev=S_ISBLK(inode->i_mode)?1:0;
 dev=isblkdev?inode->i_rdev:inode->i_dev;
 if(!isblkdev&&!S_ISREG(inode->i_mode))
  {
   printk(KERN_ERR "%s: %s not a regular file or block device\n",
		   cloop_name, filename);
   error=-EBADF; goto error_release;
  }

 clo->backing_file = file;
 clo->dev = dev;
 clo->backing_inode= inode ;

 if(!isblkdev&&inode->i_size<sizeof(struct cloop_head))
  {
   printk(KERN_ERR "%s: %lu bytes (must be >= %u bytes)\n",
                   cloop_name, (unsigned long)inode->i_size,
		   (unsigned)sizeof(struct cloop_head));
   error=-EBADF; goto error_release;
  }

#ifdef __BZLOOP
 clo->underlying_blksize = sizeof(struct cloop_head);
#else
  /* Get initial block size out of device */
 clo->underlying_blksize = get_blksize(dev);
#endif
 DEBUGP("%s: Underlying blocksize is %u\n", cloop_name, clo->underlying_blksize);

 bbuf = vmalloc(clo->underlying_blksize);
 if(!bbuf)
  {
   printk(KERN_ERR "%s: out of kernel mem for block buffer (%lu bytes)\n",
                   cloop_name, (unsigned long)clo->underlying_blksize);
   error=-ENOMEM; goto error_release;
  }

 total_offsets = 1; /* Dummy total_offsets: will be filled in first time around */
 for (i = 0, offsets_read = 0; offsets_read < total_offsets; i++)
  {
   unsigned int offset = 0, num_readable;

   /* Kernel 2.4 version */
   size_t bytes_read = clo_read_from_file(clo, file, bbuf,
                                          i*clo->underlying_blksize,
                                          clo->underlying_blksize);
   if(bytes_read != clo->underlying_blksize) { error=-EBADF; goto error_release; }
#ifdef __BZLOOP
   DEBUGP("%s: bytes_read=%u, i=%u\n", cloop_name, bytes_read, i);
#endif

   if(i==0)
    {
     memcpy(&clo->head, bbuf, sizeof(struct cloop_head));
     offset = sizeof(struct cloop_head);
#ifdef __BZLOOP
     DEBUGP("%s: preamble=%s", cloop_name, clo->head.preamble);
     DEBUGP("%s: offset=%u(%u), version=%u, block_size=%u, num_blocks=%u\n",
            cloop_name, offset, clo->underlying_blksize,
            clo->head.version, clo->head.block_size, clo->head.num_blocks);
#endif
     if (ntohl(clo->head.block_size) % 512 != 0)
      {
       printk(KERN_ERR "%s: blocksize %u not multiple of 512\n",
              cloop_name, ntohl(clo->head.block_size));
       error=-EBADF; goto error_release;
      }

     total_offsets=ntohl(clo->head.num_blocks)+1;

     if (!isblkdev && (sizeof(struct cloop_head)+sizeof(u_int32_t)*
                       total_offsets > inode->i_size))
      {
       printk(KERN_ERR "%s: file too small for %u blocks\n",
              cloop_name, ntohl(clo->head.num_blocks));
       error=-EBADF; goto error_release;
      }

     DEBUGP("%s: total offsets is %u\n", cloop_name, total_offsets);
     clo->offsets = vmalloc(sizeof(loff_t) * total_offsets);
     if (!clo->offsets)
      {
       printk(KERN_ERR "%s: out of kernel mem for offsets\n", cloop_name);
       error=-ENOMEM; goto error_release;
      }
    }

   num_readable = MIN(total_offsets - offsets_read,
                      (clo->underlying_blksize - offset)
                      / sizeof(loff_t));
   memcpy(&clo->offsets[offsets_read], bbuf+offset, num_readable * sizeof(loff_t));
   offsets_read += num_readable;
  }

  { /* Search for largest block rather than estimate. KK. */
   int i;
   for(i=0;i<total_offsets-1;i++)
    {
     loff_t d=clo->offsets[i+1] - clo->offsets[i];
     largest_block=MAX(largest_block,d);
    }
   printk("%s: %s: %u blocks, %u bytes/block, largest block is %lu bytes.\n",
          cloop_name, filename, ntohl(clo->head.num_blocks),
          ntohl(clo->head.block_size), largest_block);
  }

/* Combo kmalloc used too large chunks (>130000). */
 DEBUGP("%s: header blocksize is %u\n", cloop_name, clo->head.block_size);
 clo->buffer = vmalloc(ntohl(clo->head.block_size));
 if(!clo->buffer)
  {
   printk(KERN_ERR "%s: out of memory for buffer %lu\n",
          cloop_name, (unsigned long) ntohl(clo->head.block_size));
   error=-ENOMEM; goto error_release_free;
  }

 DEBUGP("%s: largest blocksize is %lu\n", cloop_name, largest_block);
 clo->compressed_buffer = vmalloc(largest_block);

 if(!clo->compressed_buffer)
  {
   printk(KERN_ERR "%s: out of memory for compressed buffer %lu\n",
          cloop_name, largest_block);
   error=-ENOMEM; goto error_release_free_buffer;
  }
#ifdef __BZLOOP
 DEBUGP("%s: initialising bzlib\n", cloop_name);
 BZ2_bzDecompressInit(&clo->zstream, 0, 0);
#else
 clo->zstream.workspace = vmalloc(zlib_inflate_workspacesize());
 if(!clo->zstream.workspace)
  {
   printk(KERN_ERR "%s: out of mem for zlib working area %u\n",
          cloop_name, zlib_inflate_workspacesize());
   error=-ENOMEM; goto error_release_free_all;
  }
 zlib_inflateInit(&clo->zstream);
#endif

 if(!isblkdev &&
    clo->offsets[ntohl(clo->head.num_blocks)] != inode->i_size)
  {
   printk(KERN_ERR "%s: final offset wrong (%Lu not %Lu)\n",
          cloop_name,
          clo->offsets[ntohl(clo->head.num_blocks)],
          inode->i_size);
#ifndef __BZLOOP
   vfree(clo->zstream.workspace); clo->zstream.workspace=NULL;
#endif
   goto error_release_free_all;
  }

 clo->buffered_blocknum = -1;
 cloop_sizes[cloop_num] = ntohl(clo->head.num_blocks)
                      * ( ntohl(clo->head.block_size) / BLOCK_SIZE );
 /* this seems to be the maximum allowed blocksize (Kernel limit) */
 cloop_blksizes[cloop_num] = PAGE_SIZE;
 return error;

error_release_free_all:
 vfree(clo->compressed_buffer);
 clo->compressed_buffer=NULL;
error_release_free_buffer:
 vfree(clo->buffer);
 clo->buffer=NULL;
error_release_free:
 vfree(clo->offsets);
 clo->offsets=NULL;
error_release:
 if(bbuf) vfree(bbuf);
 clo->backing_file=NULL;
 return error;
}

/* Code adapted from Theodore Ts'o's linux/drivers/block/loop.c */
/* Get file from ioctl arg (losetup) */
static int clo_set_fd(int cloop_num, struct file *clo_file, kdev_t dev,
		       unsigned int arg)
{
 struct cloop_device *clo=&cloop_dev[cloop_num];
 struct file *file=NULL;
 int error = 0;

 DEBUGP("%s: clo_set_fd: enter\n", cloop_name);
 /* Already an allocated file present */
 if(clo->backing_file) return -EBUSY;
 file = fget(arg); /* get filp struct from ioctl arg fd */
 if(!file) return -EBADF;
 error=clo_set_file(cloop_num,file,"losetup_file");
 if(error) fput(file);
 return error;
}

static int clo_clr_fd(int cloop_num, struct block_device *bdev)
{
 struct cloop_device *clo = &cloop_dev[cloop_num];
 struct file *filp = clo->backing_file;

 DEBUGP("%s: clo_clr_fd: enter\n", cloop_name);
 if(clo->refcnt > 1)	/* we needed one fd for the ioctl */
   return -EBUSY;
 if(filp==NULL) return -EINVAL;
 if(filp!=initial_file) fput(filp);
 else { filp_close(initial_file,0); initial_file=NULL; }
 clo->backing_file  = NULL;
 clo->backing_inode = NULL;
 cloop_sizes[cloop_num] = 0;
 cloop_blksizes[cloop_num] = 0;
 return 0;
}

static int clo_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct cloop_device *clo;
	int cloop_num, err=0;

	DEBUGP("%s: clo_ioctl: enter\n", cloop_name);
	if (!inode) return -EINVAL;
	if (MAJOR(inode->i_rdev) != MAJOR_NR) {
		printk(KERN_WARNING "cloop_ioctl: pseudo-major != %d\n", MAJOR_NR);
		return -ENODEV;
	}
	cloop_num = MINOR(inode->i_rdev);
	if (cloop_num >= max_cloop) return -ENODEV;
	clo = &cloop_dev[cloop_num];
	switch (cmd) { /* We use the same ioctls that loop does */
	case LOOP_SET_FD:
	 err = clo_set_fd(cloop_num, file, inode->i_rdev, arg);
	 break;
	case LOOP_CLR_FD:
	 err = clo_clr_fd(cloop_num, inode->i_bdev);
	 break;
        case LOOP_SET_STATUS:
        case LOOP_GET_STATUS:
	 err=0; break;
	default:
	 err = -EINVAL;
	}
	return err;
}


static int clo_open(struct inode *inode, struct file *file)
{
 int cloop_num;
 if(!inode) return -EINVAL;

 DEBUGP("%s: clo_open: enter\n", cloop_name);
 if(MAJOR(inode->i_rdev) != MAJOR_NR)
  {
   printk(KERN_WARNING "%s: pseudo-major != %d\n", cloop_name, MAJOR_NR);
   return -ENODEV;
  }

 cloop_num=MINOR(inode->i_rdev);
 if(cloop_num >= max_cloop) return -ENODEV;

 /* Allow write open for ioctl, but not for mount. */
 /* losetup uses write-open and flags=0x8002 to set a new file */
 if((file->f_mode & FMODE_WRITE) && !(file->f_flags & 0x2))
  {
   printk(KERN_WARNING "%s: Can't open device read-write\n", cloop_name);
   return -EROFS;
  }

 cloop_dev[cloop_num].refcnt+=1;
 MOD_INC_USE_COUNT;
 return 0;
}

static int clo_close(struct inode *inode, struct file *file)
{
 int cloop_num, err=0;

 DEBUGP("%s: clo_close: enter\n", cloop_name);
 if(!inode) return 0;

 if(MAJOR(inode->i_rdev) != MAJOR_NR)
  {
   printk(KERN_WARNING "%s: pseudo-major != %d\n", cloop_name, MAJOR_NR);
   return 0;
  }

 cloop_num=MINOR(inode->i_rdev);
 if(cloop_num >= max_cloop) return 0;

 err = fsync_dev(inode->i_rdev);
 cloop_dev[cloop_num].refcnt-=1;
 MOD_DEC_USE_COUNT;
 return err;
}

static struct block_device_operations clo_fops =
{
        owner:		THIS_MODULE,
        open:           clo_open,
        release:        clo_close,
        ioctl:          clo_ioctl
};

int init_module(void)
{
 int i, error=0;
 DEBUGP("%s: init_module: enter\n", cloop_name);
 printk("%s: Initializing cloop v"CLOOP_VERSION"\n", cloop_name);
 DEBUGP("%s: creating max devices\n", cloop_name);
 for(i=0;i<max_cloop;i++)
  {
   memset(&cloop_dev[i],0,sizeof(struct cloop_device));
   init_MUTEX(&cloop_dev[i].clo_lock);
  }
 DEBUGP("%s: opening loop file, %s\n", cloop_name, file);
 if(file) /* global file name for first cloop-Device is a module option string. */
  {
   initial_file=filp_open(file,0x00,0x00);
   if(initial_file==NULL||IS_ERR(initial_file))
    {
     printk(KERN_ERR
            "%s: Unable to get file %s for cloop device\n",
            cloop_name, file);
     return -EINVAL;
    }
   error=clo_set_file(0,initial_file,file);
   if(error) goto error_filp_close;
  }
 DEBUGP("%s: registering block device\n", cloop_name);
 if(devfs_register_blkdev(MAJOR_NR, cloop_name, &clo_fops))
  {
   printk(KERN_WARNING "%s: Unable to get major %d for cloop\n",
          cloop_name, MAJOR_NR);
   if(initial_file) { error=-EIO; goto error_filp_close; }
  }

 blk_size[MAJOR_NR] = cloop_sizes;
 blksize_size[MAJOR_NR] = cloop_blksizes;
 blk_queue_make_request(BLK_DEFAULT_QUEUE(MAJOR_NR), make_clo_request);

 for (i=0;i<max_cloop;i++) register_disk(NULL,MKDEV(MAJOR_NR,i),1,&clo_fops,0);

#ifdef __BZLOOP
 devfs_handle = devfs_mk_dir(NULL, "bzloop", NULL);
#else
 devfs_handle = devfs_mk_dir(NULL, "cloop", NULL);
#endif
 devfs_register_series(devfs_handle, "%u", max_cloop, DEVFS_FL_DEFAULT,
		 MAJOR_NR, 0,
		 S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP,
		 &clo_fops, NULL);

 printk(KERN_INFO "%s: loaded (max %d devices)\n", cloop_name, max_cloop);

 return 0;
error_filp_close:
 filp_close(initial_file,0); initial_file=NULL;
 cloop_dev[0].backing_file=NULL;
 return error;
}

void cleanup_module(void)
{
 int i;
 DEBUGP("%s: cleanup_module: enter\n", cloop_name);
 if(devfs_unregister_blkdev(MAJOR_NR, cloop_name) != 0)
   printk(KERN_WARNING "%s: cannot unregister block device\n", cloop_name);
 for(i=0;i<max_cloop;i++)
  {
   if(cloop_dev[i].offsets) vfree(cloop_dev[i].offsets);
   if(cloop_dev[i].buffer)  vfree(cloop_dev[i].buffer);
   if(cloop_dev[i].compressed_buffer) vfree(cloop_dev[i].compressed_buffer);
#ifdef __BZLOOP
   BZ2_bzDecompressEnd(&cloop_dev[i].zstream);
#else
   zlib_inflateEnd(&cloop_dev[i].zstream);
   if(cloop_dev[i].zstream.workspace) vfree(cloop_dev[i].zstream.workspace);
#endif
   if(cloop_dev[i].backing_file && cloop_dev[i].backing_file!=initial_file)
    {
     fput(cloop_dev[i].backing_file);
    }
  }
 if(initial_file) filp_close(initial_file,0);
 printk("%s: unloaded.\n", cloop_name);
}
