#include "system.h"

#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>

#include "cpio.h"
#include "install.h"
#include "misc.h"
#include "rpmdb.h"

struct callbackInfo {
    unsigned long archiveSize;
    rpmCallbackFunction notify;
    const char ** specFilePtr;
    Header h;
    void * notifyData;
    const void * pkgKey;
};

struct fileMemory {
    /*@owned@*/ const char ** names;
    /*@owned@*/ const char ** cpioNames;
    /*@owned@*/ struct fileInfo * files;
};

struct fileInfo {
    /*@dependent@*/ const char * cpioPath;
    /*@dependent@*/ const char * relativePath;	/* relative to root */
    uid_t uid;
    gid_t gid;
    uint_32 flags;
    uint_32 size;
    mode_t mode;
    char state;
    enum fileActions action;
    int install;
} ;

/* XXX add more tags */
static struct tagMacro {
	const char *	macroname;
	int		tag;
} tagMacros[] = {
	{ "name",	RPMTAG_NAME },
	{ "version",	RPMTAG_VERSION },
	{ "release",	RPMTAG_RELEASE },
#if 0
	{ "epoch",	RPMTAG_EPOCH },
#endif
	{ NULL, 0 }
};

static int rpmInstallLoadMacros(Header h)
{
    struct tagMacro *tagm;
    const char *body;
    char numbuf[32];
    int type;

    for (tagm = tagMacros; tagm->macroname != NULL; tagm++) {
	if (!headerGetEntry(h, tagm->tag, &type, (void **) &body, NULL))
	    continue;
	switch (type) {
	case RPM_INT32_TYPE:
	    sprintf(numbuf, "%d", ((int_32)body));
	    body = numbuf;
	    /*@fallthrough@*/
	case RPM_STRING_TYPE:
	    addMacro(NULL, tagm->macroname, NULL, body, -1);
	    break;
	}
    }
    return 0;
}

static /*@only@*/ struct fileMemory *newFileMemory(void)
{
    struct fileMemory *fileMem = xmalloc(sizeof(*fileMem));
    fileMem->files = NULL;
    fileMem->names = NULL;
    fileMem->cpioNames = NULL;
    return fileMem;
}

static void freeFileMemory( /*@only@*/ struct fileMemory *fileMem)
{
    if (fileMem->files) free(fileMem->files);
    if (fileMem->names) free(fileMem->names);
    if (fileMem->cpioNames) free(fileMem->cpioNames);
    free(fileMem);
}

/* files should not be preallocated */
static int assembleFileList(Header h, /*@out@*/ struct fileMemory ** memPtr, 
	 /*@out@*/ int * fileCountPtr, /*@out@*/ struct fileInfo ** filesPtr, 
	 int stripPrefixLength, enum fileActions * actions)
{
    uint_32 * fileFlags;
    uint_32 * fileSizes;
    uint_16 * fileModes;
    struct fileMemory *mem = newFileMemory();
    struct fileInfo * files;
    struct fileInfo * file;
    int fileCount;
    int i;

    *memPtr = mem;

    if (!headerIsEntry(h, RPMTAG_COMPFILELIST)) return 0; 

    buildFileList(h, &mem->names, fileCountPtr);

    if (headerIsEntry(h, RPMTAG_ORIGCOMPFILELIST)) {
	buildOrigFileList(h, &mem->cpioNames, fileCountPtr);
    } else {
	buildFileList(h, &mem->cpioNames, fileCountPtr);
    }

    fileCount = *fileCountPtr;

    files = *filesPtr = mem->files = xcalloc(fileCount, sizeof(*mem->files));
    
    headerGetEntry(h, RPMTAG_FILEFLAGS, NULL, (void **) &fileFlags, NULL);
    headerGetEntry(h, RPMTAG_FILEMODES, NULL, (void **) &fileModes, NULL);
    headerGetEntry(h, RPMTAG_FILESIZES, NULL, (void **) &fileSizes, NULL);

    for (i = 0, file = files; i < fileCount; i++, file++) {
	file->state = RPMFILE_STATE_NORMAL;
	if (actions)
	    file->action = actions[i];
	else
	    file->action = FA_UNKNOWN;
	file->install = 1;

	file->relativePath = mem->names[i];
	file->cpioPath = mem->cpioNames[i] + stripPrefixLength;
	file->mode = fileModes[i];
	file->size = fileSizes[i];
	file->flags = fileFlags[i];

	rpmMessage(RPMMESS_DEBUG, _("   file: %s action: %s\n"),
		    file->relativePath, fileActionString(file->action));
    }

    return 0;
}

