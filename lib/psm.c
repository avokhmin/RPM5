/** \ingroup rpmtrans payload
 * \file lib/psm.c
 */

#include "system.h"

#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>

#include "psm.h"
#include "rpmlead.h"		/* writeLead proto */
#include "signature.h"		/* signature constants */
#include "misc.h"
#include "debug.h"

/*@access Header @*/		/* compared with NULL */
/*@access rpmTransactionSet @*/	/* compared with NULL */
/*@access TFI_t @*/		/* compared with NULL */

extern int _fsm_debug;

/**
 * Wrapper to free(3), hides const compilation noise, permit NULL, return NULL.
 * @param this		memory to free
 * @retval		NULL always
 */
static /*@null@*/ void * _free(/*@only@*/ /*@null@*/ const void * this) {
    if (this)   free((void *)this);
    return NULL;
}

void loadFi(Header h, TFI_t fi)
{
    HGE_t hge;
    HFD_t hfd;
    uint_32 * uip;
    int len;
    int rc;
    int i;
    
    if (fi->fsm == NULL)
	fi->fsm = newFSM();

    /* XXX avoid gcc noise on pointer (4th arg) cast(s) */
    hge = (fi->type == TR_ADDED)
	? (HGE_t) headerGetEntryMinMemory : (HGE_t) headerGetEntry;
    fi->hge = hge;

    fi->hfd = hfd = headerFreeData;

    if (h && fi->h == NULL)	fi->h = headerLink(h);

    /* Duplicate name-version-release so that headers can be free'd. */
    hge(fi->h, RPMTAG_NAME, NULL, (void **) &fi->name, NULL);
    fi->name = xstrdup(fi->name);
    hge(fi->h, RPMTAG_VERSION, NULL, (void **) &fi->version, NULL);
    fi->version = xstrdup(fi->version);
    hge(fi->h, RPMTAG_RELEASE, NULL, (void **) &fi->release, NULL);
    fi->release = xstrdup(fi->release);

    /* -1 means not found */
    rc = hge(fi->h, RPMTAG_EPOCH, NULL, (void **) &uip, NULL);
    fi->epoch = (rc ? *uip : -1);
    /* 0 means unknown */
    rc = hge(fi->h, RPMTAG_ARCHIVESIZE, NULL, (void **) &uip, NULL);
    fi->archiveSize = (rc ? *uip : 0);

    if (!hge(fi->h, RPMTAG_BASENAMES, NULL, (void **) &fi->bnl, &fi->fc)) {
	fi->dc = 0;
	fi->fc = 0;
	return;
    }

    hge(fi->h, RPMTAG_DIRINDEXES, NULL, (void **) &fi->dil, NULL);
    hge(fi->h, RPMTAG_DIRNAMES, NULL, (void **) &fi->dnl, &fi->dc);
    hge(fi->h, RPMTAG_FILEMODES, NULL, (void **) &fi->fmodes, NULL);
    hge(fi->h, RPMTAG_FILEFLAGS, NULL, (void **) &fi->fflags, NULL);
    hge(fi->h, RPMTAG_FILESIZES, NULL, (void **) &fi->fsizes, NULL);
    hge(fi->h, RPMTAG_FILESTATES, NULL, (void **) &fi->fstates, NULL);

    fi->action = FA_UNKNOWN;
    fi->flags = 0;

    /* actions is initialized earlier for added packages */
    if (fi->actions == NULL)
	fi->actions = xcalloc(fi->fc, sizeof(*fi->actions));

    switch (fi->type) {
    case TR_ADDED:
	fi->mapflags = CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	hge(fi->h, RPMTAG_FILEMD5S, NULL, (void **) &fi->fmd5s, NULL);
	hge(fi->h, RPMTAG_FILELINKTOS, NULL, (void **) &fi->flinks, NULL);
	hge(fi->h, RPMTAG_FILELANGS, NULL, (void **) &fi->flangs, NULL);
	hge(fi->h, RPMTAG_FILEMTIMES, NULL, (void **) &fi->fmtimes, NULL);

	/* 0 makes for noops */
	fi->replacedSizes = xcalloc(fi->fc, sizeof(*fi->replacedSizes));

	break;
    case TR_REMOVED:
	fi->mapflags = CPIO_MAP_ABSOLUTE | CPIO_MAP_ADDDOT | CPIO_MAP_PATH | CPIO_MAP_MODE;
	hge(fi->h, RPMTAG_FILEMD5S, NULL, (void **) &fi->fmd5s, NULL);
	hge(fi->h, RPMTAG_FILELINKTOS, NULL, (void **) &fi->flinks, NULL);
	fi->fsizes = memcpy(xmalloc(fi->fc * sizeof(*fi->fsizes)),
				fi->fsizes, fi->fc * sizeof(*fi->fsizes));
	fi->fflags = memcpy(xmalloc(fi->fc * sizeof(*fi->fflags)),
				fi->fflags, fi->fc * sizeof(*fi->fflags));
	fi->fmodes = memcpy(xmalloc(fi->fc * sizeof(*fi->fmodes)),
				fi->fmodes, fi->fc * sizeof(*fi->fmodes));
	/* XXX there's a tedious segfault here for some version(s) of rpm */
	if (fi->fstates)
	    fi->fstates = memcpy(xmalloc(fi->fc * sizeof(*fi->fstates)),
				fi->fstates, fi->fc * sizeof(*fi->fstates));
	else
	    fi->fstates = xcalloc(1, fi->fc * sizeof(*fi->fstates));
	fi->dil = memcpy(xmalloc(fi->fc * sizeof(*fi->dil)),
				fi->dil, fi->fc * sizeof(*fi->dil));
	headerFree(fi->h);
	fi->h = NULL;
	break;
    }

    fi->dnlmax = -1;
    for (i = 0; i < fi->dc; i++) {
	if ((len = strlen(fi->dnl[i])) > fi->dnlmax)
	    fi->dnlmax = len;
    }

    fi->bnlmax = -1;
    for (i = 0; i < fi->fc; i++) {
	if ((len = strlen(fi->bnl[i])) > fi->bnlmax)
	    fi->bnlmax = len;
    }

    fi->dperms = 0755;
    fi->fperms = 0644;

    return;
}

void freeFi(TFI_t fi)
{
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);

    fi->name = _free(fi->name);
    fi->version = _free(fi->version);
    fi->release = _free(fi->release);
    fi->actions = _free(fi->actions);
    fi->replacedSizes = _free(fi->replacedSizes);
    fi->replaced = _free(fi->replaced);

    fi->bnl = hfd(fi->bnl, -1);
    fi->dnl = hfd(fi->dnl, -1);
    fi->obnl = hfd(fi->obnl, -1);
    fi->odnl = hfd(fi->odnl, -1);
    fi->flinks = hfd(fi->flinks, -1);
    fi->fmd5s = hfd(fi->fmd5s, -1);
    fi->fuser = hfd(fi->fuser, -1);
    fi->fgroup = hfd(fi->fgroup, -1);
    fi->flangs = hfd(fi->flangs, -1);

    fi->apath = _free(fi->apath);
    fi->fuids = _free(fi->fuids);
    fi->fgids = _free(fi->fgids);
    fi->fmapflags = _free(fi->fmapflags);

    fi->fsm = freeFSM(fi->fsm);

    switch (fi->type) {
    case TR_ADDED:
	    break;
    case TR_REMOVED:
	fi->fsizes = hfd(fi->fsizes, -1);
	fi->fflags = hfd(fi->fflags, -1);
	fi->fmodes = hfd(fi->fmodes, -1);
	fi->fstates = hfd(fi->fstates, -1);
	fi->dil = hfd(fi->dil, -1);
	break;
    }
    if (fi->h) {
	headerFree(fi->h); fi->h = NULL;
    }
}

