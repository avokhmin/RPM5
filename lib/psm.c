/** \ingroup rpmts payload
 * \file lib/psm.c
 * Package state machine to handle a package from a transaction set.
 */

#include "system.h"

#include <signal.h>
#include <sys/signal.h>

#include <rpmio_internal.h>
#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>

#include "cpio.h"
#include "fsm.h"		/* XXX CPIO_FOO/FSM_FOO constants */
#include "psm.h"

#include "rpmds.h"

#define _RPMFI_INTERNAL
#include "rpmfi.h"

#define	_RPMTE_INTERNAL
#include "rpmte.h"

#define	_RPMTS_INTERNAL		/* XXX ts->notify */
#include "rpmts.h"

#include "rpmlead.h"		/* writeLead proto */
#include "signature.h"		/* signature constants */
#include "legacy.h"		/* XXX buildOrigFileList() */
#include "ugid.h"		/* XXX unameToUid() and gnameToGid() */
#include "misc.h"		/* XXX stripTrailingChar() */
#include "rpmdb.h"		/* XXX for db_chrootDone */
#include "debug.h"

#define	_PSM_DEBUG	0
/*@unchecked@*/
int _psm_debug = _PSM_DEBUG;

/*@access Header @*/		/* compared with NULL */
/*@access rpmdbMatchIterator @*//* compared with NULL */
/*@access FD_t @*/		/* compared with NULL */
/*@access rpmdb @*/		/* compared with NULL */

/*@access FSM_t @*/		/* compared with NULL */
/*@access rpmpsm @*/

/*@access rpmfi @*/
/*@access rpmte @*/	/* XXX rpmInstallSourcePackage */
/*@access rpmts @*/	/* XXX ts->notify */

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
/*@-boundsread@*/
	if (*epochOne < *epochTwo)
	    return -1;
	else if (*epochOne > *epochTwo)
	    return 1;
/*@=boundsread@*/
    }

    rc = headerGetEntry(first, RPMTAG_VERSION, NULL, (void **) &one, NULL);
    rc = headerGetEntry(second, RPMTAG_VERSION, NULL, (void **) &two, NULL);

    rc = rpmvercmp(one, two);
    if (rc)
	return rc;

    rc = headerGetEntry(first, RPMTAG_RELEASE, NULL, (void **) &one, NULL);
    rc = headerGetEntry(second, RPMTAG_RELEASE, NULL, (void **) &two, NULL);

    return rpmvercmp(one, two);
}

/**
 * Macros to be defined from per-header tag values.
 * @todo Should other macros be added from header when installing a package?
 */
/*@observer@*/ /*@unchecked@*/
static struct tagMacro {
/*@observer@*/ /*@null@*/ const char *	macroname; /*!< Macro name to define. */
    rpmTag	tag;		/*!< Header tag to use for value. */
} tagMacros[] = {
    { "name",		RPMTAG_NAME },
    { "version",	RPMTAG_VERSION },
    { "release",	RPMTAG_RELEASE },
    { "epoch",		RPMTAG_EPOCH },
    { NULL, 0 }
};

/**
 * Define per-header macros.
 * @param fi		transaction element file info
 * @param h		header
 * @return		0 always
 */
static int rpmInstallLoadMacros(rpmfi fi, Header h)
	/*@globals rpmGlobalMacroContext @*/
	/*@modifies rpmGlobalMacroContext @*/
{
    HGE_t hge = (HGE_t) fi->hge;
    struct tagMacro * tagm;
    union {
/*@unused@*/ void * ptr;
/*@unused@*/ const char ** argv;
	const char * str;
	int_32 * i32p;
    } body;
    char numbuf[32];
    rpmTagType type;

    for (tagm = tagMacros; tagm->macroname != NULL; tagm++) {
	if (!hge(h, tagm->tag, &type, (void **) &body, NULL))
	    continue;
	switch (type) {
	case RPM_INT32_TYPE:
/*@-boundsread@*/
	    sprintf(numbuf, "%d", *body.i32p);
/*@=boundsread@*/
	    addMacro(NULL, tagm->macroname, NULL, numbuf, -1);
	    /*@switchbreak@*/ break;
	case RPM_STRING_TYPE:
	    addMacro(NULL, tagm->macroname, NULL, body.str, -1);
	    /*@switchbreak@*/ break;
	case RPM_NULL_TYPE:
	case RPM_CHAR_TYPE:
	case RPM_INT8_TYPE:
	case RPM_INT16_TYPE:
	case RPM_BIN_TYPE:
	case RPM_STRING_ARRAY_TYPE:
	case RPM_I18NSTRING_TYPE:
	default:
	    /*@switchbreak@*/ break;
	}
    }
    return 0;
}

/**
 * Copy file data from h to newH.
 * @param fi		transaction element file info
 * @param h		header from
 * @param newH		header to
 * @return		0 on success
 */
/*@-boundswrite@*/
static rpmRC mergeFiles(rpmfi fi, Header h, Header newH)
	/*@modifies h @*/
{
    HGE_t hge = (HGE_t)fi->hge;
    HME_t hme = (HME_t)fi->hme;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    fileAction * actions = fi->actions;
    int i, j, k, fc, xx;
    rpmTagType type = 0;
    int_32 count = 0;
    int_32 dirNamesCount, dirCount;
    void * data, * newdata;
    int_32 * dirIndexes, * newDirIndexes;
    uint_32 * fileSizes, fileSize;
    const char ** dirNames;
    const char ** newDirNames;
    static rpmTag mergeTags[] = {
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
    static rpmTag requireTags[] = {
	RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS,
	RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS,
	RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS
    };

    xx = hge(h, RPMTAG_SIZE, NULL, (void **) &fileSizes, NULL);
    fileSize = *fileSizes;
    xx = hge(newH, RPMTAG_FILESIZES, NULL, (void **) &fileSizes, &count);
    for (i = 0, fc = 0; i < count; i++)
	if (actions[i] != FA_SKIPMULTILIB) {
	    fc++;
	    fileSize += fileSizes[i];
	}
    xx = hme(h, RPMTAG_SIZE, RPM_INT32_TYPE, &fileSize, 1);

    /*@-sizeoftype@*/
    for (i = 0; mergeTags[i]; i++) {
        if (!hge(newH, mergeTags[i], &type, (void **) &data, &count))
	    continue;
	switch (type) {
	case RPM_CHAR_TYPE:
	case RPM_INT8_TYPE:
	    newdata = xcalloc(fc, sizeof(int_8));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
			((int_8 *) newdata)[k++] = ((int_8 *) data)[j];
	    xx = headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    /*@switchbreak@*/ break;
	case RPM_INT16_TYPE:
	    newdata = xcalloc(fc, sizeof(int_16));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((int_16 *) newdata)[k++] = ((int_16 *) data)[j];
	    xx = headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    /*@switchbreak@*/ break;
	case RPM_INT32_TYPE:
	    newdata = xcalloc(fc, sizeof(int_32));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((int_32 *) newdata)[k++] = ((int_32 *) data)[j];
	    xx = headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    /*@switchbreak@*/ break;
	case RPM_STRING_ARRAY_TYPE:
	    newdata = xcalloc(fc, sizeof(char *));
	    for (j = 0, k = 0; j < count; j++)
		if (actions[j] != FA_SKIPMULTILIB)
		    ((char **) newdata)[k++] = ((char **) data)[j];
	    xx = headerAddOrAppendEntry(h, mergeTags[i], type, newdata, fc);
	    free (newdata);
	    /*@switchbreak@*/ break;
	default:
	    rpmError(RPMERR_DATATYPE, _("Data type %d not supported\n"),
			(int) type);
	    return RPMRC_FAIL;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	}
	data = hfd(data, type);
    }
    /*@=sizeoftype@*/
    xx = hge(newH, RPMTAG_DIRINDEXES, NULL, (void **) &newDirIndexes, &count);
    xx = hge(newH, RPMTAG_DIRNAMES, NULL, (void **) &newDirNames, NULL);
    xx = hge(h, RPMTAG_DIRINDEXES, NULL, (void **) &dirIndexes, NULL);
    xx = hge(h, RPMTAG_DIRNAMES, NULL, (void **) &data, &dirNamesCount);

    dirNames = xcalloc(dirNamesCount + fc, sizeof(*dirNames));
    for (i = 0; i < dirNamesCount; i++)
	dirNames[i] = ((char **) data)[i];
    dirCount = dirNamesCount;
    newdata = xcalloc(fc, sizeof(*newDirIndexes));
    for (i = 0, k = 0; i < count; i++) {
	if (actions[i] == FA_SKIPMULTILIB)
	    continue;
	for (j = 0; j < dirCount; j++)
	    if (!strcmp(dirNames[j], newDirNames[newDirIndexes[i]]))
		/*@innerbreak@*/ break;
	if (j == dirCount)
	    dirNames[dirCount++] = newDirNames[newDirIndexes[i]];
	((int_32 *) newdata)[k++] = j;
    }
    xx = headerAddOrAppendEntry(h, RPMTAG_DIRINDEXES, RPM_INT32_TYPE, newdata, fc);
    if (dirCount > dirNamesCount)
	xx = headerAddOrAppendEntry(h, RPMTAG_DIRNAMES, RPM_STRING_ARRAY_TYPE,
			       dirNames + dirNamesCount,
			       dirCount - dirNamesCount);
    data = hfd(data, -1);
    newDirNames = hfd(newDirNames, -1);
    free (newdata);
    free (dirNames);

    for (i = 0; i < 9; i += 3) {
	const char **Names, **EVR, **newNames, **newEVR;
	rpmTagType nnt, nvt, rnt;
	uint_32 *Flags, *newFlags;
	int Count = 0, newCount = 0;

	if (!hge(newH, requireTags[i], &nnt, (void **) &newNames, &newCount))
	    continue;

	xx = hge(newH, requireTags[i+1], &nvt, (void **) &newEVR, NULL);
	xx = hge(newH, requireTags[i+2], NULL, (void **) &newFlags, NULL);
	if (hge(h, requireTags[i], &rnt, (void **) &Names, &Count))
	{
	    xx = hge(h, requireTags[i+1], NULL, (void **) &EVR, NULL);
	    xx = hge(h, requireTags[i+2], NULL, (void **) &Flags, NULL);
	    for (j = 0; j < newCount; j++)
		for (k = 0; k < Count; k++)
		    if (!strcmp (newNames[j], Names[k])
			&& !strcmp (newEVR[j], EVR[k])
			&& (newFlags[j] & RPMSENSE_SENSEMASK) ==
			   (Flags[k] & RPMSENSE_SENSEMASK))
		    {
			newNames[j] = NULL;
			/*@innerbreak@*/ break;
		    }
	}
	for (j = 0, k = 0; j < newCount; j++) {
	    if (!newNames[j] || !isDependsMULTILIB(newFlags[j]))
		/*@innercontinue@*/ continue;
	    if (j != k) {
		newNames[k] = newNames[j];
		newEVR[k] = newEVR[j];
		newFlags[k] = newFlags[j];
	    }
	    k++;
	}
	if (k) {
	    xx = headerAddOrAppendEntry(h, requireTags[i],
				       RPM_STRING_ARRAY_TYPE, newNames, k);
	    xx = headerAddOrAppendEntry(h, requireTags[i+1],
				       RPM_STRING_ARRAY_TYPE, newEVR, k);
	    xx = headerAddOrAppendEntry(h, requireTags[i+2], RPM_INT32_TYPE,
				       newFlags, k);
	}
	newNames = hfd(newNames, nnt);
	newEVR = hfd(newEVR, nvt);
	Names = hfd(Names, rnt);
    }
    return RPMRC_OK;
}
/*@=boundswrite@*/

/**
 * Mark files in database shared with this package as "replaced".
 * @param psm		package state machine data
 * @return		0 always
 */
/*@-bounds@*/
static rpmRC markReplacedFiles(const rpmpsm psm)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    HGE_t hge = (HGE_t)fi->hge;
    sharedFileInfo replaced = fi->replaced;
    sharedFileInfo sfi;
    rpmdbMatchIterator mi;
    Header h;
    unsigned int * offsets;
    unsigned int prev;
    int num, xx;

    if (!(rpmfiFC(fi) > 0 && fi->replaced))
	return RPMRC_OK;

    num = prev = 0;
    for (sfi = replaced; sfi->otherPkg; sfi++) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	num++;
    }
    if (num == 0)
	return RPMRC_OK;

    offsets = alloca(num * sizeof(*offsets));
    offsets[0] = 0;
    num = prev = 0;
    for (sfi = replaced; sfi->otherPkg; sfi++) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	offsets[num++] = sfi->otherPkg;
    }

    mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES, NULL, 0);
    xx = rpmdbAppendIterator(mi, offsets, num);
    xx = rpmdbSetIteratorRewrite(mi, 1);

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
		    xx = rpmdbSetIteratorModified(mi, modified);
		}
		num++;
	    }
	    sfi++;
	}
    }
    mi = rpmdbFreeIterator(mi);

    return RPMRC_OK;
}
/*@=bounds@*/

