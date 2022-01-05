/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1980, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <libufs.h>

/*
 * The following two constants set the default block and fragment sizes.
 * Both constants must be a power of 2 and meet the following constraints:
 *	MINBSIZE <= DESBLKSIZE <= MAXBSIZE
 *	sectorsize <= DESFRAGSIZE <= DESBLKSIZE
 *	DESBLKSIZE / DESFRAGSIZE <= 8
 */
#define	UFS_DFL_FRAGSIZE	4096
#define	UFS_DFL_BLKSIZE	32768

/*
 * Cylinder groups may have up to UFS_MAXBLKSPERCG blocks. The actual
 * number used depends upon how much information can be stored
 * in a cylinder group map which must fit in a single file system
 * block. The default is to use as many as possible blocks per group.
 */
#define	UFS_MAXBLKSPERCG	0x7fffffff	/* desired fs_fpg ("infinity") */

/*
 * UFS_MAXBLKPG determines the maximum number of data blocks which are
 * placed in a single cylinder group. The default is one indirect
 * block worth of data blocks.
 */
#define UFS_MAXBLKPG(bsize)	((bsize) / sizeof(ufs2_daddr_t))

/*
 * Each file system has a number of inodes statically allocated.
 * We allocate one inode slot per NFPI fragments, expecting this
 * to be far more than we will ever need.
 */
#define	UFS_NFPI		2

/*
 * variables set up by front end.
 */
extern int	ufs_Eflag;		/* Erase previous disk contents */
extern int	ufs_Lflag;		/* add a volume label */
extern int	ufs_Nflag;		/* run mkfs_ufs without writing file system */
extern int	ufs_Oflag;		/* build UFS1 format file system */
extern int	ufs_Rflag;		/* regression test */
extern int	ufs_Uflag;		/* enable soft updates for file system */
extern int	ufs_jflag;		/* enable soft updates journaling for filesys */
extern int	ufs_Xflag;		/* exit in middle of newfs for testing */
extern int	ufs_Jflag;		/* enable gjournal for file system */
extern int	ufs_lflag;		/* enable multilabel MAC for file system */
extern int	ufs_nflag;		/* do not create .snap directory */
extern int	ufs_tflag;		/* enable TRIM */
extern intmax_t	ufs_fssize;		/* file system size */
extern off_t	ufs_mediasize;	/* device size */
extern int	ufs_sectorsize;	/* bytes/sector */
extern int	ufs_realsectorsize;	/* bytes/sector in hardware*/
extern int	ufs_fsize;		/* fragment size */
extern int	ufs_bsize;		/* block size */
extern int	ufs_maxbsize;	/* maximum clustering */
extern int	ufs_maxblkspercg;	/* maximum blocks per cylinder group */
extern int	ufs_minfree;	/* free space threshold */
extern int	ufs_metaspace;	/* space held for metadata blocks */
extern int	ufs_opt;		/* optimization preference (space or time) */
extern int	ufs_density;	/* number of bytes per inode */
extern int	ufs_maxcontig;	/* max contiguous blocks to allocate */
extern int	ufs_maxbpg;		/* maximum blocks per file in a cyl group */
extern int	ufs_avgfilesize;	/* expected average file size */
extern int	ufs_avgfilesperdir;	/* expected number of files per directory */
extern u_char	*ufs_volumelabel;	/* volume label for filesystem */
extern struct uufsd ufs_disk;	/* libufs disk structure */

/*
 * To override a limitation in libufs, export the offset (in sectors) of the
 * partition on the underlying media (file or disk). The value is used as
 * an offset for all accesses to the media through bread(), which is only
 * invoked directly in this program.
 * For bwrite() we need a different approach, namely override the library
 * version with one defined here. This is because bwrite() is called also
 * by the library function sbwrite() which we cannot intercept nor want to
 * rewrite. As a consequence, the internal version of bwrite() adds the
 * partition offset itself when calling the underlying function, pwrite().
 *
 * XXX This info really ought to go into the struct uufsd, at which point
 * we can remove the above hack.
 */
extern ufs2_daddr_t ufs_part_ofs;	/* partition offset in blocks */

void mkfs_ufs (struct partition *, char *);