/*@observer@*/ const char *const fiTypeString(TFI_t fi) {
    switch(fi->type) {
    case TR_ADDED:	return " install";
    case TR_REMOVED:	return "   erase";
    default:		return "???";
    }
    /*@noteached@*/
}

/*@obserever@*/ const char *const fileActionString(fileAction a)
{
    switch (a) {
    case FA_UNKNOWN:	return "unknown";
    case FA_CREATE:	return "create";
    case FA_COPYOUT:	return "copyout";
    case FA_COPYIN:	return "copyin";
    case FA_BACKUP:	return "backup";
    case FA_SAVE:	return "save";
    case FA_SKIP:	return "skip";
    case FA_ALTNAME:	return "altname";
    case FA_ERASE:	return "erase";
    case FA_SKIPNSTATE: return "skipnstate";
    case FA_SKIPNETSHARED: return "skipnetshared";
    case FA_SKIPMULTILIB: return "skipmultilib";
    default:		return "???";
    }
    /*@notreached@*/
}

/**
 * Macros to be defined from per-header tag values.
 * @todo Should other macros be added from header when installing a package?
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
static int rpmInstallLoadMacros(TFI_t fi, Header h)
{
    HGE_t hge = (HGE_t)fi->hge;
    struct tagMacro *tagm;
    union {
	const char * ptr;
	int_32 * i32p;
    } body;
    char numbuf[32];
    int_32 type;

    for (tagm = tagMacros; tagm->macroname != NULL; tagm++) {
	if (!hge(h, tagm->tag, &type, (void **) &body, NULL))
	    continue;
	switch (type) {
	case RPM_INT32_TYPE:
	    sprintf(numbuf, "%d", *body.i32p);
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
 * Localize user/group id's.
 * @param fi		transaction element file info
 */
static void setFileOwners(TFI_t fi)
{
    uid_t uid;
    gid_t gid;
    int i;

    for (i = 0; i < fi->fc; i++) {
	if (unameToUid(fi->fuser[i], &uid)) {
	    rpmMessage(RPMMESS_WARNING,
		_("user %s does not exist - using root\n"), fi->fuser[i]);
	    uid = 0;
	    /* XXX this diddles header memory. */
	    fi->fmodes[i] &= ~S_ISUID;	/* turn off the suid bit */
	}

	if (gnameToGid(fi->fgroup[i], &gid)) {
	    rpmMessage(RPMMESS_WARNING,
		_("group %s does not exist - using root\n"), fi->fgroup[i]);
	    gid = 0;
	    /* XXX this diddles header memory. */
	    fi->fmodes[i] &= ~S_ISGID;	/* turn off the sgid bit */
	}
	fi->fuids[i] = uid;
	fi->fgids[i] = gid;
    }
}

/**
 * Copy file data from h to newH.
 * @param h		header from
 * @param newH		header to
 * @param actions	array of file dispositions
 * @return		0 on success, 1 on failure
 */
static int mergeFiles(TFI_t fi, Header h, Header newH)
{
    HGE_t hge = (HGE_t)fi->hge;
    HFD_t hfd = fi->hfd;
    fileAction * actions = fi->actions;
    int i, j, k, fc;
    int_32 type = 0;
    int_32 count = 0;
    int_32 dirNamesCount, dirCount;
    void * data, * newdata;
    int_32 * dirIndexes, * newDirIndexes;
    uint_32 * fileSizes, fileSize;
    const char ** dirNames;
    const char ** newDirNames;
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

    hge(h, RPMTAG_SIZE, NULL, (void **) &fileSizes, NULL);
    fileSize = *fileSizes;
    hge(newH, RPMTAG_FILESIZES, NULL, (void **) &fileSizes, &count);
    for (i = 0, fc = 0; i < count; i++)
	if (actions[i] != FA_SKIPMULTILIB) {
	    fc++;
	    fileSize += fileSizes[i];
	}
    headerModifyEntry(h, RPMTAG_SIZE, RPM_INT32_TYPE, &fileSize, 1);

    for (i = 0; mergeTags[i]; i++) {
        if (!hge(newH, mergeTags[i], &type, (void **) &data, &count))
	    continue;
	switch (type) {
	case RPM_CHAR_TYPE:
	case RPM_INT8_TYPE:
	    newdata = xmalloc(fc * sizeof(int_8));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
			((int_8 *) newdata)[k++] = ((int_8 *) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    break;
	case RPM_INT16_TYPE:
	    newdata = xmalloc(fc * sizeof(int_16));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((int_16 *) newdata)[k++] = ((int_16 *) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    break;
	case RPM_INT32_TYPE:
	    newdata = xmalloc(fc * sizeof(int_32));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((int_32 *) newdata)[k++] = ((int_32 *) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    break;
	case RPM_STRING_ARRAY_TYPE:
	    newdata = xmalloc(fc * sizeof(char *));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((char **) newdata)[k++] = ((char **) data)[j];
	    headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    break;
	default:
	    rpmError(RPMERR_DATATYPE, _("Data type %d not supported\n"),
			(int) type);
	    return 1;
	    /*@notreached@*/ break;
	}
	data = hfd(data, type);
    }
    hge(newH, RPMTAG_DIRINDEXES, NULL, (void **) &newDirIndexes, &count);
    hge(newH, RPMTAG_DIRNAMES, NULL, (void **) &newDirNames, NULL);
    hge(h, RPMTAG_DIRINDEXES, NULL, (void **) &dirIndexes, NULL);
    hge(h, RPMTAG_DIRNAMES, NULL, (void **) &data, &dirNamesCount);

    dirNames = xcalloc(dirNamesCount + fc, sizeof(char *));
    for (i = 0; i < dirNamesCount; i++)
	dirNames[i] = ((char **) data)[i];
    dirCount = dirNamesCount;
    newdata = xmalloc(fc * sizeof(int_32));
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
    headerAddOrAppendEntry(h, RPMTAG_DIRINDEXES, RPM_INT32_TYPE, newdata, fc);
    if (dirCount > dirNamesCount)
	headerAddOrAppendEntry(h, RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE,
			       dirNames + dirNamesCount,
			       dirCount - dirNamesCount);
    data = hfd(data, -1);
    newDirNames = hfd(newDirNames, -1);
    free (newdata);
    free (dirNames);

    for (i = 0; i < 9; i += 3) {
	const char **Names, **EVR, **newNames, **newEVR;
	int nnt, nvt, rnt;
	uint_32 *Flags, *newFlags;
	int Count = 0, newCount = 0;

	if (!hge(newH, requireTags[i], &nnt, (void **) &newNames, &newCount))
	    continue;

	hge(newH, requireTags[i+1], &nvt, (void **) &newEVR, NULL);
	hge(newH, requireTags[i+2], NULL, (void **) &newFlags, NULL);
	if (hge(h, requireTags[i], &rnt, (void **) &Names, &Count))
	{
	    hge(h, requireTags[i+1], NULL, (void **) &EVR, NULL);
	    hge(h, requireTags[i+2], NULL, (void **) &Flags, NULL);
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
	newNames = hfd(newNames, nnt);
	newEVR = hfd(newEVR, nvt);
	Names = hfd(Names, rnt);
    }
    return 0;
}

/**
 * Mark files in database shared with this package as "replaced".
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @return		0 always
 */
static int markReplacedFiles(const rpmTransactionSet ts, const TFI_t fi)
{
    HGE_t hge = (HGE_t)fi->hge;
    rpmdb rpmdb = ts->rpmdb;
    const struct sharedFileInfo * replaced = fi->replaced;
    const struct sharedFileInfo * sfi;
    rpmdbMatchIterator mi;
    Header h;
    unsigned int * offsets;
    unsigned int prev;
    int num;

    if (!(fi->fc > 0 && fi->replaced))
	return 0;

    num = prev = 0;
    for (sfi = replaced; sfi->otherPkg; sfi++) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	num++;
    }
    if (num == 0)
	return 0;

    offsets = alloca(num * sizeof(*offsets));
    num = prev = 0;
    for (sfi = replaced; sfi->otherPkg; sfi++) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	offsets[num++] = sfi->otherPkg;
    }

    mi = rpmdbInitIterator(rpmdb, RPMDBI_PACKAGES, NULL, 0);
    rpmdbAppendIterator(mi, offsets, num);

    sfi = replaced;
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	char * secStates;
	int modified;
	int count;

	modified = 0;

	if (!hge(h, RPMTAG_FILESTATES, NULL, (void **)&secStates, &count))
	    continue;
	
	prev = rpmdbGetIteratorOffset(mi);
	num = 0;
	while (sfi->otherPkg && sfi->otherPkg == prev) {
	    assert(sfi->otherFileNum < count);
	    if (secStates[sfi->otherFileNum] != RPMFILE_STATE_REPLACED) {
		secStates[sfi->otherFileNum] = RPMFILE_STATE_REPLACED;
		if (modified == 0) {
		    /* Modified header will be rewritten. */
		    modified = 1;
		    rpmdbSetIteratorModified(mi, modified);
		}
		num++;
	    }
	    sfi++;
	}
    }
    rpmdbFreeIterator(mi);

    return 0;
}

/**
 * Setup payload map and install payload archive.
 *
 * @todo Add endian tag so that srpm MD5 sums can be verified when installed.
 *
 * @param ts		transaction set
 * @param fi		transaction element file info (NULL means all files)
 * @param allFiles	install all files?
 * @return		0 on success
 */
static int installArchive(PSM_t psm, int allFiles)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    struct availablePackage * alp = fi->ap;
    int saveerrno;
    int rc;

    if (allFiles) {
	/* install all files */
    } else if (fi->fc == 0) {
	/* no files to install */
	return 0;
    }

    /* Retrieve type of payload compression. */
    {	const char * payload_compressor = NULL;
	char * t;

	if (!headerGetEntry(fi->h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    (void **) &payload_compressor, NULL))
	    payload_compressor = "gzip";
	psm->rpmio_flags = t = xmalloc(sizeof("r.gzdio"));
	*t++ = 'r';
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
    }

    {

	psm->cfd = Fdopen(fdDup(Fileno(alp->fd)), psm->rpmio_flags);

	rc = fsmSetup(fi->fsm, FSM_PKGINSTALL, ts, fi,
			psm->cfd, NULL, &psm->failedFile);
	saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	Fclose(psm->cfd);
	psm->cfd = NULL;
	(void) fsmTeardown(fi->fsm);

	if (!rc && ts->transFlags & RPMTRANS_FLAG_PKGCOMMIT) {
	    rc = fsmSetup(fi->fsm, FSM_PKGCOMMIT, ts, fi,
			NULL, NULL, &psm->failedFile);
	    (void) fsmTeardown(fi->fsm);
	}
    }

    if (rc) {
	/*
	 * This would probably be a good place to check if disk space
	 * was used up - if so, we should return a different error.
	 */
	errno = saveerrno; /* XXX FIXME: Fclose with libio destroys errno */
	rpmError(RPMERR_CPIO, _("unpacking of archive failed%s%s: %s\n"),
		(psm->failedFile != NULL ? _(" on file ") : ""),
		(psm->failedFile != NULL ? psm->failedFile : ""),
		cpioStrerror(rc));
	rc = 1;
    } else {
	if (ts && ts->notify) {
	    unsigned int archiveSize = (fi->archiveSize ? fi->archiveSize : 100);
	    (void)ts->notify(fi->h, RPMCALLBACK_INST_PROGRESS,
			archiveSize, archiveSize,
			(fi->ap ? fi->ap->key : NULL), ts->notifyData);
	}
    }

    psm->failedFile = _free(psm->failedFile);
    psm->rpmio_flags = _free(psm->rpmio_flags);

    return rc;
}

