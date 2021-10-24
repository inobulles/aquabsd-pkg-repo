/*
 * Copyright (c) 2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/acl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/extattr.h>
#include <sys/syscall.h>
#include <sys/param.h>
#include <sys/mount.h>

#include "copyfile.h"

/*
 * The state structure keeps track of
 * the source filename, the destination filename, their
 * associated file-descriptors, the stat infomration for the
 * source file, the security information for the source file,
 * the flags passed in for the copy, a pointer to place statistics
 * (not currently implemented), debug flags, and a pointer to callbacks
 * (not currently implemented).
 */
struct _copyfile_state
{
    char *src;
    char *dst;
    int src_fd;
    int dst_fd;
    struct stat sb;
    copyfile_flags_t flags;
    void *stats;
    uint32_t debug;
    void *callbacks;
};

/*
 * Internally, the process is broken into a series of
 * private functions.
 */
static int copyfile_open	(copyfile_state_t);
static int copyfile_close	(copyfile_state_t);
static int copyfile_data	(copyfile_state_t);
static int copyfile_stat	(copyfile_state_t);

static int copyfile_preamble(copyfile_state_t *s, copyfile_flags_t flags);
static int copyfile_internal(copyfile_state_t state, copyfile_flags_t flags);

#define COPYFILE_DEBUG (1<<31)
#define COPYFILE_DEBUG_VAR "COPYFILE_DEBUG"

