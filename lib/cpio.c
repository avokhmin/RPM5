/** \ingroup payload rpmio
 * \file lib/cpio.c
 *  Handle cpio payloads within rpm packages.
 *
 * \warning FIXME: We don't translate between cpio and system mode bits! These
 * should both be the same, but really odd things are going to happen if
 * that's not true!
 */

#include "system.h"

#include "cpio.h"

#define	xfree(_p)	free((void *)_p)

#define CPIO_NEWC_MAGIC	"070701"
#define CPIO_CRC_MAGIC	"070702"
#define TRAILER		"TRAILER!!!"

/** \ingroup payload
 * Keeps track of set of all hard linked files in archive.
 */
struct hardLink {
    struct hardLink * next;
    const char ** files;	/* nlink of these, used by install */
    int * fileMaps;		/* used by build */
    dev_t dev;
    ino_t inode;
    int nlink;
    int linksLeft;
    int createdPath;
    const struct stat sb;
};

enum hardLinkType { HARDLINK_INSTALL=1, HARDLINK_BUILD };

/** \ingroup payload
 * Cpio archive header information.
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

#define	PHYS_HDR_SIZE	110		/* don't depend on sizeof(struct) */

/** \ingroup payload
 * File name and stat information.
 */
struct cpioHeader {
    /*@owned@*/ const char * path;
    struct stat sb;
};

#if 0
static void prtli(const char *msg, struct hardLink * li)
{
    if (msg) fprintf(stderr, "%s", msg);
    fprintf(stderr, " next %p files %p fileMaps %p dev %x ino %x nlink %d left %d createdPath %d size %d\n", li->next, li->files, li->fileMaps, (unsigned)li->dev, (unsigned)li->inode, li->nlink, li->linksLeft, li->createdPath, li->sb.st_size);
}
#endif

/** */
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

/** */
static inline off_t ourread(FD_t cfd, /*@out@*/void * buf, size_t size)
	/*@modifies cfd, *buf @*/
{
    off_t i = saferead(cfd, buf, size);
    if (i > 0)
	fdSetCpioPos(cfd, fdGetCpioPos(cfd) + i);
    return i;
}

/** */
static inline void padinfd(FD_t cfd, int modulo)
	/*@modifies cfd @*/
{
    int buf[10];
    int amount;

    amount = (modulo - fdGetCpioPos(cfd) % modulo) % modulo;
    (void)ourread(cfd, buf, amount);
}

/** */
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

/** */
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

/** */
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

    return strtoul(buf, endptr, base);
}

#define GET_NUM_FIELD(phys, log) \
	log = strntoul(phys, &end, 16, sizeof(phys)); \
	if (*end) return CPIOERR_BAD_HEADER;
#define SET_NUM_FIELD(phys, val, space) \
	sprintf(space, "%8.8lx", (unsigned long) (val)); \
	memcpy(phys, space, 8);

/** */
static int getNextHeader(FD_t cfd, struct cpioHeader * hdr)
	/*@modifies cfd, hdr->path, hdr->sb  @*/
{
    struct cpioCrcPhysicalHeader physHeader;
    struct stat * st = &hdr->sb;
    int nameSize;
    char * end;
    int major, minor;

    if (ourread(cfd, &physHeader, PHYS_HDR_SIZE) != PHYS_HDR_SIZE)
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
	if (ourread(cfd, t, nameSize) != nameSize) {
	    free(t);
	    hdr->path = NULL;
	    return CPIOERR_BAD_HEADER;
	}
	hdr->path = t;
    }

    /* this is unecessary hdr->path[nameSize] = '\0'; */

    padinfd(cfd, 4);

    return 0;
}