static void setFileOwners(Header h, struct fileInfo * files, int fileCount)
{
    char ** fileOwners;
    char ** fileGroups;
    int i;

    headerGetEntry(h, RPMTAG_FILEUSERNAME, NULL, (void **) &fileOwners, NULL);
    headerGetEntry(h, RPMTAG_FILEGROUPNAME, NULL, (void **) &fileGroups, NULL);

    for (i = 0; i < fileCount; i++) {
	if (unameToUid(fileOwners[i], &files[i].uid)) {
	    rpmError(RPMERR_NOUSER, _("user %s does not exist - using root"), 
			fileOwners[i]);
	    files[i].uid = 0;
	    /* turn off the suid bit */
	    files[i].mode &= ~S_ISUID;
	}

	if (gnameToGid(fileGroups[i], &files[i].gid)) {
	    rpmError(RPMERR_NOGROUP, _("group %s does not exist - using root"), 
			fileGroups[i]);
	    files[i].gid = 0;
	    /* turn off the sgid bit */
	    files[i].mode &= ~S_ISGID;
	}
    }

    free(fileOwners);
    free(fileGroups);
}

static void trimChangelog(Header h)
{
    int * times;
    char ** names, ** texts;
    long numToKeep;
    char * end;
    int count;

    {	char *buf = rpmExpand("%{_instchangelog}", NULL);
	if (!(buf && *buf != '%')) {
	    xfree(buf);
	    return;
	}
	numToKeep = strtol(buf, &end, 10);

	if (!(end && *end == '\0')) {
	    rpmError(RPMERR_RPMRC, _("%%instchangelog value in macro file "
		 "should be a number, but isn't"));
	    xfree(buf);
	    return;
	}
	xfree(buf);
    }

    if (numToKeep < 0) return;

    if (!numToKeep) {
	headerRemoveEntry(h, RPMTAG_CHANGELOGTIME);
	headerRemoveEntry(h, RPMTAG_CHANGELOGNAME);
	headerRemoveEntry(h, RPMTAG_CHANGELOGTEXT);
	return;
    }

    if (!headerGetEntry(h, RPMTAG_CHANGELOGTIME, NULL, (void **) &times, 
			&count) ||
	count < numToKeep) return;
    headerGetEntry(h, RPMTAG_CHANGELOGNAME, NULL, (void **) &names, &count);
    headerGetEntry(h, RPMTAG_CHANGELOGTEXT, NULL, (void **) &texts, &count);

    headerModifyEntry(h, RPMTAG_CHANGELOGTIME, RPM_INT32_TYPE, times, 
		      numToKeep);
    headerModifyEntry(h, RPMTAG_CHANGELOGNAME, RPM_STRING_ARRAY_TYPE, names, 
		      numToKeep);
    headerModifyEntry(h, RPMTAG_CHANGELOGTEXT, RPM_STRING_ARRAY_TYPE, texts, 
		      numToKeep);

    free(names);
    free(texts);
}

static int markReplacedFiles(rpmdb db, struct sharedFileInfo * replList)
{
    struct sharedFileInfo * fileInfo;
    Header secHeader = NULL, sh;
    char * secStates = NULL;
    int secOffset = 0;
    int type, count;

    for (fileInfo = replList; fileInfo->otherPkg; fileInfo++) {
	if (secOffset != fileInfo->otherPkg) {
	    if (secHeader != NULL) {
		/* ignore errors here - just do the best we can */

		rpmdbUpdateRecord(db, secOffset, secHeader);
		headerFree(secHeader);
	    }

	    secOffset = fileInfo->otherPkg;
	    sh = rpmdbGetRecord(db, secOffset);
	    if (sh == NULL) {
		secOffset = 0;
	    } else {
		secHeader = headerCopy(sh);	/* so we can modify it */
		headerFree(sh);
	    }

	    headerGetEntry(secHeader, RPMTAG_FILESTATES, &type, 
			   (void **) &secStates, &count);
	}

	/* by now, secHeader is the right header to modify, secStates is
	   the right states list to modify  */
	
	secStates[fileInfo->otherFileNum] = RPMFILE_STATE_REPLACED;
    }

    if (secHeader != NULL) {
	/* ignore errors here - just do the best we can */

	rpmdbUpdateRecord(db, secOffset, secHeader);
	headerFree(secHeader);
    }

    return 0;
}