rpmRC rpmInstallSourcePackage(rpmts ts, FD_t fd,
		const char ** specFilePtr, const char ** cookie)
{
    int scareMem = 1;
    rpmfi fi = NULL;
    const char * _sourcedir = NULL;
    const char * _specdir = NULL;
    const char * specFile = NULL;
    HGE_t hge;
    HFD_t hfd;
    Header h = NULL;
    struct rpmpsm_s psmbuf;
    rpmpsm psm = &psmbuf;
    int isSource;
    rpmRC rc;
    int i;

    memset(psm, 0, sizeof(*psm));
    /*@-assignexpose -usereleased @*/
    psm->ts = rpmtsLink(ts, "InstallSourcePackage");
    /*@=assignexpose =usereleased @*/

    rc = rpmReadPackageFile(ts, fd, "InstallSourcePackage", &h);
    switch (rc) {
    case RPMRC_NOTTRUSTED:
    case RPMRC_NOKEY:
    case RPMRC_OK:
	break;
    default:
	goto exit;
	/*@notreached@*/ break;
    }
    if (h == NULL)
	goto exit;

    rc = RPMRC_OK;

    isSource = headerIsEntry(h, RPMTAG_SOURCEPACKAGE);

    if (!isSource) {
	rpmError(RPMERR_NOTSRPM, _("source package expected, binary found\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

     (void) rpmtsAddInstallElement(ts, h, NULL, 0, NULL);

    fi = rpmfiNew(ts, h, RPMTAG_BASENAMES, scareMem);
    h = headerFree(h);

    if (fi == NULL) {	/* XXX can't happen */
	rc = RPMRC_FAIL;
	goto exit;
    }

/*@-onlytrans@*/	/* FIX: te reference */
    fi->te = rpmtsElement(ts, 0);
/*@=onlytrans@*/
/*@-nullpass@*/		/* FIX fi->h may be null */
    fi->te->h = headerLink(fi->h);
/*@=nullpass@*/
    fi->te->fd = fdLink(fd, "installSourcePackage");
    hge = fi->hge;
    hfd = fi->hfd;

/*@i@*/ (void) rpmInstallLoadMacros(fi, fi->h);

    /*@-assignexpose -usereleased @*/
    psm->fi = fi;
    psm->te = fi->te;
    /*@=assignexpose =usereleased @*/

    if (cookie) {
	*cookie = NULL;
	if (hge(fi->h, RPMTAG_COOKIE, NULL, (void **) cookie, NULL))
	    *cookie = xstrdup(*cookie);
    }

    /* XXX FIXME: can't do endian neutral MD5 verification yet. */
/*@i@*/ fi->fmd5s = hfd(fi->fmd5s, -1);

    /* XXX FIXME: don't do per-file mapping, force global flags. */
    fi->fmapflags = _free(fi->fmapflags);
    fi->mapflags = CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;

    fi->uid = getuid();
    fi->gid = getgid();
    fi->astriplen = 0;
    fi->striplen = 0;

    fi->fuids = xcalloc(sizeof(*fi->fuids), fi->fc);
    fi->fgids = xcalloc(sizeof(*fi->fgids), fi->fc);
    for (i = 0; i < fi->fc; i++) {
	fi->fuids[i] = fi->uid;
	fi->fgids[i] = fi->gid;
    }

    for (i = 0; i < fi->fc; i++)
	fi->actions[i] = FA_CREATE;

    i = fi->fc;

    if (fi->h != NULL) {	/* XXX can't happen */
	rpmBuildFileList(fi->h, &fi->apath, NULL);

	if (headerIsEntry(fi->h, RPMTAG_COOKIE))
	    for (i = 0; i < fi->fc; i++)
		if (fi->fflags[i] & RPMFILE_SPECFILE) break;
    }

    if (i == fi->fc) {
	/* Find the spec file by name. */
	for (i = 0; i < fi->fc; i++) {
	    const char * t = fi->apath[i];
	    t += strlen(fi->apath[i]) - 5;
	    if (!strcmp(t, ".spec")) break;
	}
    }

    _sourcedir = rpmGenPath(rpmtsRootDir(ts), "%{_sourcedir}", "");
    rc = rpmMkdirPath(_sourcedir, "sourcedir");
    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    _specdir = rpmGenPath(rpmtsRootDir(ts), "%{_specdir}", "");
    rc = rpmMkdirPath(_specdir, "specdir");
    if (rc) {
	rc = RPMRC_FAIL;
	goto exit;
    }

    /* Build dnl/dil with {_sourcedir, _specdir} as values. */
    if (i < fi->fc) {
	int speclen = strlen(_specdir) + 2;
	int sourcelen = strlen(_sourcedir) + 2;
	char * t;

/*@i@*/	fi->dnl = hfd(fi->dnl, -1);

	fi->dc = 2;
	fi->dnl = xmalloc(fi->dc * sizeof(*fi->dnl) + fi->fc * sizeof(*fi->dil) +
			speclen + sourcelen);
	fi->dil = (int *)(fi->dnl + fi->dc);
	memset(fi->dil, 0, fi->fc * sizeof(*fi->dil));
	fi->dil[i] = 1;
	/*@-dependenttrans@*/
	fi->dnl[0] = t = (char *)(fi->dil + fi->fc);
	fi->dnl[1] = t = stpcpy( stpcpy(t, _sourcedir), "/") + 1;
	/*@=dependenttrans@*/
	(void) stpcpy( stpcpy(t, _specdir), "/");

	t = xmalloc(speclen + strlen(fi->bnl[i]) + 1);
	(void) stpcpy( stpcpy( stpcpy(t, _specdir), "/"), fi->bnl[i]);
	specFile = t;
    } else {
	rpmError(RPMERR_NOSPEC, _("source package contains no .spec file\n"));
	rc = RPMRC_FAIL;
	goto exit;
    }

    psm->goal = PSM_PKGINSTALL;

    /*@-compmempass@*/	/* FIX: psm->fi->dnl should be owned. */
    rc = rpmpsmStage(psm, PSM_PROCESS);

    (void) rpmpsmStage(psm, PSM_FINI);
    /*@=compmempass@*/

    if (rc) rc = RPMRC_FAIL;

exit:
    if (specFilePtr && specFile && rc == RPMRC_OK)
	*specFilePtr = specFile;
    else
	specFile = _free(specFile);

    _specdir = _free(_specdir);
    _sourcedir = _free(_sourcedir);

    if (h) h = headerFree(h);

    /*@-branchstate@*/
    if (fi) {
	fi->te->h = headerFree(fi->te->h);
	if (fi->te->fd)
	    (void) Fclose(fi->te->fd);
	fi->te->fd = NULL;
	fi->te = NULL;
	fi = rpmfiFree(fi);
    }
    /*@=branchstate@*/

    psm->fi = NULL;
    psm->te = NULL;

    /* XXX nuke the added package(s). */
    rpmtsClean(ts);

    psm->ts = rpmtsFree(psm->ts);

    return rc;
}

/*@observer@*/ /*@unchecked@*/
static char * SCRIPT_PATH = "PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/X11R6/bin";

/**
 * Return scriptlet name from tag.
 * @param tag		scriptlet tag
 * @return		name of scriptlet
 */
static /*@observer@*/ const char * const tag2sln(int tag)
	/*@*/
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
 */
/*@unchecked@*/
static sigset_t caught;

/**
 */
/*@unchecked@*/
static struct psmtbl_s {
    int nalloced;
    int npsms;
/*@null@*/
    rpmpsm * psms;
} psmtbl = { 0, 0, NULL };

/* forward ref */
static void handler(int signum)
	/*@globals caught, psmtbl, fileSystem @*/
	/*@modifies caught, psmtbl, fileSystem @*/;

/**
 */
/*@unchecked@*/
/*@-fullinitblock@*/
static struct sigtbl_s {
    int signum;
    int active;
    struct sigaction act;
    struct sigaction oact;
} satbl[] = {
    { SIGCHLD,	0, { {handler} } },
    { -1,	0, { {NULL}    } },
};
/*@=fullinitblock@*/

/**
 */
/*@-incondefs@*/
static void handler(int signum)
{
    struct sigtbl_s * tbl;

    for(tbl = satbl; tbl->signum >= 0; tbl++) {
	if (tbl->signum != signum)
	    continue;
	if (!tbl->active)
	    continue;
	(void) sigaddset(&caught, signum);
	switch (signum) {
	case SIGCHLD:
	    while (1) {
		int status = 0;
		pid_t reaped = waitpid(0, &status, WNOHANG);
		int i;

		if (reaped <= 0)
		    /*@innerbreak@*/ break;

		if (psmtbl.psms)
		for (i = 0; i < psmtbl.npsms; i++) {
		    rpmpsm psm = psmtbl.psms[i];
		    if (psm->child != reaped)
			/*@innercontinue@*/ continue;

#if _PSM_DEBUG
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "      Reap: %p[%d:%d:%d] = %p child %d\n", psmtbl.psms, i, psmtbl.npsms, psmtbl.nalloced, psm, psm->child);
/*@=modfilesys@*/
#endif

		    psm->reaped = reaped;
		    psm->status = status;
		    /*@innerbreak@*/ break;
		}
	    }
	    /*@switchbreak@*/ break;
	default:
	    /*@switchbreak@*/ break;
	}
	break;
    }
}
/*@=incondefs@*/

/**
 * Enable a signal handler.
 */
static int enableSignal(int signum)
	/*@globals caught, satbl, fileSystem @*/
	/*@modifies caught, satbl, fileSystem @*/
{
    sigset_t newMask, oldMask;
    struct sigtbl_s * tbl;

    (void) sigfillset(&newMask);		/* block all signals */
    (void) sigprocmask(SIG_BLOCK, &newMask, &oldMask);
    for (tbl = satbl; tbl->signum >= 0; tbl++) {
	if (signum >= 0 && signum != tbl->signum)
	    continue;
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "    Enable: %p[0:%d:%d] active %d\n", psmtbl.psms, psmtbl.npsms, psmtbl.nalloced, tbl->active);
/*@=modfilesys@*/
	if (tbl->active++ <= 0) {
	    (void) sigdelset(&caught, tbl->signum);
	    (void) sigaction(tbl->signum, &tbl->act, &tbl->oact);
	}
	break;
    }
    return sigprocmask(SIG_SETMASK, &oldMask, NULL);
}

/**
 * Disable a signal handler.
 */
static int disableSignal(int signum)
	/*@globals satbl, fileSystem @*/
	/*@modifies satbl, fileSystem @*/
{
    sigset_t newMask, oldMask;
    struct sigtbl_s * tbl;

    (void) sigfillset(&newMask);		/* block all signals */
    (void) sigprocmask(SIG_BLOCK, &newMask, &oldMask);
    for (tbl = satbl; tbl->signum >= 0; tbl++) {
	if (signum >= 0 && signum != tbl->signum)
	    continue;
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "   Disable: %p[0:%d:%d] active %d\n", psmtbl.psms, psmtbl.npsms, psmtbl.nalloced, tbl->active);
/*@=modfilesys@*/
	if (--tbl->active <= 0) {
	    tbl->active = 0;		/* XXX just in case */
	    (void) sigaction(tbl->signum, &tbl->oact, NULL);
	}
	break;
    }
    return sigprocmask(SIG_SETMASK, &oldMask, NULL);
}