/**
 */
static rpmRC chkdir (const char * dpath, const char * dname)
{
    struct stat st;
    int rc;

    if ((rc = Stat(dpath, &st)) < 0) {
	int ut = urlPath(dpath, NULL);
	switch (ut) {
	case URL_IS_PATH:
	case URL_IS_UNKNOWN:
	    if (errno != ENOENT)
		break;
	    /*@fallthrough@*/
	case URL_IS_FTP:
	case URL_IS_HTTP:
	    /* XXX this will only create last component of directory path */
	    rc = Mkdir(dpath, 0755);
	    break;
	case URL_IS_DASH:
	    break;
	}
	if (rc < 0) {
	    rpmError(RPMERR_CREATE, _("cannot create %s %s\n"),
			dname, dpath);
	    return RPMRC_FAIL;
	}
    }
    if ((rc = Access(dpath, W_OK))) {
	rpmError(RPMERR_CREATE, _("cannot write to %s\n"), dpath);
	return RPMRC_FAIL;
    }
    return RPMRC_OK;
}

/**
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @retval specFilePtr	address of spec file name
 * @return		rpmRC return code
 */
static rpmRC installSources(const rpmTransactionSet ts, TFI_t fi,
			/*@out@*/ const char ** specFilePtr)
{
    HFD_t hfd = fi->hfd;
    const char * _sourcedir = rpmGenPath(ts->rootDir, "%{_sourcedir}", "");
    const char * _specdir = rpmGenPath(ts->rootDir, "%{_specdir}", "");
    const char * specFile = NULL;
    rpmRC rc = RPMRC_OK;
    int i;

    rpmMessage(RPMMESS_DEBUG, _("installing a source package\n"));

    rc = chkdir(_sourcedir, "sourcedir");
    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    rc = chkdir(_specdir, "specdir");
    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    i = fi->fc;
    if (headerIsEntry(fi->h, RPMTAG_COOKIE))
	for (i = 0; i < fi->fc; i++)
		if (fi->fflags[i] & RPMFILE_SPECFILE) break;

    if (i == fi->fc) {
	/* Find the spec file by name. */
	for (i = 0; i < fi->fc; i++) {
	    const char * t = fi->apath[i];
	    t += strlen(fi->apath[i]) - 5;
	    if (!strcmp(t, ".spec")) break;
	}
    }

    /* Build dnl/dil with {_sourcedir, _specdir} as values. */
    if (i < fi->fc) {
	int speclen = strlen(_specdir) + 2;
	int sourcelen = strlen(_sourcedir) + 2;
	char * t;

	fi->dnl = hfd(fi->dnl, -1);

	fi->dc = 2;
	fi->dnl = xmalloc(fi->dc * sizeof(*fi->dnl) + fi->fc * sizeof(*fi->dil) +
			speclen + sourcelen);
	fi->dil = (int *)(fi->dnl + fi->dc);
	memset(fi->dil, 0, fi->fc * sizeof(*fi->dil));
	fi->dil[i] = 1;
	fi->dnl[0] = t = (char *)(fi->dil + fi->fc);
	fi->dnl[1] = t = stpcpy( stpcpy(t, _sourcedir), "/") + 1;
	(void) stpcpy( stpcpy(t, _specdir), "/");

	t = xmalloc(speclen + strlen(fi->bnl[i]) + 1);
	(void) stpcpy( stpcpy( stpcpy(t, _specdir), "/"), fi->bnl[i]);
	specFile = t;
    } else {
	rpmError(RPMERR_NOSPEC, _("source package contains no .spec file\n"));
	rc = 2;
	goto exit;
    }

    {	struct psm_s psmbuf;
	PSM_t psm = &psmbuf;
	memset(psm, 0, sizeof(*psm));
	psm->ts = ts;
	psm->fi = fi;

	rc = installArchive(psm, 1);
    }

    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

exit:
    if (rc == RPMRC_OK && specFile && specFilePtr)
	*specFilePtr = specFile;
    else
	specFile = _free(specFile);
    _specdir = _free(_specdir);
    _sourcedir = _free(_sourcedir);
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

rpmRC rpmInstallSourcePackage(const char * rootDir, FD_t fd,
			const char ** specFile,
			rpmCallbackFunction notify, rpmCallbackData notifyData,
			char ** cookie)
{
    rpmdb rpmdb = NULL;
    rpmTransactionSet ts = rpmtransCreateSet(rpmdb, rootDir);
    TFI_t fi = xcalloc(sizeof(*fi), 1);
    int isSource;
    Header h;
    int major, minor;
    rpmRC rc;
    int i;

    ts->notify = notify;
    ts->notifyData = notifyData;

    rc = rpmReadPackageHeader(fd, &h, &isSource, &major, &minor);
    if (rc)
	goto exit;

    if (!isSource) {
	rpmError(RPMERR_NOTSRPM, _("source package expected, binary found\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

    if (cookie) {
	*cookie = NULL;
	if (headerGetEntry(h, RPMTAG_COOKIE, NULL, (void **) cookie, NULL))
	    *cookie = xstrdup(*cookie);
    }

    (void) rpmtransAddPackage(ts, h, fd, NULL, 0, NULL);

    fi->type = TR_ADDED;
    fi->ap = ts->addedPackages.list;
    loadFi(h, fi);
    headerFree(h);	/* XXX reference held by transaction set */

    if (fi->fmd5s) {		/* DYING */
	free((void **)fi->fmd5s); fi->fmd5s = NULL;
    }
    if (fi->fmapflags) {	/* DYING */
	free((void **)fi->fmapflags); fi->fmapflags = NULL;
    }
    fi->uid = getuid();
    fi->gid = getgid();
    fi->astriplen = 0;
    fi->striplen = 0;
    fi->mapflags = CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;

    fi->fuids = xcalloc(sizeof(*fi->fuids), fi->fc);
    fi->fgids = xcalloc(sizeof(*fi->fgids), fi->fc);
    for (i = 0; i < fi->fc; i++) {
	fi->fuids[i] = fi->uid;
	fi->fgids[i] = fi->gid;
    }

    for (i = 0; i < fi->fc; i++) {
	fi->actions[i] = FA_CREATE;
    }

    rpmBuildFileList(fi->h, &fi->apath, NULL);

    rpmInstallLoadMacros(fi, fi->h);

    rc = installSources(ts, fi, specFile);

exit:
    if (fi) {
	freeFi(fi);
	free(fi);
    }
    if (ts)
	rpmtransFree(ts);
    return rc;
}

static char * SCRIPT_PATH = "PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/X11R6/bin";

/**
 * Return scriptlet name from tag.
 * @param tag		scriptlet tag
 * @return		name of scriptlet
 */
static /*@observer@*/ const char * const tag2sln(int tag)
{
    switch (tag) {
    case RPMTAG_PREIN:		return "%pre";
    case RPMTAG_POSTIN:		return "%post";
    case RPMTAG_PREUN:		return "%preun";
    case RPMTAG_POSTUN:		return "%postun";
    case RPMTAG_VERIFYSCRIPT:	return "%verify";
    }
    return "%unknownscript";
}

/**
 * Run scriptlet with args.
 *
 * Run a script with an interpreter. If the interpreter is not specified,
 * /bin/sh will be used. If the interpreter is /bin/sh, then the args from
 * the header will be ignored, passing instead arg1 and arg2.
 * 
 * @param psm		package state machine data
 * @param h		header
 * @param sln		name of scriptlet section
 * @param progArgc	no. of args from header
 * @param progArgv	args from header, progArgv[0] is the interpreter to use
 * @param script	scriptlet from header
 * @param arg1		no. instances of package installed after scriptlet exec
 *			(-1 is no arg)
 * @param arg2		ditto, but for the target package
 * @return		0 on success, 1 on error
 */
static int runScript(PSM_t psm, Header h,
		const char * sln,
		int progArgc, const char ** progArgv, 
		const char * script, int arg1, int arg2)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = fi->hfd;
    const char ** argv = NULL;
    int argc = 0;
    const char ** prefixes = NULL;
    int numPrefixes;
    int_32 ipt;
    const char * oldPrefix;
    int maxPrefixLength;
    int len;
    char * prefixBuf = NULL;
    pid_t child;
    int status = 0;
    const char * fn = NULL;
    int i;
    int freePrefixes = 0;
    FD_t out;
    int rc = 0;
    const char *n, *v, *r;

    if (!progArgv && !script)
	return 0;

    if (!progArgv) {
	argv = alloca(5 * sizeof(char *));
	argv[0] = "/bin/sh";
	argc = 1;
    } else {
	argv = alloca((progArgc + 4) * sizeof(char *));
	memcpy(argv, progArgv, progArgc * sizeof(char *));
	argc = progArgc;
    }

    headerNVR(h, &n, &v, &r);
    if (hge(h, RPMTAG_INSTPREFIXES, &ipt, (void **) &prefixes, &numPrefixes)) {
	freePrefixes = 1;
    } else if (hge(h, RPMTAG_INSTALLPREFIX, NULL, (void **) &oldPrefix, NULL)) {
	prefixes = &oldPrefix;
	numPrefixes = 1;
    } else {
	numPrefixes = 0;
    }

    maxPrefixLength = 0;
    for (i = 0; i < numPrefixes; i++) {
	len = strlen(prefixes[i]);
	if (len > maxPrefixLength) maxPrefixLength = len;
    }
    prefixBuf = alloca(maxPrefixLength + 50);

    if (script) {
	FD_t fd;
	if (makeTempFile((!ts->chrootDone ? ts->rootDir : "/"), &fn, &fd)) {
	    if (freePrefixes) free(prefixes);
	    return 1;
	}

	if (rpmIsDebug() &&
	    (!strcmp(argv[0], "/bin/sh") || !strcmp(argv[0], "/bin/bash")))
	    (void)Fwrite("set -x\n", sizeof(char), 7, fd);

	(void)Fwrite(script, sizeof(script[0]), strlen(script), fd);
	Fclose(fd);

	{   const char * sn = fn;
	    if (!ts->chrootDone &&
		!(ts->rootDir[0] == '/' && ts->rootDir[1] == '\0'))
	    {
		sn += strlen(ts->rootDir)-1;
	    }
	    argv[argc++] = sn;
	}

	if (arg1 >= 0) {
	    char *av = alloca(20);
	    sprintf(av, "%d", arg1);
	    argv[argc++] = av;
	}
	if (arg2 >= 0) {
	    char *av = alloca(20);
	    sprintf(av, "%d", arg2);
	    argv[argc++] = av;
	}
    }

    argv[argc] = NULL;

    if (ts->scriptFd != NULL) {
	if (rpmIsVerbose()) {
	    out = fdDup(Fileno(ts->scriptFd));
	} else {
	    out = Fopen("/dev/null", "w.fdio");
	    if (Ferror(out)) {
		out = fdDup(Fileno(ts->scriptFd));
	    }
	}
    } else {
	out = fdDup(STDOUT_FILENO);
	out = fdLink(out, "runScript persist");
    }
    
    if (!(child = fork())) {
	const char * rootDir;
	int pipes[2];

	pipes[0] = pipes[1] = 0;
	/* make stdin inaccessible */
	pipe(pipes);
	close(pipes[1]);
	dup2(pipes[0], STDIN_FILENO);
	close(pipes[0]);

	if (ts->scriptFd != NULL) {
	    if (Fileno(ts->scriptFd) != STDERR_FILENO)
		dup2(Fileno(ts->scriptFd), STDERR_FILENO);
	    if (Fileno(out) != STDOUT_FILENO)
		dup2(Fileno(out), STDOUT_FILENO);
	    /* make sure we don't close stdin/stderr/stdout by mistake! */
	    if (Fileno(out) > STDERR_FILENO && Fileno(out) != Fileno(ts->scriptFd)) {
		Fclose (out);
	    }
	    if (Fileno(ts->scriptFd) > STDERR_FILENO) {
		Fclose (ts->scriptFd);
	    }
	}

	{   const char *ipath = rpmExpand("PATH=%{_install_script_path}", NULL);
	    const char *path = SCRIPT_PATH;

	    if (ipath && ipath[5] != '%')
		path = ipath;
	    doputenv(path);
	    if (ipath)	free((void *)ipath);
	}

	for (i = 0; i < numPrefixes; i++) {
	    sprintf(prefixBuf, "RPM_INSTALL_PREFIX%d=%s", i, prefixes[i]);
	    doputenv(prefixBuf);

	    /* backwards compatibility */
	    if (i == 0) {
		sprintf(prefixBuf, "RPM_INSTALL_PREFIX=%s", prefixes[i]);
		doputenv(prefixBuf);
	    }
	}

	rootDir = ts->rootDir;
	switch(urlIsURL(rootDir)) {
	case URL_IS_PATH:
	    rootDir += sizeof("file://") - 1;
	    rootDir = strchr(rootDir, '/');
	    /*@fallthrough@*/
	case URL_IS_UNKNOWN:
	    if (!ts->chrootDone && !(rootDir[0] == '/' && rootDir[1] == '\0')) {
		/*@-unrecog@*/ chroot(rootDir); /*@=unrecog@*/
	    }
	    chdir("/");
	    execv(argv[0], (char *const *)argv);
	    break;
	default:
	    break;
	}

 	_exit(-1);
	/*@notreached@*/
    }

    if (waitpid(child, &status, 0) < 0) {
	rpmError(RPMERR_SCRIPT,
		 _("execution of %s scriptlet from %s-%s-%s failed, waitpid returned %s\n"),
		 sln, n, v, r, strerror (errno));
	/* XXX what to do here? */
	rc = 0;
    } else {
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	    rpmError(RPMERR_SCRIPT,
		     _("execution of %s scriptlet from %s-%s-%s failed, exit status %d\n"),
		     sln, n, v, r, WEXITSTATUS(status));
	    rc = 1;
	}
    }

    if (freePrefixes) prefixes = hfd(prefixes, ipt);

    Fclose(out);	/* XXX dup'd STDOUT_FILENO */
    
    if (script) {
	if (!rpmIsDebug()) unlink(fn);
	free((void *)fn);
    }

    return rc;
}

/**
 * Retrieve and run scriptlet from header.
 * @param psm		package state machine data
 * @return		0 on success
 */
static int runInstScript(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = fi->hfd;
    void ** programArgv;
    int programArgc;
    const char ** argv;
    int_32 ptt, stt;
    const char * script;
    int rc;

    if (ts->transFlags & RPMTRANS_FLAG_NOSCRIPTS)
	return 0;

    /*
     * headerGetEntry() sets the data pointer to NULL if the entry does
     * not exist.
     */
    hge(fi->h, psm->progTag, &ptt, (void **) &programArgv, &programArgc);
    hge(fi->h, psm->scriptTag, &stt, (void **) &script, NULL);

    if (programArgv && ptt == RPM_STRING_TYPE) {
	argv = alloca(sizeof(char *));
	*argv = (const char *) programArgv;
    } else {
	argv = (const char **) programArgv;
    }

    rc = runScript(psm, fi->h, tag2sln(psm->scriptTag), programArgc, argv,
		script, psm->scriptArg, -1);
    programArgv = hfd(programArgv, ptt);
    script = hfd(script, stt);
    return rc;
}

/**
 * @param psm		package state machine data
 * @param sourceH
 * @param triggeredH
 * @param arg2
 * @param triggersAlreadyRun
 * @return
 */
static int handleOneTrigger(PSM_t psm, Header sourceH, Header triggeredH,
			int arg2, char * triggersAlreadyRun)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = fi->hfd;
    const char ** triggerNames;
    const char ** triggerEVR;
    const char ** triggerScripts;
    const char ** triggerProgs;
    int_32 * triggerFlags;
    int_32 * triggerIndices;
    int_32 tnt, tvt, tft;
    const char * triggerPackageName;
    const char * sourceName;
    int numTriggers;
    int rc = 0;
    int i;
    int skip;

    if (!hge(triggeredH, RPMTAG_TRIGGERNAME, &tnt, 
			(void **) &triggerNames, &numTriggers))
	return 0;

    headerNVR(sourceH, &sourceName, NULL, NULL);

    hge(triggeredH, RPMTAG_TRIGGERFLAGS, &tft, (void **) &triggerFlags, NULL);
    hge(triggeredH, RPMTAG_TRIGGERVERSION, &tvt, (void **) &triggerEVR, NULL);

    for (i = 0; i < numTriggers; i++) {
	int_32 tit, tst, tpt;

	if (!(triggerFlags[i] & psm->sense)) continue;
	if (strcmp(triggerNames[i], sourceName)) continue;

	/*
	 * For some reason, the TRIGGERVERSION stuff includes the name of
	 * the package which the trigger is based on. We need to skip
	 * over that here. I suspect that we'll change our minds on this
	 * and remove that, so I'm going to just 'do the right thing'.
	 */
	skip = strlen(triggerNames[i]);
	if (!strncmp(triggerEVR[i], triggerNames[i], skip) &&
	    (triggerEVR[i][skip] == '-'))
	    skip++;
	else
	    skip = 0;

	if (!headerMatchesDepFlags(sourceH, triggerNames[i],
		triggerEVR[i] + skip, triggerFlags[i]))
	    continue;

	hge(triggeredH, RPMTAG_TRIGGERINDEX, &tit,
		       (void **) &triggerIndices, NULL);
	hge(triggeredH, RPMTAG_TRIGGERSCRIPTS, &tst,
		       (void **) &triggerScripts, NULL);
	hge(triggeredH, RPMTAG_TRIGGERSCRIPTPROG, &tpt,
		       (void **) &triggerProgs, NULL);

	headerNVR(triggeredH, &triggerPackageName, NULL, NULL);

	{   int arg1;
	    int index;

	    arg1 = rpmdbCountPackages(ts->rpmdb, triggerPackageName);
	    if (arg1 < 0) {
		rc = 1;	/* XXX W2DO? same as "execution of script failed" */
	    } else {
		arg1 += psm->countCorrection;
		index = triggerIndices[i];
		if (!triggersAlreadyRun || !triggersAlreadyRun[index]) {
		    rc = runScript(psm, triggeredH, "%trigger", 1,
			    triggerProgs + index, triggerScripts[index], 
			    arg1, arg2);
		    if (triggersAlreadyRun) triggersAlreadyRun[index] = 1;
		}
	    }
	}

	triggerIndices = hfd(triggerIndices, tit);
	triggerScripts = hfd(triggerScripts, tst);
	triggerProgs = hfd(triggerProgs, tpt);

	/*
	 * Each target/source header pair can only result in a single
	 * script being run.
	 */
	break;
    }

    triggerNames = hfd(triggerNames, tnt);
    triggerFlags = hfd(triggerFlags, tft);
    triggerEVR = hfd(triggerEVR, tvt);

    return rc;
}

/**
 * Run trigger scripts in the database that are fired by this header.
 * @param psm		package state machine data
 * @return		0 on success, 1 on error
 */
static int runTriggers(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    int numPackage;
    int rc = 0;

    numPackage = rpmdbCountPackages(ts->rpmdb, fi->name) + psm->countCorrection;
    if (numPackage < 0)
	return 1;

    {	Header triggeredH;
	rpmdbMatchIterator mi;
	int countCorrection = psm->countCorrection;

	psm->countCorrection = 0;
	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_TRIGGERNAME, fi->name, 0);
	while((triggeredH = rpmdbNextIterator(mi)) != NULL) {
	    rc |= handleOneTrigger(psm, fi->h, triggeredH, numPackage, NULL);
	}

	rpmdbFreeIterator(mi);
	psm->countCorrection = countCorrection;
    }

    return rc;
}