static void callback(struct cpioCallbackInfo * cpioInfo, void * data)
{
    struct callbackInfo * ourInfo = data;
    const char * chptr;

    if (ourInfo->notify)
	(void)ourInfo->notify(ourInfo->h, RPMCALLBACK_INST_PROGRESS,
			cpioInfo->bytesProcessed, 
			ourInfo->archiveSize, ourInfo->pkgKey, 
			ourInfo->notifyData);

    if (ourInfo->specFilePtr) {
	chptr = cpioInfo->file + strlen(cpioInfo->file) - 5;
	if (!strcmp(chptr, ".spec")) 
	    *ourInfo->specFilePtr = xstrdup(cpioInfo->file);
    }
}

/* NULL files means install all files */
static int installArchive(FD_t fd, struct fileInfo * files,
			  int fileCount, rpmCallbackFunction notify, 
			  void * notifyData, const void * pkgKey, Header h,
			  /*@out@*/const char ** specFile, int archiveSize)
{
    int rc, i;
    struct cpioFileMapping * map = NULL;
    int mappedFiles = 0;
    const char * failedFile = NULL;
    struct callbackInfo info;
    FD_t cfd;
    int urltype;

    if (!files) {
	/* install all files */
	fileCount = 0;
    } else if (!fileCount) {
	/* no files to install */
	return 0;
    }

    info.archiveSize = archiveSize;
    info.notify = notify;
    info.notifyData = notifyData;
    info.specFilePtr = specFile;
    info.h = headerLink(h);
    info.pkgKey = pkgKey;

    if (specFile) *specFile = NULL;

    if (files) {
	map = alloca(sizeof(*map) * fileCount);
	for (i = 0, mappedFiles = 0; i < fileCount; i++) {
	    if (!files[i].install) continue;

	    map[mappedFiles].archivePath = files[i].cpioPath;
#ifdef DYING
	    map[mappedFiles].fsPath = files[i].relativePath;
#else
	    urltype = urlPath(files[i].relativePath, &map[mappedFiles].fsPath);
#endif
	    map[mappedFiles].finalMode = files[i].mode;
	    map[mappedFiles].finalUid = files[i].uid;
	    map[mappedFiles].finalGid = files[i].gid;
	    map[mappedFiles].mapFlags = CPIO_MAP_PATH | CPIO_MAP_MODE | 
					CPIO_MAP_UID | CPIO_MAP_GID;
	    mappedFiles++;
	}

	qsort(map, mappedFiles, sizeof(*map), cpioFileMapCmp);
    }

    if (notify)
	(void)notify(h, RPMCALLBACK_INST_PROGRESS, 0, archiveSize, pkgKey, 
	       notifyData);

    cfd = Fdopen(fdDup(Fileno(fd)), "r.gzdio");
    rc = cpioInstallArchive(cfd, map, mappedFiles, 
		    ((notify && archiveSize) || specFile) ? callback : NULL, 
		    &info, &failedFile);
    Fclose(cfd);
    headerFree(info.h);

    if (rc) {
	/* this would probably be a good place to check if disk space
	   was used up - if so, we should return a different error */
	rpmError(RPMERR_CPIO, _("unpacking of archive failed%s%s: %s"), 
		(failedFile != NULL ? _(" on file ") : ""),
		(failedFile != NULL ? failedFile : ""),
		cpioStrerror(rc));
	rc = 1;
    } else if (notify) {
	if (archiveSize)
	    (void)notify(h, RPMCALLBACK_INST_PROGRESS, archiveSize, archiveSize,
	       pkgKey, notifyData);
	else
	    (void)notify(h, RPMCALLBACK_INST_PROGRESS, 100, 100,
		pkgKey, notifyData);
	rc = 0;
    }

    if (failedFile)
	xfree(failedFile);

    return rc;
}

