/** \ingroup payload rpmio
 * \file lib/cpio.c
 *  Handle cpio payloads within rpm packages.
 *
 * \warning FIXME: We don't translate between cpio and system mode bits! These
 * should both be the same, but really odd things are going to happen if
 * that's not true!
 */

#include "system.h"
#include <rpmlib.h>

#include "rollback.h"
#include "rpmerr.h"
#include "debug.h"

/*@access FD_t@*/

#define CPIO_NEWC_MAGIC	"070701"
#define CPIO_CRC_MAGIC	"070702"
#define TRAILER		"TRAILER!!!"

/** \ingroup payload
 * Defines a single file to be included in a cpio payload.
 */
struct cpioFileMapping {
/*@dependent@*/ const char * archivePath; /*!< Path to store in cpio archive. */
/*@dependent@*/ const char * dirName;	/*!< Payload file directory. */
/*@dependent@*/ const char * baseName;	/*!< Payload file base name. */
/*@dependent@*/ const char * md5sum;	/*!< File MD5 sum (NULL disables). */
    mode_t finalMode;		/*!< Mode of payload file (from header). */
    uid_t finalUid;		/*!< Uid of payload file (from header). */
    gid_t finalGid;		/*!< Gid of payload file (from header). */
    cpioMapFlags mapFlags;
};

/** \ingroup payload
 * Keeps track of set of all hard linked files in archive.
 */
struct hardLink {
    struct hardLink * next;
    const char ** files;	/* nlink of these, used by install */
    const void ** fileMaps;
    dev_t dev;
    ino_t inode;
    int nlink;
    int linksLeft;
    int createdPath;
    const struct stat sb;
};

/** \ingroup payload
 */
enum hardLinkType {
	HARDLINK_INSTALL=1,
	HARDLINK_BUILD
};

/** \ingroup payload
 * Cpio archive header information.
 * @todo Add support for tar (soon) and ar (eventually) archive formats.
 */
