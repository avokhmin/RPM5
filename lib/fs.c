/**
 * \file lib/fs.c
 */

#include "system.h"
#include <rpmio.h>
#include <rpmiotypes.h>
#include <rpmlog.h>
#include <rpmmacro.h>	/* XXX for rpmGetPath */

#include "fs.h"

#include "debug.h"

/*@-usereleased -onlytrans@*/

struct fsinfo {
/*@only@*/ /*@relnull@*/
    const char * mntPoint;	/*!< path to mount point. */
    dev_t dev;			/*!< devno for mount point. */
    int rdonly;			/*!< is mount point read only? */
};

/*@unchecked@*/
/*@only@*/ /*@null@*/
static struct fsinfo * filesystems = NULL;
/*@unchecked@*/
/*@only@*/ /*@null@*/
static const char ** fsnames = NULL;
/*@unchecked@*/
static int numFilesystems = 0;

void rpmFreeFilesystems(void)
	/*@globals filesystems, fsnames, numFilesystems @*/
	/*@modifies filesystems, fsnames, numFilesystems @*/
{
    int i;

    if (filesystems)
    for (i = 0; i < numFilesystems; i++)
	filesystems[i].mntPoint = _free(filesystems[i].mntPoint);

    filesystems = _free(filesystems);
    fsnames = _free(fsnames);
    numFilesystems = 0;
}

#if defined(HAVE_MNTCTL)

/* modeled after sample code from Till Bubeck */

#include <sys/mntctl.h>
#include <sys/vmount.h>

/* 
 * There is NO mntctl prototype in any header file of AIX 3.2.5! 
 * So we have to declare it by ourself...
 */
int mntctl(int command, int size, char *buffer);

/**
 * Get information for mounted file systems.
 * @todo determine rdonly for non-linux file systems.
 * @return		0 on success, 1 on error
 */
static int getFilesystemList(void)
	/*@*/
{
    int size;
    void * buf;
    struct vmount * vm;
    struct stat sb;
    int rdonly = 0;
    int num;
    int fsnameLength;
    int i;

    num = mntctl(MCTL_QUERY, sizeof(size), (char *) &size);
    if (num < 0) {
	rpmlog(RPMLOG_ERR, _("mntctl() failed to return size: %s\n"), 
		 strerror(errno));
	return 1;
    }

    /*
     * Double the needed size, so that even when the user mounts a 
     * filesystem between the previous and the next call to mntctl
     * the buffer still is large enough.
     */
    size *= 2;

    buf = alloca(size);
    num = mntctl(MCTL_QUERY, size, buf);
    if ( num <= 0 ) {
        rpmlog(RPMLOG_ERR, _("mntctl() failed to return mount points: %s\n"), 
		 strerror(errno));
	return 1;
    }

    numFilesystems = num;

    filesystems = xcalloc((numFilesystems + 1), sizeof(*filesystems));
    fsnames = xcalloc((numFilesystems + 1), sizeof(char *));
    
    for (vm = buf, i = 0; i < num; i++) {
	char *fsn;
	fsnameLength = vm->vmt_data[VMT_STUB].vmt_size;
	fsn = xmalloc(fsnameLength + 1);
	strncpy(fsn, (char *)vm + vm->vmt_data[VMT_STUB].vmt_off, 
		fsnameLength);

	filesystems[i].mntPoint = fsnames[i] = fsn;
	
#if defined(RPM_VENDOR_OPENPKG) /* always-skip-proc-filesystem */
	if (!(strcmp(fsn, "/proc") == 0)) {
#endif
	if (Stat(fsn, &sb) < 0) {
	    switch(errno) {
	    default:
		rpmlog(RPMLOG_ERR, _("failed to stat %s: %s\n"), fsn,
			strerror(errno));
		rpmFreeFilesystems();
		return 1;
	    case ENOENT:	/* XXX avoid /proc if leaked into *BSD jails. */
		sb.st_dev = 0;	/* XXXX make sure st_dev is initialized. */
		/*@switchbreak@*/ break;
	    }
	}
	
	filesystems[i].dev = sb.st_dev;
	filesystems[i].rdonly = rdonly;
#if defined(RPM_VENDOR_OPENPKG) /* always-skip-proc-filesystem */
        }
#endif

	/* goto the next vmount structure: */
	vm = (struct vmount *)((char *)vm + vm->vmt_length);
    }

    filesystems[i].mntPoint = NULL;
    fsnames[i]              = NULL;

    return 0;
}