/**
 * Run triggers from this header that are fired by headers in the database.
 * @param psm		package state machine data
 * @return		0 on success, 1 on error
 */
static int runImmedTriggers(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = fi->hfd;
    const char ** triggerNames;
    int numTriggers;
    int_32 * triggerIndices;
    int_32 tnt, tit;
    int numTriggerIndices;
    char * triggersRun;
    int rc = 0;

    if (!hge(fi->h, RPMTAG_TRIGGERNAME, &tnt,
			(void **) &triggerNames, &numTriggers))
	return 0;

    hge(fi->h, RPMTAG_TRIGGERINDEX, &tit, (void **) &triggerIndices, 
		   &numTriggerIndices);
    triggersRun = alloca(sizeof(*triggersRun) * numTriggerIndices);
    memset(triggersRun, 0, sizeof(*triggersRun) * numTriggerIndices);

    {	Header sourceH = NULL;
	int i;

	for (i = 0; i < numTriggers; i++) {
	    rpmdbMatchIterator mi;

	    if (triggersRun[triggerIndices[i]]) continue;
	
	    mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, triggerNames[i], 0);

	    while((sourceH = rpmdbNextIterator(mi)) != NULL) {
		rc |= handleOneTrigger(psm, sourceH, fi->h, 
				rpmdbGetIteratorCount(mi),
				triggersRun);
	    }

	    rpmdbFreeIterator(mi);
	}
    }
    triggerIndices = hfd(triggerNames, tit);
    triggerNames = hfd(triggerNames, tnt);
    return rc;
}

