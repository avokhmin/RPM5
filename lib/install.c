/** \ingroup rpmtrans payload
 * \file lib/install.c
 */

#include "system.h"

#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>

#include "cpio.h"
#include "install.h"
#include "depends.h"
#include "misc.h"
#include "debug.h"

/*@access Header@*/		/* XXX compared with NULL */

/**
 * Private data for cpio callback.
 */
struct callbackInfo {
    unsigned long archiveSize;
    rpmCallbackFunction notify;
    const char ** specFilePtr;
    Header h;
    rpmCallbackData notifyData;
    const void * pkgKey;
};

/**
 * Keeps track of memory allocated while accessing header tags.
 */
struct fileMemory {
/*@owned@*/ const char ** names;
/*@owned@*/ const char ** cpioNames;
/*@owned@*/ const char ** md5sums;
/*@owned@*/ struct fileInfo * files;
};

struct fileInfo {
/*@dependent@*/ const char * cpioPath;
/*@dependent@*/ const char * relativePath;	/* relative to root */
/*@dependent@*/ const char * md5sum;
    uid_t uid;
    gid_t gid;
    uint_32 flags;
    uint_32 size;
    mode_t mode;
    char state;
    enum fileAction_e action;
    int install;
} ;

/* XXX add more tags */
/**
 * Macros to be defined from per-header tag values.
 */
static struct tagMacro {
	const char *	macroname;	/*!< Macro name to define. */
	int		tag;		/*!< Header tag to use for value. */
} tagMacros[] = {
	{ "name",	RPMTAG_NAME },
	{ "version",	RPMTAG_VERSION },
	{ "release",	RPMTAG_RELEASE },
#if 0
	{ "epoch",	RPMTAG_EPOCH },
#endif
	{ NULL, 0 }
};

/**
 * Define per-header macros.
 * @param h		header
 * @return		0 always
 */
static int rpmInstallLoadMacros(Header h)
{
    struct tagMacro *tagm;
    union {
	const char * ptr;
	int_32 i32;
    } body;
    char numbuf[32];
    int type;

    for (tagm = tagMacros; tagm->macroname != NULL; tagm++) {
	if (!headerGetEntry(h, tagm->tag, &type, (void **) &body, NULL))
	    continue;
	switch (type) {
	case RPM_INT32_TYPE:
	    sprintf(numbuf, "%d", body.i32);
	    addMacro(NULL, tagm->macroname, NULL, numbuf, -1);
	    break;
	case RPM_STRING_TYPE:
	    addMacro(NULL, tagm->macroname, NULL, body.ptr, -1);
	    break;
	}
    }
    return 0;
}

/**
 * Create memory used to access header.
 * @return		pointer to memory
 */
static /*@only@*/ struct fileMemory *newFileMemory(void)
{
    struct fileMemory *fileMem = xmalloc(sizeof(*fileMem));
    fileMem->files = NULL;
    fileMem->names = NULL;
    fileMem->cpioNames = NULL;
    fileMem->md5sums = NULL;
    return fileMem;
}

/**
 * Destroy memory used to access header.
 * @param fileMem	pointer to memory
 */
static void freeFileMemory( /*@only@*/ struct fileMemory *fileMem)
{
    if (fileMem->files) free(fileMem->files);
    if (fileMem->names) free(fileMem->names);
    if (fileMem->cpioNames) free(fileMem->cpioNames);
    if (fileMem->md5sums) free(fileMem->md5sums);
    free(fileMem);
}

/* files should not be preallocated */
/**
 * Build file information array.
 * @param h		header
 * @retval memPtr	address of allocated memory from header access
 * @retval fileCountPtr	address of install file count
 * @retval files	address of install file information
 * @param stripPrefixLength no. bytes of file prefix to skip
 * @param actions	array of file dispositions
 * @return		0 always
 */