/**
 * Register a child reaper, then fork a child.
 * @param psm		package state machine data
 * @return		fork(2) pid
 */
static pid_t psmRegisterFork(rpmpsm psm)
	/*@globals psmtbl, fileSystem, internalState @*/
	/*@modifies psm, psmtbl, fileSystem, internalState @*/
{
    sigset_t newMask, oldMask;
    int empty = -1;
    int i = psmtbl.npsms;

    (void) sigfillset(&newMask);		/* block all signals */
    (void) sigprocmask(SIG_BLOCK, &newMask, &oldMask);

    if (psmtbl.psms)
    for (i = 0; i < psmtbl.npsms; i++) {
	if (empty == -1 && psmtbl.psms[i] == NULL)
	    empty = i;
	if (psm != psmtbl.psms[i])
	    continue;
	break;
    }
    if (i == psmtbl.npsms) {
	if (i >= psmtbl.nalloced) {
	    if (psmtbl.nalloced == 0) psmtbl.nalloced = 5;
	    while (psmtbl.nalloced < i)
		psmtbl.nalloced += psmtbl.nalloced;
	    psmtbl.psms = xrealloc(psmtbl.psms,
			psmtbl.nalloced * sizeof(*psmtbl.psms));
	}
	empty = psmtbl.npsms++;
    }
    if (psmtbl.psms)	/* XXX can't happen */
	psmtbl.psms[empty] = rpmpsmLink(psm, "psmRegister");
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "  Register: %p[%d:%d:%d] = %p\n", psmtbl.psms, empty, psmtbl.npsms, psmtbl.nalloced, psm);
/*@=modfilesys@*/

    (void) enableSignal(SIGCHLD);

    psm->reaped = 0;
    if ((psm->child = fork()) != 0) {
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "      Fork: %p[%d:%d:%d] = %p child %d\n", psmtbl.psms, 0, psmtbl.npsms, psmtbl.nalloced, psm, psm->child);
/*@=modfilesys@*/
    }

    (void) sigprocmask(SIG_SETMASK, &oldMask, NULL);

    return psm->child;
}

/**
 * Unregister a child reaper.
 */
static int psmWaitUnregister(rpmpsm psm, pid_t child)
	/*@globals psmtbl, fileSystem, internalState @*/
	/*@modifies psmtbl, fileSystem, internalState @*/
{
    sigset_t newMask, oldMask;
    int i = 0;

    (void) sigfillset(&newMask);		/* block all signals */
    (void) sigprocmask(SIG_BLOCK, &newMask, &oldMask);
    
    /*@-infloops@*/
    while (psm->reaped != psm->child) {
	(void) sigprocmask(SIG_SETMASK, &oldMask, NULL);
	(void) sleep(1);	/* XXX sleep guarantees loop traversal. */
	(void) sigprocmask(SIG_BLOCK, &newMask, &oldMask);
    }
    /*@=infloops@*/

/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "      Wait: %p[%d:%d:%d] = %p child %d\n", psmtbl.psms, 0, psmtbl.npsms, psmtbl.nalloced, psm, psm->child);
/*@=modfilesys@*/

    if (psmtbl.psms)
    for (i = 0; i < psmtbl.npsms; i++) {
	if (psmtbl.psms[i] == NULL)
	    continue;
	if (psm != psmtbl.psms[i])
	    continue;
	if (child != psm->child)
	    continue;
	break;
    }

    if (i < psmtbl.npsms) {
	(void) disableSignal(SIGCHLD);
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "Unregister: %p[%d:%d:%d] = %p child %d\n", psmtbl.psms, i, psmtbl.npsms, psmtbl.nalloced, psm, child);
/*@=modfilesys@*/
	if (psmtbl.psms)	/* XXX can't happen */
	    psmtbl.psms[i] = rpmpsmFree(psmtbl.psms[i]);
	if (psmtbl.npsms == (i+1))
	    psmtbl.npsms--;
	if (psmtbl.npsms == 0) {
	    psmtbl.psms = _free(psmtbl.psms);
	    psmtbl.nalloced = 0;
	}
    }

    return sigprocmask(SIG_SETMASK, &oldMask, NULL);
}