int psmStage(PSM_t psm, pkgStage stage)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = (HGE_t)fi->hge;
    int rc = psm->rc;
    int i;

    switch (stage) {
    case PSM_UNKNOWN:
	break;
    case PSM_INIT:
	break;
    case PSM_PRE:
	break;
    case PSM_PROCESS:
	break;
    case PSM_POST:
	break;
    case PSM_UNDO:
	break;
    case PSM_FINI:
	break;

    case PSM_PKGINSTALL:
	rc = fsmSetup(fi->fsm, FSM_PKGINSTALL, ts, fi,
			psm->cfd, NULL, &psm->failedFile);
	(void) fsmTeardown(fi->fsm);
	break;
    case PSM_PKGERASE:
	if (fi->fc <= 0)				break;
	if (ts->transFlags & RPMTRANS_FLAG_JUSTDB)	break;
    {	const void * pkgKey = NULL;

	if (ts->notify)
	    (void)ts->notify(fi->h, RPMCALLBACK_UNINST_START,
				fi->fc, fi->fc, pkgKey, ts->notifyData);

	/* XXX failedFile? */
	rc = fsmSetup(fi->fsm, FSM_PKGERASE, ts, fi,
			NULL, NULL, &psm->failedFile);
	(void) fsmTeardown(fi->fsm);

	if (ts->notify)
	    (void)ts->notify(fi->h, RPMCALLBACK_UNINST_STOP,
				0, fi->fc, pkgKey, ts->notifyData);
    }	break;
    case PSM_PKGCOMMIT:
	if (!(ts->transFlags & RPMTRANS_FLAG_PKGCOMMIT)) break;
	rc = fsmSetup(fi->fsm, FSM_PKGCOMMIT, ts, fi,
			NULL, NULL, &psm->failedFile);
	(void) fsmTeardown(fi->fsm);
	break;
    case PSM_PKGSAVE:
    {	fileAction * actions = fi->actions;
	fileAction action = fi->action;

	fi->action = FA_COPYOUT;
	fi->actions = NULL;

	/* XXX failedFile? */
	rc = fsmSetup(fi->fsm, FSM_PKGSAVE, ts, fi, psm->cfd, NULL, NULL);
	(void) fsmTeardown(fi->fsm);

	fi->action = action;
	fi->actions = actions;
    }	break;

    case PSM_CREATE:
	break;
    case PSM_NOTIFY:
	break;
    case PSM_DESTROY:
	break;
    case PSM_COMMIT:
	break;

    case PSM_CHROOT_IN:
	/* Change root directory if requested and not already done. */
	if (ts->rootDir && !ts->chrootDone && !psm->chrootDone) {
	    static int _loaded = 0;

	    /*
	     * This loads all of the name services libraries, in case we
	     * don't have access to them in the chroot().
	     */
	    if (!_loaded) {
		(void)getpwnam("root");
		endpwent();
		_loaded++;
	    }

	    chdir("/");
	    /*@-unrecog@*/
	    rc = chroot(ts->rootDir);
	    /*@=unrecog@*/
	    psm->chrootDone = ts->chrootDone = 1;
	}
	break;
    case PSM_CHROOT_OUT:
	/* Restore root directory if changed. */
	if (psm->chrootDone) {
	    /*@-unrecog@*/
	    rc = chroot(".");
	    /*@=unrecog@*/
	    psm->chrootDone = ts->chrootDone = 0;
	    chdir(ts->currDir);
	}
	break;
    case PSM_SCRIPT:
	rpmMessage(RPMMESS_DEBUG, _("%s: running %s script(s) (if any)\n"),
		psm->stepName, tag2sln(psm->scriptTag));
	rc = runInstScript(psm);
	break;
    case PSM_TRIGGERS:
	if (ts->transFlags & RPMTRANS_FLAG_NOTRIGGERS)	break;
	rc = runTriggers(psm);
	break;
    case PSM_IMMED_TRIGGERS:
	if (ts->transFlags & RPMTRANS_FLAG_NOTRIGGERS)	break;
	rc = runImmedTriggers(psm);
	break;

    case PSM_RPMIO_FLAGS:
    {	const char * payload_compressor = NULL;
	char * t;

	if (!hge(fi->h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    (void **) &payload_compressor, NULL))
	    payload_compressor = "gzip";
	psm->rpmio_flags = t = xmalloc(sizeof("w9.gzdio"));
	t = stpcpy(t, "w9");
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
    }	break;

    case PSM_RPMDB_LOAD:
    {	rpmdbMatchIterator mi = NULL;

	mi = rpmdbInitIterator(ts->rpmdb, RPMDBI_PACKAGES,
				&fi->record, sizeof(fi->record));

	fi->h = rpmdbNextIterator(mi);
	if (fi->h)
	    fi->h = headerLink(fi->h);
	else
	    rc = 2;
	rpmdbFreeIterator(mi);
    }	break;
    case PSM_RPMDB_ADD:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;
	rc = rpmdbAdd(ts->rpmdb, ts->id, fi->h);
	break;
    case PSM_RPMDB_REMOVE:
	if (ts->transFlags & RPMTRANS_FLAG_TEST)	break;
	rc = rpmdbRemove(ts->rpmdb, ts->id, fi->record);
	break;

    default:
	break;
    }

    return rc;
}