static int assembleFileList(Header h, /*@out@*/ struct fileMemory ** memPtr,
	 /*@out@*/ int * fileCountPtr, /*@out@*/ struct fileInfo ** filesPtr,
	 int stripPrefixLength, enum fileAction_e * actions)
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

    if (!headerIsEntry(h, RPMTAG_BASENAMES)) return 0;

    rpmBuildFileList(h, &mem->names, fileCountPtr);

    if (headerIsEntry(h, RPMTAG_ORIGBASENAMES)) {
	buildOrigFileList(h, &mem->cpioNames, fileCountPtr);
    } else {
	rpmBuildFileList(h, &mem->cpioNames, fileCountPtr);
    }

    fileCount = *fileCountPtr;

    files = *filesPtr = mem->files = xcalloc(fileCount, sizeof(*mem->files));

    headerGetEntry(h, RPMTAG_FILEMD5S, NULL, (void **) &mem->md5sums, NULL);
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
	file->md5sum = mem->md5sums[i];
	file->mode = fileModes[i];
	file->size = fileSizes[i];
	file->flags = fileFlags[i];

	rpmMessage(RPMMESS_DEBUG, _("   file: %s action: %s\n"),
		    file->relativePath, fileActionString(file->action));
    }

    return 0;
}

/**
 * Localize user/group id's.
 * @param h		header
 * @param files		install file information
 * @param fileCount	install file count
 */
static void setFileOwners(Header h, struct fileInfo * files, int fileCount)
{
    char ** fileOwners;
    char ** fileGroups;
    int i;

    headerGetEntry(h, RPMTAG_FILEUSERNAME, NULL, (void **) &fileOwners, NULL);
    headerGetEntry(h, RPMTAG_FILEGROUPNAME, NULL, (void **) &fileGroups, NULL);

    for (i = 0; i < fileCount; i++) {
	if (unameToUid(fileOwners[i], &files[i].uid)) {
	    rpmMessage(RPMMESS_WARNING,
		_("user %s does not exist - using root\n"), fileOwners[i]);
	    files[i].uid = 0;
	    /* turn off the suid bit */
	    files[i].mode &= ~S_ISUID;
	}

	if (gnameToGid(fileGroups[i], &files[i].gid)) {
	    rpmMessage(RPMMESS_WARNING,
		_("group %s does not exist - using root\n"), fileGroups[i]);
	    files[i].gid = 0;
	    /* turn off the sgid bit */
	    files[i].mode &= ~S_ISGID;
	}
    }

    free(fileOwners);
    free(fileGroups);
}

#ifdef DYING
/**
 * Truncate header changelog tag to configurable limit before installing.
 * @param h		header
 * @return		none
 */