#else	/* HAVE_MNTCTL */

/**
 * Get information for mounted file systems.
 * @todo determine rdonly for non-linux file systems.
 * @return		0 on success, 1 on error
 */
static int getFilesystemList(void)
	/*@globals h_errno, filesystems, fsnames, numFilesystems,
		fileSystem, internalState @*/
	/*@modifies filesystems, fsnames, numFilesystems,
		fileSystem, internalState @*/
{
    int numAlloced = 10;
    struct stat sb;
    int i;
    const char * mntdir;
    int rdonly = 0;

#   if GETMNTENT_ONE || GETMNTENT_TWO
    our_mntent item;
    FILE * mtab;

	mtab = fopen(MOUNTED, "r");
	if (!mtab) {
	    rpmlog(RPMLOG_ERR, _("failed to open %s: %s\n"), MOUNTED, 
		     strerror(errno));
	    return 1;
	}
#   elif defined(HAVE_GETMNTINFO_R)
    /* This is OSF */
    struct statfs * mounts = NULL;
    int mntCount = 0, bufSize = 0, flags = MNT_NOWAIT;
    int nextMount = 0;

	getmntinfo_r(&mounts, flags, &mntCount, &bufSize);
#   elif defined(HAVE_GETMNTINFO)
    /* This is Mac OS X */
#if defined(__NetBSD__)
    struct statvfs * mounts = NULL;
#else
    struct statfs * mounts = NULL;
#endif
    int mntCount = 0, flags = MNT_NOWAIT;
    int nextMount = 0;

	/* XXX 0 on error, errno set */
	mntCount = getmntinfo(&mounts, flags);
#   endif

    filesystems = xcalloc((numAlloced + 1), sizeof(*filesystems));	/* XXX memory leak */

    numFilesystems = 0;
    while (1) {
#	if GETMNTENT_ONE
	    /* this is Linux */
	    /*@-modunconnomods -moduncon @*/
	    our_mntent * itemptr = getmntent(mtab);
	    if (!itemptr) break;
	    item = *itemptr;	/* structure assignment */
	    mntdir = item.our_mntdir;
#if defined(MNTOPT_RO)
	    /*@-compdef@*/
	    if (hasmntopt(itemptr, MNTOPT_RO) != NULL)
		rdonly = 1;
	    /*@=compdef@*/
#endif
	    /*@=modunconnomods =moduncon @*/
#	elif GETMNTENT_TWO
	    /* Solaris, maybe others */
	    if (getmntent(mtab, &item)) break;
	    mntdir = item.our_mntdir;
#	elif defined(HAVE_GETMNTINFO_R)
	    /* This is OSF */
	    if (nextMount == mntCount) break;
	    mntdir = mounts[nextMount++].f_mntonname;
#	elif defined(HAVE_GETMNTINFO)
	    /* This is Mac OS X */
	    if (nextMount == mntCount) break;
	    mntdir = mounts[nextMount++].f_mntonname;
#	endif

#if defined(RPM_VENDOR_OPENPKG) /* always-skip-proc-filesystem */
	if (strcmp(mntdir, "/proc") == 0)
		continue;
#endif

	if (Stat(mntdir, &sb) < 0) {
	    switch(errno) {
	    default:
		rpmlog(RPMLOG_ERR, _("failed to stat %s: %s\n"), mntdir,
			strerror(errno));
		rpmFreeFilesystems();
		return 1;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    case ENOENT:	/* XXX avoid /proc if leaked into *BSD jails. */
	    case EACCES:	/* XXX fuse fs #220991 */
	    case ESTALE:
		continue;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }
	}

	if ((numFilesystems + 2) == numAlloced) {
	    numAlloced += 10;
	    filesystems = xrealloc(filesystems, 
				  sizeof(*filesystems) * (numAlloced + 1));
	}

	filesystems[numFilesystems].dev = sb.st_dev;
	filesystems[numFilesystems].mntPoint = xstrdup(mntdir);
	filesystems[numFilesystems].rdonly = rdonly;
#if 0
	rpmlog(RPMLOG_DEBUG, "%5d 0x%04x %s %s\n",
		numFilesystems,
		(unsigned) filesystems[numFilesystems].dev,
		(filesystems[numFilesystems].rdonly ? "ro" : "rw"),
		filesystems[numFilesystems].mntPoint);
#endif
	numFilesystems++;
    }

#   if GETMNTENT_ONE || GETMNTENT_TWO
	(void) fclose(mtab);
#   elif defined(HAVE_GETMNTINFO_R)
	mounts = _free(mounts);
#   endif

    filesystems[numFilesystems].dev = 0;
    filesystems[numFilesystems].mntPoint = NULL;
    filesystems[numFilesystems].rdonly = 0;

    fsnames = xcalloc((numFilesystems + 1), sizeof(*fsnames));
    for (i = 0; i < numFilesystems; i++)
	fsnames[i] = filesystems[i].mntPoint;
    fsnames[numFilesystems] = NULL;

/*@-nullstate@*/ /* FIX: fsnames[] may be NULL */
    return 0; 
/*@=nullstate@*/
}
#endif	/* HAVE_MNTCTL */