/**
 * Wait for child process to be reaped.
 * @param psm		package state machine data
 * @return		
 */
static pid_t psmWait(rpmpsm psm)
	/*@globals fileSystem, internalState @*/
	/*@modifies psm, fileSystem, internalState @*/
{
    if (psm->reaper) {
	(void) psmWaitUnregister(psm, psm->child);
    } else {
	do {
	    psm->reaped = waitpid(psm->child, &psm->status, 0);
	} while (psm->reaped >= 0 && psm->reaped != psm->child);
    }

    rpmMessage(RPMMESS_DEBUG, _("%s: waitpid(%d) rc %d status %x\n"),
	psm->stepName, (unsigned)psm->child,
	(unsigned)psm->reaped, psm->status);

    return psm->reaped;
}

/**
 */
/*@unchecked@*/
static int ldconfig_done = 0;

/*@unchecked@*/ /*@observer@*/ /*@null@*/
static const char * ldconfig_path = "/sbin/ldconfig";

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
 * @return		0 on success
 */
static rpmRC runScript(rpmpsm psm, Header h, const char * sln,
		int progArgc, const char ** progArgv, 
		const char * script, int arg1, int arg2)
	/*@globals ldconfig_done, rpmGlobalMacroContext,
		fileSystem, internalState@*/
	/*@modifies psm, ldconfig_done, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    const char ** argv = NULL;
    int argc = 0;
    const char ** prefixes = NULL;
    int numPrefixes;
    rpmTagType ipt;
    const char * oldPrefix;
    int maxPrefixLength;
    int len;
    char * prefixBuf = NULL;
    const char * fn = NULL;
    int i, xx;
    int freePrefixes = 0;
    FD_t scriptFd;
    FD_t out;
    rpmRC rc = RPMRC_OK;
    const char *n, *v, *r;

    if (progArgv == NULL && script == NULL)
	return rc;

    psm->child = 0;
    psm->reaped = 0;
    psm->status = 0;
    psm->reaper = 1;

    /* XXX FIXME: except for %verifyscript, rpmteNEVR can be used. */
    xx = headerNVR(h, &n, &v, &r);

    /* XXX bash must have functional libtermcap.so.2 */
    if (!strcmp(n, "libtermcap"))
	ldconfig_done = 0;

    /*
     * If a successor node, and ldconfig was just run, don't bother.
     */
    if (ldconfig_path && progArgv && psm->unorderedSuccessor) {
 	if (ldconfig_done && !strcmp(progArgv[0], ldconfig_path)) {
	    rpmMessage(RPMMESS_DEBUG,
		_("%s: %s(%s-%s-%s) skipping redundant \"%s\".\n"),
		psm->stepName, tag2sln(psm->scriptTag), n, v, r,
		progArgv[0]);
	    return rc;
	}
    }

    rpmMessage(RPMMESS_DEBUG,
		_("%s: %s(%s-%s-%s) %ssynchronous scriptlet start\n"),
		psm->stepName, tag2sln(psm->scriptTag), n, v, r,
		(psm->unorderedSuccessor ? "a" : ""));

    if (!progArgv) {
	argv = alloca(5 * sizeof(*argv));
	argv[0] = "/bin/sh";
	argc = 1;
	ldconfig_done = 0;
    } else {
	argv = alloca((progArgc + 4) * sizeof(*argv));
	memcpy(argv, progArgv, progArgc * sizeof(*argv));
	argc = progArgc;
	ldconfig_done = (ldconfig_path && !strcmp(argv[0], ldconfig_path)
		? 1 : 0);
    }

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
	const char * rootDir = rpmtsRootDir(ts);
	FD_t fd;

	/*@-branchstate@*/
	if (makeTempFile((!rpmtsChrootDone(ts) ? rootDir : "/"), &fn, &fd)) {
	    if (freePrefixes) free(prefixes);
	    return RPMRC_FAIL;
	}
	/*@=branchstate@*/

	if (rpmIsDebug() &&
	    (!strcmp(argv[0], "/bin/sh") || !strcmp(argv[0], "/bin/bash")))
	{
	    static const char set_x[] = "set -x\n";
	    xx = Fwrite(set_x, sizeof(set_x[0]), sizeof(set_x)-1, fd);
	}

	if (ldconfig_path && strstr(script, ldconfig_path) != NULL)
	    ldconfig_done = 1;

	xx = Fwrite(script, sizeof(script[0]), strlen(script), fd);
	xx = Fclose(fd);

	{   const char * sn = fn;
	    if (!rpmtsChrootDone(ts) && rootDir != NULL &&
		!(rootDir[0] == '/' && rootDir[1] == '\0'))
	    {
		sn += strlen(rootDir)-1;
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

    scriptFd = rpmtsScriptFd(ts);
    if (scriptFd != NULL) {
	if (rpmIsVerbose()) {
	    out = fdDup(Fileno(scriptFd));
	} else {
	    out = Fopen("/dev/null", "w.fdio");
	    if (Ferror(out)) {
		out = fdDup(Fileno(scriptFd));
	    }
	}
    } else {
	out = fdDup(STDOUT_FILENO);
    }
    if (out == NULL) return RPMRC_FAIL;	/* XXX can't happen */
    
    /*@-branchstate@*/
    (void) psmRegisterFork(psm);
    if (psm->child == 0) {
	const char * rootDir;
	int pipes[2];

	pipes[0] = pipes[1] = 0;
	/* make stdin inaccessible */
	xx = pipe(pipes);
	xx = close(pipes[1]);
	xx = dup2(pipes[0], STDIN_FILENO);
	xx = close(pipes[0]);

	if (scriptFd != NULL) {
	    int sfdno = Fileno(scriptFd);
	    int ofdno = Fileno(out);
	    if (sfdno != STDERR_FILENO)
		xx = dup2(sfdno, STDERR_FILENO);
	    if (ofdno != STDOUT_FILENO)
		xx = dup2(ofdno, STDOUT_FILENO);
	    /* make sure we don't close stdin/stderr/stdout by mistake! */
	    if (ofdno > STDERR_FILENO && ofdno != sfdno) {
		xx = Fclose (out);
	    }
	    if (sfdno > STDERR_FILENO) {
		xx = Fclose (scriptFd);
	    }
	}

	{   const char *ipath = rpmExpand("PATH=%{_install_script_path}", NULL);
	    const char *path = SCRIPT_PATH;

	    if (ipath && ipath[5] != '%')
		path = ipath;

	    xx = doputenv(path);
	    /*@-modobserver@*/
	    ipath = _free(ipath);
	    /*@=modobserver@*/
	}

	for (i = 0; i < numPrefixes; i++) {
	    sprintf(prefixBuf, "RPM_INSTALL_PREFIX%d=%s", i, prefixes[i]);
	    xx = doputenv(prefixBuf);

	    /* backwards compatibility */
	    if (i == 0) {
		sprintf(prefixBuf, "RPM_INSTALL_PREFIX=%s", prefixes[i]);
		xx = doputenv(prefixBuf);
	    }
	}

	rootDir = rpmtsRootDir(ts);
	if (rootDir  != NULL)	/* XXX can't happen */
	switch(urlIsURL(rootDir)) {
	case URL_IS_PATH:
	    rootDir += sizeof("file://") - 1;
	    rootDir = strchr(rootDir, '/');
	    /*@fallthrough@*/
	case URL_IS_UNKNOWN:
	    if (!rpmtsChrootDone(ts) &&
		!(rootDir[0] == '/' && rootDir[1] == '\0'))
	    {
		/*@-superuser -noeffect @*/
		xx = chroot(rootDir);
		/*@=superuser =noeffect @*/
	    }
	    xx = chdir("/");
	    rpmMessage(RPMMESS_DEBUG, _("%s: %s(%s-%s-%s)\texecv(%s) pid %d\n"),
			psm->stepName, sln, n, v, r,
			argv[0], (unsigned)getpid());
/*@-modfilesys@*/
if (_psm_debug)
fprintf(stderr, "      Exec: %s \"%s\"\n", sln, argv[0]);
/*@=modfilesys@*/
	    unsetenv("MALLOC_CHECK_");
	    xx = execv(argv[0], (char *const *)argv);
	    break;
	default:
	    break;
	}

 	_exit(-1);
	/*@notreached@*/
    }
    /*@=branchstate@*/

    (void) psmWait(psm);

    if (psm->reaped < 0) {
	rpmError(RPMERR_SCRIPT,
		_("%s(%s-%s-%s) scriptlet failed, waitpid(%d) rc %d: %s\n"),
		 sln, n, v, r, psm->child, psm->reaped, strerror(errno));
	rc = RPMRC_FAIL;
    } else
    if (!WIFEXITED(psm->status) || WEXITSTATUS(psm->status)) {
	rpmError(RPMERR_SCRIPT,
		_("%s(%s-%s-%s) scriptlet failed, exit status %d\n"),
		sln, n, v, r, WEXITSTATUS(psm->status));
	rc = RPMRC_FAIL;
    }

    if (freePrefixes) prefixes = hfd(prefixes, ipt);

    xx = Fclose(out);	/* XXX dup'd STDOUT_FILENO */
    
    /*@-branchstate@*/
    if (script) {
	if (!rpmIsDebug())
	    xx = unlink(fn);
	fn = _free(fn);
    }
    /*@=branchstate@*/

    return rc;
}

/**
 * Retrieve and run scriptlet from header.
 * @param psm		package state machine data
 * @return		rpmRC return code
 */
static rpmRC runInstScript(rpmpsm psm)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    void ** progArgv;
    int progArgc;
    const char ** argv;
    rpmTagType ptt, stt;
    const char * script;
    rpmRC rc = RPMRC_OK;
    int xx;

    /*
     * headerGetEntry() sets the data pointer to NULL if the entry does
     * not exist.
     */
    xx = hge(fi->h, psm->scriptTag, &stt, (void **) &script, NULL);
    xx = hge(fi->h, psm->progTag, &ptt, (void **) &progArgv, &progArgc);
    if (progArgv == NULL && script == NULL)
	goto exit;

    /*@-branchstate@*/
    if (progArgv && ptt == RPM_STRING_TYPE) {
	argv = alloca(sizeof(*argv));
	*argv = (const char *) progArgv;
    } else {
	argv = (const char **) progArgv;
    }
    /*@=branchstate@*/

    if (fi->h != NULL)	/* XXX can't happen */
    rc = runScript(psm, fi->h, tag2sln(psm->scriptTag), progArgc, argv,
		script, psm->scriptArg, -1);

exit:
    progArgv = hfd(progArgv, ptt);
    script = hfd(script, stt);
    return rc;
}