static void trimChangelog(Header h)
{
    int * times;
    char ** names, ** texts;
    long numToKeep = rpmExpandNumeric(
		"%{?_instchangelog:%{_instchagelog}}%{!?_instchangelog:5}");
    char * end;
    int count;

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
#endif	/* DYING */

/**
 * Copy file data from h to newH.
 * @param h		header from
 * @param newH		header to
 * @param actions	array of file dispositions
 * @return		0 on success, 1 on failure
 */
static int mergeFiles(Header h, Header newH, enum fileAction_e * actions)
{
    int i, j, k, fileCount;
    int_32 type, count, dirNamesCount, dirCount;
    void * data, * newdata;
    int_32 * dirIndexes, * newDirIndexes;
    uint_32 * fileSizes, fileSize;
    char ** dirNames, ** newDirNames;
    static int_32 mergeTags[] = {
	RPMTAG_FILESIZES,
	RPMTAG_FILESTATES,
	RPMTAG_FILEMODES,
	RPMTAG_FILERDEVS,
	RPMTAG_FILEMTIMES,
	RPMTAG_FILEMD5S,
	RPMTAG_FILELINKTOS,
	RPMTAG_FILEFLAGS,
	RPMTAG_FILEUSERNAME,
	RPMTAG_FILEGROUPNAME,
	RPMTAG_FILEVERIFYFLAGS,
	RPMTAG_FILEDEVICES,
	RPMTAG_FILEINODES,
	RPMTAG_FILELANGS,
	RPMTAG_BASENAMES,
	0,
    };
    static int_32 requireTags[] = {
	RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS,
	RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS,
	RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS
    };

    headerGetEntry(h, RPMTAG_SIZE, NULL, (void **) &fileSizes, NULL);
    fileSize = *fileSizes;
    headerGetEntry(newH, RPMTAG_FILESIZES, NULL, (void **) &fileSizes, &count);
    for (i = 0, fileCount = 0; i < count; i++)
	if (actions[i] != FA_SKIPMULTILIB) {
	    fileCount++;
	    fileSize += fileSizes[i];
	}
    headerModifyEntry(h, RPMTAG_SIZE, RPM_INT32_TYPE, &fileSize, 1);
    for (i = 0; mergeTags[i]; i++) {
        if (!headerGetEntryMinMemory(newH, mergeTags[i], &type,
				    (const void **) &data, &count))
	    continue;
	switch (type) {
	case RPM_CHAR_TYPE:
	case RPM_INT8_TYPE:
	    newdata = xmalloc(fileCount * sizeof(int_8));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
			((int_8 *) newdata)[k++] = ((int_8 *) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata,
				       fileCount);
	    free (newdata);
	    break;
	case RPM_INT16_TYPE:
	    newdata = xmalloc(fileCount * sizeof(int_16));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((int_16 *) newdata)[k++] = ((int_16 *) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fileCount);
	    free (newdata);
	    break;
	case RPM_INT32_TYPE:
	    newdata = xmalloc(fileCount * sizeof(int_32));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((int_32 *) newdata)[k++] = ((int_32 *) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fileCount);
	    free (newdata);
	    break;
	case RPM_STRING_ARRAY_TYPE:
	    newdata = xmalloc(fileCount * sizeof(char *));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((char **) newdata)[k++] = ((char **) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fileCount);
	    free (newdata);
	    free (data);
	    break;
	default:
	    rpmError(RPMERR_DATATYPE, _("Data type %d not supported\n"),
			(int) type);
	    return 1;
	    /*@notreached@*/ break;
	}
    }
    headerGetEntry(newH, RPMTAG_DIRINDEXES, NULL, (void **) &newDirIndexes,
		   &count);
    headerGetEntryMinMemory(newH, RPMTAG_DIRNAMES, NULL,
			    (const void **) &newDirNames, NULL);
    headerGetEntry(h, RPMTAG_DIRINDEXES, NULL, (void **) &dirIndexes, NULL);
    headerGetEntryMinMemory(h, RPMTAG_DIRNAMES, NULL, (const void **) &data,
			    &dirNamesCount);

    dirNames = xcalloc(dirNamesCount + fileCount, sizeof(char *));
    for (i = 0; i < dirNamesCount; i++)
	dirNames[i] = ((char **) data)[i];
    dirCount = dirNamesCount;
    newdata = xmalloc(fileCount * sizeof(int_32));
    for (i = 0, k = 0; i < count; i++) {
	if (actions[i] == FA_SKIPMULTILIB)
	    continue;
	for (j = 0; j < dirCount; j++)
	    if (!strcmp(dirNames[j], newDirNames[newDirIndexes[i]]))
		break;
	if (j == dirCount)
	    dirNames[dirCount++] = newDirNames[newDirIndexes[i]];
	((int_32 *) newdata)[k++] = j;
    }
    headerAddOrAppendEntry(h, RPMTAG_DIRINDEXES, RPM_INT32_TYPE, newdata,
			   fileCount);
    if (dirCount > dirNamesCount)
	headerAddOrAppendEntry(h, RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE,
			       dirNames + dirNamesCount,
			       dirCount - dirNamesCount);
    if (data) free (data);
    if (newDirNames) free (newDirNames);
    free (newdata);
    free (dirNames);

    for (i = 0; i < 9; i += 3) {
	char **Names, **EVR, **newNames, **newEVR;
	uint_32 *Flags, *newFlags;
	int Count = 0, newCount = 0;

	if (!headerGetEntryMinMemory(newH, requireTags[i], NULL,
				    (const void **) &newNames, &newCount))
	    continue;

	headerGetEntryMinMemory(newH, requireTags[i+1], NULL,
				(const void **) &newEVR, NULL);
	headerGetEntry(newH, requireTags[i+2], NULL, (void **) &newFlags, NULL);
	if (headerGetEntryMinMemory(h, requireTags[i], NULL,
				    (const void **) &Names, &Count))
	{
	    headerGetEntryMinMemory(h, requireTags[i+1], NULL,
				    (const void **) &EVR, NULL);
	    headerGetEntry(h, requireTags[i+2], NULL, (void **) &Flags, NULL);
	    for (j = 0; j < newCount; j++)
		for (k = 0; k < Count; k++)
		    if (!strcmp (newNames[j], Names[k])
			&& !strcmp (newEVR[j], EVR[k])
			&& (newFlags[j] & RPMSENSE_SENSEMASK) ==
			   (Flags[k] & RPMSENSE_SENSEMASK))
		    {
			newNames[j] = NULL;
			break;
		    }
	}
	for (j = 0, k = 0; j < newCount; j++) {
	    if (!newNames[j] || !isDependsMULTILIB(newFlags[j]))
		continue;
	    if (j != k) {
		newNames[k] = newNames[j];
		newEVR[k] = newEVR[j];
		newFlags[k] = newFlags[j];
	    }
	    k++;
	}
	if (k) {
	    headerAddOrAppendEntry(h, requireTags[i],
				       RPM_STRING_ARRAY_TYPE, newNames, k);
	    headerAddOrAppendEntry(h, requireTags[i+1],
				       RPM_STRING_ARRAY_TYPE, newEVR, k);
	    headerAddOrAppendEntry(h, requireTags[i+2], RPM_INT32_TYPE,
				       newFlags, k);
	}
    }
    return 0;
}

/**
 * Mark files in database shared with current package as "replaced".
 * @param db		rpm database
 * @param replist	shared file list
 * @return		0 always
 */
static int markReplacedFiles(rpmdb rpmdb, const struct sharedFileInfo * replList)
{
    const struct sharedFileInfo * fileInfo;
    rpmdbMatchIterator mi;
    Header h;
    unsigned int * offsets;
    unsigned int prev;
    int num;

    num = prev = 0;
    for (fileInfo = replList; fileInfo->otherPkg; fileInfo++) {
	if (prev && prev == fileInfo->otherPkg)
	    continue;
	prev = fileInfo->otherPkg;
	num++;
    }
    if (num == 0)
	return 0;

    offsets = alloca(num * sizeof(*offsets));
    num = prev = 0;
    for (fileInfo = replList; fileInfo->otherPkg; fileInfo++) {
	if (prev && prev == fileInfo->otherPkg)
	    continue;
	prev = fileInfo->otherPkg;
	offsets[num++] = fileInfo->otherPkg;
    }

    mi = rpmdbInitIterator(rpmdb, RPMDBI_PACKAGES, NULL, 0);
    rpmdbAppendIterator(mi, offsets, num);

    fileInfo = replList;
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	char * secStates;
	int modified;
	int count;

	modified = 0;

	if (!headerGetEntry(h, RPMTAG_FILESTATES, NULL, (void **)&secStates, &count))
	    continue;
	
	prev = rpmdbGetIteratorOffset(mi);
	num = 0;
	while (fileInfo->otherPkg && fileInfo->otherPkg == prev) {
	    assert(fileInfo->otherFileNum < count);
	    if (secStates[fileInfo->otherFileNum] != RPMFILE_STATE_REPLACED) {
		secStates[fileInfo->otherFileNum] = RPMFILE_STATE_REPLACED;
		if (modified == 0) {
		    /* Modified header will be rewritten. */
		    modified = 1;
		    rpmdbSetIteratorModified(mi, modified);
		}
		num++;
	    }
	    fileInfo++;
	}
    }
    rpmdbFreeIterator(mi);

    return 0;
}

