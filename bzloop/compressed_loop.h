/* 
 * $Id: compressed_loop.h,v 1.1 2003/09/25 09:19:33 jaco Exp $ 
 */

#ifndef _COMPRESSED_LOOP_H
#define _COMPRESSED_LOOP_H

#ifdef __BZLOOP
#  define CLOOP_HEADROOM        16384-12 /* create a lot more space for our script than in cloop */
                                         /* try to keep sizeof(cloop_head) a multiple of 2048    */
#  define BZLOOP_VERSION        0x1000   /* only change if compression details change */
#else
#  define CLOOP_HEADROOM        128
#endif
#define CLOOP_VERSION           "1.02"   /* moved here to be available to all */

struct cloop_head
{
	char preamble[CLOOP_HEADROOM];
#ifdef __BZLOOP
	u_int32_t version;
#endif
	u_int32_t block_size;
	u_int32_t num_blocks;
};

#endif /*_COMPRESSED_LOOP_H*/