/**
 * @todo Packages w/o files never get a callback, hence don't get displayed
 * on install with -v.
 */
int installBinaryPackage(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = (HGE_t)fi->hge;
    Header oldH = NULL;
    int otherOffset = 0;
    int ec = 2;		/* assume error return */
    int rc;

psm->goal = PSM_PKGINSTALL;
psm->stepName = " install";

    rpmMessage(RPMMESS_DEBUG, _("%s: %s-%s-%s has %d files, test = %d\n"),
		psm->stepName, fi->name, fi->version, fi->release,
		fi->fc, (ts->transFlags & RPMTRANS_FLAG_TEST));

    /*
     * When we run scripts, we pass an argument which is the number of 
     * versions of this package that will be installed when we are finished.
     */
    psm->scriptArg = rpmdbCountPackages(ts->rpmdb, fi->name) + 1;
    if (psm->scriptArg < 1)
	goto exit;

    {	rpmdbMatchIterator mi;
	mi = rpmdbInitIterator(ts->rpmdb, RPMTAG_NAME, fi->name, 0);
	rpmdbSetIteratorVersion(mi, fi->version);
	rpmdbSetIteratorRelease(mi, fi->release);
	while ((oldH = rpmdbNextIterator(mi))) {
	    otherOffset = rpmdbGetIteratorOffset(mi);
	    oldH = (ts->transFlags & RPMTRANS_FLAG_MULTILIB)
		? headerCopy(oldH) : NULL;
	    break;
	}
	rpmdbFreeIterator(mi);
    }

    if (fi->fc > 0 && fi->fstates == NULL) {
	fi->fstates = xmalloc(sizeof(*fi->fstates) * fi->fc);
	memset(fi->fstates, RPMFILE_STATE_NORMAL, fi->fc);
    }

    if (fi->fc > 0 && !(ts->transFlags & RPMTRANS_FLAG_JUSTDB)) {
	const char * p;

	/*
	 * Old format relocateable packages need the entire default
	 * prefix stripped to form the cpio list, while all other packages
	 * need the leading / stripped.
	 */
	rc = hge(fi->h, RPMTAG_DEFAULTPREFIX, NULL, (void **) &p, NULL);
	fi->striplen = (rc ? strlen(p) + 1 : 1); 
	fi->mapflags =
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;

	if (headerIsEntry(fi->h, RPMTAG_ORIGBASENAMES))
	    buildOrigFileList(fi->h, &fi->apath, NULL);
	else
	    rpmBuildFileList(fi->h, &fi->apath, NULL);

	if (fi->fuser == NULL)
	    hge(fi->h, RPMTAG_FILEUSERNAME, NULL, (void **) &fi->fuser, NULL);
	if (fi->fgroup == NULL)
	    hge(fi->h, RPMTAG_FILEGROUPNAME, NULL, (void **) &fi->fgroup, NULL);
	if (fi->fuids == NULL)
	    fi->fuids = xcalloc(sizeof(*fi->fuids), fi->fc);
	if (fi->fgids == NULL)
	    fi->fgids = xcalloc(sizeof(*fi->fgids), fi->fc);

    }

    if (ts->transFlags & RPMTRANS_FLAG_TEST) {
	ec = 0;
	goto exit;
    }

    psm->scriptTag = RPMTAG_PREIN;
    psm->progTag = RPMTAG_PREINPROG;
    rc = psmStage(psm, PSM_SCRIPT);
    if (rc) {
	rpmError(RPMERR_SCRIPT,
		_("skipping %s-%s-%s install, %%pre scriptlet failed rc %d\n"),
		fi->name, fi->version, fi->release, rc);
	goto exit;
    }

    /* Change root directory if requested and not already done. */
    (void) psmStage(psm, PSM_CHROOT_IN);

    if (fi->fc > 0 && !(ts->transFlags & RPMTRANS_FLAG_JUSTDB)) {

	setFileOwners(fi);

	rc = installArchive(psm, 0);

	if (rc)
	    goto exit;
    }

    /* Restore root directory if changed. */
    (void) psmStage(psm, PSM_CHROOT_OUT);

    if (fi->fc > 0 && fi->fstates) {
	headerAddEntry(fi->h, RPMTAG_FILESTATES, RPM_CHAR_TYPE,
			fi->fstates, fi->fc);
    }

    {	int_32 installTime = time(NULL);
	headerAddEntry(fi->h, RPMTAG_INSTALLTIME, RPM_INT32_TYPE,
			&installTime, 1);
    }

    /*
     * Legacy: changelogs used to be trimmed to (configurable) lsat N entries
     * here. This doesn't make sense now that headers have immutable regions,
     * as trimming changelogs would only increase the size of the header.
     */

    /*
     * If this package has already been installed, remove it from the database
     * before adding the new one.
     */
    if (otherOffset)
        rpmdbRemove(ts->rpmdb, ts->id, otherOffset);

    if (ts->transFlags & RPMTRANS_FLAG_MULTILIB) {
	uint_32 multiLib, * newMultiLib, * p;

	if (hge(fi->h, RPMTAG_MULTILIBS, NULL, (void **) &newMultiLib, NULL) &&
	    hge(oldH, RPMTAG_MULTILIBS, NULL, (void **) &p, NULL)) {
	    multiLib = *p;
	    multiLib |= *newMultiLib;
	    headerModifyEntry(oldH, RPMTAG_MULTILIBS, RPM_INT32_TYPE,
			      &multiLib, 1);
	}
	if (mergeFiles(fi, oldH, fi->h))
	    goto exit;
    }

    if (rpmdbAdd(ts->rpmdb, ts->id, fi->h))
	goto exit;

    psm->scriptTag = RPMTAG_POSTIN;
    psm->progTag = RPMTAG_POSTINPROG;
    psm->sense = RPMSENSE_TRIGGERIN;
    psm->countCorrection = 0;

    rc = psmStage(psm, PSM_SCRIPT);
    if (rc)
	goto exit;

    rc = psmStage(psm, PSM_TRIGGERS);
    if (rc)
	goto exit;

    rc = psmStage(psm, PSM_IMMED_TRIGGERS);
    if (rc)
	goto exit;

    markReplacedFiles(ts, fi);

    ec = 0;

exit:
    /* Restore root directory if changed. */
    (void) psmStage(psm, PSM_CHROOT_OUT);

    if (oldH)
	headerFree(oldH);
    return ec;
}