/**
 * Execute triggers.
 * @todo Trigger on any provides, not just package NVR.
 * @param psm		package state machine data
 * @param sourceH
 * @param triggeredH
 * @param arg2
 * @param triggersAlreadyRun
 * @return
 */
static rpmRC handleOneTrigger(const rpmpsm psm,
			Header sourceH, Header triggeredH,
			int arg2, unsigned char * triggersAlreadyRun)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState@*/
	/*@modifies psm, sourceH, triggeredH, *triggersAlreadyRun,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    int scareMem = 1;
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    rpmds trigger = NULL;
    const char ** triggerScripts;
    const char ** triggerProgs;
    int_32 * triggerIndices;
    const char * sourceName;
    rpmRC rc = RPMRC_OK;
    int xx;
    int i;

    xx = headerNVR(sourceH, &sourceName, NULL, NULL);

    trigger = rpmdsInit(rpmdsNew(triggeredH, RPMTAG_TRIGGERNAME, scareMem));
    if (trigger == NULL)
	return rc;

    (void) rpmdsSetNoPromote(trigger, 1);

    while ((i = rpmdsNext(trigger)) >= 0) {
	rpmTagType tit, tst, tpt;
	const char * Name;
	int_32 Flags = rpmdsFlags(trigger);

	if ((Name = rpmdsN(trigger)) == NULL)
	    continue;   /* XXX can't happen */

	if (strcmp(Name, sourceName))
	    continue;
	if (!(Flags & psm->sense))
	    continue;

	/*
	 * XXX Trigger on any provided dependency, not just the package NEVR.
	 */
	if (!rpmdsAnyMatchesDep(sourceH, trigger, 1))
	    continue;

	if (!(	hge(triggeredH, RPMTAG_TRIGGERINDEX, &tit,
		       (void **) &triggerIndices, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERSCRIPTS, &tst,
		       (void **) &triggerScripts, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERSCRIPTPROG, &tpt,
		       (void **) &triggerProgs, NULL))
	    )
	    continue;

	{   int arg1;
	    int index;

	    arg1 = rpmdbCountPackages(rpmtsGetRdb(ts), Name);
	    if (arg1 < 0) {
		/* XXX W2DO? fails as "execution of script failed" */
		rc = RPMRC_FAIL;
	    } else {
		arg1 += psm->countCorrection;
		index = triggerIndices[i];
		if (triggersAlreadyRun == NULL ||
		    triggersAlreadyRun[index] == 0)
		{
		    rc = runScript(psm, triggeredH, "%trigger", 1,
			    triggerProgs + index, triggerScripts[index], 
			    arg1, arg2);
		    if (triggersAlreadyRun != NULL)
			triggersAlreadyRun[index] = 1;
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

    trigger = rpmdsFree(trigger);

    return rc;
}

/**
 * Run trigger scripts in the database that are fired by this header.
 * @param psm		package state machine data
 * @return		0 on success
 */
static rpmRC runTriggers(rpmpsm psm)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    int numPackage = -1;
    rpmRC rc = RPMRC_OK;
    const char * N = NULL;

    if (psm->te) 	/* XXX can't happen */
	N = rpmteN(psm->te);
    if (N) 		/* XXX can't happen */
	numPackage = rpmdbCountPackages(rpmtsGetRdb(ts), N)
				+ psm->countCorrection;
    if (numPackage < 0)
	return RPMRC_NOTFOUND;

    if (fi->h != NULL)	/* XXX can't happen */
    {	Header triggeredH;
	rpmdbMatchIterator mi;
	int countCorrection = psm->countCorrection;

	psm->countCorrection = 0;
	mi = rpmtsInitIterator(ts, RPMTAG_TRIGGERNAME, N, 0);
	while((triggeredH = rpmdbNextIterator(mi)) != NULL)
	    rc |= handleOneTrigger(psm, fi->h, triggeredH, numPackage, NULL);
	mi = rpmdbFreeIterator(mi);
	psm->countCorrection = countCorrection;
    }

    return rc;
}

/**
 * Run triggers from this header that are fired by headers in the database.
 * @param psm		package state machine data
 * @return		0 on success
 */
static rpmRC runImmedTriggers(rpmpsm psm)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    const char ** triggerNames;
    int numTriggers;
    int_32 * triggerIndices;
    rpmTagType tnt, tit;
    int numTriggerIndices;
    unsigned char * triggersRun;
    rpmRC rc = RPMRC_OK;

    if (fi->h == NULL)	return rc;	/* XXX can't happen */

    if (!(	hge(fi->h, RPMTAG_TRIGGERNAME, &tnt,
			(void **) &triggerNames, &numTriggers) &&
		hge(fi->h, RPMTAG_TRIGGERINDEX, &tit,
			(void **) &triggerIndices, &numTriggerIndices))
	)
	return rc;

    triggersRun = alloca(sizeof(*triggersRun) * numTriggerIndices);
    memset(triggersRun, 0, sizeof(*triggersRun) * numTriggerIndices);

    {	Header sourceH = NULL;
	int i;

	for (i = 0; i < numTriggers; i++) {
	    rpmdbMatchIterator mi;

	    if (triggersRun[triggerIndices[i]] != 0) continue;
	
	    mi = rpmtsInitIterator(ts, RPMTAG_NAME, triggerNames[i], 0);

	    while((sourceH = rpmdbNextIterator(mi)) != NULL) {
		rc |= handleOneTrigger(psm, sourceH, fi->h, 
				rpmdbGetIteratorCount(mi),
				triggersRun);
	    }

	    mi = rpmdbFreeIterator(mi);
	}
    }
    triggerIndices = hfd(triggerIndices, tit);
    triggerNames = hfd(triggerNames, tnt);
    return rc;
}

/*@observer@*/ static const char *const pkgStageString(pkgStage a)
	/*@*/
{
    switch(a) {
    case PSM_UNKNOWN:		return "unknown";

    case PSM_PKGINSTALL:	return "  install";
    case PSM_PKGERASE:		return "    erase";
    case PSM_PKGCOMMIT:		return "   commit";
    case PSM_PKGSAVE:		return "repackage";

    case PSM_INIT:		return "init";
    case PSM_PRE:		return "pre";
    case PSM_PROCESS:		return "process";
    case PSM_POST:		return "post";
    case PSM_UNDO:		return "undo";
    case PSM_FINI:		return "fini";

    case PSM_CREATE:		return "create";
    case PSM_NOTIFY:		return "notify";
    case PSM_DESTROY:		return "destroy";
    case PSM_COMMIT:		return "commit";

    case PSM_CHROOT_IN:		return "chrootin";
    case PSM_CHROOT_OUT:	return "chrootout";
    case PSM_SCRIPT:		return "script";
    case PSM_TRIGGERS:		return "triggers";
    case PSM_IMMED_TRIGGERS:	return "immedtriggers";

    case PSM_RPMIO_FLAGS:	return "rpmioflags";

    case PSM_RPMDB_LOAD:	return "rpmdbload";
    case PSM_RPMDB_ADD:		return "rpmdbadd";
    case PSM_RPMDB_REMOVE:	return "rpmdbremove";

    default:			return "???";
    }
    /*@noteached@*/
}