int cpioFileMapCmp(const void * a, const void * b)
{
    const char * afn = ((const struct cpioFileMapping *)a)->archivePath;
    const char * bfn = ((const struct cpioFileMapping *)b)->archivePath;

    /* Match payloads with ./ prefixes as well. */
    if (afn[0] == '.' && afn[1] == '/')	afn += 2;
    if (bfn[0] == '.' && bfn[1] == '/')	bfn += 2;

    return strcmp(afn, bfn);
}

/* This could trash files in the path! I'm not sure that's a good thing */
/** */
static int createDirectory(const char * path, mode_t perms)
	/*@modifies fileSystem @*/
{
    struct stat sb;

    if (!lstat(path, &sb)) {
	int dounlink = 0;	/* XXX eliminate, dounlink==1 on all paths */
	if (S_ISDIR(sb.st_mode)) {
	    return 0;
	} else if (S_ISLNK(sb.st_mode)) {
	    if (stat(path, &sb)) {
		if (errno != ENOENT)
		    return CPIOERR_STAT_FAILED;
		dounlink = 1;
	    } else {
		if (S_ISDIR(sb.st_mode))
		    return 0;
		dounlink = 1;
	    }
	} else {
	    dounlink = 1;
	}

	if (dounlink && unlink(path)) {
	    return CPIOERR_UNLINK_FAILED;
	}
    }

    if (mkdir(path, 000))
	return CPIOERR_MKDIR_FAILED;

    if (chmod(path, perms))
	return CPIOERR_CHMOD_FAILED;

    return 0;
}