#ifndef _COPYFILE_TEST
# define copyfile_warn(str, ...) syslog(LOG_WARNING, str ": %m", ## __VA_ARGS__)
# define copyfile_debug(d, str, ...) \
   do { \
    if (s && (d <= s->debug)) {\
	syslog(LOG_DEBUG, "%s:%d:%s() " str "\n", __FILE__, __LINE__ , __FUNCTION__, ## __VA_ARGS__); \
    } \
   } while (0)
#else
#define copyfile_warn(str, ...) \
    fprintf(stderr, "%s:%d:%s() " str ": %s\n", __FILE__, __LINE__ , __FUNCTION__, ## __VA_ARGS__, (errno) ? strerror(errno) : "")
# define copyfile_debug(d, str, ...) \
    do { \
    if (s && (d <= s->debug)) {\
	fprintf(stderr, "%s:%d:%s() " str "\n", __FILE__, __LINE__ , __FUNCTION__, ## __VA_ARGS__); \
    } \
    } while(0)
#endif

/*
 * fcopyfile() is used to copy a source file descriptor to a destination file
 * descriptor.  This allows an application to figure out how it wants to open
 * the files (doing various security checks, perhaps), and then just pass in
 * the file descriptors.
 */
int fcopyfile(int src_fd, int dst_fd, copyfile_state_t state, copyfile_flags_t flags)
{
    int ret = 0;
    copyfile_state_t s = state;
    struct stat dst_sb;

    if (src_fd < 0 || dst_fd < 0)
    {
	errno = EINVAL;
	return -1;
    }

    if (copyfile_preamble(&s, flags) < 0)
	return -1;

	s->src_fd = src_fd;
	s->dst_fd = dst_fd;

    copyfile_debug(2, "set src_fd <- %d", src_fd);
    fstat(s->src_fd, &s->sb);

    /* prevent copying on unsupported types */
    switch (s->sb.st_mode & S_IFMT)
    {
	case S_IFLNK:
	case S_IFDIR:
	case S_IFREG:
	    break;
	default:
	    errno = ENOTSUP;
	    return -1;
    }

    copyfile_debug(2, "set dst_fd <- %d", dst_fd);
    if (s->dst_fd == -2 && dst_fd > -1)
	s->dst_fd = dst_fd;

    (void)fstat(s->dst_fd, &dst_sb);
    (void)fchmod(s->dst_fd, (dst_sb.st_mode & ~S_IFMT) | (S_IRUSR | S_IWUSR));

    ret = copyfile_internal(s, flags);

    if (ret >= 0 && !(s->flags & COPYFILE_STAT))
    {
	(void)fchmod(s->dst_fd, dst_sb.st_mode & ~S_IFMT);
    }

    if (state == NULL)
	copyfile_state_free(s);

    return ret;

}

/*
 * the original copyfile() routine; this copies a source file to a destination
 * file.  Note that because we need to set the names in the state variable, this
 * is not just the same as opening the two files, and then calling fcopyfile().
 * Oh, if only life were that simple!
 */
int copyfile(const char *src, const char *dst, copyfile_state_t state, copyfile_flags_t flags)
{
    int ret = 0;
    copyfile_state_t s = state;
    struct stat sb;

    if (src == NULL && dst == NULL)
    {
	errno = EINVAL;
	return -1;
    }

    if (copyfile_preamble(&s, flags) < 0)
    {
	return -1;
    }

/*
 * This macro is... well, it's not the worst thing you can do with cpp, not
 *  by a long shot.  Essentially, we are setting the filename (src or dst)
 * in the state structure; since the structure may not have been cleared out
 * before being used again, we do some of the cleanup here:  if the given
 * filename (e.g., src) is set, and state->src is not equal to that, then
 * we need to check to see if the file descriptor had been opened, and if so,
 * close it.  After that, we set state->src to be a copy of the given filename,
 * releasing the old copy if necessary.
 */
#define COPYFILE_SET_FNAME(NAME, S) \
  do { \
    if (NAME != NULL) {									\
	if (S->NAME != NULL && strncmp(NAME, S->NAME, MAXPATHLEN)) {			\
	    copyfile_debug(2, "replacing string %s (%s) -> (%s)", #NAME, NAME, S->NAME);\
	    if (S->NAME##_fd != -2 && S->NAME##_fd > -1) {				\
		copyfile_debug(4, "closing %s fd: %d", #NAME, S->NAME##_fd);		\
		close(S->NAME##_fd);							\
		S->NAME##_fd = -2;							\
	    }										\
	}										\
	if (S->NAME) {									\
	    free(S->NAME);								\
	    S->NAME = NULL;								\
	}										\
	if ((S->NAME = strdup(NAME)) == NULL)						\
	    return -1;									\
    }											\
  } while (0)

    COPYFILE_SET_FNAME(src, s);
    COPYFILE_SET_FNAME(dst, s);
	
	if ((ret = copyfile_open(s)) < 0)
	goto error_exit;

    ret = copyfile_internal(s, flags);

exit:
    if (state == NULL)
	copyfile_state_free(s);

    return ret;

error_exit:
    ret = -1;
    goto exit;
}

/*
 * Shared prelude to the {f,}copyfile().  This initializes the
 * state variable, if necessary, and also checks for both debugging
 * and disabling environment variables.
 */
static int copyfile_preamble(copyfile_state_t *state, copyfile_flags_t flags)
{
    copyfile_state_t s;

    if (*state == NULL)
    {
	if ((*state = copyfile_state_alloc()) == NULL)
	    return -1;
    }

    s = *state;

    if (COPYFILE_DEBUG & flags)
    {
	char *e;
	if ((e = getenv(COPYFILE_DEBUG_VAR)))
	{
	    errno = 0;
	    s->debug = (uint32_t)strtol(e, NULL, 0);

	    /* clamp s->debug to 1 if the environment variable is not parsable */
	    if (s->debug == 0 && errno != 0)
		s->debug = 1;
	}
	copyfile_debug(2, "debug value set to: %d", s->debug);
    }

#if 0
    /* Temporarily disabled */
    if (getenv(COPYFILE_DISABLE_VAR) != NULL)
    {
	copyfile_debug(1, "copyfile disabled");
	return 2;
    }
#endif
    copyfile_debug(2, "setting flags: %d", s->flags);
    s->flags = flags;

    return 0;
}

/*
 * The guts of {f,}copyfile().
 * This looks through the flags in a particular order, and calls the
 * associated functions.
 */
static int copyfile_internal(copyfile_state_t s, copyfile_flags_t flags)
{
    int ret = 0;

    if (s->dst_fd < 0 || s->src_fd < 0)
    {
	copyfile_debug(1, "file descriptors not open (src: %d, dst: %d)",
	s->src_fd, s->dst_fd);
	errno = EINVAL;
	return -1;
    }

    /*
     * Similar to above, this tells us whether or not to copy
     * the non-meta data portion of the file.  We attempt to
     * remove (via unlink) the destination file if we fail.
     */
    if (COPYFILE_DATA & flags)
    {
	if ((ret = copyfile_data(s)) < 0)
	{
	    copyfile_warn("error processing data");
	    if (s->dst && unlink(s->dst))
		    copyfile_warn("%s: remove", s->src);
	    goto exit;
	}
    }

    if (COPYFILE_STAT & flags)
    {
	if ((ret = copyfile_stat(s)) < 0)
	{
	    copyfile_warn("error processing POSIX information");
	    goto exit;
	}
    }

exit:
    return ret;

error_exit:
    ret = -1;
    goto exit;
}

/*
 * A publicly-visible routine, copyfile_state_alloc() sets up the state variable.
 */
copyfile_state_t copyfile_state_alloc(void)
{
    copyfile_state_t s = (copyfile_state_t) calloc(1, sizeof(struct _copyfile_state));

    if (s != NULL)
    {
	s->src_fd = -2;
	s->dst_fd = -2;
    } else
	errno = ENOMEM;

    return s;
}

/*
 * copyfile_state_free() returns the memory allocated to the state structure.
 * It also closes the file descriptors, if they've been opened.
 */
int copyfile_state_free(copyfile_state_t s)
{
    if (s != NULL)
    {

	if (copyfile_close(s) < 0)
	{
	    copyfile_warn("error closing files");
	    return -1;
	}
	if (s->dst)
	    free(s->dst);
	if (s->src)
	    free(s->src);
	free(s);
    }
    return 0;
}

/*
 * Should we worry if we can't close the source?  NFS says we
 * should, but it's pretty late for us at this point.
 */
static int copyfile_close(copyfile_state_t s)
{
    if (s->src && s->src_fd >= 0)
	close(s->src_fd);

    if (s->dst && s->dst_fd >= 0) {
	if (close(s->dst_fd))
	    return -1;
    }

    return 0;
}

/*
 * copyfile_open() does what one expects:  it opens up the files
 * given in the state structure, if they're not already open.
 * It also does some type validation, to ensure that we only
 * handle file types we know about.
 */
static int copyfile_open(copyfile_state_t s)
{
    int oflags = O_EXCL | O_CREAT | O_WRONLY;
    int islnk = 0, isdir = 0;
    int osrc = 0, dsrc = 0;

    if (s->src && s->src_fd == -2)
    {
	/* prevent copying on unsupported types */
	switch (s->sb.st_mode & S_IFMT)
	{
	    case S_IFDIR:
		isdir = 1;
		break;
	    case S_IFREG:
		break;
	    default:
		errno = ENOTSUP;
		return -1;
	}
	
	if ((s->src_fd = open(s->src, O_RDONLY | osrc , 0)) < 0)
	{
		copyfile_warn("open on %s", s->src);
		return -1;
	} else
	    copyfile_debug(2, "open successful on source (%s)", s->src);

    }

    if (s->dst && s->dst_fd == -2)
    {
	/*
	 * COPYFILE_UNLINK tells us to try removing the destination
	 * before we create it.  We don't care if the file doesn't
	 * exist, so we ignore ENOENT.
	 */
	if (COPYFILE_UNLINK & s->flags)
	{
	    if (remove(s->dst) < 0 && errno != ENOENT)
	    {
		copyfile_warn("%s: remove", s->dst);
		return -1;
	    }
	}

	if (s->flags & COPYFILE_NOFOLLOW_DST)
		dsrc = O_NOFOLLOW;

	if (isdir) {
		mode_t mode;
		mode = s->sb.st_mode & ~S_IFMT;

		if (mkdir(s->dst, mode) == -1) {
			if (errno != EEXIST || (s->flags & COPYFILE_EXCL)) {
				copyfile_warn("Cannot make directory %s", s->dst);
				return -1;
			}
		}
		s->dst_fd = open(s->dst, O_RDONLY | dsrc);
		if (s->dst_fd == -1) {
			copyfile_warn("Cannot open directory %s for reading", s->dst);
			return -1;
		}
	} else while((s->dst_fd = open(s->dst, oflags | dsrc, s->sb.st_mode | S_IWUSR)) < 0)
	{
	    /*
	     * We set S_IWUSR because fsetxattr does not -- at the time this comment
	     * was written -- allow one to set an extended attribute on a file descriptor
	     * for a read-only file, even if the file descriptor is opened for writing.
	     * This will only matter if the file does not already exist.
	     */
	    switch(errno)
	    {
		case EEXIST:
		    copyfile_debug(3, "open failed, retrying (%s)", s->dst);
		    if (s->flags & COPYFILE_EXCL)
			break;
		    oflags = oflags & ~O_CREAT;
		    continue;
		case EACCES:
		    if(chmod(s->dst, (s->sb.st_mode | S_IWUSR) & ~S_IFMT) == 0)
			continue;
		    else {
			break;
		    }
		case EISDIR:
		    copyfile_debug(3, "open failed because it is a directory (%s)", s->dst);
		    if ((s->flags & COPYFILE_EXCL) ||
			(!isdir && (s->flags & COPYFILE_DATA)))
			break;
		    oflags = (oflags & ~O_WRONLY) | O_RDONLY;
		    continue;
	    }
	    copyfile_warn("open on %s", s->dst);
	    return -1;
	}
	copyfile_debug(2, "open successful on destination (%s)", s->dst);
    }

    if (s->dst_fd < 0 || s->src_fd < 0)
    {
	copyfile_debug(1, "file descriptors not open (src: %d, dst: %d)",
		s->src_fd, s->dst_fd);
	errno = EINVAL;
	return -1;
    }
    return 0;
}

/*
 * Attempt to copy the data section of a file.  Using blockisize
 * is not necessarily the fastest -- it might be desirable to
 * specify a blocksize, somehow.  But it's a size that should be
 * guaranteed to work.
 */
static int copyfile_data(copyfile_state_t s)
{
    size_t blen;
    char *bp = 0;
    ssize_t nread;
    int ret = 0;
    size_t iBlocksize = 0;
    struct statfs sfs;

    if (fstatfs(s->src_fd, &sfs) == -1) {
	iBlocksize = s->sb.st_blksize;
    } else {
	iBlocksize = sfs.f_iosize;
    }

    if ((bp = malloc(iBlocksize)) == NULL)
	return -1;

    blen = iBlocksize;

/* If supported, do preallocation for Xsan / HFS volumes */
#ifdef F_PREALLOCATE
    {
       fstore_t fst;

       fst.fst_flags = 0;
       fst.fst_posmode = F_PEOFPOSMODE;
       fst.fst_offset = 0;
       fst.fst_length = s->sb.st_size;
       /* Ignore errors; this is merely advisory. */
       (void)fcntl(s->dst_fd, F_PREALLOCATE, &fst);
    }
#endif

    while ((nread = read(s->src_fd, bp, blen)) > 0)
    {
	size_t nwritten;
	size_t left = nread;
	void *ptr = bp;

	while (left > 0) {
		int loop = 0;
		nwritten = write(s->dst_fd, ptr, left);
		switch (nwritten) {
		case 0:
			if (++loop > 5) {
				copyfile_warn("writing to output %d times resulted in 0 bytes written", loop);
				ret = -1;
				errno = EAGAIN;
				goto exit;
			}
			break;
		case -1:
			copyfile_warn("writing to output file got error");
			ret = -1;
			goto exit;
		default:
			left -= nwritten;
			ptr = ((char*)ptr) + nwritten;
			break;
		}
	}
    }
    if (nread < 0)
    {
	copyfile_warn("reading from %s", s->src);
	goto exit;
    }

    if (ftruncate(s->dst_fd, s->sb.st_size) < 0)
    {
	ret = -1;
	goto exit;
    }

exit:
    free(bp);
    return ret;
}

/*
 * Attempt to set the destination file's stat information -- including
 * flags and time-related fields -- to the source's.
 */
static int copyfile_stat(copyfile_state_t s)
{
    struct timeval tval[2];
    /*
     * NFS doesn't support chflags; ignore errors unless there's reason
     * to believe we're losing bits.  (Note, this still won't be right
     * if the server supports flags and we were trying to *remove* flags
     * on a file that we copied, i.e., that we didn't create.)
     */
    if (fchflags(s->dst_fd, (u_int)s->sb.st_flags))
	if (errno != EOPNOTSUPP || s->sb.st_flags != 0)
	    copyfile_warn("%s: set flags (was: 0%07o)", s->dst, s->sb.st_flags);

    /* If this fails, we don't care */
    (void)fchown(s->dst_fd, s->sb.st_uid, s->sb.st_gid);

    /* This may have already been done in copyfile_security() */
    (void)fchmod(s->dst_fd, s->sb.st_mode & ~S_IFMT);

    tval[0].tv_sec = s->sb.st_atime;
    tval[1].tv_sec = s->sb.st_mtime;
    tval[0].tv_usec = tval[1].tv_usec = 0;
    if (futimes(s->dst_fd, tval))
	    copyfile_warn("%s: set times", s->dst);
    return 0;
}

/*
 * API interface into getting data from the opaque data type.
 */
int copyfile_state_get(copyfile_state_t s, uint32_t flag, void *ret)
{
    if (ret == NULL)
    {
	errno = EFAULT;
	return -1;
    }

    switch(flag)
    {
	case COPYFILE_STATE_SRC_FD:
	    *(int*)ret = s->src_fd;
	    break;
	case COPYFILE_STATE_DST_FD:
	    *(int*)ret = s->dst_fd;
	    break;
	case COPYFILE_STATE_SRC_FILENAME:
	    *(char**)ret = s->src;
	    break;
	case COPYFILE_STATE_DST_FILENAME:
	    *(char**)ret = s->dst;
	    break;
#if 0
	case COPYFILE_STATE_STATS:
	    ret = s->stats.global;
	    break;
	case COPYFILE_STATE_PROGRESS_CB:
	    ret = s->callbacks.progress;
	    break;
#endif
	default:
	    errno = EINVAL;
	    ret = NULL;
	    return -1;
    }
    return 0;
}

/*
 * Public API for setting state data (remember that the state is
 * an opaque data type).
 */
int copyfile_state_set(copyfile_state_t s, uint32_t flag, const void * thing)
{
#define copyfile_set_string(DST, SRC) \
    do {					\
	if (SRC != NULL) {			\
	    DST = strdup((char *)SRC);		\
	} else {				\
	    if (DST != NULL) {			\
		free(DST);			\
	    }					\
	    DST = NULL;				\
	}					\
    } while (0)

    if (thing == NULL)
    {
	errno = EFAULT;
	return  -1;
    }

    switch(flag)
    {
	case COPYFILE_STATE_SRC_FD:
	    s->src_fd = *(int*)thing;
	    break;
	case COPYFILE_STATE_DST_FD:
	     s->dst_fd = *(int*)thing;
	    break;
	case COPYFILE_STATE_SRC_FILENAME:
	    copyfile_set_string(s->src, thing);
	    break;
	case COPYFILE_STATE_DST_FILENAME:
	    copyfile_set_string(s->dst, thing);
	    break;
#if 0
	case COPYFILE_STATE_STATS:
	     s->stats.global = thing;
	    break;
	case COPYFILE_STATE_PROGRESS_CB:
	     s->callbacks.progress = thing;
	    break;
#endif
	default:
	    errno = EINVAL;
	    return -1;
    }
    return 0;
#undef copyfile_set_string
}


/*
 * Make this a standalone program for testing purposes by
 * defining _COPYFILE_TEST.
 */
#ifdef _COPYFILE_TEST
#define COPYFILE_OPTION(x) { #x, COPYFILE_ ## x },

struct {char *s; int v;} opts[] = {
    COPYFILE_OPTION(ACL)
    COPYFILE_OPTION(STAT)
    COPYFILE_OPTION(XATTR)
    COPYFILE_OPTION(DATA)
    COPYFILE_OPTION(SECURITY)
    COPYFILE_OPTION(METADATA)
    COPYFILE_OPTION(ALL)
    COPYFILE_OPTION(NOFOLLOW_SRC)
    COPYFILE_OPTION(NOFOLLOW_DST)
    COPYFILE_OPTION(NOFOLLOW)
    COPYFILE_OPTION(EXCL)
    COPYFILE_OPTION(MOVE)
    COPYFILE_OPTION(UNLINK)
    COPYFILE_OPTION(CHECK)
    COPYFILE_OPTION(VERBOSE)
    COPYFILE_OPTION(DEBUG)
    {NULL, 0}
};

int main(int c, char *v[])
{
    int i;
    int flags = 0;

    if (c < 3)
	errx(1, "insufficient arguments");

    while(c-- > 3)
    {
	for (i = 0; opts[i].s != NULL; ++i)
	{
	    if (strcasecmp(opts[i].s, v[c]) == 0)
	    {
		printf("option %d: %s <- %d\n", c, opts[i].s, opts[i].v);
		flags |= opts[i].v;
		break;
	    }
	}
    }

    return copyfile(v[1], v[2], NULL, flags);
}
#endif
/*
 * Apple Double Create
 *
 * Create an Apple Double "._" file from a file's extented attributes
 *
 * Copyright (c) 2004 Apple Computer, Inc. All rights reserved.
 */


#define offsetof(type, member)	((size_t)(&((type *)0)->member))

#define	XATTR_MAXATTRLEN   (4*1024)


/*
   Typical "._" AppleDouble Header File layout:
  ------------------------------------------------------------
         MAGIC          0x00051607
         VERSION        0x00020000
         FILLER         0
         COUNT          2
     .-- AD ENTRY[0]    Finder Info Entry (must be first)
  .--+-- AD ENTRY[1]    Resource Fork Entry (must be last)
  |  '-> FINDER INFO
  |      /////////////  Fixed Size Data (32 bytes)
  |      EXT ATTR HDR
  |      /////////////
  |      ATTR ENTRY[0] --.
  |      ATTR ENTRY[1] --+--.
  |      ATTR ENTRY[2] --+--+--.
  |         ...          |  |  |
  |      ATTR ENTRY[N] --+--+--+--.
  |      ATTR DATA 0   <-'  |  |  |
  |      ////////////       |  |  |
  |      ATTR DATA 1   <----'  |  |
  |      /////////////         |  |
  |      ATTR DATA 2   <-------'  |
  |      /////////////            |
  |         ...                   |
  |      ATTR DATA N   <----------'
  |      /////////////
  |                      Attribute Free Space
  |
  '----> RESOURCE FORK
         /////////////   Variable Sized Data
         /////////////
         /////////////
         /////////////
         /////////////
         /////////////
            ...
         /////////////
 
  ------------------------------------------------------------

   NOTE: The EXT ATTR HDR, ATTR ENTRY's and ATTR DATA's are
   stored as part of the Finder Info.  The length in the Finder
   Info AppleDouble entry includes the length of the extended
   attribute header, attribute entries, and attribute data.
*/


/*
 * On Disk Data Structures
 *
 * Note: Motorola 68K alignment and big-endian.
 *
 * See RFC 1740 for additional information about the AppleDouble file format.
 *
 */

#define ADH_MAGIC     0x00051607
#define ADH_VERSION   0x00020000
#define ADH_MACOSX    "Mac OS X        "

/*
 * AppleDouble Entry ID's
 */
#define AD_DATA          1   /* Data fork */
#define AD_RESOURCE      2   /* Resource fork */
#define AD_REALNAME      3   /* File's name on home file system */
#define AD_COMMENT       4   /* Standard Mac comment */
#define AD_ICONBW        5   /* Mac black & white icon */
#define AD_ICONCOLOR     6   /* Mac color icon */
#define AD_UNUSED        7   /* Not used */
#define AD_FILEDATES     8   /* File dates; create, modify, etc */
#define AD_FINDERINFO    9   /* Mac Finder info & extended info */
#define AD_MACINFO      10   /* Mac file info, attributes, etc */
#define AD_PRODOSINFO   11   /* Pro-DOS file info, attrib., etc */
#define AD_MSDOSINFO    12   /* MS-DOS file info, attributes, etc */
#define AD_AFPNAME      13   /* Short name on AFP server */
#define AD_AFPINFO      14   /* AFP file info, attrib., etc */
#define AD_AFPDIRID     15   /* AFP directory ID */ 
#define AD_ATTRIBUTES   AD_FINDERINFO


#define ATTR_FILE_PREFIX   "._"
#define ATTR_HDR_MAGIC     0x41545452   /* 'ATTR' */

#define ATTR_BUF_SIZE      4096        /* default size of the attr file and how much we'll grow by */

/* Implementation Limits */
#define ATTR_MAX_SIZE      (128*1024)  /* 128K maximum attribute data size */
#define ATTR_MAX_NAME_LEN  128
#define ATTR_MAX_HDR_SIZE  (65536+18)

/*
 * Note: ATTR_MAX_HDR_SIZE is the largest attribute header
 * size supported (including the attribute entries). All of
 * the attribute entries must reside within this limit.
 */


#define FINDERINFOSIZE	32

typedef struct apple_double_entry
{
	u_int32_t   type;     /* entry type: see list, 0 invalid */ 
	u_int32_t   offset;   /* entry data offset from the beginning of the file. */
	u_int32_t   length;   /* entry data length in bytes. */
} __attribute__((aligned(2), packed)) apple_double_entry_t;


typedef struct apple_double_header
{
	u_int32_t   magic;         /* == ADH_MAGIC */
	u_int32_t   version;       /* format version: 2 = 0x00020000 */ 
	u_int32_t   filler[4];
	u_int16_t   numEntries;	   /* number of entries which follow */ 
	apple_double_entry_t   entries[2];  /* 'finfo' & 'rsrc' always exist */
	u_int8_t    finfo[FINDERINFOSIZE];  /* Must start with Finder Info (32 bytes) */
	u_int8_t    pad[2];        /* get better alignment inside attr_header */
} __attribute__((aligned(2), packed)) apple_double_header_t;


/* Entries are aligned on 4 byte boundaries */
typedef struct attr_entry
{
	u_int32_t   offset;    /* file offset to data */
	u_int32_t   length;    /* size of attribute data */
	u_int16_t   flags;
	u_int8_t    namelen;   /* length of name including NULL termination char */ 
	u_int8_t    name[1];   /* NULL-terminated UTF-8 name (up to 128 bytes max) */
} __attribute__((aligned(2), packed)) attr_entry_t;



/* Header + entries must fit into 64K */
typedef struct attr_header
{
	apple_double_header_t  appledouble;
	u_int32_t   magic;        /* == ATTR_HDR_MAGIC */
	u_int32_t   debug_tag;    /* for debugging == file id of owning file */
	u_int32_t   total_size;   /* total size of attribute header + entries + data */ 
	u_int32_t   data_start;   /* file offset to attribute data area */
	u_int32_t   data_length;  /* length of attribute data area */
	u_int32_t   reserved[3];
	u_int16_t   flags;
	u_int16_t   num_attrs;
} __attribute__((aligned(2), packed)) attr_header_t;

/* Empty Resource Fork Header */
/* This comes by way of xnu's vfs_xattr.c */
typedef struct rsrcfork_header {
	u_int32_t	fh_DataOffset;
	u_int32_t	fh_MapOffset;
	u_int32_t	fh_DataLength;
	u_int32_t	fh_MapLength;
	u_int8_t	systemData[112];
	u_int8_t	appData[128];
	u_int32_t	mh_DataOffset; 
	u_int32_t	mh_MapOffset;
	u_int32_t	mh_DataLength; 
	u_int32_t	mh_MapLength;
	u_int32_t	mh_Next;
	u_int16_t	mh_RefNum;
	u_int8_t	mh_Attr;
	u_int8_t	mh_InMemoryAttr; 
	u_int16_t	mh_Types;
	u_int16_t	mh_Names;
	u_int16_t	typeCount;
} rsrcfork_header_t;
#define RF_FIRST_RESOURCE    256
#define RF_NULL_MAP_LENGTH    30   
#define RF_EMPTY_TAG  "This resource fork intentionally left blank   "

#define ATTR_ALIGN 3L  /* Use four-byte alignment */

#define ATTR_ENTRY_LENGTH(namelen)  \
        ((sizeof(attr_entry_t) - 1 + (namelen) + ATTR_ALIGN) & (~ATTR_ALIGN))

#define ATTR_NEXT(ae)  \
	 (attr_entry_t *)((u_int8_t *)(ae) + ATTR_ENTRY_LENGTH((ae)->namelen))

#define	XATTR_SECURITY_NAME	  "com.apple.acl.text"