rpmpsm XrpmpsmUnlink(rpmpsm psm, const char * msg, const char * fn, unsigned ln)
{
    if (psm == NULL) return NULL;
/*@-modfilesys@*/
if (_psm_debug && msg != NULL)
fprintf(stderr, "--> psm %p -- %d %s at %s:%u\n", psm, psm->nrefs, msg, fn, ln);
/*@=modfilesys@*/
    psm->nrefs--;
    return NULL;
}

rpmpsm XrpmpsmLink(rpmpsm psm, const char * msg, const char * fn, unsigned ln)
{
    if (psm == NULL) return NULL;
    psm->nrefs++;

/*@-modfilesys@*/
if (_psm_debug && msg != NULL)
fprintf(stderr, "--> psm %p ++ %d %s at %s:%u\n", psm, psm->nrefs, msg, fn, ln);
/*@=modfilesys@*/

    /*@-refcounttrans@*/ return psm; /*@=refcounttrans@*/
}

rpmpsm rpmpsmFree(rpmpsm psm)
{
    const char * msg = "rpmpsmFree";
    if (psm == NULL)
	return NULL;

    if (psm->nrefs > 1)
	return rpmpsmUnlink(psm, msg);

/*@-nullstate@*/
    psm->fi = rpmfiFree(psm->fi);
#ifdef	NOTYET
    psm->te = rpmteFree(psm->te);
#else
    psm->te = NULL;
#endif
    psm->ts = rpmtsFree(psm->ts);

    (void) rpmpsmUnlink(psm, msg);

    /*@-refcounttrans -usereleased@*/
/*@-boundswrite@*/
    memset(psm, 0, sizeof(*psm));		/* XXX trash and burn */
/*@=boundswrite@*/
    psm = _free(psm);
    /*@=refcounttrans =usereleased@*/

    return NULL;
/*@=nullstate@*/
}

rpmpsm rpmpsmNew(rpmts ts, rpmte te, rpmfi fi)
{
    const char * msg = "rpmpsmNew";
    rpmpsm psm = xcalloc(1, sizeof(*psm));

    if (ts)	psm->ts = rpmtsLink(ts, msg);
#ifdef	NOTYET
    if (te)	psm->te = rpmteLink(te, msg);
#else
/*@-assignexpose -temptrans @*/
    if (te)	psm->te = te;
/*@=assignexpose =temptrans @*/
#endif
    if (fi)	psm->fi = rpmfiLink(fi, msg);

    return rpmpsmLink(psm, msg);
}

/**
 * @todo Packages w/o files never get a callback, hence don't get displayed
 * on install with -v.
 */