/**
 */
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

/**
 * Setup payload map and install payload archive.
 *
 * @todo Add endian tag so that srpm MD5 sums can ber verified when installed.
 *
 * @param fd		file handle of package (positioned at payload)
 * @param files		files to install (NULL means "all files")
 * @param fileCount	no. files to install
 * @param notify	callback function
 * @param notifyData	callback private data
 * @param pkgKey	package private data (e.g. file name)
 * @param h		header
 * @retval specFile	address of spec file name
 * @param archiveSize	@todo Document.
 * @return		0 on success
 */
static int installArchive(FD_t fd, struct fileInfo * files, int fileCount,
			rpmCallbackFunction notify, rpmCallbackData notifyData,
			const void * pkgKey, Header h,
			/*@out@*/ const char ** specFile, int archiveSize)
{
    int rc, i;
    struct cpioFileMapping * map = NULL;
    int mappedFiles = 0;
    const char * failedFile = NULL;
    struct callbackInfo info;
    char * rpmio_flags;
    FD_t cfd;
    int urltype;
    int saveerrno;

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
	    /* XXX Can't do src rpm MD5 sum verification (yet). */
    /* XXX binary rpms always have RPMTAG_SOURCERPM, source rpms do not */
	    map[mappedFiles].md5sum = headerIsEntry(h, RPMTAG_SOURCERPM)
			? files[i].md5sum : NULL;
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

    /* Retrieve type of payload compression. */
    {	const char * payload_compressor = NULL;
	char * t;

	if (!headerGetEntry(h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    (void **) &payload_compressor, NULL))
	    payload_compressor = "gzip";
	rpmio_flags = t = alloca(sizeof("r.gzdio"));
	*t++ = 'r';
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
    }

    (void) Fflush(fd);
    cfd = Fdopen(fdDup(Fileno(fd)), rpmio_flags);
    rc = cpioInstallArchive(cfd, map, mappedFiles,
		    ((notify && archiveSize) || specFile) ? callback : NULL,
		    &info, &failedFile);
    saveerrno = errno;	/* XXX FIXME: Fclose with libio destroys errno */
    Fclose(cfd);
    headerFree(info.h);

    if (rc) {
	/* this would probably be a good place to check if disk space
	   was used up - if so, we should return a different error */
	errno = saveerrno; /* XXX FIXME: Fclose with libio destroys errno */
	rpmError(RPMERR_CPIO, _("unpacking of archive failed%s%s: %s\n"),
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
	free((void *)failedFile);

    return rc;
}

/**
 * @param h		header
 * @param rootDir	path to top of install tree
 * @param fd		file handle of package (positioned at payload)
 * @retval specFilePtr	address of spec file name
 * @param notify	callback function
 * @param notifyData	callback private data
 * @return		0 on success, 1 on bad magic, 2 on error
 */
static int installSources(Header h, const char * rootDir, FD_t fd,
			const char ** specFilePtr,
			rpmCallbackFunction notify, rpmCallbackData notifyData)
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

    realSourceDir = rpmGenPath(rootDir, "%{_sourcedir}", "");
    if ((rc = Stat(realSourceDir, &st)) < 0) {
	int ut = urlPath(realSourceDir, NULL);
	switch (ut) {
	case URL_IS_PATH:
	case URL_IS_UNKNOWN:
	    if (errno != ENOENT)
		break;
	    /*@fallthrough@*/
	case URL_IS_FTP:
	case URL_IS_HTTP:
	    /* XXX this will only create last component of directory path */
	    rc = Mkdir(realSourceDir, 0755);
	    break;
	case URL_IS_DASH:
	    break;
	}
	if (rc < 0) {
	    rpmError(RPMERR_CREATE, _("cannot create sourcedir %s\n"),
			realSourceDir);
	    rc = 2;
	    goto exit;
	}
    }
    if ((rc = Access(realSourceDir, W_OK))) {
	rpmError(RPMERR_CREATE, _("cannot write to %s\n"), realSourceDir);
	rc = 2;
	goto exit;
    }
    rpmMessage(RPMMESS_DEBUG, _("sources in: %s\n"), realSourceDir);

    realSpecDir = rpmGenPath(rootDir, "%{_specdir}", "");
    if ((rc = Stat(realSpecDir, &st)) < 0) {
	int ut = urlPath(realSpecDir, NULL);
	switch (ut) {
	case URL_IS_PATH:
	case URL_IS_UNKNOWN:
	    if (errno != ENOENT)
		break;
	    /*@fallthrough@*/
	case URL_IS_FTP:
	case URL_IS_HTTP:
	    /* XXX this will only create last component of directory path */
	    rc = Mkdir(realSpecDir, 0755);
	    break;
	case URL_IS_DASH:
	    break;
	}
	if (rc < 0) {
	    rpmError(RPMERR_CREATE, _("cannot create specdir %s\n"),
			realSpecDir);
	    rc = 2;
	    goto exit;
	}
    }
    if ((rc = Access(realSpecDir, W_OK))) {
	rpmError(RPMERR_CREATE, _("cannot write to %s\n"), realSpecDir);
	rc = 2;
	goto exit;
    }
    rpmMessage(RPMMESS_DEBUG, _("spec file in: %s\n"), realSpecDir);

    if (h != NULL && headerIsEntry(h, RPMTAG_BASENAMES)) {
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
	    (void)stpcpy(stpcpy(stpcpy(t,realSpecDir), "/"), files[i].cpioPath);
	    files[i].relativePath = t;
	    specFileIndex = i;
	} else {
	    rpmError(RPMERR_NOSPEC,
		_("source package contains no .spec file\n"));
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

    Chdir(realSourceDir);
    if (installArchive(fd, fileCount > 0 ? files : NULL,
			  fileCount, notify, notifyData, NULL, h,
			  specFileIndex >= 0 ? NULL : &specFile,
			  archiveSizePtr ? *archiveSizePtr : 0)) {
	rc = 2;
	goto exit;
    }
    Chdir(currDir);

    if (specFileIndex == -1) {
	if (specFile == NULL) {
	    rpmError(RPMERR_NOSPEC,
		_("source package contains no .spec file\n"));
	    rc = 1;
	    goto exit;
	}

	/* This logic doesn't work if realSpecDir and realSourceDir are on
	   different filesystems, but we only do this on v1 source packages
	   so I don't really care much. */
	instSpecFile = alloca(strlen(realSourceDir) + strlen(specFile) + 2);
	(void)stpcpy(stpcpy(stpcpy(instSpecFile,realSourceDir), "/"), specFile);

	correctSpecFile = alloca(strlen(realSpecDir) + strlen(specFile) + 2);
	(void)stpcpy(stpcpy(stpcpy(correctSpecFile,realSpecDir), "/"), specFile);

	free((void *)specFile);

	if (strcmp(instSpecFile, correctSpecFile)) {
	    rpmMessage(RPMMESS_DEBUG,
		    _("renaming %s to %s\n"), instSpecFile, correctSpecFile);
	    if ((rc = Rename(instSpecFile, correctSpecFile))) {
		rpmError(RPMERR_RENAME, _("rename of %s to %s failed: %s\n"),
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
    if (currDir)	free((void *)currDir);
    if (realSpecDir)	free((void *)realSpecDir);
    if (realSourceDir)	free((void *)realSourceDir);
    return rc;
}

int rpmVersionCompare(Header first, Header second)
{
    const char * one, * two;
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

/*@obserever@*/ const char *const fileActionString(enum fileAction_e a)
{
    switch (a) {
    case FA_UNKNOWN:	return "unknown";
    case FA_CREATE:	return "create";
    case FA_COPYOUT:    return "copyout";
    case FA_COPYIN:     return "copyin";
    case FA_BACKUP:	return "backup";
    case FA_SAVE:	return "save";
    case FA_SKIP:	return "skip";
    case FA_ALTNAME:	return "altname";
    case FA_ERASE:	return "erase";
    case FA_SKIPNSTATE:	return "skipnstate";
    case FA_SKIPNETSHARED: return "skipnetshared";
    case FA_SKIPMULTILIB: return "skipmultilib";
    default:		return "???";
    }
    /*@notreached@*/
}

int rpmInstallSourcePackage(const char * rootDir, FD_t fd,
			const char ** specFile,
			rpmCallbackFunction notify, rpmCallbackData notifyData,
			char ** cookie)
{
    int rc, isSource;
    Header h;
    int major, minor;

    rc = rpmReadPackageHeader(fd, &h, &isSource, &major, &minor);
    if (rc) return rc;

    if (!isSource) {
	rpmError(RPMERR_NOTSRPM, _("source package expected, binary found\n"));
	return 2;
    }

    if (cookie) {
	*cookie = NULL;
	if (h != NULL &&
	   headerGetEntry(h, RPMTAG_COOKIE, NULL, (void **) cookie, NULL)) {
	    *cookie = xstrdup(*cookie);
	}
    }

    rpmInstallLoadMacros(h);

    rc = installSources(h, rootDir, fd, specFile, notify, notifyData);
    if (h)
 	headerFree(h);

    return rc;
}

int installBinaryPackage(const rpmTransactionSet ts, FD_t fd, Header h,
			const void * pkgKey, enum fileAction_e * actions,
			struct sharedFileInfo * sharedList)
{
    rpmtransFlags transFlags = ts->transFlags;
    int rc;
    const char * name, * version, * release;
    int fileCount = 0;
    int type, count;
    struct fileInfo * files;
    int i;
    Header oldH = NULL;
    int otherOffset = 0;
    int scriptArg;
    int stripSize = 1;		/* strip at least first / for cpio */
    struct fileMemory *fileMem = NULL;

    /* XXX this looks broke, as libraries may need /sbin/ldconfig for example */
    if (transFlags & (RPMTRANS_FLAG_JUSTDB | RPMTRANS_FLAG_MULTILIB))
	transFlags |= RPMTRANS_FLAG_NOSCRIPTS;

    headerNVR(h, &name, &version, &release);

    rpmMessage(RPMMESS_DEBUG, _("package: %s-%s-%s files test = %d\n"),
		name, version, release, transFlags & RPMTRANS_FLAG_TEST);

    if ((scriptArg = rpmdbCountPackages(ts->rpmdb, name)) < 0) {
	rc = 2;
	goto exit;
    }
    scriptArg += 1;

    {	rpmdbMatchIterator mi;
	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, name, 0);
	rpmdbSetIteratorVersion(mi, version);
	rpmdbSetIteratorRelease(mi, release);
	while ((oldH = rpmdbNextIterator(mi))) {
	    otherOffset = rpmdbGetIteratorOffset(mi);
	    oldH = (transFlags & RPMTRANS_FLAG_MULTILIB)
		? headerCopy(oldH) : NULL;
	    break;
	}
	rpmdbFreeIterator(mi);
    }

    if (!(transFlags & RPMTRANS_FLAG_JUSTDB) && headerIsEntry(h, RPMTAG_BASENAMES)) {
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

    if (transFlags & RPMTRANS_FLAG_TEST) {
	rpmMessage(RPMMESS_DEBUG, _("stopping install as we're running --test\n"));
	rc = 0;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("running preinstall script (if any)\n"));

    rc = runInstScript(ts, h, RPMTAG_PREIN, RPMTAG_PREINPROG, scriptArg,
		      transFlags & RPMTRANS_FLAG_NOSCRIPTS);

    if (rc) {
	rc = 2;
	rpmError(RPMERR_SCRIPT,
		_("skipping %s-%s-%s install, %%pre scriptlet failed rc %d\n"),
		name, version, release, rc);
	goto exit;
    }

    if (ts->rootDir) {
	/* this loads all of the name services libraries, in case we
	   don't have access to them in the chroot() */
	(void)getpwnam("root");
	endpwent();

	chdir("/");
	/*@-unrecog@*/ chroot(ts->rootDir); /*@=unrecog@*/
	ts->chrootDone = 1;
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
		(void)stpcpy(stpcpy(newpath, files[i].relativePath), ".rpmnew");
		rpmMessage(RPMMESS_WARNING, _("%s created as %s\n"),
			files[i].relativePath, newpath);
		files[i].relativePath = newpath;
		break;

	      case FA_SAVE:
		ext = ".rpmsave";
		break;

	      case FA_CREATE:
		break;

	      case FA_SKIP:
	      case FA_SKIPMULTILIB:
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
	      case FA_ERASE:
	      case FA_COPYIN:
	      case FA_COPYOUT:
	      default:
		files[i].install = 0;
		break;
	    }

	    if (ext && access(files[i].relativePath, F_OK) == 0) {
		newpath = alloca(strlen(files[i].relativePath) + 20);
		(void)stpcpy(stpcpy(newpath, files[i].relativePath), ext);
		rpmMessage(RPMMESS_WARNING, _("%s saved as %s\n"),
			files[i].relativePath, newpath);

		if (rename(files[i].relativePath, newpath)) {
		    rpmError(RPMERR_RENAME, _("rename of %s to %s failed: %s\n"),
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

	    if (ts->notify) {
		(void)ts->notify(h, RPMCALLBACK_INST_START, 0, 0,
		    pkgKey, ts->notifyData);
	    }

	    /* the file pointer for fd is pointing at the cpio archive */
	    if (installArchive(fd, files, fileCount, ts->notify, ts->notifyData, pkgKey,
			h, NULL, archiveSizePtr ? *archiveSizePtr : 0)) {
		rc = 2;
		goto exit;
	    }
	}

	{  char *fileStates = xmalloc(sizeof(*fileStates) * fileCount);
	    for (i = 0; i < fileCount; i++)
		fileStates[i] = files[i].state;

	    headerAddEntry(h, RPMTAG_FILESTATES, RPM_CHAR_TYPE, fileStates,
			fileCount);

	    free(fileStates);
	}
	if (fileMem) freeFileMemory(fileMem);
	fileMem = NULL;
    } else if (transFlags & RPMTRANS_FLAG_JUSTDB) {
	if (headerGetEntry(h, RPMTAG_BASENAMES, NULL, NULL, &fileCount)) {
	    char * fileStates = xmalloc(sizeof(*fileStates) * fileCount);
	    memset(fileStates, RPMFILE_STATE_NORMAL, fileCount);
	    headerAddEntry(h, RPMTAG_FILESTATES, RPM_CHAR_TYPE, fileStates,
			    fileCount);
	    free(fileStates);
	}
    }

    {	int_32 installTime = time(NULL);
	headerAddEntry(h, RPMTAG_INSTALLTIME, RPM_INT32_TYPE, &installTime, 1);
    }

    if (ts->rootDir) {
	/*@-unrecog@*/ chroot("."); /*@=unrecog@*/
	ts->chrootDone = 0;
	chdir(ts->currDir);
    }

#ifdef	DYING
    trimChangelog(h);
#endif

    /* if this package has already been installed, remove it from the database
       before adding the new one */
    if (otherOffset)
        rpmdbRemove(ts->rpmdb, ts->id, otherOffset);

    if (transFlags & RPMTRANS_FLAG_MULTILIB) {
	uint_32 multiLib, * newMultiLib, * p;

	if (headerGetEntry(h, RPMTAG_MULTILIBS, NULL, (void **) &newMultiLib,
			   NULL)
	    && headerGetEntry(oldH, RPMTAG_MULTILIBS, NULL,
			      (void **) &p, NULL)) {
	    multiLib = *p;
	    multiLib |= *newMultiLib;
	    headerModifyEntry(oldH, RPMTAG_MULTILIBS, RPM_INT32_TYPE,
			      &multiLib, 1);
	}
	if (mergeFiles(oldH, h, actions)) {
	    rc = 2;
	    goto exit;
	}
    }

    if (rpmdbAdd(ts->rpmdb, ts->id, h)) {
	rc = 2;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("running postinstall scripts (if any)\n"));

    if (runInstScript(ts, h, RPMTAG_POSTIN, RPMTAG_POSTINPROG, scriptArg,
		      (transFlags & RPMTRANS_FLAG_NOSCRIPTS))) {
	rc = 2;
	goto exit;
    }

    if (!(transFlags & RPMTRANS_FLAG_NOTRIGGERS)) {
	/* Run triggers this package sets off */
	if (runTriggers(ts, RPMSENSE_TRIGGERIN, h, 0)) {
	    rc = 2;
	    goto exit;
	}

	/*
	 * Run triggers in this package which are set off by other packages in
	 * the database.
	 */
	if (runImmedTriggers(ts, RPMSENSE_TRIGGERIN, h, 0)) {
	    rc = 2;
	    goto exit;
	}
    }

    if (sharedList)
	markReplacedFiles(ts->rpmdb, sharedList);

    rc = 0;

exit:
    if (ts->chrootDone) {
	/*@-unrecog@*/ chroot("."); /*@=unrecog@*/
	chdir(ts->currDir);
	ts->chrootDone = 0;
    }
    if (fileMem)
	freeFileMemory(fileMem);
    if (oldH)
	headerFree(oldH);
    return rc;
}
