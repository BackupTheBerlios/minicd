#!/usr/bin/make
#
# $Id: Makefile,v 1.1 2003/09/25 09:19:32 jaco Exp $

KERNEL_DIR=/usr/src/linux

CFLAGS:=-Wall -Wstrict-prototypes -Wno-trigraphs -O2 -s -I. -fno-strict-aliasing -fno-common -fomit-frame-pointer -mpreferred-stack-boundary=2 -march=i386 -DBZ_NO_STDIO
BZFLAGS:=$(CFLAGS) -D__BZLOOP
CKERNOPS:=-D__KERNEL__ -DMODULE -fno-builtin -nostdlib -I$(KERNEL_DIR)/include

ifndef APPSONLY
include $(KERNEL_DIR)/.config
endif

ifdef CONFIG_MODVERSIONS
MODVERSIONS:= -DMODVERSIONS -include $(KERNEL_DIR)/include/linux/modversions.h
CKERNOPS += $(MODVERSIONS)
endif

# Check for SMP in config and #define __SMP__ if necessary.
# This is ESSENTIAL when compiling a SMP version of cloop.o
# Otherwise, the module will block the entire block buffer management on read.

ifdef CONFIG_SMP
CKERNOPS += -D__SMP__
endif

KERNOBJ:=compressed_loop.o

BZIP2_DIR  = bzip2-1.0.2
BZIP2_OBJS = $(BZIP2_DIR)/blocksort.o  \
             $(BZIP2_DIR)/huffman.o    \
             $(BZIP2_DIR)/crctable.o   \
             $(BZIP2_DIR)/randtable.o  \
             $(BZIP2_DIR)/compress.o   \
             $(BZIP2_DIR)/decompress.o \
             $(BZIP2_DIR)/bzlib.o

all: all-cloop all-bzloop

all-cloop: cloop.o create_compressed_fs extract_compressed_fs

cloop.o: compressed_loop.o
	$(LD) -r -o $@ $^

create_compressed_fs: create_compressed_fs.o
	$(CC) -o $@ $< -lz

extract_compressed_fs: extract_compressed_fs.o
	$(CC) -o $@ $< -lz

all-bzloop: bzloop.o create_bzfs extract_bzfs

bzloop.o: $(BZIP2_OBJS) bzcompressed_loop.o
	$(LD) -r -o $@ $^

$(BZIP2_DIR)/blocksort.o: $(BZIP2_DIR)/blocksort.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

$(BZIP2_DIR)/huffman.o: $(BZIP2_DIR)/huffman.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

$(BZIP2_DIR)/crctable.o: $(BZIP2_DIR)/crctable.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

$(BZIP2_DIR)/randtable.o: $(BZIP2_DIR)/randtable.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

$(BZIP2_DIR)/compress.o: $(BZIP2_DIR)/compress.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

$(BZIP2_DIR)/decompress.o: $(BZIP2_DIR)/decompress.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

$(BZIP2_DIR)/bzlib.o: $(BZIP2_DIR)/bzlib.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

bzcompressed_loop.o: compressed_loop.c
	$(CC) $(BZFLAGS) $(CKERNOPS) $< -c -o $@

create_bzfs: create_bzfs.o
	$(CC) $(BZCFLAGS) -o $@ $< -lbz2

extract_bzfs: extract_bzfs.o
	$(CC) $(BZFLAGS) -o $@ $< -lbz2

create_bzfs.o: create_compressed_fs.c
	$(CC) $(BZFLAGS) -c -o $@ $<

extract_bzfs.o: extract_compressed_fs.c
	$(CC) $(BZFLAGS) -c -o $@ $<

clean:
	rm -f cloop.o $(KERNOBJ) create_compressed_fs extract_compressed_fs zoom *.o
	rm -f $(BZIP2_OBJS) create_bzfs extract_bzfs

dist: clean
	cd .. ; \
	tar -cf - cloop/{Makefile,*.[ch],CHANGELOG,README} | \
	bzip2 -9 > $(HOME)/redhat/SOURCES/cloop.tar.bz2

$(KERNOBJ): %.o : %.c
	$(CC) $(CFLAGS) $(CKERNOPS) $< -c -o $@

compressed_loop.o create_compressed_fs.o: compressed_loop.h