/*@-bounds -nullpass@*/ /* FIX: testing null annotation for fi->h */
rpmRC rpmpsmStage(rpmpsm psm, pkgStage stage)
{
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HME_t hme = fi->hme;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    rpmRC rc = psm->rc;
    int saveerrno;
    int xx;

    /*@-branchstate@*/
    switch (stage) {
    case PSM_UNKNOWN:
	break;
    case PSM_INIT:
	rpmMessage(RPMMESS_DEBUG, _("%s: %s has %d files, test = %d\n"),
		psm->stepName, rpmteNEVR(psm->te),
		rpmfiFC(fi), (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST));

	/*
	 * When we run scripts, we pass an argument which is the number of 
	 * versions of this package that will be installed when we are
	 * finished.
	 */
	psm->npkgs_installed = rpmdbCountPackages(rpmtsGetRdb(ts), rpmteN(psm->te));
	if (psm->npkgs_installed < 0) {
	    rc = RPMRC_FAIL;
	    break;
	}

	if (psm->goal == PSM_PKGINSTALL) {
	    int fc = rpmfiFC(fi);

	    psm->scriptArg = psm->npkgs_installed + 1;

assert(psm->mi == NULL);
	    psm->mi = rpmtsInitIterator(ts, RPMTAG_NAME, rpmteN(psm->te), 0);
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_EPOCH, RPMMIRE_DEFAULT,
			rpmteE(psm->te));
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_VERSION, RPMMIRE_DEFAULT,
			rpmteV(psm->te));
	    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_RELEASE, RPMMIRE_DEFAULT,
			rpmteR(psm->te));

	    while ((psm->oh = rpmdbNextIterator(psm->mi))) {
		fi->record = rpmdbGetIteratorOffset(psm->mi);
		if (rpmtsFlags(ts) & RPMTRANS_FLAG_MULTILIB)
		    psm->oh = headerCopy(psm->oh);
		else
		    psm->oh = NULL;
		/*@loopbreak@*/ break;
	    }
	    psm->mi = rpmdbFreeIterator(psm->mi);
	    rc = RPMRC_OK;

	    /* XXX lazy alloc here may need to be done elsewhere. */
	    if (fi->fstates == NULL && fc > 0) {
		fi->fstates = xmalloc(sizeof(*fi->fstates) * fc);
		memset(fi->fstates, RPMFILE_STATE_NORMAL, fc);
	    }

	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)	break;
	    if (fc <= 0)				break;
	
	    /*
	     * Old format relocateable packages need the entire default
	     * prefix stripped to form the cpio list, while all other packages
	     * need the leading / stripped.
	     */
	    {   const char * p;
		xx = hge(fi->h, RPMTAG_DEFAULTPREFIX, NULL, (void **) &p, NULL);
		fi->striplen = (xx ? strlen(p) + 1 : 1); 
	    }
	    fi->mapflags =
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;
	
	    if (headerIsEntry(fi->h, RPMTAG_ORIGBASENAMES))
		buildOrigFileList(fi->h, &fi->apath, NULL);
	    else
		rpmBuildFileList(fi->h, &fi->apath, NULL);
	
	    if (fi->fuser == NULL)
		xx = hge(fi->h, RPMTAG_FILEUSERNAME, NULL,
				(void **) &fi->fuser, NULL);
	    if (fi->fgroup == NULL)
		xx = hge(fi->h, RPMTAG_FILEGROUPNAME, NULL,
				(void **) &fi->fgroup, NULL);
	    if (fi->fuids == NULL)
		fi->fuids = xcalloc(sizeof(*fi->fuids), fc);
	    if (fi->fgids == NULL)
		fi->fgids = xcalloc(sizeof(*fi->fgids), fc);
	    rc = RPMRC_OK;
	}
	if (psm->goal == PSM_PKGERASE || psm->goal == PSM_PKGSAVE) {
	    psm->scriptArg = psm->npkgs_installed - 1;
	
	    /* Retrieve installed header. */
	    rc = rpmpsmStage(psm, PSM_RPMDB_LOAD);
if (rc == RPMRC_OK)
if (psm->te)
psm->te->h = headerLink(fi->h);
	}
	if (psm->goal == PSM_PKGSAVE) {
	    /* Open output package for writing. */
	    {	const char * bfmt = rpmGetPath("%{_repackage_name_fmt}", NULL);
		const char * pkgbn =
			headerSprintf(fi->h, bfmt, rpmTagTable, rpmHeaderFormats, NULL);

		bfmt = _free(bfmt);
		psm->pkgURL = rpmGenPath("%{?_repackage_root}",
					 "%{?_repackage_dir}",
					pkgbn);
		pkgbn = _free(pkgbn);
		(void) urlPath(psm->pkgURL, &psm->pkgfn);
		psm->fd = Fopen(psm->pkgfn, "w.ufdio");
		if (psm->fd == NULL || Ferror(psm->fd)) {
		    rc = RPMRC_FAIL;
		    break;
		}
	    }
	}
	break;
    case PSM_PRE:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

	/* Change root directory if requested and not already done. */
	rc = rpmpsmStage(psm, PSM_CHROOT_IN);

	if (psm->goal == PSM_PKGINSTALL) {
	    psm->scriptTag = RPMTAG_PREIN;
	    psm->progTag = RPMTAG_PREINPROG;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERPREIN)) {
		/* XXX FIXME: implement %triggerprein. */
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPRE)) {
		rc = rpmpsmStage(psm, PSM_SCRIPT);
		if (rc != RPMRC_OK) {
		    rpmError(RPMERR_SCRIPT,
			_("%s: %s scriptlet failed (%d), skipping %s\n"),
			psm->stepName, tag2sln(psm->scriptTag), rc,
			rpmteNEVR(psm->te));
		    break;
		}
	    }
	}

	if (psm->goal == PSM_PKGERASE) {
	    psm->scriptTag = RPMTAG_PREUN;
	    psm->progTag = RPMTAG_PREUNPROG;
	    psm->sense = RPMSENSE_TRIGGERUN;
	    psm->countCorrection = -1;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERUN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmStage(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmStage(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPREUN))
		rc = rpmpsmStage(psm, PSM_SCRIPT);
	}
	if (psm->goal == PSM_PKGSAVE) {
	    int noArchiveSize = 0;

	    /* Regenerate original header. */
	    {	void * uh = NULL;
		int_32 uht, uhc;

		if (headerGetEntry(fi->h, RPMTAG_HEADERIMMUTABLE, &uht, &uh, &uhc)) {
		    psm->oh = headerCopyLoad(uh);
		    uh = hfd(uh, uht);
		} else
		if (headerGetEntry(fi->h, RPMTAG_HEADERIMAGE, &uht, &uh, &uhc))
		{
		    HeaderIterator hi;
		    int_32 tag, type, count;
		    hPTR_t ptr;
		    Header oh;

		    /* Load the original header from the blob. */
		    oh = headerCopyLoad(uh);

		    /* XXX this is headerCopy w/o headerReload() */
		    psm->oh = headerNew();

		    /*@-branchstate@*/
		    for (hi = headerInitIterator(oh);
		        headerNextIterator(hi, &tag, &type, &ptr, &count);
		        ptr = headerFreeData((void *)ptr, type))
		    {
			if (tag == RPMTAG_ARCHIVESIZE)
			    noArchiveSize = 1;
		        if (ptr) (void) headerAddEntry(psm->oh, tag, type, ptr, count);
		    }
		    hi = headerFreeIterator(hi);
		    /*@=branchstate@*/

		    oh = headerFree(oh);
		    uh = hfd(uh, uht);
		} else
		    break;	/* XXX shouldn't ever happen */
	    }

	    /* Retrieve type of payload compression. */
	    /*@-nullstate@*/	/* FIX: psm->oh may be NULL */
	    rc = rpmpsmStage(psm, PSM_RPMIO_FLAGS);
	    /*@=nullstate@*/

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
		lead.major = 3;
		lead.minor = 0;
		lead.type = RPMLEAD_BINARY;
		lead.archnum = archnum;
		lead.osnum = osnum;
		lead.signature_type = RPMSIGTYPE_HEADERSIG;

		strncpy(lead.name, rpmteNEVR(psm->te), sizeof(lead.name));

		rc = writeLead(psm->fd, &lead);
		if (rc != RPMRC_OK) {
		    rpmError(RPMERR_NOSPACE, _("Unable to write package: %s\n"),
			 Fstrerror(psm->fd));
		    break;
		}
	    }

	    /* Write the signature section into the package. */
	    /* XXX rpm-4.1 and later has archive size in signature header. */
	    {	Header sigh = headerRegenSigHeader(fi->h, noArchiveSize);
		/* Reallocate the signature into one contiguous region. */
		sigh = headerReload(sigh, RPMTAG_HEADERSIGNATURES);
		if (sigh == NULL) {
		    rpmError(RPMERR_NOSPACE, _("Unable to reload signature header\n"));
		    rc = RPMRC_FAIL;
		    break;
		}
		rc = rpmWriteSignature(psm->fd, sigh);
		sigh = rpmFreeSignature(sigh);
		if (rc) break;
	    }

	    /* Add remove transaction id to header. */
	    if (psm->oh)
	    {	int_32 tid = rpmtsGetTid(ts);
		xx = headerAddEntry(psm->oh, RPMTAG_REMOVETID,
			RPM_INT32_TYPE, &tid, 1);
	    }

	    /* Write the metadata section into the package. */
	    rc = headerWrite(psm->fd, psm->oh, HEADER_MAGIC_YES);
	    if (rc) break;
	}
	break;
    case PSM_PROCESS:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

	if (psm->goal == PSM_PKGINSTALL) {
	    int i;

	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)	break;

	    /* XXX Synthesize callbacks for packages with no files. */
	    if (rpmfiFC(fi) <= 0) {
		void * ptr;
		ptr = rpmtsNotify(ts, fi->te, RPMCALLBACK_INST_START, 0, 100);
		ptr = rpmtsNotify(ts, fi->te, RPMCALLBACK_INST_PROGRESS, 100, 100);
		break;
	    }

	    (void) rpmfiInit(fi, 0);
	    while ((i = rpmfiNext(fi)) >= 0) {
		uid_t uid;
		gid_t gid;

		uid = fi->uid;
		gid = fi->gid;
		if (fi->fuser && unameToUid(fi->fuser[i], &uid)) {
		    rpmMessage(RPMMESS_WARNING,
			_("user %s does not exist - using root\n"),
			fi->fuser[i]);
		    uid = 0;
		    /* XXX this diddles header memory. */
		    fi->fmodes[i] &= ~S_ISUID;	/* turn off the suid bit */
		}

		if (fi->fgroup && gnameToGid(fi->fgroup[i], &gid)) {
		    rpmMessage(RPMMESS_WARNING,
			_("group %s does not exist - using root\n"),
			fi->fgroup[i]);
		    gid = 0;
		    /* XXX this diddles header memory. */
		    fi->fmodes[i] &= ~S_ISGID;	/* turn off the sgid bit */
		}
		if (fi->fuids) fi->fuids[i] = uid;
		if (fi->fgids) fi->fgids[i] = gid;
	    }

	    /* Retrieve type of payload compression. */
	    rc = rpmpsmStage(psm, PSM_RPMIO_FLAGS);

	    if (rpmteFd(fi->te) == NULL) {	/* XXX can't happen */
		rc = RPMRC_FAIL;
		break;
	    }

	    /*@-nullpass@*/	/* LCL: fi->fd != NULL here. */
	    psm->cfd = Fdopen(fdDup(Fileno(rpmteFd(fi->te))), psm->rpmio_flags);
	    /*@=nullpass@*/
	    if (psm->cfd == NULL) {	/* XXX can't happen */
		rc = RPMRC_FAIL;
		break;
	    }

	    rc = fsmSetup(fi->fsm, FSM_PKGINSTALL, ts, fi,
			psm->cfd, NULL, &psm->failedFile);
	    xx = fsmTeardown(fi->fsm);

	    saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	    xx = Fclose(psm->cfd);
	    psm->cfd = NULL;
	    /*@-mods@*/
	    errno = saveerrno; /* XXX FIXME: Fclose with libio destroys errno */
	    /*@=mods@*/

	    if (!rc)
		rc = rpmpsmStage(psm, PSM_COMMIT);

	    /* XXX make sure progress is closed out */
	    psm->what = RPMCALLBACK_INST_PROGRESS;
	    psm->amount = (fi->archiveSize ? fi->archiveSize : 100);
	    psm->total = psm->amount;
	    xx = rpmpsmStage(psm, PSM_NOTIFY);

	    if (rc) {
		rpmError(RPMERR_CPIO,
			_("unpacking of archive failed%s%s: %s\n"),
			(psm->failedFile != NULL ? _(" on file ") : ""),
			(psm->failedFile != NULL ? psm->failedFile : ""),
			cpioStrerror(rc));
		rc = RPMRC_FAIL;

		/* XXX notify callback on error. */
		psm->what = RPMCALLBACK_UNPACK_ERROR;
		psm->amount = 0;
		psm->total = 0;
		xx = rpmpsmStage(psm, PSM_NOTIFY);

		break;
	    }
	}
	if (psm->goal == PSM_PKGERASE) {
	    int fc = rpmfiFC(fi);

	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)	break;
	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY)	break;
	    if (fc <= 0)				break;

	    psm->what = RPMCALLBACK_UNINST_START;
	    psm->amount = fc;		/* XXX W2DO? looks wrong. */
	    psm->total = fc;
	    xx = rpmpsmStage(psm, PSM_NOTIFY);

	    rc = fsmSetup(fi->fsm, FSM_PKGERASE, ts, fi,
			NULL, NULL, &psm->failedFile);
	    xx = fsmTeardown(fi->fsm);

	    psm->what = RPMCALLBACK_UNINST_STOP;
	    psm->amount = 0;		/* XXX W2DO? looks wrong. */
	    psm->total = fc;
	    xx = rpmpsmStage(psm, PSM_NOTIFY);

	}
	if (psm->goal == PSM_PKGSAVE) {
	    fileAction * actions = fi->actions;
	    fileAction action = fi->action;

	    fi->action = FA_COPYOUT;
	    fi->actions = NULL;

	    if (psm->fd == NULL) {	/* XXX can't happen */
		rc = RPMRC_FAIL;
		break;
	    }
	    /*@-nullpass@*/	/* FIX: fdDup mey return NULL. */
	    xx = Fflush(psm->fd);
	    psm->cfd = Fdopen(fdDup(Fileno(psm->fd)), psm->rpmio_flags);
	    /*@=nullpass@*/
	    if (psm->cfd == NULL) {	/* XXX can't happen */
		rc = RPMRC_FAIL;
		break;
	    }

	    rc = fsmSetup(fi->fsm, FSM_PKGBUILD, ts, fi, psm->cfd,
			NULL, &psm->failedFile);
	    xx = fsmTeardown(fi->fsm);

	    saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	    xx = Fclose(psm->cfd);
	    psm->cfd = NULL;
	    /*@-mods@*/
	    errno = saveerrno;
	    /*@=mods@*/

	    /* XXX make sure progress is closed out */
	    psm->what = RPMCALLBACK_INST_PROGRESS;
	    psm->amount = (fi->archiveSize ? fi->archiveSize : 100);
	    psm->total = psm->amount;
	    xx = rpmpsmStage(psm, PSM_NOTIFY);

	    fi->action = action;
	    fi->actions = actions;
	}
	break;
    case PSM_POST:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

	if (psm->goal == PSM_PKGINSTALL) {
	    int_32 installTime = (int_32) time(NULL);
	    int fc = rpmfiFC(fi);

	    if (fi->h == NULL) break;	/* XXX can't happen */
	    if (fi->fstates != NULL && fc > 0)
		xx = headerAddEntry(fi->h, RPMTAG_FILESTATES, RPM_CHAR_TYPE,
				fi->fstates, fc);

	    xx = headerAddEntry(fi->h, RPMTAG_INSTALLTIME, RPM_INT32_TYPE,
				&installTime, 1);

	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_MULTILIB) {
		uint_32 multiLib, * newMultiLib, * p;

		if (hge(fi->h, RPMTAG_MULTILIBMASK, NULL,
				(void **) &newMultiLib, NULL) &&
		    hge(psm->oh, RPMTAG_MULTILIBMASK, NULL,
				(void **) &p, NULL))
		{
		    multiLib = *p;
		    multiLib |= *newMultiLib;
		    xx = hme(psm->oh, RPMTAG_MULTILIBMASK, RPM_INT32_TYPE,
				      &multiLib, 1);
		}
		rc = mergeFiles(fi, psm->oh, fi->h);
		if (rc) break;
	    }


	    /*
	     * If this package has already been installed, remove it from
	     * the database before adding the new one.
	     */
	    if (fi->record && !(rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY)) {
		rc = rpmpsmStage(psm, PSM_RPMDB_REMOVE);
		if (rc) break;
	    }

	    rc = rpmpsmStage(psm, PSM_RPMDB_ADD);
	    if (rc) break;

	    psm->scriptTag = RPMTAG_POSTIN;
	    psm->progTag = RPMTAG_POSTINPROG;
	    psm->sense = RPMSENSE_TRIGGERIN;
	    psm->countCorrection = 0;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPOST)) {
		rc = rpmpsmStage(psm, PSM_SCRIPT);
		if (rc) break;
	    }
	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERIN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmStage(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmStage(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY))
		rc = markReplacedFiles(psm);

	}
	if (psm->goal == PSM_PKGERASE) {

	    psm->scriptTag = RPMTAG_POSTUN;
	    psm->progTag = RPMTAG_POSTUNPROG;
	    psm->sense = RPMSENSE_TRIGGERPOSTUN;
	    psm->countCorrection = -1;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPOSTUN)) {
		rc = rpmpsmStage(psm, PSM_SCRIPT);
		/* XXX WTFO? postun failures don't cause erasure failure. */
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERPOSTUN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmStage(psm, PSM_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY))
		rc = rpmpsmStage(psm, PSM_RPMDB_REMOVE);
	}
	if (psm->goal == PSM_PKGSAVE) {
	}

	/* Restore root directory if changed. */
	xx = rpmpsmStage(psm, PSM_CHROOT_OUT);
	break;
    case PSM_UNDO:
	break;
    case PSM_FINI:
	/* Restore root directory if changed. */
	xx = rpmpsmStage(psm, PSM_CHROOT_OUT);

	if (psm->fd) {
	    saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	    xx = Fclose(psm->fd);
	    psm->fd = NULL;
	    /*@-mods@*/
	    errno = saveerrno;
	    /*@=mods@*/
	}

	if (psm->goal == PSM_PKGSAVE) {
	    if (!rc && ts && ts->notify == NULL) {
		rpmMessage(RPMMESS_VERBOSE, _("Wrote: %s\n"),
			(psm->pkgURL ? psm->pkgURL : "???"));
	    }
	}

	if (rc) {
	    if (psm->failedFile)
		rpmError(RPMERR_CPIO,
			_("%s failed on file %s: %s\n"),
			psm->stepName, psm->failedFile, cpioStrerror(rc));
	    else
		rpmError(RPMERR_CPIO, _("%s failed: %s\n"),
			psm->stepName, cpioStrerror(rc));

	    /* XXX notify callback on error. */
	    psm->what = RPMCALLBACK_CPIO_ERROR;
	    psm->amount = 0;
	    psm->total = 0;
	    /*@-nullstate@*/ /* FIX: psm->fd may be NULL. */
	    xx = rpmpsmStage(psm, PSM_NOTIFY);
	    /*@=nullstate@*/
	}

	if (psm->goal == PSM_PKGERASE || psm->goal == PSM_PKGSAVE) {
if (psm->te)
if (psm->te->h)
psm->te->h = headerFree(psm->te->h);
	    if (fi->h)
		fi->h = headerFree(fi->h);
 	}
	psm->oh = headerFree(psm->oh);
	psm->pkgURL = _free(psm->pkgURL);
	psm->rpmio_flags = _free(psm->rpmio_flags);
	psm->failedFile = _free(psm->failedFile);

	fi->fgids = _free(fi->fgids);
	fi->fuids = _free(fi->fuids);
	fi->fgroup = hfd(fi->fgroup, -1);
	fi->fuser = hfd(fi->fuser, -1);
	fi->apath = _free(fi->apath);
	fi->fstates = _free(fi->fstates);
	break;

    case PSM_PKGINSTALL:
    case PSM_PKGERASE:
    case PSM_PKGSAVE:
	psm->goal = stage;
	psm->rc = RPMRC_OK;
	psm->stepName = pkgStageString(stage);

	rc = rpmpsmStage(psm, PSM_INIT);
	if (!rc) rc = rpmpsmStage(psm, PSM_PRE);
	if (!rc) rc = rpmpsmStage(psm, PSM_PROCESS);
	if (!rc) rc = rpmpsmStage(psm, PSM_POST);
	xx = rpmpsmStage(psm, PSM_FINI);
	break;
    case PSM_PKGCOMMIT:
	break;

    case PSM_CREATE:
	break;
    case PSM_NOTIFY:
    {	void * ptr;
/*@-nullpass@*/ /* FIX: psm->te may be NULL */
	ptr = rpmtsNotify(ts, psm->te, psm->what, psm->amount, psm->total);
/*@-nullpass@*/
    }	break;
    case PSM_DESTROY:
	break;
    case PSM_COMMIT:
	if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_PKGCOMMIT)) break;
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY) break;

	rc = fsmSetup(fi->fsm, FSM_PKGCOMMIT, ts, fi,
			NULL, NULL, &psm->failedFile);
	xx = fsmTeardown(fi->fsm);
	break;

    case PSM_CHROOT_IN:
    {	const char * rootDir = rpmtsRootDir(ts);
	/* Change root directory if requested and not already done. */
	if (rootDir != NULL && !rpmtsChrootDone(ts) && !psm->chrootDone) {
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

	    xx = chdir("/");
	    /*@-superuser@*/
	    rc = chroot(rootDir);
	    /*@=superuser@*/
	    psm->chrootDone = 1;
	    (void) rpmtsSetChrootDone(ts, 1);
	}
    }	break;
    case PSM_CHROOT_OUT:
	/* Restore root directory if changed. */
	if (psm->chrootDone) {
	    const char * currDir = rpmtsCurrDir(ts);
	    /*@-superuser@*/
	    rc = chroot(".");
	    /*@=superuser@*/
	    psm->chrootDone = 0;
	    (void) rpmtsSetChrootDone(ts, 0);
	    if (currDir != NULL)	/* XXX can't happen */
		xx = chdir(currDir);
	}
	break;
    case PSM_SCRIPT:	/* Run current package scriptlets. */
	rc = runInstScript(psm);
	break;
    case PSM_TRIGGERS:
	/* Run triggers in other package(s) this package sets off. */
	rc = runTriggers(psm);
	break;
    case PSM_IMMED_TRIGGERS:
	/* Run triggers in this package other package(s) set off. */
	rc = runImmedTriggers(psm);
	break;

    case PSM_RPMIO_FLAGS:
    {	const char * payload_compressor = NULL;
	char * t;

	/*@-branchstate@*/
	if (!hge(fi->h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    (void **) &payload_compressor, NULL))
	    payload_compressor = "gzip";
	/*@=branchstate@*/
	psm->rpmio_flags = t = xmalloc(sizeof("w9.gzdio"));
	*t = '\0';
	t = stpcpy(t, ((psm->goal == PSM_PKGSAVE) ? "w9" : "r"));
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
	rc = RPMRC_OK;
    }	break;

    case PSM_RPMDB_LOAD:
