/*
 * Creates a compressed image, given a file as an argument.
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
 * * Wed Sep 24 2003 Jaco Greeff <jaco@linuxminicd.org>
 * - Added bzip2 support for bzloop.o
 * * Sat Sep 29 2001 Klaus Knopper <knopper@knopper.net>
 * - changed compression to Z_BEST_COMPRESSION,
 * * Sat Jun 17 2000 Klaus Knopper <knopper@knopper.net>
 * - Support for reading file from stdin,
 * - Changed Preamble.
 * * Sat Jul 28 2001 Klaus Knopper <knopper@knopper.net>
 * - cleanup and gcc 2.96 / glibc checking
 *
 * $Id: create_compressed_fs.c,v 1.2 2003/09/25 09:39:11 jaco Exp $
 */

#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef __BZLOOP
#  include <bzlib.h>
#else
#  include <zlib.h>
#endif
#include "compressed_loop.h"

#define MAX_KMALLOC_SIZE 2L<<17

#ifdef __BZLOOP
/* also cater for devfs in the bzloop script */
#  define CLOOP_PREAMBLE "#!/bin/sh\n"                                  \
                         "# bzloop: start\n"                            \
                         "# create_bzfs, version " CLOOP_VERSION "\n"   \
                         "insmod bzloop.o file=$0 || exit 1\n"          \
                         "DEV=/dev/bzloop/0\n"                          \
                         "[ ! -e $DEV ] && DEV=/dev/bzloop\n"           \
                         "mount -r -t iso9660 $DEV $1\n"                \
                         "exit $?\n"                                    \
                         "# bzloop: end\n"
#else
#  define CLOOP_PREAMBLE "#!/bin/sh\n" \
                         "#V1.0 Format\n" \
                         "insmod cloop.o file=$0 && mount -r -t iso9660 /dev/cloop $1\n" \
                         "exit $?\n"
#endif

struct cb_list
{
	struct cb_list *next;
	size_t size;
	char data[0];
};

void free_cb_list(struct cb_list *cbl)
{
 if(cbl->next) free_cb_list(cbl->next);
 free(cbl);
}

/* Now using the goto style because it is quicker to read */
static struct cb_list *create_compressed_blocks(int handle, unsigned long
                          blocksize, unsigned long *numblocks)
{
 struct cb_list *cbl,**cbp=&cbl;
 unsigned long i=0;
 unsigned int last;
 unsigned long long total_uncompressed=0,total_compressed=0;
#ifdef __BZLOOP
 /* this size is too large, but works in all circumstances, we should
    really fix it with the real maximum size (also in extract_compressed_fs.c) */
 unsigned long maxlen = blocksize + blocksize/100 + 12;
#else
 unsigned long maxlen = blocksize + blocksize/1000 + 12;
#endif
 char *compressed, *uncompressed;
 if((uncompressed=malloc(blocksize))==NULL)
  {
   fprintf(stderr, "*** Can't malloc(%ld).\n",blocksize);
   return NULL;
  }
 if((compressed=malloc(maxlen))==NULL)
  {
   fprintf(stderr, "*** Can't malloc(%ld).\n",blocksize);
   goto free_uncompressed;
  }
 for(i=0,last=0; !last; i++)
  {
   int z_error;
   unsigned long total=0;
#ifdef __BZLOOP
   unsigned int len = maxlen;
#else
  unsigned long len = maxlen;
#endif
   memset(compressed,0,len); memset(uncompressed,0,blocksize);
   while(total<blocksize) /* Read a complete block */
    {
     ssize_t r=read(handle, uncompressed+total, blocksize-total);
     if(r<=0) { last=1; break; }
     total+=r;
    }
   total_uncompressed += total;
   if (total != blocksize)
    {
     last=1;
     fprintf(stderr, "Partial read (%lu bytes of %lu), padding with zeros.\n",
					total, blocksize);
    }
#ifdef __BZLOOP
   if((z_error=BZ2_bzBuffToBuffCompress(compressed, &len, uncompressed, blocksize, 9, 0 /* 0 to 4*/, 0)) != BZ_OK)
#else
   if((z_error=compress2(compressed, &len, uncompressed, blocksize, Z_BEST_COMPRESSION)) != Z_OK)
#endif
    {
#ifdef __BZLOOP
     fprintf(stderr, "*** Error %d compressing block %lu! (compressed=%p, len=%u, uncompressed=%p, blocksize=%lu)\n", z_error, i, compressed,len,uncompressed,blocksize);
#else
     fprintf(stderr, "*** Error %d compressing block %lu! (compressed=%p, len=%lu, uncompressed=%p, blocksize=%lu)\n", z_error, i, compressed,len,uncompressed,blocksize);
#endif
     goto error_free_cb_list;
    }
   if((*cbp = malloc(sizeof(struct cb_list)+len))==NULL) /* get another block */
    {
     fprintf(stderr, "*** Out of memory allocating block ptrs (virtual memory exhausted).\n");
     goto error_free_cb_list;
    }
   total_compressed+=len;
   /* Print status */
#ifdef __BZLOOP
   fprintf(stderr, "Block# %5lu size %6lu -> %6u [compression ratio %3lu%%, overall: %3Lu%%]\n", i, total, len, total>0?((len*100)/total):100,total_uncompressed>0?((total_compressed*100)/total_uncompressed):100);
#else
   fprintf(stderr, "Block# %5lu size %6lu -> %6lu [compression ratio %3lu%%, overall: %3Lu%%]\n", i, total, len, total>0?((len*100)/total):100,total_uncompressed>0?((total_compressed*100)/total_uncompressed):100);
#endif
   (*cbp)->size = len;
   memcpy((*cbp)->data, compressed, len);
   (*cbp)->next=NULL;
   cbp=&((*cbp)->next);
  } /* for */
 goto free_compressed;

 error_free_cb_list:
    if(cbl) { free_cb_list(cbl); cbl=NULL; i=0; }

 free_compressed:
    free(compressed);
 free_uncompressed:
    free(uncompressed);

 *numblocks=i;
 return cbl;
}