/* 0 success */
/* 1 bad magic */
/* 2 error */
static int installSources(Header h, const char * rootdir, FD_t fd, 
			  const char ** specFilePtr, rpmCallbackFunction notify,
			  void * notifyData)
{
    const char * specFile = NULL;
    int specFileIndex = -1;
    const char * realSourceDir = NULL;
    const char * realSpecDir = NULL;
    char * instSpecFile, * correctSpecFile;
    int fileCount = 0;
    uint_32 * archiveSizePtr = NULL;
    struct fileMemory *fileMem = NULL;
    struct fileInfo * files = NULL;
    int i;
    const char * currDir = NULL;
    uid_t currUid = getuid();
    gid_t currGid = getgid();
    struct stat st;
    int rc = 0;

    rpmMessage(RPMMESS_DEBUG, _("installing a source package\n"));

    realSourceDir = rpmGenPath(rootdir, "%{_sourcedir}", "");
    if ((rc = Stat(realSourceDir, &st)) < 0) {
	switch (errno) {
	case ENOENT:
	    /* XXX this will only create last component of directory path */
	    if (Mkdir(realSourceDir, 0755) == 0)
		break;
	    /*@fallthrough@*/
	default:
	    rpmError(RPMERR_CREATE, _("cannot create %s: %s"),
		realSourceDir, strerror(errno));
	    rc = 2;
	    goto exit;
	    /*@notreached@*/ break;
	}
    }
    if ((rc = Access(realSourceDir, W_OK))) {
	rpmError(RPMERR_CREATE, _("cannot write to %s"), realSourceDir);
	rc = 2;
	goto exit;
    }
    rpmMessage(RPMMESS_DEBUG, _("sources in: %s\n"), realSourceDir);

    realSpecDir = rpmGenPath(rootdir, "%{_specdir}", "");
    if ((rc = Stat(realSpecDir, &st)) < 0) {
	switch (errno) {
	case ENOENT:
	    /* XXX this will only create last component of directory path */
	    if (mkdir(realSpecDir, 0755) == 0)
		break;
	    /*@fallthrough@*/
	default:
	    rpmError(RPMERR_CREATE, _("cannot create %s"), realSpecDir);
	    rc = 2;
	    goto exit;
	    /*@notreached@*/ break;
	}
    }
    if ((rc = Access(realSpecDir, W_OK))) {
	rpmError(RPMERR_CREATE, _("cannot write to %s"), realSpecDir);
	rc = 2;
	goto exit;
    }
    rpmMessage(RPMMESS_DEBUG, _("spec file in: %s\n"), realSpecDir);

    if (h != NULL && headerIsEntry(h, RPMTAG_COMPFILELIST)) {
	/* we can't remap v1 packages */
	assembleFileList(h, &fileMem, &fileCount, &files, 0, NULL);

	for (i = 0; i < fileCount; i++) {
	    files[i].relativePath = files[i].relativePath;
	    files[i].uid = currUid;
	    files[i].gid = currGid;
	}

	if (headerIsEntry(h, RPMTAG_COOKIE))
	    for (i = 0; i < fileCount; i++)
		if (files[i].flags & RPMFILE_SPECFILE) break;

	if (i == fileCount) {
	    /* find the spec file by name */
	    for (i = 0; i < fileCount; i++) {
		const char *chptr;
		chptr = files[i].cpioPath + strlen(files[i].cpioPath) - 5;
		if (!strcmp(chptr, ".spec")) break;
	    }
	}

	if (i < fileCount) {
	    char *t = alloca(strlen(realSpecDir) + 
			 strlen(files[i].cpioPath) + 5);
	    strcpy(t, realSpecDir);
	    strcat(t, "/");
	    strcat(t, files[i].cpioPath);
	    files[i].relativePath = t;
	    specFileIndex = i;
	} else {
	    rpmError(RPMERR_NOSPEC, _("source package contains no .spec file"));
	    rc = 2;
	    goto exit;
	}
    }

    if (notify) {
	(void)notify(h, RPMCALLBACK_INST_START, 0, 0, NULL, notifyData);
    }

    currDir = currentDirectory();

    if (!headerGetEntry(h, RPMTAG_ARCHIVESIZE, NULL,
			    (void **) &archiveSizePtr, NULL))
	archiveSizePtr = NULL;

    chdir(realSourceDir);
    if (installArchive(fd, fileCount > 0 ? files : NULL,
			  fileCount, notify, notifyData, NULL, h,
			  specFileIndex >=0 ? NULL : &specFile, 
			  archiveSizePtr ? *archiveSizePtr : 0)) {
	rc = 2;
	goto exit;
    }
    chdir(currDir);

    if (specFileIndex == -1) {
	if (specFile == NULL) {
	    rpmError(RPMERR_NOSPEC, _("source package contains no .spec file"));
	    rc = 1;
	    goto exit;
	}

	/* This logic doesn't work if realSpecDir and realSourceDir are on
	   different filesystems, but we only do this on v1 source packages
	   so I don't really care much. */
	instSpecFile = alloca(strlen(realSourceDir) + strlen(specFile) + 2);
	strcpy(instSpecFile, realSourceDir);
	strcat(instSpecFile, "/");
	strcat(instSpecFile, specFile);

	correctSpecFile = alloca(strlen(realSpecDir) + strlen(specFile) + 2);
	strcpy(correctSpecFile, realSpecDir);
	strcat(correctSpecFile, "/");
	strcat(correctSpecFile, specFile);

	xfree(specFile);

	if (strcmp(instSpecFile, correctSpecFile)) {
	    rpmMessage(RPMMESS_DEBUG, 
		    _("renaming %s to %s\n"), instSpecFile, correctSpecFile);
	    if ((rc = Rename(instSpecFile, correctSpecFile))) {
		rpmError(RPMERR_RENAME, _("rename of %s to %s failed: %s"),
			instSpecFile, correctSpecFile, strerror(errno));
		rc = 2;
		goto exit;
	    }
	}

	if (specFilePtr)
	    *specFilePtr = xstrdup(correctSpecFile);
    } else {
	if (specFilePtr)
	    *specFilePtr = xstrdup(files[specFileIndex].relativePath);
    }
    rc = 0;

exit:
    if (fileMem)	freeFileMemory(fileMem);
    if (currDir)	xfree(currDir);
    if (realSpecDir)	xfree(realSpecDir);
    if (realSourceDir)	xfree(realSourceDir);
    return rc;
}

