/*
 * Extracts a filesystem back from a compressed fs file
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
 *
 * $Id: extract_compressed_fs.c,v 1.2 2003/09/25 09:39:11 jaco Exp $
 */

#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#ifdef __BZLOOP
#  include <bzlib.h>
#else
#  include <zlib.h>
#endif
#include "compressed_loop.h"

struct compressed_block
{
	size_t size;
	void *data;
};

int main(int argc, char *argv[])
{
	int handle;
	struct cloop_head head;
	unsigned int i;
	unsigned char *buffer, *clear_buffer;

	if (argc != 2) {
		fprintf(stderr, "Need filename\n");
		exit(1);
	}

	handle = open(argv[1], O_RDONLY);
	if (handle < 0) {
		perror("Opening compressed file\n");
		exit(1);
	}

	if (read(handle, &head, sizeof(head)) != sizeof(head)) {
		perror("Reading compressed file header\n");
		exit(1);
	}

#ifdef __BZLOOP
	/* this size is too large, but works in all circumstances, we should
	   really fix it with the real maximum size (also in create_compressed_fs.c) */
	buffer = malloc(ntohl(head.block_size) + ntohl(head.block_size)/100
			+ 12 + 4);
#else
	buffer = malloc(ntohl(head.block_size) + ntohl(head.block_size)/1000
			+ 12 + 4);
#endif
	clear_buffer = malloc(ntohl(head.block_size));
#ifdef __BZLOOP
	fprintf(stderr, "version %u, %u blocks of size %u. Preamble:\n%s\n",
		ntohl(head.version), ntohl(head.num_blocks), ntohl(head.block_size), head.preamble);
#else
	fprintf(stderr, "%u blocks of size %u. Preamble:\n%s\n",
		ntohl(head.num_blocks), ntohl(head.block_size), head.preamble);
#endif

	for (i = 0; i < ntohl(head.num_blocks); i++) {
		int currpos;
#ifdef __BZLOOP
		unsigned int destlen = (unsigned int)ntohl(head.block_size);
#else
		unsigned long destlen = ntohl(head.block_size);
#endif
		loff_t offset[2];
		unsigned int size;

		read(handle, &offset, 2*sizeof(loff_t));
                lseek(handle, -sizeof(loff_t), SEEK_CUR);

		currpos = lseek(handle, 0, SEEK_CUR);
		if (lseek(handle, offset[0], SEEK_SET) < 0) {
			fprintf(stderr, "lseek to %Lu: %s\n",
				offset[0], strerror(errno));
			exit(1);
		}

                size=offset[1]-offset[0];
#ifdef __BZLOOP
		if (size > ntohl(head.block_size) + ntohl(head.block_size)/100
		    + 12 + 4) {
#else
		if (size > ntohl(head.block_size) + ntohl(head.block_size)/1000
		    + 12 + 4) {
#endif
			fprintf(stderr,
				"Size %u for block %u (offset %Lu) too big\n",
				size, i, offset[0]);
			exit(1);
		}
		read(handle, buffer, size);
		if (lseek(handle, currpos, SEEK_SET) < 0) {
			perror("seeking");
			exit(1);
		}

#ifdef __BZLOOP
		fprintf(stderr, "Block %u length %u => %u\n",
			i, size, destlen);
#else
		fprintf(stderr, "Block %u length %u => %lu\n",
			i, size, destlen);
#endif
		if (i == 3) {
			fprintf(stderr,
				"Block head:%02X%02X%02X%02X%02X%02X%02X%02X\n",
				buffer[0],
				buffer[1],
				buffer[2],
				buffer[3],
				buffer[4],
				buffer[5],
				buffer[6],
				buffer[7]);
			fprintf(stderr,
				"Block tail:%02X%02X%02X%02X%02X%02X%02X%02X\n",
				buffer[3063],
				buffer[3064],
				buffer[3065],
				buffer[3066],
				buffer[3067],
				buffer[3068],
				buffer[3069],
				buffer[3070]);
		}
#ifdef __BZLOOP
		switch (BZ2_bzBuffToBuffDecompress(clear_buffer, &destlen, buffer, size, 0, 0 /* 0 -> 4*/)) {
		case BZ_OK:
			break;

		case BZ_SEQUENCE_ERROR:
			fprintf(stderr, "Uncomp: BZ_SEQUENCE_ERROR %u\n", i);
			exit(1);

		case BZ_PARAM_ERROR:
			fprintf(stderr, "Uncomp: BZ_PARAM_ERROR %u\n", i);
			exit(1);

		case BZ_MEM_ERROR:
			fprintf(stderr, "Uncomp: BZ_MEM_ERROR %u\n", i);
			exit(1);

		case BZ_DATA_ERROR:
			fprintf(stderr, "Uncomp: BZ_DATA_ERROR %u\n", i);
			exit(1);

		case BZ_DATA_ERROR_MAGIC:
			fprintf(stderr, "Uncomp: BZ_DATA_ERROR_MAGIC %u\n", i);
			exit(1);

		case BZ_IO_ERROR:
			fprintf(stderr, "Uncomp: BZ_IO_ERROR %u\n", i);
			exit(1);

		case BZ_UNEXPECTED_EOF:
			fprintf(stderr, "Uncomp: BZ_UNEXPECTED_EOF %u\n", i);
			exit(1);

		case BZ_OUTBUFF_FULL:
			fprintf(stderr, "Uncomp: BZ_OUTBUFF_FULL %u\n", i);
			exit(1);

		case BZ_CONFIG_ERROR:
			fprintf(stderr, "Uncomp: BZ_CONFIG_ERROR %u\n", i);
			exit(1);

		default:
			fprintf(stderr, "Uncomp: unknown error %u\n", i);
			exit(1);
		}
#else
		switch (uncompress(clear_buffer, &destlen,
				   buffer, size)) {
		case Z_OK:
			break;

		case Z_MEM_ERROR:
			fprintf(stderr, "Uncomp: oom block %u\n", i);
			exit(1);

		case Z_BUF_ERROR:
			fprintf(stderr, "Uncomp: not enough out room %u\n", i);
			exit(1);

		case Z_DATA_ERROR:
			fprintf(stderr, "Uncomp: input corrupt %u\n", i);
			exit(1);

		default:
			fprintf(stderr, "Uncomp: unknown error %u\n", i);
			exit(1);
		}
#endif
		if (destlen != ntohl(head.block_size)) {
#ifdef __BZLOOP
			fprintf(stderr, "Uncomp: bad len %u (%u not %u)\n", i,
				destlen, ntohl(head.block_size));
#else
			fprintf(stderr, "Uncomp: bad len %u (%lu not %u)\n", i,
				destlen, ntohl(head.block_size));
#endif
			exit(1);
		}
		write(STDOUT_FILENO, clear_buffer, ntohl(head.block_size));
	}
	return 0;
}