int removeBinaryPackage(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    int rc = 0;

psm->goal = PSM_PKGERASE;
psm->stepName = "   erase";
    rpmMessage(RPMMESS_DEBUG, _("%s: %s-%s-%s has %d files, test = %d\n"),
		psm->stepName, fi->name, fi->version, fi->release,
		fi->fc, (ts->transFlags & RPMTRANS_FLAG_TEST));

assert(fi->type == TR_REMOVED);
    /*
     * When we run scripts, we pass an argument which is the number of 
     * versions of this package that will be installed when we are finished.
     */
    psm->scriptArg = rpmdbCountPackages(ts->rpmdb, fi->name) - 1;
    if (psm->scriptArg < 0) {
	rc = 1;
	goto exit;
    }

    {	rpmdbMatchIterator mi = NULL;
	Header h;

	mi = rpmdbInitIterator(ts->rpmdb, RPMDBI_PACKAGES,
				&fi->record, sizeof(fi->record));

	h = rpmdbNextIterator(mi);
	if (h == NULL) {
	    rpmdbFreeIterator(mi);
	    rc = 2;
	    goto exit;
	}
	fi->h = headerLink(h);
	rpmdbFreeIterator(mi);
    }

    psm->scriptTag = RPMTAG_PREUN;
    psm->progTag = RPMTAG_PREUNPROG;
    psm->sense = RPMSENSE_TRIGGERUN;
    psm->countCorrection = -1;

    /* Change root directory if requested and not already done. */
    (void) psmStage(psm, PSM_CHROOT_IN);

    rc = psmStage(psm, PSM_TRIGGERS);
    if (rc) {
	rc = 2;
	goto exit;
    }
    rc = psmStage(psm, PSM_IMMED_TRIGGERS);
    if (rc) {
	rc = 1;
	goto exit;
    }

    rc = psmStage(psm, PSM_SCRIPT);
    if (rc) {
	rc = 1;
	goto exit;
    }

    if (fi->fc > 0 && !(ts->transFlags & RPMTRANS_FLAG_JUSTDB)) {
	const void * pkgKey = NULL;

	if (ts->notify)
	    (void)ts->notify(fi->h, RPMCALLBACK_UNINST_START, fi->fc, fi->fc,
		pkgKey, ts->notifyData);

	rc = fsmSetup(fi->fsm, FSM_PKGERASE, ts, fi,
			NULL, NULL, &psm->failedFile);
	(void) fsmTeardown(fi->fsm);

	if (ts->notify)
	    (void)ts->notify(fi->h, RPMCALLBACK_UNINST_STOP, 0, fi->fc,
			pkgKey, ts->notifyData);
    }
    /* XXX WTFO? erase failures are not cause for stopping. */

    psm->scriptTag = RPMTAG_POSTUN;
    psm->progTag = RPMTAG_POSTUNPROG;
    psm->sense = RPMSENSE_TRIGGERPOSTUN;
    psm->countCorrection = -1;

    rc = psmStage(psm, PSM_SCRIPT);
    /* XXX WTFO? postun failures are not cause for erasure failure. */

    rc = psmStage(psm, PSM_TRIGGERS);
    if (rc) {
	rc = 2;
	goto exit;
    }
    rc = 0;

exit:
    /* Restore root directory if changed. */
    (void) psmStage(psm, PSM_CHROOT_OUT);

    if (!rc && !(ts->transFlags & RPMTRANS_FLAG_TEST))
	rpmdbRemove(ts->rpmdb, ts->id, fi->record);

    if (fi->h) {
	headerFree(fi->h);
	fi->h = NULL;
    }
    psm->failedFile = _free(psm->failedFile);

    return rc;
}

