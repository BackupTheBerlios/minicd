/*
 * Header file for the cloop/bzloop compressed kernel module
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
 * - Added bzloop support to header
 *
 * $Id: compressed_loop.h,v 1.2 2003/09/25 09:39:11 jaco Exp $
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