int rpmVersionCompare(Header first, Header second)
{
    char * one, * two;
    int_32 * epochOne, * epochTwo;
    int rc;

    if (!headerGetEntry(first, RPMTAG_EPOCH, NULL, (void **) &epochOne, NULL))
	epochOne = NULL;
    if (!headerGetEntry(second, RPMTAG_EPOCH, NULL, (void **) &epochTwo, 
			NULL))
	epochTwo = NULL;

    if (epochOne && !epochTwo) 
	return 1;
    else if (!epochOne && epochTwo) 
	return -1;
    else if (epochOne && epochTwo) {
	if (*epochOne < *epochTwo)
	    return -1;
	else if (*epochOne > *epochTwo)
	    return 1;
    }
	
    headerGetEntry(first, RPMTAG_VERSION, NULL, (void **) &one, NULL);
    headerGetEntry(second, RPMTAG_VERSION, NULL, (void **) &two, NULL);

    rc = rpmvercmp(one, two);
    if (rc)
	return rc;

    headerGetEntry(first, RPMTAG_RELEASE, NULL, (void **) &one, NULL);
    headerGetEntry(second, RPMTAG_RELEASE, NULL, (void **) &two, NULL);
    
    return rpmvercmp(one, two);
}

const char *const fileActionString(enum fileActions a)
{
    switch (a) {
      case FA_UNKNOWN: return "unknown";
      case FA_CREATE: return "create";
      case FA_BACKUP: return "backup";
      case FA_SAVE: return "save";
      case FA_SKIP: return "skip";
      case FA_ALTNAME: return "altname";
      case FA_REMOVE: return "remove";
      case FA_SKIPNSTATE: return "skipnstate";
      case FA_SKIPNETSHARED: return "skipnetshared";
    }
    /*@notreached@*/
    return "???";
}

/* 0 success */
/* 1 bad magic */
/* 2 error */
int rpmInstallSourcePackage(const char * rootdir, FD_t fd, 
			    const char ** specFile, rpmCallbackFunction notify, 
			    void * notifyData, char ** cookie)
{
    int rc, isSource;
    Header h;
    int major, minor;

    rc = rpmReadPackageHeader(fd, &h, &isSource, &major, &minor);
    if (rc) return rc;

    if (!isSource) {
	rpmError(RPMERR_NOTSRPM, _("source package expected, binary found"));
	return 2;
    }

#if defined(ENABLE_V1_PACKAGES)
    if (major == 1) {
	notify = NULL;
	h = NULL;
    }
#endif	/* ENABLE_V1_PACKAGES */

    if (cookie) {
	*cookie = NULL;
	if (h != NULL &&
	   headerGetEntry(h, RPMTAG_COOKIE, NULL, (void **) cookie, NULL)) {
	    *cookie = xstrdup(*cookie);
	}
    }

    rpmInstallLoadMacros(h);
    
    rc = installSources(h, rootdir, fd, specFile, notify, notifyData);
    if (h != NULL) {
 	headerFree(h);
    }
 
    return rc;
}