int rpmGetFilesystemList(const char *** listptr, rpmuint32_t * num)
{
    if (!fsnames) 
	if (getFilesystemList())
	    return 1;

    if (listptr) *listptr = fsnames;
    if (num) *num = numFilesystems;

    return 0;
}

int rpmGetFilesystemUsage(const char ** fileList, rpmuint32_t * fssizes,
		int numFiles, rpmuint64_t ** usagesPtr,
		/*@unused@*/ int flags)
{
    rpmuint64_t * usages;
    int i, j;
    char * buf, * dirName;
    char * chptr;
    size_t maxLen;
    size_t len;
    char * lastDir;
    const char * sourceDir;
    int lastfs = 0;
    dev_t lastDev = (dev_t)-1;		/* I hope nobody uses -1 for a st_dev */
    struct stat sb;
    int rc = 1;		/* assume failure */

    if (!fsnames) 
	if (getFilesystemList())
	    return rc;

    usages = xcalloc(numFilesystems, sizeof(*usages));

    sourceDir = rpmGetPath("%{_sourcedir}", NULL);

    maxLen = strlen(sourceDir);
    for (i = 0; i < numFiles; i++) {
	len = strlen(fileList[i]);
	if (maxLen < len) maxLen = len;
    }
    
    buf = alloca(maxLen + 1);
    lastDir = alloca(maxLen + 1);
    dirName = alloca(maxLen + 1);
    *lastDir = '\0';

    /* cut off last filename */
    for (i = 0; i < numFiles; i++) {
	if (*fileList[i] == '/') {
	    strcpy(buf, fileList[i]);
	    chptr = buf + strlen(buf) - 1;
	    while (*chptr != '/') chptr--;
	    if (chptr == buf)
		buf[1] = '\0';
	    else
		*chptr-- = '\0';
	} else {
	    /* this should only happen for source packages (gulp) */
	    strcpy(buf,  sourceDir);
	}

	if (strcmp(lastDir, buf)) {
	    strcpy(dirName, buf);
	    chptr = dirName + strlen(dirName) - 1;
	    while (Stat(dirName, &sb) < 0) {
		switch(errno) {
		default:
		    rpmlog(RPMLOG_ERR, _("failed to stat %s: %s\n"), buf,
				strerror(errno));
		    goto exit;
		    /*@notreached@*/ /*@switchbreak@*/ break;
		case ENOENT:	/* XXX paths in empty chroot's don't exist. */
		    /*@switchbreak@*/ break;
		}

		/* cut off last directory part, because it was not found. */
		while (*chptr != '/') chptr--;

		if (chptr == dirName)
		    dirName[1] = '\0';
		else
		    *chptr-- = '\0';
	    }

	    if (lastDev != sb.st_dev) {
		for (j = 0; j < numFilesystems; j++)
		    if (filesystems && filesystems[j].dev == sb.st_dev)
			/*@innerbreak@*/ break;

		if (j == numFilesystems) {
		    rpmlog(RPMLOG_ERR, 
				_("file %s is on an unknown device\n"), buf);
		    goto exit;
		}

		lastfs = j;
		lastDev = sb.st_dev;
	    }
	}

	strcpy(lastDir, buf);
	usages[lastfs] += fssizes[i];
    }
    rc = 0;

exit:
    sourceDir = _free(sourceDir);

    if (rc == 0 && usagesPtr)
	*usagesPtr = usages;
    else
	usages = _free(usages);

    return rc;
}
/*@=usereleased =onlytrans@*/