struct cpioCrcPhysicalHeader {
    char magic[6];
    char inode[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devMajor[8];
    char devMinor[8];
    char rdevMajor[8];
    char rdevMinor[8];
    char namesize[8];
    char checksum[8];			/* ignored !! */
};

#define	PHYS_HDR_SIZE	110		/*!< Don't depend on sizeof(struct) */

/** \ingroup payload
 * File name and stat information.
 */
struct cpioHeader {
/*@owned@*/ const char * path;
    FD_t cfd;
    void * mapi;
    struct stat sb;
};

#if 0
static void prtli(const char *msg, struct hardLink * li)
{
    if (msg) fprintf(stderr, "%s", msg);
    fprintf(stderr, " next %p files %p fileMaps %p dev %x ino %x nlink %d left %d createdPath %d size %d\n", li->next, li->files, li->fileMaps, (unsigned)li->dev, (unsigned)li->inode, li->nlink, li->linksLeft, li->createdPath, li->sb.st_size);
}
#endif

/**
 */
static int mapFlags(const void * this, cpioMapFlags mask) {
    const struct cpioFileMapping * map = this;
    return (map->mapFlags & mask);
}

/**
 */
static /*@only@*/ const char * mapArchivePath(const void * this) {
    const struct cpioFileMapping * map = this;
    return xstrdup(map->archivePath);
}

/**
 */
static /*@only@*/ const char * mapFsPath(const void * this) {
    const struct cpioFileMapping * map = this;
    char * t = xmalloc(	strlen(map->dirName) + strlen(map->baseName) + 1);
    (void) stpcpy( stpcpy(t, map->dirName), map->baseName);
    return t;
}

/**
 */
static mode_t mapFinalMode(const void * this) {
    const struct cpioFileMapping * map = this;
    return map->finalMode;
}

/**
 */
static uid_t mapFinalUid(const void * this) {
    const struct cpioFileMapping * map = this;
    return map->finalUid;
}

/**
 */
static gid_t mapFinalGid(const void * this) {
    const struct cpioFileMapping * map = this;
    return map->finalGid;
}

/**
 */
static const char * mapMd5sum(const void * this) {
    const struct cpioFileMapping * map = this;
    return map->md5sum;
}

/**
 */
struct mapi {
    rpmTransactionSet ts;
    TFI_t fi;
    int i;
    struct cpioFileMapping map;
};

/**
 */
static const void * mapLink(const void * this) {
    const struct cpioFileMapping * omap = this;
    struct cpioFileMapping * nmap = xcalloc(sizeof(*nmap), 1);
    *nmap = *omap;	/* structure assignment */
    return nmap;
}

/**
 */
static void mapFree(const void * this) {
    free((void *)this);
}

/**
 */
static void mapFreeIterator(/*@only@*/ const void * this)
{
    struct mapi * mapi;
    rpmTransactionSet ts;
    TFI_t fi;

    if (this == NULL)
	return;

    mapi = (void *)this;
    ts = mapi->ts;
    fi = mapi->fi;

    if (ts && ts->notify) {
	unsigned int archiveSize = (fi->archiveSize ? fi->archiveSize : 100);
	(void)ts->notify(fi->h, RPMCALLBACK_INST_PROGRESS,
			archiveSize, archiveSize,
			(fi->ap ? fi->ap->key : NULL), ts->notifyData);
    }
    free((void *)this);
}

/**
 */
static void * mapInitIterator(const void * this, const void * that)
{
    struct mapi * mapi;
    rpmTransactionSet ts = (void *)this;
    TFI_t fi = (void *)that;

    if (fi == NULL)
	return NULL;
    mapi = xcalloc(sizeof(*mapi), 1);
    mapi->ts = ts;
    mapi->fi = fi;
    mapi->i = 0;

    if (ts && ts->notify) {
	(void)ts->notify(fi->h, RPMCALLBACK_INST_START, 0, fi->archiveSize,
		(fi->ap ? fi->ap->key : NULL), ts->notifyData);
    }

    return mapi;
}

/**
 */
static const void * mapNextIterator(void * this) {
    struct mapi * mapi = this;
    TFI_t fi = mapi->fi;
    struct cpioFileMapping * map = &mapi->map;
    int i = mapi->i;

    do {
	if (!((i = mapi->i++) < fi->fc))
	    return NULL;
    } while (fi->actions && XFA_SKIPPING(fi->actions[i]));

    /* src rpms have simple base name in payload. */
    map->archivePath = (fi->apath ? fi->apath[i] + fi->striplen : fi->bnl[i]);
    map->dirName = fi->dnl[fi->dil[i]];
    map->baseName = fi->bnl[i];
    map->md5sum = (fi->fmd5s ? fi->fmd5s[i] : NULL);
    map->finalMode = fi->fmodes[i];
    map->finalUid = (fi->fuids ? fi->fuids[i] : fi->uid); /* XXX chmod u-s */
    map->finalGid = (fi->fgids ? fi->fgids[i] : fi->gid); /* XXX chmod g-s */
    map->mapFlags = (fi->fmapflags ? fi->fmapflags[i] : fi->mapflags);
    return map;
}

/**
 */
static int cpioStrCmp(const void * a, const void * b) {
    const char * afn = *(const char **)a;
    const char * bfn = *(const char **)b;

    /* Match rpm-4.0 payloads with ./ prefixes. */
    if (afn[0] == '.' && afn[1] == '/')	afn += 2;
    if (bfn[0] == '.' && bfn[1] == '/')	bfn += 2;

    /* If either path is absolute, make it relative. */
    if (afn[0] == '/')	afn += 1;
    if (bfn[0] == '/')	bfn += 1;

    return strcmp(afn, bfn);
}

/**
 */
static const void * mapFind(void * this, const char * hdrPath) {
    struct mapi * mapi = this;
    const TFI_t fi = mapi->fi;
    const char ** p;

    p = bsearch(&hdrPath, fi->apath, fi->fc, sizeof(hdrPath), cpioStrCmp);
    if (p == NULL) {
	fprintf(stderr, "*** not mapped %s\n", hdrPath);
	return NULL;
    }
    mapi->i = p - fi->apath;
    return mapNextIterator(this);
}

/**
 */
static void hdrCallback(const struct cpioHeader * hdr)
{
    struct mapi * mapi = hdr->mapi;
    rpmTransactionSet ts;
    TFI_t fi;

    if (mapi == NULL)
	return;
    ts = mapi->ts;
    fi = mapi->fi;

    if (ts && ts->notify)
        (void)ts->notify(fi->h, RPMCALLBACK_INST_PROGRESS,
			fdGetCpioPos(hdr->cfd), fi->archiveSize,
		(fi->ap ? fi->ap->key : NULL), ts->notifyData);
}

/**
 * Read data from payload.
 * @param cfd		payload file handle
 * @retval vbuf		data from read
 * @param amount	no. bytes to read
 * @return		no. bytes read
 */
static inline off_t saferead(FD_t cfd, /*@out@*/void * vbuf, size_t amount)
	/*@modifies cfd, *vbuf @*/
{
    off_t rc = 0;
    char * buf = vbuf;

    while (amount > 0) {
	size_t nb;

	nb = Fread(buf, sizeof(buf[0]), amount, cfd);
	if (nb <= 0)
		return nb;
	rc += nb;
	if (rc >= amount)
		break;
	buf += nb;
	amount -= nb;
    }
    return rc;
}

/**
 * Read data from payload and update number of bytes read.
 * @param cfd		payload file handle
 * @retval buf		data from read
 * @param size		no. bytes to read
 * @return		no. bytes read
 */
static inline off_t ourread(FD_t cfd, /*@out@*/void * buf, size_t size)
	/*@modifies cfd, *buf @*/
{
    off_t i = saferead(cfd, buf, size);
    if (i > 0)
	fdSetCpioPos(cfd, fdGetCpioPos(cfd) + i);
    return i;
}

/**
 * Align input payload handle, skipping input bytes.
 * @param cfd		payload file handle
 * @param modulo	data alignment
 */
static inline void padinfd(FD_t cfd, int modulo)
	/*@modifies cfd @*/
{
    int buf[10];
    int amount;

    amount = (modulo - fdGetCpioPos(cfd) % modulo) % modulo;
    (void)ourread(cfd, buf, amount);
}

/**
 * Write data to payload.
 * @param cfd		payload file handle
 * @param vbuf		data to write
 * @param amount	no. bytes to write
 * @return		no. bytes written
 */
static inline off_t safewrite(FD_t cfd, const void * vbuf, size_t amount)
	/*@modifies cfd @*/
{
    off_t rc = 0;
    const char * buf = vbuf;

    while (amount > 0) {
	size_t nb;

	nb = Fwrite(buf, sizeof(buf[0]), amount, cfd);
	if (nb <= 0)
		return nb;
	rc += nb;
	if (rc >= amount)
		break;
	buf += nb;
	amount -= nb;
    }

    return rc;
}

/**
 * Align output payload handle, padding with zeroes.
 * @param cfd		payload file handle
 * @param modulo	data alignment
 * @return		0 on success, CPIOERR_WRITE_FAILED
 */
static inline int padoutfd(FD_t cfd, size_t * where, int modulo)
	/*@modifies cfd, *where @*/
{
    static int buf[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int amount;

    amount = (modulo - *where % modulo) % modulo;
    *where += amount;

    if (safewrite(cfd, buf, amount) != amount)
	return CPIOERR_WRITE_FAILED;
    return 0;
}

/**
 * Convert string to unsigned integer (with buffer size check).
 * @param		input string
 * @retval		address of 1st character not processed
 * @param base		numerical conversion base
 * @param num		max no. of bytes to read
 * @return		converted integer
 */
static int strntoul(const char *str, /*@out@*/char **endptr, int base, int num)
	/*@modifies *endptr @*/
{
    char * buf, * end;
    unsigned long ret;

    buf = alloca(num + 1);
    strncpy(buf, str, num);
    buf[num] = '\0';

    ret = strtoul(buf, &end, base);
    if (*end)
	*endptr = ((char *)str) + (end - buf);	/* XXX discards const */
    else
	*endptr = ((char *)str) + strlen(str);

    return ret;
}

#define GET_NUM_FIELD(phys, log) \
	log = strntoul(phys, &end, 16, sizeof(phys)); \
	if (*end) return CPIOERR_BAD_HEADER;
#define SET_NUM_FIELD(phys, val, space) \
	sprintf(space, "%8.8lx", (unsigned long) (val)); \
	memcpy(phys, space, 8);

/**
 * Process next cpio heasder.
 * @retval hdr		file name and stat info
 * @return		0 on success
 */
static int getNextHeader(struct cpioHeader * hdr)
	/*@modifies hdr->cfd, hdr->path, hdr->sb  @*/
{
    struct cpioCrcPhysicalHeader physHeader;
    struct stat * st = &hdr->sb;
    int nameSize;
    char * end;
    int major, minor;

    if (ourread(hdr->cfd, &physHeader, PHYS_HDR_SIZE) != PHYS_HDR_SIZE)
	return CPIOERR_READ_FAILED;

    if (strncmp(CPIO_CRC_MAGIC, physHeader.magic, sizeof(CPIO_CRC_MAGIC)-1) &&
	strncmp(CPIO_NEWC_MAGIC, physHeader.magic, sizeof(CPIO_NEWC_MAGIC)-1))
	return CPIOERR_BAD_MAGIC;

    GET_NUM_FIELD(physHeader.inode, st->st_ino);
    GET_NUM_FIELD(physHeader.mode, st->st_mode);
    GET_NUM_FIELD(physHeader.uid, st->st_uid);
    GET_NUM_FIELD(physHeader.gid, st->st_gid);
    GET_NUM_FIELD(physHeader.nlink, st->st_nlink);
    GET_NUM_FIELD(physHeader.mtime, st->st_mtime);
    GET_NUM_FIELD(physHeader.filesize, st->st_size);

    GET_NUM_FIELD(physHeader.devMajor, major);
    GET_NUM_FIELD(physHeader.devMinor, minor);
    st->st_dev = /*@-shiftsigned@*/ makedev(major, minor) /*@=shiftsigned@*/ ;

    GET_NUM_FIELD(physHeader.rdevMajor, major);
    GET_NUM_FIELD(physHeader.rdevMinor, minor);
    st->st_rdev = /*@-shiftsigned@*/ makedev(major, minor) /*@=shiftsigned@*/ ;

    GET_NUM_FIELD(physHeader.namesize, nameSize);

    {	char * t = xmalloc(nameSize + 1);
	if (ourread(hdr->cfd, t, nameSize) != nameSize) {
	    free(t);
	    hdr->path = NULL;
	    return CPIOERR_BAD_HEADER;
	}
	hdr->path = t;
    }

    /* this is unecessary hdr->path[nameSize] = '\0'; */

    padinfd(hdr->cfd, 4);

    return 0;
}

/* This could trash files in the path! I'm not sure that's a good thing */
/**
 * @param path		directory path
 * @param perms		directory permissions
 * @return		0 on success
 */
static int createDirectory(const char * path, mode_t perms)
	/*@modifies fileSystem @*/
{
    struct stat st;

    if (!lstat(path, &st)) {
	if (S_ISDIR(st.st_mode))
	    return 0;
	if (S_ISLNK(st.st_mode)) {
	    if (stat(path, &st)) {
		if (errno != ENOENT)
		    return CPIOERR_STAT_FAILED;
	    } else {
		if (S_ISDIR(st.st_mode))
		    return 0;
	    }
	}

	if (unlink(path))
	    return CPIOERR_UNLINK_FAILED;
    }

    if (mkdir(path, 000))
	return CPIOERR_MKDIR_FAILED;

    if (perms != 000 && chmod(path, perms))
	return CPIOERR_CHMOD_FAILED;

    return 0;
}

/**
 * Set owner, group, and modify/access times.
 * @param hdr		file name and stat info
 * @return		0 on success
 */
static int setInfo(const struct cpioHeader * hdr)
	/*@modifies fileSystem @*/
{
    int rc = 0;
    struct utimbuf stamp;
    const struct stat * st = &hdr->sb;

    stamp.actime = st->st_mtime;
    stamp.modtime = st->st_mtime;

    if (!S_ISLNK(st->st_mode)) {
	if (!getuid() && chown(hdr->path, st->st_uid, st->st_gid))
	    rc = CPIOERR_CHOWN_FAILED;
	if (!rc && chmod(hdr->path, st->st_mode & 07777))
	    rc = CPIOERR_CHMOD_FAILED;
	if (!rc && utime(hdr->path, &stamp))
	    rc = CPIOERR_UTIME_FAILED;
    } else {
#       if ! CHOWN_FOLLOWS_SYMLINK
	    if (!getuid() && !rc && lchown(hdr->path, st->st_uid, st->st_gid))
		rc = CPIOERR_CHOWN_FAILED;
#       endif
    }

    return rc;
}

/**
 * Create directories in file path (like "mkdir -p").
 * @param filename	file path
 * @return		0 on success
 */
static int inline checkDirectory(const char * filename)	/*@*/
{
    /*@only@*/ static char * lastDir = NULL;	/* XXX memory leak */
    static int lastDirLength = 0;
    static int lastDirAlloced = 0;
    int length = strlen(filename);
    char * buf;
    char * chptr;
    int rc = 0;

    buf = alloca(length + 1);
    strcpy(buf, filename);

    for (chptr = buf + length - 1; chptr > buf; chptr--) {
	if (*chptr == '/') break;
    }

    if (chptr == buf) return 0;     /* /filename - no directories */

    *chptr = '\0';                  /* buffer is now just directories */

    length = strlen(buf);
    if (lastDirLength == length && !strcmp(buf, lastDir)) return 0;

    if (lastDirAlloced < (length + 1)) {
	lastDirAlloced = length + 100;
	lastDir = xrealloc(lastDir, lastDirAlloced);	/* XXX memory leak */
    }

    strcpy(lastDir, buf);
    lastDirLength = length;

    for (chptr = buf + 1; *chptr; chptr++) {
	if (*chptr == '/') {
	    *chptr = '\0';
	    rc = createDirectory(buf, 0755);
	    *chptr = '/';
	    if (rc) return rc;
	}
    }
    rc = createDirectory(buf, 0755);

    return rc;
}

/**
 * Create file from payload stream.
 * @todo Legacy: support brokenEndian MD5 checks?
 * @param cfd		payload file handle
 * @param hdr		file name and stat info
 * @param filemd5	file md5 sum
 * @return		0 on success
 */
static int expandRegular(const struct cpioHeader * hdr, const char * filemd5)
		/*@modifies fileSystem, hdr->cfd @*/
{
    FD_t ofd;
    char buf[BUFSIZ];
    int bytesRead;
    const struct stat * st = &hdr->sb;
    int left = st->st_size;
    int rc = 0;
    struct stat sb;

    /* Rename the old file before attempting unlink to avoid EBUSY errors */
    if (!lstat(hdr->path, &sb)) {
	strcpy(buf, hdr->path);
	strcat(buf, "-RPMDELETE");
	if (rename(hdr->path, buf)) {
	    rpmError(RPMERR_RENAME, _("can't rename %s to %s: %s\n"),
		hdr->path, buf, strerror(errno));
            return CPIOERR_UNLINK_FAILED;
	}

	if (unlink(buf)) {
	    rpmError(RPMERR_UNLINK, _("can't unlink %s: %s\n"),
		buf, strerror(errno));
#if 0
	    return CPIOERR_UNLINK_FAILED;
#endif
	}
    }

    ofd = Fopen(hdr->path, "w.ufdio");
    if (ofd == NULL || Ferror(ofd))
	return CPIOERR_OPEN_FAILED;

    /* XXX This doesn't support brokenEndian checks. */
    if (filemd5)
	fdInitMD5(ofd, 0);

    while (left) {
	bytesRead = ourread(hdr->cfd, buf, left < sizeof(buf) ? left : sizeof(buf));
	if (bytesRead <= 0) {
	    rc = CPIOERR_READ_FAILED;
	    break;
	}

	if (Fwrite(buf, sizeof(buf[0]), bytesRead, ofd) != bytesRead) {
	    rc = CPIOERR_COPY_FAILED;
	    break;
	}

	left -= bytesRead;

	/* don't call this with fileSize == fileComplete */
	if (!rc && left)
	    hdrCallback(hdr);
    }

    if (filemd5) {
	const char * md5sum = NULL;

	Fflush(ofd);
	fdFiniMD5(ofd, (void **)&md5sum, NULL, 1);

	if (md5sum == NULL) {
	    rc = CPIOERR_MD5SUM_MISMATCH;
	} else {
	    if (strcmp(md5sum, filemd5))
		rc = CPIOERR_MD5SUM_MISMATCH;
	    free((void *)md5sum);
	}
    }

    Fclose(ofd);

    return rc;
}

/**
 * Create symlink from payload stream.
 * @param hdr		file name and stat info
 * @return		0 on success
 */
static int expandSymlink(const struct cpioHeader * hdr)
		/*@modifies fileSystem, hdr->cfd @*/
{
    char buf[2048], buf2[2048];
    struct stat sb;
    const struct stat * st = &hdr->sb;
    int len;

    if ((st->st_size + 1)> sizeof(buf))
	return CPIOERR_HDR_SIZE;

    if (ourread(hdr->cfd, buf, st->st_size) != st->st_size)
	return CPIOERR_READ_FAILED;

    buf[st->st_size] = '\0';

    if (!lstat(hdr->path, &sb)) {
	if (S_ISLNK(sb.st_mode)) {
	    len = readlink(hdr->path, buf2, sizeof(buf2) - 1);
	    if (len > 0) {
		buf2[len] = '\0';
		if (!strcmp(buf, buf2)) return 0;
	    }
	}

	if (unlink(hdr->path))
	    return CPIOERR_UNLINK_FAILED;
    }

    if (symlink(buf, hdr->path) < 0)
	return CPIOERR_SYMLINK_FAILED;

    return 0;
}

/**
 * Create fifo from payload stream.
 * @param cfd		payload file handle
 * @param hdr		file name and stat info
 * @return		0 on success
 */
static int expandFifo(const struct cpioHeader * hdr)
		/*@modifies fileSystem @*/
{
    struct stat sb;

    if (!lstat(hdr->path, &sb)) {
	if (S_ISFIFO(sb.st_mode)) return 0;

	if (unlink(hdr->path))
	    return CPIOERR_UNLINK_FAILED;
    }

    if (mkfifo(hdr->path, 0))
	return CPIOERR_MKFIFO_FAILED;

    return 0;
}

/**
 * Create fifo from payload stream.
 * @param cfd		payload file handle
 * @param hdr		file name and stat info
 * @return		0 on success
 */
static int expandDevice(const struct cpioHeader * hdr)
		/*@modifies fileSystem @*/
{
    const struct stat * st = &hdr->sb;
    struct stat sb;

    if (!lstat(hdr->path, &sb)) {
	if ((S_ISCHR(sb.st_mode) || S_ISBLK(sb.st_mode)) &&
		(sb.st_rdev == st->st_rdev))
	    return 0;
	if (unlink(hdr->path))
	    return CPIOERR_UNLINK_FAILED;
    }

    if ( /*@-unrecog@*/ mknod(hdr->path, st->st_mode & (~0777), st->st_rdev) /*@=unrecog@*/ )
	return CPIOERR_MKNOD_FAILED;

    return 0;
}

/**
 * Create and initialize set of hard links.
 * @param st		link stat info
 * @param hltype	type of hard link set to create
 * @return		pointer to set of hard links
 */
static /*@only@*/ struct hardLink * newHardLink(const struct stat * st,
				enum hardLinkType hltype)	/*@*/
{
    struct hardLink * li = xmalloc(sizeof(*li));

    li->next = NULL;
    li->nlink = st->st_nlink;
    li->dev = st->st_dev;
    li->inode = st->st_ino;
    li->createdPath = -1;

    switch (hltype) {
    case HARDLINK_INSTALL:
	li->linksLeft = st->st_nlink;
	li->fileMaps = xmalloc(sizeof(li->fileMaps[0]) * st->st_nlink);
	li->files = NULL;
	break;
    case HARDLINK_BUILD:
	li->linksLeft = 0;
	li->fileMaps = NULL;
	li->files = xcalloc(st->st_nlink, sizeof(*li->files));
	break;
    }

    {	struct stat * myst = (struct stat *) &li->sb;
	*myst = *st;	/* structure assignment */
    }

    return li;
}

/**
 * Destroy set of hard links.
 * @param li		set of hard links
 */
static void freeHardLink( /*@only@*/ struct hardLink * li)
{

    if (li->files) {
	int i;
	for (i = 0; i < li->nlink; i++) {
	    if (li->files[i] == NULL) continue;
	    /*@-unqualifiedtrans@*/ free((void *)li->files[i]) /*@=unqualifiedtrans@*/ ;
	    li->files[i] = NULL;
	}
	free(li->files);
	li->files = NULL;
    }
    if (li->fileMaps) {
	free(li->fileMaps);
	li->fileMaps = NULL;
    }
    free(li);
}

/**
 * Create hard links to existing file.
 * @param li		set of hard links
 * @retval failedFile	on error, file name that failed
 * @return		0 on success
 */
static int createLinks(struct hardLink * li, /*@out@*/ const char ** failedFile)
	/*@modifies fileSystem, *failedFile, li->files, li->linksLeft @*/
{
    int i;
    struct stat sb;

    for (i = 0; i < li->nlink; i++) {
	if (i == li->createdPath) continue;
	if (li->files[i] == NULL) continue;

	if (!lstat(li->files[i], &sb)) {
	    if (unlink(li->files[i])) {
		if (failedFile)
		    *failedFile = xstrdup(li->files[i]);
		return CPIOERR_UNLINK_FAILED;
	    }
	}

	if (link(li->files[li->createdPath], li->files[i])) {
	    if (failedFile)
		*failedFile = xstrdup(li->files[i]);
	    return CPIOERR_LINK_FAILED;
	}

	/*@-unqualifiedtrans@*/ free((void *)li->files[i]) /*@=unqualifiedtrans@*/ ;
	li->files[i] = NULL;
	li->linksLeft--;
    }

    return 0;
}

/**
 * Skip amount bytes on input payload stream.
 * @param cfd		payload file handle
 * @param amount	no. bytes to skip
 * @return		0 on success
 */
static int eatBytes(FD_t cfd, int amount)
	/*@modifies cfd @*/
{
    char buf[4096];
    int bite;

    while (amount) {
	bite = (amount > sizeof(buf)) ? sizeof(buf) : amount;
	if (ourread(cfd, buf, bite) != bite)
	    return CPIOERR_READ_FAILED;
	amount -= bite;
    }

    return 0;
}

/** @todo Verify payload MD5 sum. */
int cpioInstallArchive(const rpmTransactionSet ts, const TFI_t fi, FD_t cfd,
		const char ** failedFile)
{
    struct cpioHeader ch, *hdr = &ch;
    const void * map = NULL;
    struct hardLink * links = NULL;
    struct hardLink * li = NULL;
    int rc = 0;

#ifdef	NOTYET
    char * md5sum = NULL;

    fdInitMD5(cfd, 0);
#endif

    memset(hdr, 0, sizeof(*hdr));
    hdr->path = NULL;
    hdr->mapi = mapInitIterator(ts, fi);
    hdr->cfd = fdLink(cfd, "persist (cpioInstallArchive");
    fdSetCpioPos(hdr->cfd, 0);
    if (failedFile)
	*failedFile = NULL;

    do {
	struct stat * st;

	if (hdr->path) {
	    free((void *)hdr->path);
	    hdr->path = NULL;
	}
	if ((rc = getNextHeader(hdr))) {
#if 0	/* XXX this is the failure point for an unreadable rpm */
	    rpmError(RPMERR_BADPACKAGE, _("getNextHeader: %s\n"),
			cpioStrerror(rc));
#endif
	    goto exit;
	}
	st = &hdr->sb;

	if (!strcmp(hdr->path, TRAILER))
	    break;

	if (hdr->mapi)
	    map = mapFind(hdr->mapi, hdr->path);

	if (hdr->mapi && map == NULL) {
	    eatBytes(hdr->cfd, st->st_size);
	} else {
	    if (map) {
		if (mapFlags(map, CPIO_MAP_PATH)) {
		    if (hdr->path) free((void *)hdr->path);
		    hdr->path = mapFsPath(map);
		}

		if (mapFlags(map, CPIO_MAP_MODE))
		    st->st_mode = mapFinalMode(map);
		if (mapFlags(map,  CPIO_MAP_UID))
		    st->st_uid = mapFinalUid(map);
		if (mapFlags(map, CPIO_MAP_GID))
		    st->st_gid = mapFinalGid(map);
	    }

	    /*
	     * This won't get hard linked symlinks right, but I can't seem
	     * to create those anyway.
	     */
	    if (S_ISREG(st->st_mode) && st->st_nlink > 1) {
		for (li = links; li; li = li->next) {
		    if (li->inode == st->st_ino && li->dev == st->st_dev) break;
		}

		if (li == NULL) {
		    li = newHardLink(st, HARDLINK_BUILD);
		    li->next = links;
		    links = li;
		}

		li->files[li->linksLeft++] = xstrdup(hdr->path);

		/* XXX FIXME 0 length hard linked files are broke. */
		if (st->st_size && li->createdPath == -1) {
		    createLinks(li, failedFile);

		    /*
		     * This only happens for cpio archives which contain
		     * hardlinks w/ the contents of each hardlink being
		     * listed (intead of the data being given just once. This
		     * shouldn't happen, but I've made it happen w/ buggy
		     * code, so what the heck? GNU cpio handles this well fwiw.
		     */
		    eatBytes(hdr->cfd, st->st_size);
		}

	    } else {
		rc = checkDirectory(hdr->path);

		if (!rc) {
		    if (S_ISREG(st->st_mode))
			rc = expandRegular(hdr, mapMd5sum(map));
		    else if (S_ISDIR(st->st_mode))
			rc = createDirectory(hdr->path, 000);
		    else if (S_ISLNK(st->st_mode))
			rc = expandSymlink(hdr);
		    else if (S_ISFIFO(st->st_mode))
			rc = expandFifo(hdr);
		    else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
			rc = expandDevice(hdr);
		    else if (S_ISSOCK(st->st_mode)) {
			/* this mimics cpio but probably isnt' right */
			rc = expandFifo(hdr);
		    } else {
			rc = CPIOERR_UNKNOWN_FILETYPE;
		    }
		}

		if (!rc)
		    rc = setInfo(hdr);

		if (S_ISREG(st->st_mode) && st->st_nlink > 1) {
		    li->createdPath = --li->linksLeft;
		    rc = createLinks(li, failedFile);
		}
	    }

	    if (rc && failedFile && *failedFile == NULL) {
		int olderrno;

		*failedFile = xstrdup(hdr->path);
		olderrno = errno;
		unlink(hdr->path);
		errno = olderrno;
	    }
	}

	padinfd(hdr->cfd, 4);

	if (!rc)
	    hdrCallback(hdr);

    } while (rc == 0);

    if (hdr->path) {
	free((void *)hdr->path);
	hdr->path = NULL;
    }

    /* Create any remaining links (if no error), and clean up. */
    while ((li = links) != NULL) {
	links = li->next;
	li->next = NULL;

	if (rc == 0 && li->linksLeft) {
	    if (li->createdPath == -1)
		rc = CPIOERR_MISSING_HARDLINK;
	    else
		rc = createLinks(li, failedFile);
	}

	freeHardLink(li);
    }

#ifdef	NOTYET
    fdFiniMD5(hdr->cfd, (void **)&md5sum, NULL, 1);

    if (md5sum)
	free(md5sum);
#endif
    rc = 0;

exit:
    fdFree(hdr->cfd, "persist (cpioInstallArchive");
    hdr->cfd = NULL;
    if (hdr->mapi)
	mapFreeIterator(hdr->mapi);
    return rc;
}

/**
 * Write next item to payload stream.
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @param cfd		payload file handle
 * @param st		stat info for item
 * @param map		mapping name and flags for item
 * @retval sizep	address of no. bytes written
 * @param writeData	should data be written?
 * @return		0 on success
 */
static int writeFile(const rpmTransactionSet ts, TFI_t fi, FD_t cfd,
	const struct stat * st, const void * map, /*@out@*/ size_t * sizep,
	int writeData)
	/*@modifies cfd, *sizep @*/
{
    const char * fsPath = mapFsPath(map);
    const char * archivePath = NULL;
    const char * hdrPath = !mapFlags(map, CPIO_MAP_PATH)
		? fsPath : (archivePath = mapArchivePath(map));
    struct cpioCrcPhysicalHeader hdr;
    char buf[8192], symbuf[2048];
    dev_t num;
    FD_t datafd;
    size_t st_size = st->st_size;	/* XXX hard links need size preserved */
    mode_t st_mode = st->st_mode;
    uid_t st_uid = st->st_uid;
    gid_t st_gid = st->st_gid;
    size_t size, amount = 0;
    int rc;

    if (mapFlags(map, CPIO_MAP_MODE))
	st_mode = (st_mode & S_IFMT) | mapFinalMode(map);
    if (mapFlags(map, CPIO_MAP_UID))
	st_uid = mapFinalUid(map);
    if (mapFlags(map, CPIO_MAP_GID))
	st_gid = mapFinalGid(map);

    if (!writeData || S_ISDIR(st_mode)) {
	st_size = 0;
    } else if (S_ISLNK(st_mode)) {
	/*
	 * While linux puts the size of a symlink in the st_size field,
	 * I don't think that's a specified standard.
	 */
	amount = Readlink(fsPath, symbuf, sizeof(symbuf));
	if (amount <= 0) {
	    rc = CPIOERR_READLINK_FAILED;
	    goto exit;
	}

	st_size = amount;
    }

    memcpy(hdr.magic, CPIO_NEWC_MAGIC, sizeof(hdr.magic));
    SET_NUM_FIELD(hdr.inode, st->st_ino, buf);
    SET_NUM_FIELD(hdr.mode, st_mode, buf);
    SET_NUM_FIELD(hdr.uid, st_uid, buf);
    SET_NUM_FIELD(hdr.gid, st_gid, buf);
    SET_NUM_FIELD(hdr.nlink, st->st_nlink, buf);
    SET_NUM_FIELD(hdr.mtime, st->st_mtime, buf);
    SET_NUM_FIELD(hdr.filesize, st_size, buf);

    num = major((unsigned)st->st_dev); SET_NUM_FIELD(hdr.devMajor, num, buf);
    num = minor((unsigned)st->st_dev); SET_NUM_FIELD(hdr.devMinor, num, buf);
    num = major((unsigned)st->st_rdev); SET_NUM_FIELD(hdr.rdevMajor, num, buf);
    num = minor((unsigned)st->st_rdev); SET_NUM_FIELD(hdr.rdevMinor, num, buf);

    num = strlen(hdrPath) + 1; SET_NUM_FIELD(hdr.namesize, num, buf);
    memcpy(hdr.checksum, "00000000", 8);

    if ((rc = safewrite(cfd, &hdr, PHYS_HDR_SIZE)) != PHYS_HDR_SIZE)
	goto exit;
    if ((rc = safewrite(cfd, hdrPath, num)) != num)
	goto exit;
    size = PHYS_HDR_SIZE + num;
    if ((rc = padoutfd(cfd, &size, 4)))
	goto exit;

    if (writeData && S_ISREG(st_mode)) {
	char *b;
#if HAVE_MMAP
	void *mapped;
	size_t nmapped;
#endif

	/* XXX unbuffered mmap generates *lots* of fdio debugging */
	datafd = Fopen(fsPath, "r.ufdio");
	if (datafd == NULL || Ferror(datafd)) {
	    rc = CPIOERR_OPEN_FAILED;
	    goto exit;
	}

#if HAVE_MMAP
	nmapped = 0;
	mapped = mmap(NULL, st_size, PROT_READ, MAP_SHARED, Fileno(datafd), 0);
	if (mapped != (void *)-1) {
	    b = (char *)mapped;
	    nmapped = st_size;
	} else
#endif
	{
	    b = buf;
	}

	size += st_size;

	while (st_size) {
#if HAVE_MMAP
	  if (mapped != (void *)-1) {
	    amount = nmapped;
	  } else
#endif
	  {
	    amount = Fread(b, sizeof(buf[0]),
			(st_size > sizeof(buf) ? sizeof(buf) : st_size),
			datafd);
	    if (amount <= 0) {
		int olderrno = errno;
		Fclose(datafd);
		errno = olderrno;
		rc = CPIOERR_READ_FAILED;
		goto exit;
	    }
	  }

	    if ((rc = safewrite(cfd, b, amount)) != amount) {
		int olderrno = errno;
		Fclose(datafd);
		errno = olderrno;
		goto exit;
	    }

	    st_size -= amount;
	}

#if HAVE_MMAP
	if (mapped != (void *)-1) {
	    /*@-noeffect@*/ munmap(mapped, nmapped) /*@=noeffect@*/;
	}
#endif

	Fclose(datafd);
    } else if (writeData && S_ISLNK(st_mode)) {
	if ((rc = safewrite(cfd, symbuf, amount)) != amount)
	    goto exit;
	size += amount;
    }

    /* this is a noop for most file types */
    if ((rc = padoutfd(cfd, &size, 4)))
	goto exit;

    if (sizep)
	*sizep = size;

    if (ts && fi && ts->notify) {
	(void)ts->notify(fi->h, RPMCALLBACK_INST_PROGRESS, size, size,
			(fi->ap ? fi->ap->key : NULL), ts->notifyData);
    }

    rc = 0;

exit:
    if (fsPath) free((void *)fsPath);
    return rc;
}

/**
 * Write set of linked files to payload stream.
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @param cfd		payload file handle
 * @param hlink		set of linked files
 * @retval sizep	address of no. bytes written
 * @retval failedFile	on error, file name that failed
 * @return		0 on success
 */
static int writeLinkedFile(const rpmTransactionSet ts, TFI_t fi, FD_t cfd,
		const struct hardLink * hlink, /*@out@*/size_t * sizep,
		/*@out@*/const char ** failedFile)
	/*@modifies cfd, *sizep, *failedFile @*/
{
    const void * map = NULL;
    size_t total = 0;
    size_t size;
    int rc = 0;
    int i;

    for (i = hlink->nlink - 1; i > hlink->linksLeft; i--) {
	map = hlink->fileMaps[i];
	if ((rc = writeFile(ts, fi, cfd, &hlink->sb, map, &size, 0)) != 0) {
	    if (failedFile)
		*failedFile = mapFsPath(map);
	    goto exit;
	}

	total += size;

	mapFree(map); map = NULL;
    }

    i = hlink->linksLeft;
    map = hlink->fileMaps[i];
    if ((rc = writeFile(ts, fi, cfd, &hlink->sb, map, &size, 1))) {
	if (sizep)
	    *sizep = total;
	if (failedFile)
	    *failedFile = mapFsPath(map);
	goto exit;
    }
    total += size;

    if (sizep)
	*sizep = total;

    rc = 0;

exit:
    if (map)
	mapFree(map);
    return rc;
}

int cpioBuildArchive(const rpmTransactionSet ts, const TFI_t fi, FD_t cfd,
		unsigned int * archiveSize, const char ** failedFile)
{
    void * mapi = mapInitIterator(ts, fi);
    const void * map;
/*@-fullinitblock@*/
    struct hardLink hlinkList = { NULL };
/*@=fullinitblock@*/
    struct stat * st = (struct stat *) &hlinkList.sb;
    struct hardLink * hlink;
    size_t totalsize = 0;
    size_t size;
    int rc;

    hlinkList.next = NULL;

    while ((map = mapNextIterator(mapi)) != NULL) {
	const char * fsPath;

	fsPath = mapFsPath(map);

	if (mapFlags(map, CPIO_FOLLOW_SYMLINKS))
	    rc = Stat(fsPath, st);
	else
	    rc = Lstat(fsPath, st);

	if (rc) {
	    if (failedFile)
		*failedFile = fsPath;
	    return CPIOERR_STAT_FAILED;
	}

	if (!S_ISDIR(st->st_mode) && st->st_nlink > 1) {
	    hlink = hlinkList.next;
	    while (hlink &&
		  (hlink->dev != st->st_dev || hlink->inode != st->st_ino))
			hlink = hlink->next;
	    if (hlink == NULL) {
		hlink = newHardLink(st, HARDLINK_INSTALL);
		hlink->next = hlinkList.next;
		hlinkList.next = hlink;
	    }

	    hlink->fileMaps[--hlink->linksLeft] = mapLink(map);

	    if (hlink->linksLeft == 0) {
		struct hardLink * prev;
		rc = writeLinkedFile(ts, fi, cfd, hlink, &size, failedFile);
		if (rc) {
		    free((void *)fsPath);
		    return rc;
		}

		totalsize += size;

		prev = &hlinkList;
		do {
		    if (prev->next != hlink)
			continue;
		    prev->next = hlink->next;
		    hlink->next = NULL;
		    freeHardLink(hlink);
		    hlink = NULL;
		    break;
		} while ((prev = prev->next) != NULL);
	    }
	} else {
	    if ((rc = writeFile(ts, fi, cfd, st, map, &size, 1))) {
		if (failedFile)
		    *failedFile = fsPath;
		return rc;
	    }

	    totalsize += size;
	}
	free((void *)fsPath);
	fsPath = NULL;
    }
    mapFreeIterator(mapi);

    rc = 0;
    while ((hlink = hlinkList.next) != NULL) {
	hlinkList.next = hlink->next;
	hlink->next = NULL;

	if (rc == 0) {
	    rc = writeLinkedFile(ts, fi, cfd, hlink, &size, failedFile);
	    totalsize += size;
	}
	freeHardLink(hlink);
    }
    if (rc)
	return rc;

    {	struct cpioCrcPhysicalHeader hdr;
	memset(&hdr, '0', PHYS_HDR_SIZE);
	memcpy(hdr.magic, CPIO_NEWC_MAGIC, sizeof(hdr.magic));
	memcpy(hdr.nlink, "00000001", 8);
	memcpy(hdr.namesize, "0000000b", 8);
	if ((rc = safewrite(cfd, &hdr, PHYS_HDR_SIZE)) != PHYS_HDR_SIZE)
	    return rc;
	if ((rc = safewrite(cfd, "TRAILER!!!", 11)) != 11)
	    return rc;
	totalsize += PHYS_HDR_SIZE + 11;
    }

    /* GNU cpio pads to 512 bytes here, but we don't. I'm not sure if
       it matters or not */

    if ((rc = padoutfd(cfd, &totalsize, 4)))
	return rc;

    if (archiveSize) *archiveSize = totalsize;

    return 0;
}

const char *const cpioStrerror(int rc)
{
    static char msg[256];
    char *s;
    int l, myerrno = errno;

    strcpy(msg, "cpio: ");
    switch (rc) {
    default:
	s = msg + strlen(msg);
	sprintf(s, _("(error 0x%x)"), (unsigned)rc);
	s = NULL;
	break;
    case CPIOERR_BAD_MAGIC:	s = _("Bad magic");		break;
    case CPIOERR_BAD_HEADER:	s = _("Bad/unreadable  header");break;

    case CPIOERR_OPEN_FAILED:	s = "open";	break;
    case CPIOERR_CHMOD_FAILED:	s = "chmod";	break;
    case CPIOERR_CHOWN_FAILED:	s = "chown";	break;
    case CPIOERR_WRITE_FAILED:	s = "write";	break;
    case CPIOERR_UTIME_FAILED:	s = "utime";	break;
    case CPIOERR_UNLINK_FAILED:	s = "unlink";	break;
    case CPIOERR_SYMLINK_FAILED: s = "symlink";	break;
    case CPIOERR_STAT_FAILED:	s = "stat";	break;
    case CPIOERR_MKDIR_FAILED:	s = "mkdir";	break;
    case CPIOERR_MKNOD_FAILED:	s = "mknod";	break;
    case CPIOERR_MKFIFO_FAILED:	s = "mkfifo";	break;
    case CPIOERR_LINK_FAILED:	s = "link";	break;
    case CPIOERR_READLINK_FAILED: s = "readlink";	break;
    case CPIOERR_READ_FAILED:	s = "read";	break;
    case CPIOERR_COPY_FAILED:	s = "copy";	break;

    case CPIOERR_HDR_SIZE:	s = _("Header size too big");	break;
    case CPIOERR_UNKNOWN_FILETYPE: s = _("Unknown file type");	break;
    case CPIOERR_MISSING_HARDLINK: s = _("Missing hard link");	break;
    case CPIOERR_MD5SUM_MISMATCH: s = _("MD5 sum mismatch");	break;
    case CPIOERR_INTERNAL:	s = _("Internal error");	break;
    }

    l = sizeof(msg) - strlen(msg) - 1;
    if (s != NULL) {
	if (l > 0) strncat(msg, s, l);
	l -= strlen(s);
    }
    if ((rc & CPIOERR_CHECK_ERRNO) && myerrno) {
	s = _(" failed - ");
	if (l > 0) strncat(msg, s, l);
	l -= strlen(s);
	if (l > 0) strncat(msg, strerror(myerrno), l);
    }
    return msg;
}