/* 0 success */
/* 1 bad magic */
/* 2 error */
int installBinaryPackage(const char * rootdir, rpmdb db, FD_t fd, Header h,
		         int flags, rpmCallbackFunction notify, 
			 void * notifyData, const void * pkgKey, 
			 enum fileActions * actions, 
			 struct sharedFileInfo * sharedList, FD_t scriptFd)
{
    int rc;
    const char * name, * version, * release;
    int fileCount = 0;
    int type, count;
    struct fileInfo * files;
    char * fileStates = NULL;
    int i;
    int otherOffset = 0;
    dbiIndexSet matches;
    int scriptArg;
    int stripSize = 1;		/* strip at least first / for cpio */
    struct fileMemory *fileMem = NULL;
    char * currDir = NULL;

    if (flags & RPMTRANS_FLAG_JUSTDB)
	flags |= RPMTRANS_FLAG_NOSCRIPTS;

    headerNVR(h, &name, &version, &release);

    rpmMessage(RPMMESS_DEBUG, _("package: %s-%s-%s files test = %d\n"), 
		name, version, release, flags & RPMTRANS_FLAG_TEST);

    rc = rpmdbFindPackage(db, name, &matches);
    switch (rc) {
    case -1:
	rc = 2;
	goto exit;
	/*@notreached@*/ break;
    case 0:
	scriptArg = dbiIndexSetCount(matches) + 1;
	dbiFreeIndexRecord(matches);
	break;
    default:
 	scriptArg = 1;
	break;
    }

    if (!rpmdbFindByHeader(db, h, &matches)) {
	otherOffset = dbiIndexRecordOffset(matches, 0);
	dbiFreeIndexRecord(matches);
    }

    if (rootdir) {
	/* this loads all of the name services libraries, in case we
	   don't have access to them in the chroot() */
	(void)getpwnam("root");
	endpwent();

	{   const char *s = currentDirectory();
	    currDir = alloca(strlen(s) + 1);
	    strcpy(currDir, s);
	    xfree(s);
	}

	chdir("/");
	/*@-unrecog@*/ chroot(rootdir); /*@=unrecog@*/
    }

    if (!(flags & RPMTRANS_FLAG_JUSTDB) && headerIsEntry(h, RPMTAG_COMPFILELIST)) {
	const char * defaultPrefix;
	/* old format relocateable packages need the entire default
	   prefix stripped to form the cpio list, while all other packages 
	   need the leading / stripped */
	if (headerGetEntry(h, RPMTAG_DEFAULTPREFIX, NULL, (void **)
				  &defaultPrefix, NULL)) {
	    stripSize = strlen(defaultPrefix) + 1;
	} else {
	    stripSize = 1;
	}

	if (assembleFileList(h, &fileMem, &fileCount, &files, stripSize,
			     actions)) {
	    rc = 2;
	    goto exit;
	}
    } else {
	files = NULL;
    }
    
    if (flags & RPMTRANS_FLAG_TEST) {
	rpmMessage(RPMMESS_DEBUG, _("stopping install as we're running --test\n"));
	rc = 0;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("running preinstall script (if any)\n"));
    if (runInstScript("/", h, RPMTAG_PREIN, RPMTAG_PREINPROG, scriptArg, 
		      flags & RPMTRANS_FLAG_NOSCRIPTS, scriptFd)) {
	rc = 2;
	goto exit;
    }