/** */
static int setInfo(struct cpioHeader * hdr)
	/*@modifies fileSystem @*/
{
    int rc = 0;
    struct utimbuf stamp;
    struct stat * st = &hdr->sb;

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

/** */
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

/** */
static int expandRegular(FD_t cfd, const struct cpioHeader * hdr,
			 cpioCallback cb, void * cbData)
		/*@modifies fileSystem, cfd @*/
{
    FD_t ofd;
    char buf[BUFSIZ];
    int bytesRead;
    const struct stat * st = &hdr->sb;
    int left = st->st_size;
    int rc = 0;
    struct cpioCallbackInfo cbInfo = { NULL, 0, 0, 0 };
    struct stat sb;

    /* Rename the old file before attempting unlink to avoid EBUSY errors */
    if (!lstat(hdr->path, &sb)) {
	strcpy(buf, hdr->path);
	strcat(buf, "-RPMDELETE");
	if (rename(hdr->path, buf)) {
	    fprintf(stderr, _("can't rename %s to %s: %s\n"),
		hdr->path, buf, strerror(errno));
            return CPIOERR_UNLINK_FAILED;
	}

	if (unlink(buf)) {
	    fprintf(stderr, _("can't unlink %s: %s\n"),
		buf, strerror(errno));
#if 0
	    return CPIOERR_UNLINK_FAILED;
#endif
	}
    }

    ofd = Fopen(hdr->path, "w.ufdio");
    if (ofd == NULL || Ferror(ofd))
	return CPIOERR_OPEN_FAILED;

    cbInfo.file = hdr->path;
    cbInfo.fileSize = st->st_size;

    while (left) {
	bytesRead = ourread(cfd, buf, left < sizeof(buf) ? left : sizeof(buf));
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
	if (!rc && cb && left) {
	    cbInfo.fileComplete = st->st_size - left;
	    cbInfo.bytesProcessed = fdGetCpioPos(cfd);
	    cb(&cbInfo, cbData);
	}
    }

    Fclose(ofd);

    return rc;
}

/** */
static int expandSymlink(FD_t cfd, const struct cpioHeader * hdr)
		/*@modifies fileSystem, cfd @*/
{
    char buf[2048], buf2[2048];
    struct stat sb;
    const struct stat * st = &hdr->sb;
    int len;

    if ((st->st_size + 1)> sizeof(buf))
	return CPIOERR_HDR_SIZE;

    if (ourread(cfd, buf, st->st_size) != st->st_size)
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

/** */
static int expandFifo( /*@unused@*/ FD_t cfd, const struct cpioHeader * hdr)
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

/** */
static int expandDevice( /*@unused@*/ FD_t cfd, const struct cpioHeader * hdr)
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

/** */
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

/** */
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

/** */
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

/** */
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

int cpioInstallArchive(FD_t cfd, const struct cpioFileMapping * mappings,
		       int numMappings, cpioCallback cb, void * cbData,
		       const char ** failedFile)
{
    struct cpioHeader ch, *hdr = &ch;
    struct cpioFileMapping * map = NULL;
    struct cpioFileMapping needle;
    struct cpioCallbackInfo cbInfo = { NULL, 0, 0, 0 };
    struct hardLink * links = NULL;
    struct hardLink * li = NULL;
    int rc = 0;

    fdSetCpioPos(cfd, 0);
    if (failedFile)
	*failedFile = NULL;

    memset(hdr, 0, sizeof(*hdr));
    hdr->path = NULL;
    do {
	struct stat * st;

	if (hdr->path) {
	    xfree(hdr->path);
	    hdr->path = NULL;
	}
	if ((rc = getNextHeader(cfd, hdr))) {
#if 0	/* XXX this is the failure point for an unreadable rpm */
	    fprintf(stderr, _("getNextHeader: %s\n"), cpioStrerror(rc));
#endif
	    return rc;
	}
	st = &hdr->sb;

	if (!strcmp(hdr->path, TRAILER))
	    break;

	if (mappings) {
	    needle.archivePath = hdr->path;
	    map = bsearch(&needle, mappings, numMappings, sizeof(needle),
			  cpioFileMapCmp);
	}

	if (mappings && !map) {
	    eatBytes(cfd, st->st_size);
	} else {
	    if (map) {
		if (map->mapFlags & CPIO_MAP_PATH) {
		    if (hdr->path) xfree(hdr->path);
		    hdr->path = xstrdup(map->fsPath);
		}

		if (map->mapFlags & CPIO_MAP_MODE)
		    st->st_mode = map->finalMode;
		if (map->mapFlags & CPIO_MAP_UID)
		    st->st_uid = map->finalUid;
		if (map->mapFlags & CPIO_MAP_GID)
		    st->st_gid = map->finalGid;
	    }

	    /* This won't get hard linked symlinks right, but I can't seem
	       to create those anyway */

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
	    }

	    if ((st->st_nlink > 1) && S_ISREG(st->st_mode) && !st->st_size &&
		li->createdPath == -1) {
		/* defer file creation */
	    } else if ((st->st_nlink > 1) && S_ISREG(st->st_mode) &&
		       (li->createdPath != -1)) {
		createLinks(li, failedFile);

		/* this only happens for cpio archives which contain
		   hardlinks w/ the contents of each hardlink being
		   listed (intead of the data being given just once. This
		   shouldn't happen, but I've made it happen w/ buggy
		   code, so what the heck? GNU cpio handles this well fwiw */
		if (st->st_size) eatBytes(cfd, st->st_size);
	    } else {
		rc = checkDirectory(hdr->path);

		if (!rc) {
		    if (S_ISREG(st->st_mode))
			rc = expandRegular(cfd, hdr, cb, cbData);
		    else if (S_ISDIR(st->st_mode))
			rc = createDirectory(hdr->path, 000);
		    else if (S_ISLNK(st->st_mode))
			rc = expandSymlink(cfd, hdr);
		    else if (S_ISFIFO(st->st_mode))
			rc = expandFifo(cfd, hdr);
		    else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
			rc = expandDevice(cfd, hdr);
		    else if (S_ISSOCK(st->st_mode)) {
			/* this mimicks cpio but probably isnt' right */
			rc = expandFifo(cfd, hdr);
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

	padinfd(cfd, 4);

	if (!rc && cb) {
	    cbInfo.file = hdr->path;
	    cbInfo.fileSize = st->st_size;
	    cbInfo.fileComplete = st->st_size;
	    cbInfo.bytesProcessed = fdGetCpioPos(cfd);
	    cb(&cbInfo, cbData);
	}

    } while (rc == 0);

    if (hdr->path) {
	xfree(hdr->path);
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

    return rc;
}

/** */
static int writeFile(FD_t cfd, const struct stat * st,
	const struct cpioFileMapping * map, /*@out@*/ size_t * sizep,
	int writeData)
	/*@modifies cfd, *sizep @*/
{
    struct cpioCrcPhysicalHeader hdr;
    char buf[8192], symbuf[2048];
    dev_t num;
    FD_t datafd;
    size_t st_size = st->st_size;	/* XXX hard links need size preserved */
    const char * archivePath;
    mode_t st_mode = st->st_mode;
    uid_t st_uid = st->st_uid;
    gid_t st_gid = st->st_gid;
    size_t size, amount = 0;
    int rc;

    archivePath = (!(map->mapFlags & CPIO_MAP_PATH))
	? map->fsPath : map->archivePath;

    if (map->mapFlags & CPIO_MAP_MODE)
	st_mode = (st_mode & S_IFMT) | map->finalMode;
    if (map->mapFlags & CPIO_MAP_UID)
	st_uid = map->finalUid;
    if (map->mapFlags & CPIO_MAP_GID)
	st_gid = map->finalGid;

    if (!writeData || S_ISDIR(st_mode)) {
	st_size = 0;
    } else if (S_ISLNK(st_mode)) {
	/* While linux puts the size of a symlink in the st_size field,
	   I don't think that's a specified standard */

	amount = Readlink(map->fsPath, symbuf, sizeof(symbuf));
	if (amount <= 0) {
	    return CPIOERR_READLINK_FAILED;
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

    num = strlen(archivePath) + 1; SET_NUM_FIELD(hdr.namesize, num, buf);
    memcpy(hdr.checksum, "00000000", 8);

    if ((rc = safewrite(cfd, &hdr, PHYS_HDR_SIZE)) != PHYS_HDR_SIZE)
	return rc;
    if ((rc = safewrite(cfd, archivePath, num)) != num)
	return rc;
    size = PHYS_HDR_SIZE + num;
    if ((rc = padoutfd(cfd, &size, 4)))
	return rc;

    if (writeData && S_ISREG(st_mode)) {
	char *b;
#if HAVE_MMAP
	void *mapped;
	size_t nmapped;
#endif

	/* XXX unbuffered mmap generates *lots* of fdio debugging */
	datafd = Fopen(map->fsPath, "r.ufdio");
	if (datafd == NULL || Ferror(datafd))
	    return CPIOERR_OPEN_FAILED;

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
		return CPIOERR_READ_FAILED;
	    }
	  }

	    if ((rc = safewrite(cfd, b, amount)) != amount) {
		int olderrno = errno;
		Fclose(datafd);
		errno = olderrno;
		return rc;
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
	    return rc;
	size += amount;
    }

    /* this is a noop for most file types */
    if ((rc = padoutfd(cfd, &size, 4)))
	return rc;

    if (sizep)
	*sizep = size;

    return 0;
}

/** */
static int writeLinkedFile(FD_t cfd, const struct hardLink * hlink,
			   const struct cpioFileMapping * mappings,
			   cpioCallback cb, void * cbData,
			   /*@out@*/size_t * sizep,
			   /*@out@*/const char ** failedFile)
	/*@modifies cfd, *sizep, *failedFile @*/
{
    int i, rc;
    size_t size, total;
    struct cpioCallbackInfo cbInfo = { NULL, 0, 0, 0 };

    total = 0;

    for (i = hlink->nlink - 1; i > hlink->linksLeft; i--) {
	if ((rc = writeFile(cfd, &hlink->sb, mappings + hlink->fileMaps[i],
			    &size, 0))) {
	    if (failedFile)
		*failedFile = xstrdup(mappings[hlink->fileMaps[i]].fsPath);
	    return rc;
	}

	total += size;

	if (cb) {
	    cbInfo.file = mappings[i].archivePath;
	    cb(&cbInfo, cbData);
	}
    }

    if ((rc = writeFile(cfd, &hlink->sb,
			mappings + hlink->fileMaps[hlink->linksLeft],
			&size, 1))) {
	if (sizep)
	    *sizep = total;
	if (failedFile)
	    *failedFile = xstrdup(mappings[hlink->fileMaps[hlink->linksLeft]].fsPath);
	return rc;
    }
    total += size;

    if (sizep)
	*sizep = total;

    if (cb) {
	cbInfo.file = mappings[i].archivePath;
	cb(&cbInfo, cbData);
    }

    return 0;
}

int cpioBuildArchive(FD_t cfd, const struct cpioFileMapping * mappings,
		     int numMappings, cpioCallback cb, void * cbData,
		     unsigned int * archiveSize, const char ** failedFile)
{
    size_t size, totalsize = 0;
    int rc;
    int i;
    struct cpioCallbackInfo cbInfo = { NULL, 0, 0, 0 };
    struct cpioCrcPhysicalHeader hdr;
/*@-fullinitblock@*/
    struct hardLink hlinkList = { NULL };
/*@=fullinitblock@*/
    struct stat * st = (struct stat *) &hlinkList.sb;
    struct hardLink * hlink;

    hlinkList.next = NULL;

    for (i = 0; i < numMappings; i++) {
	const struct cpioFileMapping * map;

	map = mappings + i;

	if (map->mapFlags & CPIO_FOLLOW_SYMLINKS)
	    rc = Stat(map->fsPath, st);
	else
	    rc = Lstat(map->fsPath, st);

	if (rc) {
	    if (failedFile)
		*failedFile = xstrdup(map->fsPath);
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

	    hlink->fileMaps[--hlink->linksLeft] = i;

	    if (hlink->linksLeft == 0) {
		struct hardLink * prev;
		if ((rc = writeLinkedFile(cfd, hlink, mappings, cb, cbData,
			 		  &size, failedFile)))
		    return rc;

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
	    if ((rc = writeFile(cfd, st, map, &size, 1))) {
		if (failedFile)
		    *failedFile = xstrdup(mappings[i].fsPath);
		return rc;
	    }

	    if (cb) {
		cbInfo.file = map->archivePath;
		cb(&cbInfo, cbData);
	    }

	    totalsize += size;
	}
    }

    rc = 0;
    while ((hlink = hlinkList.next) != NULL) {
	hlinkList.next = hlink->next;
	hlink->next = NULL;

	if (rc == 0) {
	    rc = writeLinkedFile(cfd, hlink, mappings, cb, cbData,
				  &size, failedFile);
	    totalsize += size;
	}
	freeHardLink(hlink);
    }
    if (rc)
	return rc;

    memset(&hdr, '0', PHYS_HDR_SIZE);
    memcpy(hdr.magic, CPIO_NEWC_MAGIC, sizeof(hdr.magic));
    memcpy(hdr.nlink, "00000001", 8);
    memcpy(hdr.namesize, "0000000b", 8);
    if ((rc = safewrite(cfd, &hdr, PHYS_HDR_SIZE)) != PHYS_HDR_SIZE)
	return rc;
    if ((rc = safewrite(cfd, "TRAILER!!!", 11)) != 11)
	return rc;
    totalsize += PHYS_HDR_SIZE + 11;

    /* GNU cpio pads to 512 bytes here, but we don't. I'm not sure if
       it matters or not */

    if ((rc = padoutfd(cfd, &totalsize, 4)))
	return rc;

    if (archiveSize) *archiveSize = totalsize;

    return 0;
}

const char * cpioStrerror(int rc)
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