int main(int argc, char **argv)
{
 int in;
 unsigned long blocksize;
 struct cloop_head head;
 unsigned long numblocks;
 unsigned long long bytes_so_far;
 unsigned long i;
 struct cb_list *compressed_blocks,*cbp;

 if (argc != 3)
  {
   fprintf(stderr, "Usage: %s filename blocksize(bytes).\n",argv[0]);
   fprintf(stderr, "Use '-' as filename for stdin.\n");
   return 1;
  }

 blocksize = atoi(argv[2]);
 if (blocksize == 0 || blocksize % 512 != 0)
  {
   fprintf(stderr, "*** Blocksize must be a multiple of 512.\n");
   return 1;
  }

 if (blocksize > MAX_KMALLOC_SIZE)
  {
   fprintf(stderr, "WARNING: Blocksize %lu may be too big for a kmalloc() (%lu max).\n",blocksize,MAX_KMALLOC_SIZE);
   sleep(2);
  }

 if (sizeof(CLOOP_PREAMBLE) > CLOOP_HEADROOM)
  {
   fprintf(stderr, "*** Preamble (%u chars) > headroom (%u)\n",
			sizeof(CLOOP_PREAMBLE), CLOOP_HEADROOM);
   return 1;
  }

 in=strcmp(argv[1],"-")==0?dup(fileno(stdin)):open(argv[1], O_RDONLY);

 if (in < 0)
  {
   perror("Opening input");
   return 1;
  }

 compressed_blocks = create_compressed_blocks(in, blocksize, &numblocks);

 close(in);

 memset(head.preamble, 0, sizeof(head.preamble));
 memcpy(head.preamble, CLOOP_PREAMBLE, sizeof(CLOOP_PREAMBLE));
#ifdef __BZLOOP
 head.version = htonl(BZLOOP_VERSION);
#endif
 head.block_size = htonl(blocksize);
 head.num_blocks = htonl(numblocks);

 fprintf(stderr, "Block size %lu, number of blocks %lu.\n",
         blocksize, numblocks);

 bytes_so_far = sizeof(head) + sizeof(loff_t) * (numblocks + 1);

 /* Write out head... */
 write(STDOUT_FILENO, &head, sizeof(head));

 if (!compressed_blocks) return 1;

 /* Write offsets */
 for (i=0,cbp=compressed_blocks; i < numblocks+1; i++)
  {
   loff_t tmp;
   tmp = bytes_so_far;
   write(STDOUT_FILENO, &tmp, sizeof(tmp));
   if(cbp) { bytes_so_far += cbp->size; cbp=cbp->next; }
  }

 /* Now write blocks and free them. */
 for (i = 0, cbp=compressed_blocks; cbp && i < numblocks; i++)
  {
   if (write(STDOUT_FILENO, cbp->data, cbp->size) != cbp->size)
    {
     perror("writing block");
     free_cb_list(compressed_blocks);
     return 1;
    }
   cbp=cbp->next;
   free(compressed_blocks); compressed_blocks=cbp;
  }
 fprintf(stderr,"Done.\n");
 return 0;
}