    if (files) {
	setFileOwners(h, files, fileCount);

	for (i = 0; i < fileCount; i++) {
	    char * ext;
	    char * newpath;

	    ext = NULL;

	    switch (files[i].action) {
	      case FA_BACKUP:
		ext = ".rpmorig";
		break;

	      case FA_ALTNAME:
		newpath = alloca(strlen(files[i].relativePath) + 20);
		strcpy(newpath, files[i].relativePath);
		strcat(newpath, ".rpmnew");
		rpmError(RPMMESS_ALTNAME, _("warning: %s created as %s"),
			files[i].relativePath, newpath);
		files[i].relativePath = newpath;
		break;

	      case FA_SAVE:
		ext = ".rpmsave";
		break;

	      case FA_CREATE:
		break;

	      case FA_SKIP:
		files[i].install = 0;
		break;

	      case FA_SKIPNSTATE:
		files[i].state = RPMFILE_STATE_NOTINSTALLED;
		files[i].install = 0;
		break;

	      case FA_SKIPNETSHARED:
		files[i].state = RPMFILE_STATE_NETSHARED;
		files[i].install = 0;
		break;

	      case FA_UNKNOWN:
	      case FA_REMOVE:
		files[i].install = 0;
		break;
	    }

	    if (ext) {
		newpath = alloca(strlen(files[i].relativePath) + 20);
		strcpy(newpath, files[i].relativePath);
		strcat(newpath, ext);
		rpmError(RPMMESS_BACKUP, _("warning: %s saved as %s"), 
			files[i].relativePath, newpath);

		if (rename(files[i].relativePath, newpath)) {
		    rpmError(RPMERR_RENAME, _("rename of %s to %s failed: %s"),
			  files[i].relativePath, newpath, strerror(errno));
		    rc = 2;
		    goto exit;
		}
	    }
	}

	{   uint_32 * archiveSizePtr;

	    if (!headerGetEntry(h, RPMTAG_ARCHIVESIZE, &type, 
				(void **) &archiveSizePtr, &count))
	    archiveSizePtr = NULL;

	    if (notify) {
		(void)notify(h, RPMCALLBACK_INST_START, 0, 0,
		    pkgKey, notifyData);
	    }

	    /* the file pointer for fd is pointing at the cpio archive */
	    if (installArchive(fd, files, fileCount, notify, notifyData, pkgKey,
			h, NULL, archiveSizePtr ? *archiveSizePtr : 0)) {
		rc = 2;
		goto exit;
	    }
	}

	fileStates = xmalloc(sizeof(*fileStates) * fileCount);
	for (i = 0; i < fileCount; i++)
	    fileStates[i] = files[i].state;

	headerAddEntry(h, RPMTAG_FILESTATES, RPM_CHAR_TYPE, fileStates, 
			fileCount);

	free(fileStates);
	if (fileMem) freeFileMemory(fileMem);
	fileMem = NULL;
    } else if (flags & RPMTRANS_FLAG_JUSTDB) {
	if (headerGetEntry(h, RPMTAG_COMPFILELIST, NULL, NULL, &fileCount)) {
	    fileStates = xmalloc(sizeof(*fileStates) * fileCount);
	    memset(fileStates, RPMFILE_STATE_NORMAL, fileCount);
	    headerAddEntry(h, RPMTAG_FILESTATES, RPM_CHAR_TYPE, fileStates, 
			    fileCount);
	    free(fileStates);
	}
    }

    {	int_32 installTime = time(NULL);
	headerAddEntry(h, RPMTAG_INSTALLTIME, RPM_INT32_TYPE, &installTime, 1);
    }

    if (rootdir) {
	/*@-unrecog@*/ chroot("."); /*@=unrecog@*/
	chdir(currDir);
	currDir = NULL;
    }

    trimChangelog(h);

    /* if this package has already been installed, remove it from the database
       before adding the new one */
    if (otherOffset) {
        rpmdbRemove(db, otherOffset, 1);
    }

    if (rpmdbAdd(db, h)) {
	rc = 2;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("running postinstall scripts (if any)\n"));

    if (runInstScript(rootdir, h, RPMTAG_POSTIN, RPMTAG_POSTINPROG, scriptArg,
		      flags & RPMTRANS_FLAG_NOSCRIPTS, scriptFd)) {
	rc = 2;
	goto exit;
    }

    if (!(flags & RPMTRANS_FLAG_NOTRIGGERS)) {
	/* Run triggers this package sets off */
	if (runTriggers(rootdir, db, RPMSENSE_TRIGGERIN, h, 0, scriptFd)) {
	    rc = 2;
	    goto exit;
	}

	/* Run triggers in this package which are set off by other things in
	   the database. */
	if (runImmedTriggers(rootdir, db, RPMSENSE_TRIGGERIN, h, 0, scriptFd)) {
	    rc = 2;
	    goto exit;
	}
    }

    if (sharedList) 
	markReplacedFiles(db, sharedList);

    rc = 0;

exit:
    if (rootdir && currDir) {
	/*@-unrecog@*/ chroot("."); /*@=unrecog@*/
	chdir(currDir);
    }
    if (fileMem) freeFileMemory(fileMem);
    if (rc) {
	headerFree(h);
    }
    return rc;
}