assert(psm->mi == NULL);
	psm->mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES,
				&fi->record, sizeof(fi->record));

	fi->h = rpmdbNextIterator(psm->mi);
	if (fi->h)
	    fi->h = headerLink(fi->h);

	psm->mi = rpmdbFreeIterator(psm->mi);
	rc = (fi->h ? RPMRC_OK : RPMRC_FAIL);
	break;
    case PSM_RPMDB_ADD:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;
	if (fi->h == NULL)	break;	/* XXX can't happen */
	if (!(rpmtsVSFlags(ts) & RPMVSF_NOHDRCHK))
	    rc = rpmdbAdd(rpmtsGetRdb(ts), rpmtsGetTid(ts), fi->h,
				ts, headerCheck);
	else
	    rc = rpmdbAdd(rpmtsGetRdb(ts), rpmtsGetTid(ts), fi->h,
				NULL, NULL);
	break;
    case PSM_RPMDB_REMOVE:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;
	rc = rpmdbRemove(rpmtsGetRdb(ts), rpmtsGetTid(ts), fi->record,
				NULL, NULL);
	break;

    default:
	break;
/*@i@*/    }
    /*@=branchstate@*/

    /*@-nullstate@*/	/* FIX: psm->oh and psm->fi->h may be NULL. */
    return rc;
    /*@=nullstate@*/
}
/*@=bounds =nullpass@*/