int repackage(PSM_t psm)
{
    const rpmTransactionSet ts = psm->ts;
    TFI_t fi = psm->fi;
    HGE_t hge = fi->hge;
    FD_t fd = NULL;
    const char * pkgURL = NULL;
    const char * pkgfn = NULL;
    Header h = NULL;
    Header oh = NULL;
    int saveerrno;
    int rc = 0;

psm->goal = PSM_PKGSAVE;
psm->stepName = "    save";

assert(fi->type == TR_REMOVED);
    /* Retrieve installed header. */
    {	rpmdbMatchIterator mi = NULL;

	mi = rpmdbInitIterator(ts->rpmdb, RPMDBI_PACKAGES,
				&fi->record, sizeof(fi->record));

	h = rpmdbNextIterator(mi);
	if (h == NULL) {
	    rpmdbFreeIterator(mi);
	    rc = 2;
	    goto exit;
	}
	h = headerLink(h);
	rpmdbFreeIterator(mi);
    }

    /* Regenerate original header. */
    {	void * uh = NULL;
	int_32 uht, uhc;
	HFD_t hfd = fi->hfd;

	if (headerGetEntry(h, RPMTAG_HEADERIMMUTABLE, &uht, &uh, &uhc)) {
	    oh = headerCopyLoad(uh);
	    uh = hfd(uh, uht);
	} else {
	    oh = headerLink(h);
	}
    }

    /* Open output package for writing. */
    {	const char * bfmt = rpmGetPath("%{_repackage_name_fmt}", NULL);
	const char * pkgbn =
		headerSprintf(h, bfmt, rpmTagTable, rpmHeaderFormats, NULL);

	bfmt = _free(bfmt);
	pkgURL = rpmGenPath(	"%{?_repackage_root:%{_repackage_root}}",
				"%{?_repackage_dir:%{_repackage_dir}}",
				pkgbn);
	pkgbn = _free(pkgbn);
	(void) urlPath(pkgURL, &pkgfn);
	fd = Fopen(pkgfn, "w.ufdio");
	if (fd == NULL || Ferror(fd)) {
	    rc = 1;
	    goto exit;
	}
    }

    /* Retrieve type of payload compression. */
    {	const char * payload_compressor = NULL;
	char * t;

	if (!hge(h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    (void **) &payload_compressor, NULL))
	    payload_compressor = "gzip";
	psm->rpmio_flags = t = xmalloc(sizeof("w9.gzdio"));
	t = stpcpy(t, "w9");
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
    }

    /* Write the lead section into the package. */
    {	int archnum = -1;
	int osnum = -1;
	struct rpmlead lead;

#ifndef	DYING
	rpmGetArchInfo(NULL, &archnum);
	rpmGetOsInfo(NULL, &osnum);
#endif

	memset(&lead, 0, sizeof(lead));
	/* XXX Set package version conditioned on noDirTokens. */
	lead.major = 4;
	lead.minor = 0;
	lead.type = RPMLEAD_BINARY;
	lead.archnum = archnum;
	lead.osnum = osnum;
	lead.signature_type = RPMSIGTYPE_HEADERSIG;

	{   char buf[256];
	    sprintf(buf, "%s-%s-%s", fi->name, fi->version, fi->release);
	    strncpy(lead.name, buf, sizeof(lead.name));
	}

	rc = writeLead(fd, &lead);
	if (rc) {
	    rpmError(RPMERR_NOSPACE, _("Unable to write package: %s\n"),
		 Fstrerror(fd));
	    rc = 1;
	    goto exit;
	}
    }

    /* Write the signature section into the package. */
    {	Header sig = headerRegenSigHeader(h);
	rc = rpmWriteSignature(fd, sig);
	headerFree(sig);
	if (rc) goto exit;
    }

    /* Write the metadata section into the package. */
    rc = headerWrite(fd, oh, HEADER_MAGIC_YES);
    if (rc) goto exit;

    /* Change root directory if requested and not already done. */
    (void) psmStage(psm, PSM_CHROOT_IN);

    /* Write the payload into the package. */
    {
	fileAction * actions = fi->actions;
	fileAction action = fi->action;

	fi->action = FA_COPYOUT;
	fi->actions = NULL;

	Fflush(fd);
	psm->cfd = Fdopen(fdDup(Fileno(fd)), psm->rpmio_flags);

	/* XXX failedFile? */
	rc = fsmSetup(fi->fsm, FSM_PKGSAVE, ts, fi, psm->cfd, NULL, NULL);
	(void) fsmTeardown(fi->fsm);

	saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	Fclose(psm->cfd);
	psm->cfd = NULL;
	errno = saveerrno;
	fi->action = action;
	fi->actions = actions;
    }

exit:
    /* Restore root directory if changed. */
    (void) psmStage(psm, PSM_CHROOT_OUT);

    if (h)	headerFree(h);
    if (oh)	headerFree(oh);
    if (fd) {
	saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	Fclose(fd);
	errno = saveerrno;
    }

    if (!rc)
	rpmMessage(RPMMESS_VERBOSE, _("Wrote: %s\n"), pkgURL);

    pkgURL = _free(pkgURL);
    psm->rpmio_flags = _free(psm->rpmio_flags);

    return rc;
}
