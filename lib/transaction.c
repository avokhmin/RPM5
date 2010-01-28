/** \ingroup rpmts
 * \file lib/transaction.c
 */

#include "system.h"

#include <rpmio.h>
#include <rpmiotypes.h>
#include <rpmlog.h>
#include <rpmmacro.h>	/* XXX for rpmExpand */
#include <rpmsx.h>
#include "fprint.h"

#include <rpmtypes.h>
#include <rpmtag.h>
#include <pkgio.h>

#define	_RPMDB_INTERNAL	/* XXX for dbiIndexFoo() */
#include <rpmdb.h>
#include "legacy.h"	/* XXX dodigest */

#define	_RPMFI_INTERNAL
#include <rpmfi.h>
#include "fsm.h"

#define	_RPMTE_INTERNAL
#include "rpmte.h"
#define	_RPMTS_INTERNAL
#include "rpmts.h"

#define	_RPMSQ_INTERNAL
#include "psm.h"

#include "rpmds.h"

#include "rpmlock.h"

#include "misc.h" /* XXX currentDirectory */

#if defined(RPM_VENDOR_MANDRIVA)
#include "filetriggers.h" /* XXX mayAddToFilesAwaitingFiletriggers, rpmRunFileTriggers */
#endif

#include <rpmcli.h>	/* XXX QVA_t INSTALL_FOO flags */
#include <rpmrollback.h>	/* IDTX prototypes */

#include "debug.h"

/*@access dbiIndexSet @*/

/*@access fnpyKey @*/

/*@access alKey @*/
/*@access rpmdb @*/	/* XXX cast */

/*@access rpmfi @*/
/*@access rpmps @*/	/* XXX need rpmProblemSetOK() */
/*@access rpmpsm @*/

/*@access rpmte @*/
/*@access rpmtsi @*/
/*@access rpmts @*/

/*@access IDT @*/
/*@access IDTX @*/
/*@access FD_t @*/

/**
 */
static int sharedCmp(const void * one, const void * two)
	/*@*/
{
    sharedFileInfo a = (sharedFileInfo) one;
    sharedFileInfo b = (sharedFileInfo) two;

    if (a->otherPkg < b->otherPkg)
	return -1;
    else if (a->otherPkg > b->otherPkg)
	return 1;

    return 0;
}

/**
 * handleInstInstalledFiles.
 * @param ts		transaction set
 * @param p		current transaction element
 * @param fi		file info set
 * @param shared	shared file info
 * @param sharedCount	no. of shared elements
 * @param reportConflicts
 * @return		0 on success
 *
 * XXX only ts->{probs,rpmdb} modified
 */
static int handleInstInstalledFiles(const rpmts ts,
		rpmte p, rpmfi fi,
		sharedFileInfo shared,
		int sharedCount, int reportConflicts)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies ts, p, fi, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    const char * altNVRA = NULL;
    rpmuint32_t tscolor = rpmtsColor(ts);
    rpmuint32_t prefcolor = rpmtsPrefColor(ts);
    rpmuint32_t otecolor, tecolor;
    rpmuint32_t oFColor, FColor;
    rpmuint32_t oFFlags, FFlags;
    rpmfi otherFi = NULL;
    rpmps ps;
    int xx;
    int i;

    {	rpmmi mi;
	Header h;
	int scareMem = 0;

	mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES,
			&shared->otherPkg, sizeof(shared->otherPkg));
	while ((h = rpmmiNext(mi)) != NULL) {
	    he->tag = RPMTAG_NVRA;
	    xx = headerGet(h, he, 0);
assert(he->p.str != NULL);
	    altNVRA = he->p.str;
	    otherFi = rpmfiNew(ts, h, RPMTAG_BASENAMES, scareMem);
	    break;
	}
	mi = rpmmiFree(mi);
    }

    /* Compute package color. */
    tecolor = rpmteColor(p);
    tecolor &= tscolor;

    /* Compute other pkg color. */
    otecolor = 0;
    otherFi = rpmfiInit(otherFi, 0);
    if (otherFi != NULL)
    while (rpmfiNext(otherFi) >= 0)
	otecolor |= rpmfiFColor(otherFi);
    otecolor &= tscolor;

    if (otherFi == NULL)
	return 1;

    p->replaced = xcalloc(sharedCount, sizeof(*p->replaced));
    p->nreplaced = 0;

    ps = rpmtsProblems(ts);
    for (i = 0; i < sharedCount; i++, shared++) {
	int otherFileNum, fileNum;

	otherFileNum = shared->otherFileNum;
	(void) rpmfiSetFX(otherFi, otherFileNum);
	oFFlags = rpmfiFFlags(otherFi);
	oFColor = rpmfiFColor(otherFi);
	oFColor &= tscolor;

	fileNum = shared->pkgFileNum;
	(void) rpmfiSetFX(fi, fileNum);
	FFlags = rpmfiFFlags(fi);
	FColor = rpmfiFColor(fi);
	FColor &= tscolor;

#ifdef	DYING
	/* XXX another tedious segfault, assume file state normal. */
	if (otherStates && otherStates[otherFileNum] != RPMFILE_STATE_NORMAL)
	    continue;
#endif

	if (iosmFileActionSkipped(fi->actions[fileNum]))
	    continue;

	/* Remove setuid/setgid bits on other (possibly hardlinked) files. */
	if (!(fi->mapflags & IOSM_SBIT_CHECK)) {
	    rpmuint16_t omode = rpmfiFMode(otherFi);
	    if (S_ISREG(omode) && (omode & 06000) != 0)
		fi->mapflags |= IOSM_SBIT_CHECK;
	}

	if (((FFlags | oFFlags) & RPMFILE_GHOST))
	    continue;

	if (rpmfiCompare(otherFi, fi)) {
	    int rConflicts;

	    rConflicts = reportConflicts;
	    /* Resolve file conflicts to prefer Elf64 (if not forced). */
	    if (tscolor != 0 && FColor != 0 && FColor != oFColor)
	    {
		if (oFColor & prefcolor) {
		    fi->actions[fileNum] = FA_SKIPCOLOR;
		    rConflicts = 0;
		} else
		if (FColor & prefcolor) {
		    fi->actions[fileNum] = FA_CREATE;
		    rConflicts = 0;
		}
	    }

	    if (rConflicts) {
		rpmpsAppend(ps, RPMPROB_FILE_CONFLICT,
			rpmteNEVRA(p), rpmteKey(p),
			rpmfiDN(fi), rpmfiBN(fi),
			altNVRA,
			0);
	    }

	    /* Save file identifier to mark as state REPLACED. */
	    if ( !(((FFlags | oFFlags) & RPMFILE_CONFIG) || iosmFileActionSkipped(fi->actions[fileNum])) ) {
		/*@-assignexpose@*/
		if (!shared->isRemoved)
		    p->replaced[p->nreplaced++] = *shared;
		/*@=assignexpose@*/
	    }
	}

	/* Determine config file dispostion, skipping missing files (if any). */
	if (((FFlags | oFFlags) & RPMFILE_CONFIG)) {
	    int skipMissing =
		((rpmtsFlags(ts) & RPMTRANS_FLAG_ALLFILES) ? 0 : 1);
	    iosmFileAction action = rpmfiDecideFate(otherFi, fi, skipMissing);
	    fi->actions[fileNum] = action;
	}
	fi->replacedSizes[fileNum] = rpmfiFSize(otherFi);
    }
    ps = rpmpsFree(ps);

    altNVRA = _free(altNVRA);
    otherFi = rpmfiFree(otherFi);

    p->replaced = xrealloc(p->replaced,
			   sizeof(*p->replaced) * (p->nreplaced + 1));
    memset(p->replaced + p->nreplaced, 0, sizeof(*p->replaced));

    return 0;
}

/**
 */
/* XXX only ts->rpmdb modified */
static int handleRmvdInstalledFiles(const rpmts ts, rpmfi fi,
		sharedFileInfo shared, int sharedCount)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies ts, fi, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    Header h;
    const unsigned char * otherStates;
    int i, xx;

    rpmmi mi;

    mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES,
			&shared->otherPkg, sizeof(shared->otherPkg));
    h = rpmmiNext(mi);
    if (h == NULL) {
	mi = rpmmiFree(mi);
	return 1;
    }

    he->tag = RPMTAG_FILESTATES;
    xx = headerGet(h, he, 0);
    otherStates = he->p.ptr;

    /* XXX there's an obscure segfault here w/o NULL check ... */
    if (otherStates != NULL)
    for (i = 0; i < sharedCount; i++, shared++) {
	int otherFileNum, fileNum;
	otherFileNum = shared->otherFileNum;
	fileNum = shared->pkgFileNum;

	if (otherStates[otherFileNum] != RPMFILE_STATE_NORMAL)
	    continue;

	fi->actions[fileNum] = FA_SKIP;
    }
    he->p.ptr = _free(he->p.ptr);
    mi = rpmmiFree(mi);

    return 0;
}

#define	ISROOT(_d)	(((_d)[0] == '/' && (_d)[1] == '\0') ? "" : (_d))

/*@unchecked@*/
int _fps_debug = 0;

static int fpsCompare (const void * one, const void * two)
	/*@*/
{
    const struct fingerPrint_s * a = (const struct fingerPrint_s *)one;
    const struct fingerPrint_s * b = (const struct fingerPrint_s *)two;
    size_t adnlen = strlen(a->entry->dirName);
    size_t asnlen = (a->subDir ? strlen(a->subDir) : 0);
    size_t abnlen = strlen(a->baseName);
    size_t bdnlen = strlen(b->entry->dirName);
    size_t bsnlen = (b->subDir ? strlen(b->subDir) : 0);
    size_t bbnlen = strlen(b->baseName);
    char * afn, * bfn, * t;
    int rc = 0;

    if (adnlen == 1 && asnlen != 0) adnlen = 0;
    if (bdnlen == 1 && bsnlen != 0) bdnlen = 0;

    afn = t = alloca(adnlen+asnlen+abnlen+2);
    if (adnlen) t = stpcpy(t, a->entry->dirName);
    *t++ = '/';
    if (a->subDir && asnlen) t = stpcpy(t, a->subDir);
    if (abnlen) t = stpcpy(t, a->baseName);
    if (afn[0] == '/' && afn[1] == '/') afn++;

    bfn = t = alloca(bdnlen+bsnlen+bbnlen+2);
    if (bdnlen) t = stpcpy(t, b->entry->dirName);
    *t++ = '/';
    if (b->subDir && bsnlen) t = stpcpy(t, b->subDir);
    if (bbnlen) t = stpcpy(t, b->baseName);
    if (bfn[0] == '/' && bfn[1] == '/') bfn++;

    rc = strcmp(afn, bfn);

    return rc;
}

/*@unchecked@*/
static int _linear_fps_search = 0;

static int findFps(const struct fingerPrint_s * fiFps,
		const struct fingerPrint_s * otherFps,
		int otherFc)
	/*@*/
{
    int otherFileNum;

  if (_linear_fps_search) {

linear:
    for (otherFileNum = 0; otherFileNum < otherFc; otherFileNum++, otherFps++) {

	/* If the addresses are the same, so are the values. */
	if (fiFps == otherFps)
	    break;

	/* Otherwise, compare fingerprints by value. */
	/*@-nullpass@*/	/* LCL: looks good to me */
	if (FP_EQUAL((*fiFps), (*otherFps)))
	    break;
	/*@=nullpass@*/
    }

    return otherFileNum;

  } else {

    const struct fingerPrint_s * bingoFps;

    bingoFps = bsearch(fiFps, otherFps, otherFc, sizeof(*otherFps), fpsCompare);
    if (bingoFps == NULL)
	goto linear;

    /* If the addresses are the same, so are the values. */
    if (!(fiFps == bingoFps || FP_EQUAL((*fiFps), (*bingoFps))))
	goto linear;

    otherFileNum = (bingoFps != NULL ? (bingoFps - otherFps) : 0);

  }

    return otherFileNum;
}

/**
 * Update disk space needs on each partition for this package's files.
 */
/* XXX only ts->{probs,di} modified */
static void handleOverlappedFiles(const rpmts ts,
		const rpmte p, rpmfi fi)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies ts, fi, fileSystem, internalState @*/
{
    rpmuint32_t fixupSize = 0;
    rpmps ps;
    const char * fn;
    int i, j;

    ps = rpmtsProblems(ts);
    fi = rpmfiInit(fi, 0);
    if (fi != NULL)
    while ((i = rpmfiNext(fi)) >= 0) {
	rpmuint32_t tscolor = rpmtsColor(ts);
	rpmuint32_t prefcolor = rpmtsPrefColor(ts);
	rpmuint32_t oFColor, FColor;
	struct fingerPrint_s * fiFps;
	int otherPkgNum, otherFileNum;
	rpmfi otherFi;
	rpmuint32_t FFlags;
	rpmuint16_t FMode;
	const rpmfi * recs;
	int numRecs;

	if (iosmFileActionSkipped(fi->actions[i]))
	    continue;

	fn = rpmfiFN(fi);
	fiFps = fi->fps + i;
	FFlags = rpmfiFFlags(fi);
	FMode = rpmfiFMode(fi);
	FColor = rpmfiFColor(fi);
	FColor &= tscolor;

	fixupSize = 0;

	/*
	 * Retrieve all records that apply to this file. Note that the
	 * file info records were built in the same order as the packages
	 * will be installed and removed so the records for an overlapped
	 * files will be sorted in exactly the same order.
	 */
	(void) htGetEntry(ts->ht, fiFps, &recs, &numRecs, NULL);

	/*
	 * If this package is being added, look only at other packages
	 * being added -- removed packages dance to a different tune.
	 *
	 * If both this and the other package are being added, overlapped
	 * files must be identical (or marked as a conflict). The
	 * disposition of already installed config files leads to
	 * a small amount of extra complexity.
	 *
	 * If this package is being removed, then there are two cases that
	 * need to be worried about:
	 * If the other package is being added, then skip any overlapped files
	 * so that this package removal doesn't nuke the overlapped files
	 * that were just installed.
	 * If both this and the other package are being removed, then each
	 * file removal from preceding packages needs to be skipped so that
	 * the file removal occurs only on the last occurence of an overlapped
	 * file in the transaction set.
	 *
	 */

	/* Locate this overlapped file in the set of added/removed packages. */
	for (j = 0; j < numRecs && recs[j] != fi; j++)
	    {};

	/* Find what the previous disposition of this file was. */
	otherFileNum = -1;			/* keep gcc quiet */
	otherFi = NULL;
	for (otherPkgNum = j - 1; otherPkgNum >= 0; otherPkgNum--) {
	    struct fingerPrint_s * otherFps;
	    int otherFc;

	    otherFi = recs[otherPkgNum];

	    /* Added packages need only look at other added packages. */
	    if (rpmteType(p) == TR_ADDED && rpmteType(otherFi->te) != TR_ADDED)
		/*@innercontinue@*/ continue;

	    otherFps = otherFi->fps;
	    otherFc = rpmfiFC(otherFi);

	    otherFileNum = findFps(fiFps, otherFps, otherFc);
	    (void) rpmfiSetFX(otherFi, otherFileNum);

	    /* XXX Happens iff fingerprint for incomplete package install. */
	    if (otherFi->actions[otherFileNum] != FA_UNKNOWN)
		/*@innerbreak@*/ break;
	}

	oFColor = rpmfiFColor(otherFi);
	oFColor &= tscolor;

	switch (rpmteType(p)) {
	case TR_ADDED:
	  { int reportConflicts =
		!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_REPLACENEWFILES);
	    int done = 0;

	    if (otherPkgNum < 0) {
		/* XXX is this test still necessary? */
		if (fi->actions[i] != FA_UNKNOWN)
		    /*@switchbreak@*/ break;
		if ((FFlags & RPMFILE_CONFIG) && (FFlags & RPMFILE_EXISTS)) {
		    /* Here is a non-overlapped pre-existing config file. */
		    fi->actions[i] = (FFlags & RPMFILE_NOREPLACE)
			? FA_ALTNAME : FA_BACKUP;
		} else {
		    fi->actions[i] = FA_CREATE;
		}
		/*@switchbreak@*/ break;
	    }

assert(otherFi != NULL);
	    /* Mark added overlapped non-identical files as a conflict. */
	    if (rpmfiCompare(otherFi, fi)) {
		int rConflicts;

		rConflicts = reportConflicts;
		/* Resolve file conflicts to prefer Elf64 (if not forced) ... */
		if (tscolor != 0) {
		    if (FColor & prefcolor) {
			/* ... last file of preferred colour is installed ... */
			if (!iosmFileActionSkipped(fi->actions[i])) {
			    /* XXX static helpers are order dependent. Ick. */
			    if (strcmp(fn, "/usr/sbin/libgcc_post_upgrade")
			     && strcmp(fn, "/usr/sbin/glibc_post_upgrade"))
				otherFi->actions[otherFileNum] = FA_SKIPCOLOR;
			}
			fi->actions[i] = FA_CREATE;
			rConflicts = 0;
		    } else
		    if (oFColor & prefcolor) {
			/* ... first file of preferred colour is installed ... */
			if (iosmFileActionSkipped(fi->actions[i]))
			    otherFi->actions[otherFileNum] = FA_CREATE;
			fi->actions[i] = FA_SKIPCOLOR;
			rConflicts = 0;
		    } else
		    if (FColor == 0 && oFColor == 0) {
			/* ... otherwise, do both, last in wins. */
			otherFi->actions[otherFileNum] = FA_CREATE;
			fi->actions[i] = FA_CREATE;
			rConflicts = 0;
		    }
		    done = 1;
		}

		if (rConflicts) {
		    rpmpsAppend(ps, RPMPROB_NEW_FILE_CONFLICT,
			rpmteNEVR(p), rpmteKey(p),
			fn, NULL,
			rpmteNEVR(otherFi->te),
			0);
		}
	    }

	    /* Try to get the disk accounting correct even if a conflict. */
	    fixupSize = rpmfiFSize(otherFi);

	    if ((FFlags & RPMFILE_CONFIG) && (FFlags & RPMFILE_EXISTS)) {
		/* Here is an overlapped  pre-existing config file. */
		fi->actions[i] = (FFlags & RPMFILE_NOREPLACE)
			? FA_ALTNAME : FA_SKIP;
	    } else {
		if (!done)
		    fi->actions[i] = FA_CREATE;
	    }
	  } /*@switchbreak@*/ break;

	case TR_REMOVED:
	    if (otherPkgNum >= 0) {
assert(otherFi != NULL);
		/* Here is an overlapped added file we don't want to nuke. */
		if (otherFi->actions[otherFileNum] != FA_ERASE) {
		    /* On updates, don't remove files. */
		    fi->actions[i] = FA_SKIP;
		    /*@switchbreak@*/ break;
		}
		/* Here is an overlapped removed file: skip in previous. */
		otherFi->actions[otherFileNum] = FA_SKIP;
	    }
	    if (iosmFileActionSkipped(fi->actions[i]))
		/*@switchbreak@*/ break;
	    if (rpmfiFState(fi) != RPMFILE_STATE_NORMAL)
		/*@switchbreak@*/ break;

	    /* Disposition is assumed to be FA_ERASE. */
	    fi->actions[i] = FA_ERASE;
	    if (!(S_ISREG(FMode) && (FFlags & RPMFILE_CONFIG)))
		/*@switchbreak@*/ break;
		
	    /* Check for pre-existing modified config file that needs saving. */
	    if (!(FFlags & RPMFILE_SPARSE))
	    {	int dalgo = 0;
		size_t dlen = 0;
		const unsigned char * digest = rpmfiDigest(fi, &dalgo, &dlen);
		unsigned char * fdigest;
assert(digest != NULL);
		
		fdigest = xcalloc(1, dlen);
		/* Save (by renaming) locally modified config files. */
		if (!dodigest(dalgo, fn, fdigest, 0, NULL)
		 && memcmp(digest, fdigest, dlen))
		    fi->actions[i] = FA_BACKUP;
		fdigest = _free(fdigest);
	    }
	    /*@switchbreak@*/ break;
	}

	/* Update disk space info for a file. */
	rpmtsUpdateDSI(ts, fiFps->entry->dev, rpmfiFSize(fi),
		fi->replacedSizes[i], fixupSize, fi->actions[i]);

    }
    ps = rpmpsFree(ps);
}

/**
 * Ensure that current package is newer than installed package.
 * @param ts		transaction set
 * @param p		current transaction element
 * @param h		installed header
 * @return		0 if not newer, 1 if okay
 */
/*@-nullpass@*/
static int ensureOlder(rpmts ts,
		const rpmte p, const Header h)
	/*@globals internalState @*/
	/*@modifies ts, internalState @*/
{
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    rpmuint32_t reqFlags = (RPMSENSE_LESS | RPMSENSE_EQUAL);
    const char * reqEVR;
    rpmds req;
    char * t;
    size_t nb;
    int rc;

    if (p == NULL || h == NULL)
	return 1;

    nb = strlen(rpmteNEVR(p)) + (rpmteE(p) != NULL ? strlen(rpmteE(p)) : 0) + 1;
#ifdef	RPM_VENDOR_MANDRIVA
    nb += (rpmteD(p) != NULL ? strlen(rpmteD(p)) + 1 : 0);
#endif
    t = alloca(nb);
    *t = '\0';
    reqEVR = t;
    if (rpmteE(p) != NULL)	t = stpcpy( stpcpy(t, rpmteE(p)), ":");
    if (rpmteV(p) != NULL)	t = stpcpy(t, rpmteV(p));
    *t++ = '-';
    if (rpmteR(p) != NULL)	t = stpcpy(t, rpmteR(p));
#ifdef RPM_VENDOR_MANDRIVA
    if (rpmteD(p) != NULL)	*t++ = ':', t = stpcpy(t, rpmteD(p));
#endif

    req = rpmdsSingle(RPMTAG_REQUIRENAME, rpmteN(p), reqEVR, reqFlags);
    rc = rpmdsNVRMatchesDep(h, req, _rpmds_nopromote);
    (void)rpmdsFree(req);
    req = NULL;

    if (rc == 0) {
	rpmps ps = rpmtsProblems(ts);
	he->tag = RPMTAG_NVRA;
	rc = headerGet(h, he, 0);
assert(he->p.str != NULL);
	rpmpsAppend(ps, RPMPROB_OLDPACKAGE,
		rpmteNEVR(p), rpmteKey(p),
		NULL, NULL,
		he->p.str,
		0);
	he->p.ptr = _free(he->p.ptr);
	ps = rpmpsFree(ps);
	rc = 1;
    } else
	rc = 0;

    return rc;
}
/*@=nullpass@*/

/**
 * Skip any files that do not match install policies.
 * @param ts		transaction set
 * @param fi		file info set
 */
/*@-mustmod@*/ /* FIX: fi->actions is modified. */
/*@-nullpass@*/
static void skipFiles(const rpmts ts, rpmfi fi)
	/*@globals rpmGlobalMacroContext, h_errno, internalState @*/
	/*@modifies fi, rpmGlobalMacroContext, internalState @*/
{
    rpmuint32_t tscolor = rpmtsColor(ts);
    rpmuint32_t FColor;
    int noConfigs = (rpmtsFlags(ts) & RPMTRANS_FLAG_NOCONFIGS);
    int noDocs = (rpmtsFlags(ts) & RPMTRANS_FLAG_NODOCS);
    ARGV_t netsharedPaths = NULL;
    ARGV_t languages = NULL;
    const char * dn, * bn;
    size_t dnlen, bnlen;
    int ix;
    const char * s;
    int * drc;
    char * dff;
    int dc;
    int i, j;
    int xx;

#if defined(RPM_VENDOR_OPENPKG) /* allow-excludedocs-default */
    /* The "%_excludedocs" macro is intended to set the _default_ if
       both --excludedocs and --includedocs are not specified and it
       is evaluated already before. So, do not override it here again,
       because it would not allow us to make "%_excludedocs 1" the
       default. */
#else
    if (!noDocs)
	noDocs = rpmExpandNumeric("%{_excludedocs}");
#endif

    {	const char *tmpPath = rpmExpand("%{?_netsharedpath}", NULL);
	if (tmpPath && *tmpPath)
	    xx = argvSplit(&netsharedPaths, tmpPath, ":");
	tmpPath = _free(tmpPath);
    }

    s = rpmExpand("%{?_install_langs}", NULL);
    if (!(s && *s))
	s = _free(s);
    if (s) {
	xx = argvSplit(&languages, s, ":");
	s = _free(s);
    }

    /* Compute directory refcount, skip directory if now empty. */
    dc = rpmfiDC(fi);
    drc = alloca(dc * sizeof(*drc));
    memset(drc, 0, dc * sizeof(*drc));
    dff = alloca(dc * sizeof(*dff));
    memset(dff, 0, dc * sizeof(*dff));

    fi = rpmfiInit(fi, 0);
    if (fi != NULL)	/* XXX lclint */
    while ((i = rpmfiNext(fi)) >= 0)
    {
	ARGV_t nsp;

	bn = rpmfiBN(fi);
	bnlen = strlen(bn);
	ix = rpmfiDX(fi);
	dn = rpmfiDN(fi);
	if (dn == NULL)
	    continue;	/* XXX can't happen */
	dnlen = strlen(dn);

	drc[ix]++;

	/* Don't bother with skipped files */
	if (iosmFileActionSkipped(fi->actions[i])) {
	    drc[ix]--; dff[ix] = 1;
	    continue;
	}

	/* Ignore colored files not in our rainbow. */
	FColor = rpmfiFColor(fi);
	if (tscolor && FColor && !(tscolor & FColor)) {
	    drc[ix]--;	dff[ix] = 1;
	    fi->actions[i] = FA_SKIPCOLOR;
	    continue;
	}

	/*
	 * Skip net shared paths.
	 * Net shared paths are not relative to the current root (though
	 * they do need to take package relocations into account).
	 */
	for (nsp = netsharedPaths; nsp && *nsp; nsp++) {
	    size_t len;

	    len = strlen(*nsp);
	    if (dnlen >= len) {
		if (strncmp(dn, *nsp, len))
		    /*@innercontinue@*/ continue;
		/* Only directories or complete file paths can be net shared */
		if (!(dn[len] == '/' || dn[len] == '\0'))
		    /*@innercontinue@*/ continue;
	    } else {
		if (len < (dnlen + bnlen))
		    /*@innercontinue@*/ continue;
		if (strncmp(dn, *nsp, dnlen))
		    /*@innercontinue@*/ continue;
		/* Insure that only the netsharedpath basename is compared. */
		if ((s = strchr((*nsp) + dnlen, '/')) != NULL && s[1] != '\0')
		    /*@innercontinue@*/ continue;
		if (strncmp(bn, (*nsp) + dnlen, bnlen))
		    /*@innercontinue@*/ continue;
		len = dnlen + bnlen;
		/* Only directories or complete file paths can be net shared */
		if (!((*nsp)[len] == '/' || (*nsp)[len] == '\0'))
		    /*@innercontinue@*/ continue;
	    }

	    /*@innerbreak@*/ break;
	}

	if (nsp && *nsp) {
	    drc[ix]--;	dff[ix] = 1;
	    fi->actions[i] = FA_SKIPNETSHARED;
	    continue;
	}

	/*
	 * Skip i18n language specific files.
	 */
	if (languages != NULL && fi->flangs != NULL && *fi->flangs[i]) {
	    ARGV_t lang;
	    const char *l, *le;
	    for (lang = languages; *lang != NULL; lang++) {
		if (!strcmp(*lang, "all"))
		    /*@innerbreak@*/ break;
		for (l = fi->flangs[i]; *l != '\0'; l = le) {
		    for (le = l; *le != '\0' && *le != '|'; le++)
			{};
		    if ((le-l) > 0 && !strncmp(*lang, l, (le-l)))
			/*@innerbreak@*/ break;
		    if (*le == '|') le++;	/* skip over | */
		}
		if (*l != '\0')
		    /*@innerbreak@*/ break;
	    }
	    if (*lang == NULL) {
		drc[ix]--;	dff[ix] = 1;
		fi->actions[i] = FA_SKIPNSTATE;
		continue;
	    }
	}

	/*
	 * Skip config files if requested.
	 */
	if (noConfigs && (rpmfiFFlags(fi) & RPMFILE_CONFIG)) {
	    drc[ix]--;	dff[ix] = 1;
	    fi->actions[i] = FA_SKIPNSTATE;
	    continue;
	}

	/*
	 * Skip documentation if requested.
	 */
	if (noDocs && (rpmfiFFlags(fi) & RPMFILE_DOC)) {
	    drc[ix]--;	dff[ix] = 1;
	    fi->actions[i] = FA_SKIPNSTATE;
	    continue;
	}
    }

    /* Skip (now empty) directories that had skipped files. */
#ifndef	NOTYET
    if (fi != NULL)	/* XXX can't happen */
    for (j = 0; j < dc; j++)
#else
    if ((fi = rpmfiInitD(fi)) != NULL)
    while (j = rpmfiNextD(fi) >= 0)
#endif
    {

	if (drc[j]) continue;	/* dir still has files. */
	if (!dff[j]) continue;	/* dir was not emptied here. */
	
	/* Find parent directory and basename. */
	dn = fi->dnl[j];	dnlen = strlen(dn) - 1;
	bn = dn + dnlen;	bnlen = 0;
	while (bn > dn && bn[-1] != '/') {
		bnlen++;
		dnlen--;
		bn--;
	}

	/* If explicitly included in the package, skip the directory. */
	fi = rpmfiInit(fi, 0);
	if (fi != NULL)		/* XXX lclint */
	while ((i = rpmfiNext(fi)) >= 0) {
	    const char * fdn, * fbn;
	    rpmuint16_t fFMode;

	    if (iosmFileActionSkipped(fi->actions[i]))
		/*@innercontinue@*/ continue;

	    fFMode = rpmfiFMode(fi);

	    if (!S_ISDIR(fFMode))
		/*@innercontinue@*/ continue;
	    fdn = rpmfiDN(fi);
	    if (strlen(fdn) != dnlen)
		/*@innercontinue@*/ continue;
	    if (strncmp(fdn, dn, dnlen))
		/*@innercontinue@*/ continue;
	    fbn = rpmfiBN(fi);
	    if (strlen(fbn) != bnlen)
		/*@innercontinue@*/ continue;
	    if (strncmp(fbn, bn, bnlen))
		/*@innercontinue@*/ continue;
	    rpmlog(RPMLOG_DEBUG, D_("excluding directory %s\n"), dn);
	    fi->actions[i] = FA_SKIPNSTATE;
	    /*@innerbreak@*/ break;
	}
    }

/*@-dependenttrans@*/
    netsharedPaths = argvFree(netsharedPaths);
    languages = argvFree(languages);
/*@=dependenttrans@*/
}
/*@=nullpass@*/
/*@=mustmod@*/

/**
 * Return transaction element's file info.
 * @todo Take a rpmfi refcount here.
 * @param tsi		transaction element iterator
 * @return		transaction element file info
 */
static /*@null@*/
rpmfi rpmtsiFi(const rpmtsi tsi)
	/*@*/
{
    rpmfi fi = NULL;

    if (tsi != NULL && tsi->ocsave != -1) {
	/*@-type -abstract@*/ /* FIX: rpmte not opaque */
	rpmte te = rpmtsElement(tsi->ts, tsi->ocsave);
	/*@-assignexpose@*/
	if (te != NULL && (fi = te->fi) != NULL)
	    fi->te = te;
	/*@=assignexpose@*/
	/*@=type =abstract@*/
    }
    /*@-compdef -refcounttrans -usereleased @*/
    return fi;
    /*@=compdef =refcounttrans =usereleased @*/
}

/**
 * Search for string B in argv array AV.
 * @param AV		argv array
 * @param B		string
 * @return		1 if found, 0 otherwise
 */
static int cmpArgvStr(/*@null@*/ const char ** AV, /*@null@*/ const char * B)
	/*@*/
{
    const char ** a;

    if (AV != NULL && B != NULL)
    for (a = AV; *a != NULL; a++) {
	if (**a && *B && !strcmp(*a, B))
	    return 1;
    }
    return 0;
}

/**
 * Mark all erasure elements linked to installed element p as failed.
 * @param ts		transaction set
 * @param p		failed install transaction element
 * @return		0 always
 */
static int markLinkedFailed(rpmts ts, rpmte p)
	/*@globals fileSystem @*/
	/*@modifies ts, p, fileSystem @*/
{
    rpmtsi qi; rpmte q;
    int bingo;

    p->linkFailed = 1;

    qi = rpmtsiInit(ts);
    while ((q = rpmtsiNext(qi, TR_REMOVED)) != NULL) {

	if (q->done)
	    continue;

	/*
	 * Either element may have missing data and can have multiple entries.
	 * Try for hdrid, then pkgid, finally NEVRA, argv vs. argv compares.
	 */
	bingo = cmpArgvStr(q->flink.Hdrid, p->hdrid);
	if (!bingo)
		bingo = cmpArgvStr(q->flink.Pkgid, p->pkgid);
	if (!bingo)
		bingo = cmpArgvStr(q->flink.NEVRA, p->NEVRA);

	if (!bingo)
	    continue;

	q->linkFailed = p->linkFailed;
    }
    qi = rpmtsiFree(qi);

    return 0;
}

/* ================================================================= */

/*
 * For packages being installed:
 * - verify package arch/os.
 * - verify package epoch:version-release is newer.
 */
static rpmps checkProblems(rpmts ts, uint32_t * tfcp)
{
    rpmps ps;
    rpmtsi pi;
    rpmte p;
#ifdef	REFERENCE
    rpm_color_t tscolor = rpmtsColor(ts);
    ps = rpmpsCreate();
    pi = rpmtsiInit(ts);

    /* The ordering doesn't matter here */
    /* XXX Only added packages need be checked. */
    rpmlog(RPMLOG_DEBUG, "sanity checking %d elements\n", rpmtsNElements(ts));
    while ((p = rpmtsiNext(pi, TR_ADDED)) != NULL) {
	rpmdbMatchIterator mi;
	rpmpsi psi;

	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_IGNOREARCH))
	    if (!archOkay(rpmteA(p)))
		rpmpsAppend(ps, RPMPROB_BADARCH,
			rpmteNEVRA(p), rpmteKey(p),
			rpmteA(p), NULL,
			NULL, 0);

	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_IGNOREOS))
	    if (!osOkay(rpmteO(p)))
		rpmpsAppend(ps, RPMPROB_BADOS,
			rpmteNEVRA(p), rpmteKey(p),
			rpmteO(p), NULL,
			NULL, 0);

	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_OLDPACKAGE)) {
	    Header h;
	    mi = rpmtsInitIterator(ts, RPMTAG_NAME, rpmteN(p), 0);
	    while ((h = rpmdbNextIterator(mi)) != NULL)
		ensureOlder(p, h, ps);
	    mi = rpmdbFreeIterator(mi);
	}

	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_REPLACEPKG)) {
	    mi = rpmtsInitIterator(ts, RPMTAG_NAME, rpmteN(p), 0);
	    rpmdbSetIteratorRE(mi, RPMTAG_EPOCH, RPMMIRE_STRCMP, rpmteE(p));
	    rpmdbSetIteratorRE(mi, RPMTAG_VERSION, RPMMIRE_STRCMP, rpmteV(p));
	    rpmdbSetIteratorRE(mi, RPMTAG_RELEASE, RPMMIRE_STRCMP, rpmteR(p));
	    if (tscolor) {
		rpmdbSetIteratorRE(mi, RPMTAG_ARCH, RPMMIRE_STRCMP, rpmteA(p));
		rpmdbSetIteratorRE(mi, RPMTAG_OS, RPMMIRE_STRCMP, rpmteO(p));
	    }

	    while (rpmdbNextIterator(mi) != NULL) {
		rpmpsAppend(ps, RPMPROB_PKG_INSTALLED,
			rpmteNEVRA(p), rpmteKey(p),
			NULL, NULL,
			NULL, 0);
		break;
	    }
	    mi = rpmdbFreeIterator(mi);
	}

	/* XXX rpmte problems can only be relocation problems atm */
	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_FORCERELOCATE)) {
	    psi = rpmpsInitIterator(rpmteProblems(p));
	    while (rpmpsNextIterator(psi) >= 0) {
		rpmpsAppendProblem(ps, rpmpsGetProblem(psi));
	    }
	    rpmpsFreeIterator(psi);
	}
    }
    pi = rpmtsiFree(pi);

#else	/* REFERENCE */

    uint32_t totalFileCount = 0;
    rpmfi fi;
    int fc;

rpmlog(RPMLOG_DEBUG, D_("sanity checking %d elements\n"), rpmtsNElements(ts));
    ps = rpmtsProblems(ts);
    /* The ordering doesn't matter here */
    pi = rpmtsiInit(ts);
    /* XXX Only added packages need be checked. */
    while ((p = rpmtsiNext(pi, TR_ADDED)) != NULL) {
	rpmmi mi;
	int xx;

	if (p->isSource) continue;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	fc = rpmfiFC(fi);

	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_OLDPACKAGE)) {
	    Header h;
	    mi = rpmtsInitIterator(ts, RPMTAG_NAME, rpmteN(p), 0);
	    while ((h = rpmmiNext(mi)) != NULL)
		xx = ensureOlder(ts, p, h);
	    mi = rpmmiFree(mi);
	}

	if (!(rpmtsFilterFlags(ts) & RPMPROB_FILTER_REPLACEPKG)) {
	    ARGV_t keys = NULL;
	    int nkeys;
	    xx = rpmdbMireApply(rpmtsGetRdb(ts), RPMTAG_NVRA,
		RPMMIRE_STRCMP, rpmteNEVRA(p), &keys);
	    nkeys = argvCount(keys);
	    if (nkeys > 0)
		rpmpsAppend(ps, RPMPROB_PKG_INSTALLED,
			rpmteNEVR(p), rpmteKey(p),
			NULL, NULL,
			NULL, 0);
	    keys = argvFree(keys);
	}

	/* Count no. of files (if any). */
	totalFileCount += fc;

    }
    pi = rpmtsiFree(pi);

    /* The ordering doesn't matter here */
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, TR_REMOVED)) != NULL) {

	if (p->isSource) continue;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	fc = rpmfiFC(fi);

	totalFileCount += fc;
    }
    pi = rpmtsiFree(pi);

    if (tfcp)
	*tfcp = totalFileCount;
#endif	/* REFERENCE */

    return ps;
}

/*
 * Run pre/post transaction script.
 * param ts	transaction set
 * param stag	RPMTAG_PRETRANS or RPMTAG_POSTTRANS
 * return	0 on success
 */
static int runTransScript(rpmts ts, rpmTag stag) 
{
    rpmtsi pi; 
    rpmte p;
    rpmpsm psm;
    int xx;
#ifdef	REFERENCE
    rpmTag progtag = RPMTAG_NOT_FOUND;

    if (stag == RPMTAG_PRETRANS) {
	progtag = RPMTAG_PRETRANSPROG;
    } else if (stag == RPMTAG_POSTTRANS) {
	progtag = RPMTAG_POSTTRANSPROG;
    } else {
	return -1;
    }

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, TR_ADDED)) != NULL) {
    	/* If no pre/post-transaction script, then don't bother. */
	if (!rpmteHaveTransScript(p, stag))
	    continue;

    	if (rpmteOpen(p, ts, 0)) {
	    psm = rpmpsmNew(ts, p);
	    xx = rpmpsmScriptStage(psm, stag, progtag);
	    psm = rpmpsmFree(psm);
	    rpmteClose(p, ts, 0);
	}
    }
    pi = rpmtsiFree(pi);

#else	/* REFERENCE */
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, TR_ADDED)) != NULL)
    switch (stag) {
    default:
assert(0);
	break;
    case RPMTAG_PRETRANS:
	{   rpmfi fi;

	    if (p->isSource) continue;
	    if ((fi = rpmtsiFi(pi)) == NULL)
		continue;	/* XXX can't happen */

	    /* If no pre-transaction script, then don't bother. */
	    if (fi->pretrans == NULL)
		continue;

	    p->h = NULL;
	    p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_OPEN_FILE, 0, 0);
	    if (rpmteFd(p) != NULL) {
		rpmVSFlags ovsflags = rpmtsVSFlags(ts);
		rpmVSFlags vsflags = ovsflags | RPMVSF_NEEDPAYLOAD;
		rpmRC rpmrc;
		ovsflags = rpmtsSetVSFlags(ts, vsflags);
		rpmrc = rpmReadPackageFile(ts, rpmteFd(p),
			    rpmteNEVR(p), &p->h);
		vsflags = rpmtsSetVSFlags(ts, ovsflags);
		switch (rpmrc) {
		default:
		    p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_CLOSE_FILE, 0, 0);
		    p->fd = NULL;
		    /*@switchbreak@*/ break;
		case RPMRC_NOTTRUSTED:
		case RPMRC_NOKEY:
		case RPMRC_OK:
		    /*@switchbreak@*/ break;
		}
	    }

	    if (rpmteFd(p) != NULL) {
		int scareMem = 0;

		rpmfi ofi = rpmfiLink(p->fi, "pretrans");
		(void)rpmfiFree(p->fi);
		p->fi = rpmfiNew(ts, p->h, RPMTAG_BASENAMES, scareMem);
		if (p->fi != NULL)
		    p->fi->te = p;

/*@-compdef -usereleased@*/	/* p->fi->te undefined */
		psm = rpmpsmNew(ts, p, p->fi);
/*@=compdef =usereleased@*/
assert(psm != NULL);
		psm->stepName = "pretrans";
		psm->scriptTag = RPMTAG_PRETRANS;
		psm->progTag = RPMTAG_PRETRANSPROG;
		xx = rpmpsmStage(psm, PSM_SCRIPT);
		psm = rpmpsmFree(psm, __FUNCTION__);

		(void)rpmfiFree(p->fi);
		p->fi = rpmfiLink(ofi, "pretrans");
		(void)rpmfiFree(ofi);
		ofi = NULL;

/*@-compdef -usereleased @*/
		p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_CLOSE_FILE, 0, 0);
/*@=compdef =usereleased @*/
		p->fd = NULL;
		(void)headerFree(p->h);
		p->h = NULL;
	    }
	}
	break;
    case RPMTAG_POSTTRANS:
	{   rpmfi fi;
	    int haspostscript;

	    if (p->isSource) continue;
	    if ((fi = rpmtsiFi(pi)) == NULL)
	    	continue;	/* XXX can't happen */

	    haspostscript = (fi->posttrans || fi->posttransprog ? 1 : 0);
	    p->fi = rpmfiFree(p->fi);

	    /* If no post-transaction script, then don't bother. */
	    if (!haspostscript)
	    	continue;

	    p->h = NULL;
	    p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_OPEN_FILE, 0, 0);
	    if (rpmteFd(p) != NULL) {
	    	rpmVSFlags ovsflags = rpmtsVSFlags(ts);
	    	rpmVSFlags vsflags = ovsflags | RPMVSF_NEEDPAYLOAD;
	    	rpmRC rpmrc;
	    	ovsflags = rpmtsSetVSFlags(ts, vsflags);
	    	rpmrc = rpmReadPackageFile(ts, rpmteFd(p),
	    	    	rpmteNEVR(p), &p->h);
	    	vsflags = rpmtsSetVSFlags(ts, ovsflags);
	    	switch (rpmrc) {
	    	default:
		    p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_CLOSE_FILE,
					0, 0);
	    	    p->fd = NULL;
	    	    /*@switchbreak@*/ break;
	    	case RPMRC_NOTTRUSTED:
	    	case RPMRC_NOKEY:
	    	case RPMRC_OK:
	    	    /*@switchbreak@*/ break;
	    	}
	    }

/*@-nullpass@*/
	    if (rpmteFd(p) != NULL) {
		int scareMem = 0;
	    	p->fi = rpmfiNew(ts, p->h, RPMTAG_BASENAMES, scareMem);
	    	if (p->fi != NULL)	/* XXX can't happen */
	    	    p->fi->te = p;
/*@-compdef -usereleased@*/	/* p->fi->te undefined */
	    	psm = rpmpsmNew(ts, p, p->fi);
/*@=compdef =usereleased@*/
assert(psm != NULL);
	    	psm->stepName = "posttrans";
	    	psm->scriptTag = RPMTAG_POSTTRANS;
	    	psm->progTag = RPMTAG_POSTTRANSPROG;
	    	xx = rpmpsmStage(psm, PSM_SCRIPT);
	    	psm = rpmpsmFree(psm, __FUNCTION__);

/*@-compdef -usereleased @*/
		p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_CLOSE_FILE, 0, 0);
/*@=compdef =usereleased @*/
	    	p->fd = NULL;
	    	p->fi = rpmfiFree(p->fi);
	    	(void)headerFree(p->h);
		p->h = NULL;
	    }
/*@=nullpass@*/
	}
	break;
    }
    pi = rpmtsiFree(pi);
#endif	/* REFERENCE */

    return 0;
}

/* Add fingerprint for each file not skipped. */
static void addFingerprints(rpmts ts, uint32_t fileCount, hashTable ht,
		fingerPrintCache fpc)
{
    rpmtsi pi;
    rpmte p;
    rpmfi fi;
    int i;

#ifdef	REFERENCE
    hashTable symlinks = htCreate(fileCount/16+16, 0, 0, fpHashFunction, fpEqual);
#endif

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	(void) rpmdbCheckSignals();

	if (p->isSource) continue;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */

	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), 0);

	rpmfiFpLookup(fi, fpc);

	/* Collect symlinks. */
 	fi = rpmfiInit(fi, 0);
 	if (fi != NULL)		/* XXX lclint */
	while ((i = rpmfiNext(fi)) >= 0) {
#ifdef	REFERENCE
	    char const *linktarget;
	    linktarget = rpmfiFLink(fi);
	    if (!(linktarget && *linktarget != '\0'))
		continue;
#endif
	    if (iosmFileActionSkipped(fi->actions[i]))
		/*@innercontinue@*/ continue;
#ifdef	REFERENCE
	    {	struct rpmffi_s ffi;
		ffi.p = p;
		ffi.fileno = i;
		htAddEntry(symlinks, rpmfiFpsIndex(fi, i), ffi);
	    }
#else
	    /*@-dependenttrans@*/
	    htAddEntry(ts->ht, fi->fps + i, (void *) fi);
	    /*@=dependenttrans@*/
#endif
	}

	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), rpmfiFC(fi));

    }
    pi = rpmtsiFree(pi);

#ifdef	REFERENCE
    /* ===============================================
     * Check fingerprints if they contain symlinks
     * and add them to the hash table
     */

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	(void) rpmdbCheckSignals();

	if ((fi = rpmteFI(p)) == NULL)
	    continue;	/* XXX can't happen */
	fi = rpmfiInit(fi, 0);
	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), 0);
	while ((i = rpmfiNext(fi)) >= 0) {
	    if (XFA_SKIPPING(rpmfsGetAction(rpmteGetFileStates(p), i)))
		continue;
	    fpLookupSubdir(symlinks, ht, fpc, p, i);
	}
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), 0);
    }
    pi = rpmtsiFree(pi);

    symlinks = htFree(symlinks);
#endif

}

static int rpmtsSetup(rpmts ts, rpmprobFilterFlags ignoreSet, rpmsx * sxp)
{
    int xx;

#ifdef	REFERENCE
    rpm_tid_t tid = (rpm_tid_t) time(NULL);
    int dbmode = (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST) ?  O_RDONLY : (O_RDWR|O_CREAT);

    if (rpmtsFlags(ts) & RPMTRANS_FLAG_NOSCRIPTS)
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | _noTransScripts | _noTransTriggers));
    if (rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERS)
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | _noTransTriggers));

    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | _noTransScripts | _noTransTriggers));

    /* if SELinux isn't enabled or init fails, don't bother... */
    if (!rpmtsSELinuxEnabled(ts)) {
        rpmtsSetFlags(ts, (rpmtsFlags(ts) | RPMTRANS_FLAG_NOCONTEXTS));
    }

    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOCONTEXTS)) {
	char *fn = rpmGetPath("%{?_install_file_context_path}", NULL);
	if (matchpathcon_init(fn) == -1) {
	    rpmtsSetFlags(ts, (rpmtsFlags(ts) | RPMTRANS_FLAG_NOCONTEXTS));
	}
	free(fn);
    }

    /* XXX Make sure the database is open RDWR for package install/erase. */
    if (rpmtsOpenDB(ts, dbmode)) {
	return -1;	/* XXX W2DO? */
    }

    ts->ignoreSet = ignoreSet;
    ts->probs = rpmpsFree(ts->probs);

    {	char * currDir = rpmGetCwd();
	rpmtsSetCurrDir(ts, currDir);
	currDir = _free(currDir);
    }

    (void) rpmtsSetChrootDone(ts, 0);
    (void) rpmtsSetTid(ts, tid);

    /* Get available space on mounted file systems. */
    (void) rpmtsInitDSI(ts);

#else	/* REFERENCE */

    /* --noscripts implies no scripts or triggers, duh. */
    if (rpmtsFlags(ts) & RPMTRANS_FLAG_NOSCRIPTS)
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | _noTransScripts | _noTransTriggers));
    /* --notriggers implies no triggers, duh. */
    if (rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERS)
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | _noTransTriggers));

    /* --justdb implies no scripts or triggers, duh. */
    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | _noTransScripts | _noTransTriggers));

    /* if SELinux isn't enabled or init fails, don't bother... */
    if (!rpmtsSELinuxEnabled(ts))
	(void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | (RPMTRANS_FLAG_NOCONTEXTS|RPMTRANS_FLAG_NOPOLICY)));

    if (!(rpmtsFlags(ts) & (RPMTRANS_FLAG_NOCONTEXTS|RPMTRANS_FLAG_NOPOLICY))) {
	*sxp = rpmsxNew("%{?_install_file_context_path}", 0);
        if (*sxp == NULL)
	    (void) rpmtsSetFlags(ts, (rpmtsFlags(ts) | (RPMTRANS_FLAG_NOCONTEXTS|RPMTRANS_FLAG_NOPOLICY)));
    } else
	*sxp = NULL;

    /* XXX Make sure the database is open RDWR for package install/erase. */
    {	int dbmode = O_RDONLY;

	if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)) {
	    rpmtsi pi;
	    rpmte p;
	    pi = rpmtsiInit(ts);
	    while ((p = rpmtsiNext(pi, 0)) != NULL) {
		if (p->isSource) continue;
		dbmode = (O_RDWR|O_CREAT);
		break;
	    }
	    pi = rpmtsiFree(pi);
	}

	/* Open database RDWR for installing packages. */
	if (rpmtsOpenDB(ts, dbmode))
	    return -1;	/* XXX W2DO? */

    }

    ts->ignoreSet = ignoreSet;
    ts->probs = rpmpsFree(ts->probs);

    {	const char * currDir = currentDirectory();
	rpmtsSetCurrDir(ts, currDir);
	currDir = _free(currDir);
    }

    /* XXX rpmtsCreate() sets the transaction id, but apps may not honor. */
    {	rpmuint32_t tid = (rpmuint32_t) time(NULL);
	(void) rpmtsSetTid(ts, tid);
    }

    /* Get available space on mounted file systems. */
    xx = rpmtsInitDSI(ts);

#endif	/* REFERENCE */

    return 0;
}

static int rpmtsFinish(rpmts ts, rpmsx sx)
{
#ifdef	REFERENCE
    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOCONTEXTS)) {
	matchpathcon_fini();
    }
#else	/* REFERENCE */
    if (sx != NULL) sx = rpmsxFree(sx);
#endif	/* REFERENCE */
    return 0;
}

static int rpmtsPrepare(rpmts ts, rpmsx sx, uint32_t fileCount,
		uint32_t * nrmvdp)
{
    rpmtsi pi;
    rpmte p;
    fingerPrintCache fpc;
    rpmfi fi;
    int xx;
    int rc = 0;
#ifdef	REFERENCE
    uint64_t fileCount = countFiles(ts);
    const char * rootDir = rpmtsRootDir(ts);
    int dochroot = (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/');

    rpmFpHash ht = rpmFpHashCreate(fileCount/2+1, fpHashFunction, fpEqual,
			     NULL, NULL);

    fpc = fpCacheCreate(fileCount/2 + 10001);

    rpmlog(RPMLOG_DEBUG, "computing %" PRIu64 " file fingerprints\n", fileCount);

    /* Skip netshared paths, not our i18n files, and excluded docs */
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, TR_ADDED)) != NULL) {
	if ((fi = rpmteFI(p)) == NULL)
	    continue;	/* XXX can't happen */

	if (rpmfiFC(fi) > 0) 
	    skipFiles(ts, p);
    }
    pi = rpmtsiFree(pi);

    /* Enter chroot for fingerprinting if necessary */
    if (!rpmtsChrootDone(ts)) {
	xx = chdir("/");
	if (dochroot) {
	    /* opening db before chroot not optimal, see rhbz#103852 c#3 */
	    xx = rpmdbOpenAll(ts->rdb);
	    if (chroot(rootDir) == -1) {
		rpmlog(RPMLOG_ERR, _("Unable to change root directory: %m\n"));
		rc = -1;
		goto exit;
	    }
	}
	(void) rpmtsSetChrootDone(ts, 1);
    }
    
    rpmtsNotify(ts, NULL, RPMCALLBACK_TRANS_START, 6, ts->orderCount);
    addFingerprints(ts, fileCount, ht, fpc);
    /* check against files in the rpmdb */
    checkInstalledFiles(ts, fileCount, ht, fpc);

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	if ((fi = rpmteFI(p)) == NULL)
	    continue;   /* XXX can't happen */

	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), 0);
	/* check files in ts against each other and update disk space
	   needs on each partition for this package. */
	handleOverlappedFiles(ts, ht, p, fi);

	/* Check added package has sufficient space on each partition used. */
	if (rpmteType(p) == TR_ADDED) {
	    rpmtsCheckDSIProblems(ts, p);
	}
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), 0);
    }
    pi = rpmtsiFree(pi);
    rpmtsNotify(ts, NULL, RPMCALLBACK_TRANS_STOP, 6, ts->orderCount);

    /* return from chroot if done earlier */
    if (rpmtsChrootDone(ts)) {
	const char * currDir = rpmtsCurrDir(ts);
	if (dochroot)
	    xx = chroot(".");
	(void) rpmtsSetChrootDone(ts, 0);
	if (currDir != NULL)
	    xx = chdir(currDir);
    }

    /* File info sets, fp caches etc not needed beyond here, free 'em up. */
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	rpmteSetFI(p, NULL);
    }
    pi = rpmtsiFree(pi);

exit:
    ht = rpmFpHashFree(ht);
    fpc = fpCacheFree(fpc);

#else	/* REFERENCE */
    void * ptr;
    uint32_t numAdded = 0;
    uint32_t numRemoved = 0;

rpmlog(RPMLOG_DEBUG, D_("computing %d file fingerprints\n"), fileCount);

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	int fc;

	if (p->isSource) continue;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	fc = rpmfiFC(fi);

	switch (rpmteType(p)) {
	case TR_ADDED:
	    numAdded++;
	    fi->record = 0;
	    /* Skip netshared paths, not our i18n files, and excluded docs */
	    if (fc > 0)
		skipFiles(ts, fi);
	    /*@switchbreak@*/ break;
	case TR_REMOVED:
	    numRemoved++;
	    fi->record = rpmteDBOffset(p);
	    /*@switchbreak@*/ break;
	}

	fi->fps = (fc > 0 ? xmalloc(fc * sizeof(*fi->fps)) : NULL);
    }
    pi = rpmtsiFree(pi);

    if (nrmvdp)
	*nrmvdp = numRemoved;

    if (!rpmtsChrootDone(ts)) {
	const char * rootDir = rpmtsRootDir(ts);
	static int openall_before_chroot = -1;

	if (openall_before_chroot < 0)
	    openall_before_chroot = rpmExpandNumeric("%{?_openall_before_chroot}");

	xx = Chdir("/");
	/*@-modobserver@*/
	if (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/') {
	    if (openall_before_chroot)
		xx = rpmdbOpenAll(rpmtsGetRdb(ts));
	    xx = Chroot(rootDir);
	}
	/*@=modobserver@*/
	(void) rpmtsSetChrootDone(ts, 1);
    }

    ts->ht = htCreate(fileCount * 2, 0, 0, fpHashFunction, fpEqual);
    fpc = fpCacheCreate(fileCount);

    ptr = rpmtsNotify(ts, NULL, RPMCALLBACK_TRANS_START, 6, ts->orderCount);

    /* ===============================================
     * Add fingerprint for each file not skipped.
     */
    addFingerprints(ts, fileCount, ts->ht, fpc);

    /* ===============================================
     * Compute file disposition for each package in transaction set.
     */
rpmlog(RPMLOG_DEBUG, D_("computing file dispositions\n"));

    pi = rpmtsiInit(ts);
/*@-nullpass@*/
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	dbiIndexSet * matches;
	unsigned int exclude;
	int knownBad;
	int fc;
	int i;
	int j;
	sharedFileInfo shared, sharedList;
	int nexti;
	int numShared;

	(void) rpmdbCheckSignals();

	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	fc = rpmfiFC(fi);

	ptr = rpmtsNotify(ts, NULL, RPMCALLBACK_TRANS_PROGRESS, rpmtsiOc(pi),
			ts->orderCount);

	if (fc == 0) continue;

	/* All source files get installed. */
	if (p->isSource) {
 	    fi = rpmfiInit(fi, 0);
	    if (fi != NULL)
	    while ((i = rpmfiNext(fi)) >= 0)
		fi->actions[i] = FA_CREATE;
	    continue;
	}

	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), 0);
	/* Extract file info for all files in this package from the database. */
	matches = xcalloc(fc, sizeof(*matches));
	exclude = (rpmteType(p) == TR_REMOVED ? fi->record : 0);
	if (rpmdbFindFpList(rpmtsGetRdb(ts), fi->fps, matches, fc, exclude)) {
	    rc = 1;	/* XXX WTFO? */
	    goto exit;
	}

	numShared = 0;
 	fi = rpmfiInit(fi, 0);
	while ((i = rpmfiNext(fi)) >= 0) {
	    struct stat sb, *st = &sb;
	    rpmuint32_t FFlags = rpmfiFFlags(fi);
	    numShared += dbiIndexSetCount(matches[i]);
	    if (!(FFlags & RPMFILE_CONFIG))
		/*@innercontinue@*/ continue;
	    if (!Lstat(rpmfiFN(fi), st)) {
		FFlags |= RPMFILE_EXISTS;
		if ((512 * st->st_blocks) < st->st_size)
		     FFlags |= RPMFILE_SPARSE;
		(void) rpmfiSetFFlags(fi, FFlags);
	    }
	}

	/* Build sorted file info list for this package. */
	shared = sharedList = xcalloc((numShared + 1), sizeof(*sharedList));

 	fi = rpmfiInit(fi, 0);
	while ((i = rpmfiNext(fi)) >= 0) {
	    /*
	     * Take care not to mark files as replaced in packages that will
	     * have been removed before we will get here.
	     */
	    for (j = 0; j < (int)dbiIndexSetCount(matches[i]); j++) {
		rpmtsi qi;
		rpmte q;
		int ro;

		ro = dbiIndexRecordOffset(matches[i], j);
		knownBad = 0;
		qi = rpmtsiInit(ts);
		while ((q = rpmtsiNext(qi, TR_REMOVED)) != NULL) {
		    if (ro == knownBad)
			/*@innerbreak@*/ break;
		    if (rpmteDBOffset(q) == ro)
			knownBad = ro;
		}
		qi = rpmtsiFree(qi);

		shared->pkgFileNum = i;
		shared->otherPkg = dbiIndexRecordOffset(matches[i], j);
		shared->otherFileNum = dbiIndexRecordFileNumber(matches[i], j);
		shared->isRemoved = (knownBad == ro);
		shared++;
	    }
	    matches[i] = dbiFreeIndexSet(matches[i]);
	}
	numShared = shared - sharedList;
	shared->otherPkg = -1;
	matches = _free(matches);

	/* Sort file info by other package index (otherPkg) */
	qsort(sharedList, numShared, sizeof(*shared), sharedCmp);

	/* For all files from this package that are in the database ... */
	nexti = 0;
/*@-nullpass@*/
	for (i = 0; i < numShared; i = nexti) {
	    int beingRemoved;

	    shared = sharedList + i;

	    /* Find the end of the files in the other package. */
	    for (nexti = i + 1; nexti < numShared; nexti++) {
		if (sharedList[nexti].otherPkg != shared->otherPkg)
		    /*@innerbreak@*/ break;
	    }

	    /* Is this file from a package being removed? */
	    beingRemoved = 0;
	    if (ts->removedPackages != NULL)
	    for (j = 0; j < ts->numRemovedPackages; j++) {
		if (ts->removedPackages[j] != (uint32_t)shared->otherPkg)
		    /*@innercontinue@*/ continue;
		beingRemoved = 1;
		/*@innerbreak@*/ break;
	    }

	    /* Determine the fate of each file. */
	    switch (rpmteType(p)) {
	    case TR_ADDED:
		xx = handleInstInstalledFiles(ts, p, fi, shared, nexti - i,
	!(beingRemoved || (rpmtsFilterFlags(ts) & RPMPROB_FILTER_REPLACEOLDFILES)));
		/*@switchbreak@*/ break;
	    case TR_REMOVED:
		if (!beingRemoved)
		    xx = handleRmvdInstalledFiles(ts, fi, shared, nexti - i);
		/*@switchbreak@*/ break;
	    }
	}
/*@=nullpass@*/

	free(sharedList);

	/* Update disk space needs on each partition for this package. */
/*@-nullpass@*/
	handleOverlappedFiles(ts, p, fi);
/*@=nullpass@*/

	/* Check added package has sufficient space on each partition used. */
	switch (rpmteType(p)) {
	case TR_ADDED:
	    rpmtsCheckDSIProblems(ts, p);
	    /*@switchbreak@*/ break;
	case TR_REMOVED:
	    /*@switchbreak@*/ break;
	}
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_FINGERPRINT), fc);
    }
/*@=nullpass@*/
    pi = rpmtsiFree(pi);

    if (rpmtsChrootDone(ts)) {
	const char * rootDir = rpmtsRootDir(ts);
	const char * currDir = rpmtsCurrDir(ts);
	/*@-modobserver@*/
	if (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/')
	    xx = Chroot(".");
	/*@=modobserver@*/
	(void) rpmtsSetChrootDone(ts, 0);
	if (currDir != NULL)
	    xx = Chdir(currDir);
    }

    ptr = rpmtsNotify(ts, NULL, RPMCALLBACK_TRANS_STOP, 6, ts->orderCount);

    /* ===============================================
     * Free unused memory as soon as possible.
     */
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	if (p->isSource) continue;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	if (rpmfiFC(fi) == 0)
	    continue;
	fi->fps = _free(fi->fps);
    }
    pi = rpmtsiFree(pi);

exit:
    fpc = fpCacheFree(fpc);
    ts->ht = htFree(ts->ht);
#endif	/* REFERENCE */

    return rc;
}

/*
 * Transaction main loop: install and remove packages
 */
static int rpmtsProcess(rpmts ts, rpmprobFilterFlags ignoreSet,
		int rollbackFailures)
{
    rpmtsi pi;
    rpmte p;
    int rc = 0;
#ifdef	REFERENCE

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	int failed = 1;
	rpmElementType tetype = rpmteType(p);
	rpmtsOpX op = (tetype == TR_ADDED) ? RPMTS_OP_INSTALL : RPMTS_OP_ERASE;

	rpmlog(RPMLOG_DEBUG, "========== +++ %s %s-%s 0x%x\n",
		rpmteNEVR(p), rpmteA(p), rpmteO(p), rpmteColor(p));

	if (rpmteFailed(p)) {
	    /* XXX this should be a warning, need a better message though */
	    rpmlog(RPMLOG_DEBUG, "element %s marked as failed, skipping\n",
					rpmteNEVRA(p));
	    rc++;
	    continue;
	}
	
	if (rpmteOpen(p, ts, 1)) {
	    rpmpsm psm = NULL;
	    pkgStage stage = PSM_UNKNOWN;
	    int async = (rpmtsiOc(pi) >= rpmtsUnorderedSuccessors(ts, -1)) ? 
			1 : 0;

	    switch (tetype) {
	    case TR_ADDED:
		stage = PSM_PKGINSTALL;
		break;
	    case TR_REMOVED:
		stage = PSM_PKGERASE;
		break;
	    }
	    psm = rpmpsmNew(ts, p);
	    rpmpsmSetAsync(psm, async);

	    (void) rpmswEnter(rpmtsOp(ts, op), 0);
	    failed = rpmpsmStage(psm, stage);
	    (void) rpmswExit(rpmtsOp(ts, op), 0);
	    psm = rpmpsmFree(psm);
	    rpmteClose(p, ts, 1);
	}
	if (failed) {
	    rpmteMarkFailed(p, ts);
	    rc++;
	}
	(void) rpmdbSync(rpmtsGetRdb(ts));
    }
    pi = rpmtsiFree(pi);

#else	/* REFERENCE */
    rpmpsm psm;

/*@-nullpass@*/
    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {
	rpmfi fi;
	alKey pkgKey;
	int gotfd;
	int xx;

	(void) rpmdbCheckSignals();

	gotfd = 0;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	
	psm = rpmpsmNew(ts, p, fi);
assert(psm != NULL);
	if (rpmtsiOc(pi) >= rpmtsUnorderedSuccessors(ts, -1))
	    psm->flags |= RPMPSM_FLAGS_UNORDERED;
	else
	    psm->flags &= ~RPMPSM_FLAGS_UNORDERED;

	switch (rpmteType(p)) {
	case TR_ADDED:
	    (void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_INSTALL), 0);

	    pkgKey = rpmteAddedKey(p);

	    rpmlog(RPMLOG_DEBUG, "========== +++ %s %s-%s 0x%x\n",
		rpmteNEVR(p), rpmteA(p), rpmteO(p), rpmteColor(p));

	    p->h = NULL;
	    /*@-type@*/ /* FIX: rpmte not opaque */
	    {
		p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_OPEN_FILE, 0, 0);
		if (rpmteFd(p) != NULL) {
		    rpmVSFlags ovsflags = rpmtsVSFlags(ts);
		    rpmVSFlags vsflags = ovsflags | RPMVSF_NEEDPAYLOAD;
		    rpmRC rpmrc;

		    ovsflags = rpmtsSetVSFlags(ts, vsflags);
		    rpmrc = rpmReadPackageFile(ts, rpmteFd(p),
				rpmteNEVR(p), &p->h);
		    vsflags = rpmtsSetVSFlags(ts, ovsflags);

		    switch (rpmrc) {
		    default:
			p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_CLOSE_FILE,
					0, 0);
			p->fd = NULL;
			rc++;
			/*@innerbreak@*/ break;
		    case RPMRC_NOTTRUSTED:
		    case RPMRC_NOKEY:
		    case RPMRC_OK:
			/*@innerbreak@*/ break;
		    }
		    if (rpmteFd(p) != NULL) gotfd = 1;
		} else {
		    rc++;
		    xx = markLinkedFailed(ts, p);
		}
	    }
	    /*@=type@*/

	    if (rpmteFd(p) != NULL) {
		/*
		 * XXX Sludge necessary to tranfer existing fstates/actions
		 * XXX around a recreated file info set.
		 */
		psm->fi = rpmfiFree(psm->fi);
		{
		    rpmuint8_t * fstates = fi->fstates;
		    iosmFileAction * actions = (iosmFileAction *) fi->actions;
		    int mapflags = fi->mapflags;
		    rpmte savep;
		    int scareMem = 0;

		    fi->fstates = NULL;
		    fi->actions = NULL;
/*@-nullstate@*/ /* FIX: fi->actions is NULL */
		    fi = rpmfiFree(fi);
/*@=nullstate@*/

		    savep = rpmtsSetRelocateElement(ts, p);
		    fi = rpmfiNew(ts, p->h, RPMTAG_BASENAMES, scareMem);
		    (void) rpmtsSetRelocateElement(ts, savep);

		    if (fi != NULL) {	/* XXX can't happen */
			fi->te = p;
			fi->fstates = _free(fi->fstates);
			fi->fstates = fstates;
			fi->actions = _free(fi->actions);
			fi->actions = (int *) actions;
			if (mapflags & IOSM_SBIT_CHECK)
			    fi->mapflags |= IOSM_SBIT_CHECK;
			p->fi = fi;
		    }
		}
		psm->fi = rpmfiLink(p->fi, NULL);

		if ((xx = rpmpsmStage(psm, PSM_PKGINSTALL)) != 0) {
		    rc++;
		    xx = markLinkedFailed(ts, p);
		}
#if defined(RPM_VENDOR_MANDRIVA)
		else {
		    if(!rpmteIsSource(fi->te))
	    		xx = mayAddToFilesAwaitingFiletriggers(rpmtsRootDir(ts), psm->fi, 1);
		    p->done = 1;
		}
#endif

	    } else {
		rc++;
	    }

	    if (gotfd) {
		p->fd = rpmtsNotify(ts, p, RPMCALLBACK_INST_CLOSE_FILE, 0, 0);
		p->fd = NULL;
	    }

	    (void) rpmswExit(rpmtsOp(ts, RPMTS_OP_INSTALL), 0);

	    /*@switchbreak@*/ break;

	case TR_REMOVED:
	    (void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_ERASE), 0);

	    rpmlog(RPMLOG_DEBUG, "========== --- %s %s-%s 0x%x\n",
		rpmteNEVR(p), rpmteA(p), rpmteO(p), rpmteColor(p));

	    /* If linked element install failed, then don't erase. */
	    if (p->linkFailed == 0) {
		if ((xx = rpmpsmStage(psm, PSM_PKGERASE)) != 0) {
		    rc++;
		}
#if defined(RPM_VENDOR_MANDRIVA)
		else {
		    if(!rpmteIsSource(fi->te))
			xx = mayAddToFilesAwaitingFiletriggers(rpmtsRootDir(ts), psm->fi, 0);
		    p->done = 1;
		}
#endif
	    } else
		rc++;

	    (void) rpmswExit(rpmtsOp(ts, RPMTS_OP_ERASE), 0);

	    /*@switchbreak@*/ break;
	}

	/* Would have freed header above in TR_ADD portion of switch
	 * but needed the header to add it to the autorollback transaction.
	 */
	if (rpmteType(p) == TR_ADDED) {
	    (void)headerFree(p->h);
	    p->h = NULL;
	}

/*@-nullstate@*/ /* FIX: psm->fi may be NULL */
	psm = rpmpsmFree(psm, __FUNCTION__);
/*@=nullstate@*/

	/* If we received an error, lets break out and rollback, provided
	 * autorollback is enabled.
	 */
	if (rc && rollbackFailures) {
	    xx = rpmtsRollback(ts, ignoreSet, 1, p);
	    break;
	}
    }
/*@=nullpass@*/
    pi = rpmtsiFree(pi);
#endif	/* REFERENCE */
    return rc;
}

/* ================================================================= */
static int rpmtsRepackage(rpmts ts, uint32_t numRemoved)
{
    rpmpsm psm;
    rpmfi fi;
    rpmtsi pi;
    rpmte p;
    void * ptr;
    int progress = 0;
    int rc = 0;
    int xx;

    pi = rpmtsiInit(ts);
    while ((p = rpmtsiNext(pi, 0)) != NULL) {

	(void) rpmdbCheckSignals();

	if (p->isSource) continue;
	if ((fi = rpmtsiFi(pi)) == NULL)
	    continue;	/* XXX can't happen */
	switch (rpmteType(p)) {
	case TR_ADDED:
	    /*@switchbreak@*/ break;
	case TR_REMOVED:
	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_REPACKAGE))
		/*@switchbreak@*/ break;
	    if (!progress)
		ptr = rpmtsNotify(ts, NULL, RPMCALLBACK_REPACKAGE_START,
				7, numRemoved);

	    ptr = rpmtsNotify(ts, NULL, RPMCALLBACK_REPACKAGE_PROGRESS,
				progress, numRemoved);
	    progress++;

	    (void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_REPACKAGE), 0);

	/* XXX TR_REMOVED needs IOSM_MAP_{ABSOLUTE,ADDDOT} IOSM_ALL_HARDLINKS */
	    fi->mapflags |= IOSM_MAP_ABSOLUTE;
	    fi->mapflags |= IOSM_MAP_ADDDOT;
	    fi->mapflags |= IOSM_ALL_HARDLINKS;
	    psm = rpmpsmNew(ts, p, fi);
assert(psm != NULL);
	    xx = rpmpsmStage(psm, PSM_PKGSAVE);
	    psm = rpmpsmFree(psm, __FUNCTION__);
	    fi->mapflags &= ~IOSM_MAP_ABSOLUTE;
	    fi->mapflags &= ~IOSM_MAP_ADDDOT;
	    fi->mapflags &= ~IOSM_ALL_HARDLINKS;

	    (void) rpmswExit(rpmtsOp(ts, RPMTS_OP_REPACKAGE), 0);

	    /*@switchbreak@*/ break;
	}
    }
    pi = rpmtsiFree(pi);
    if (progress)
	ptr = rpmtsNotify(ts, NULL, RPMCALLBACK_REPACKAGE_STOP, 7, numRemoved);

    return rc;
}

/**
 * Force add a failed package into the rpmdb.
 * @param ts		current transaction set
 * @param p 		failed rpmte. 
 * @return 		RPMRC_OK, or RPMRC_FAIL
 */
/*@-nullpass@*/
static rpmRC _processFailedPackage(rpmts ts, rpmte p)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies ts, p, rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
{
    int rc  = RPMRC_OK;	/* assume success */

    /* Handle failed packages. */
    /* XXX TODO: Add header to rpmdb in PSM_INIT, not PSM_POST. */
    if (p != NULL && rpmteType(p) == TR_ADDED && !p->installed) {
/*@-compdef -usereleased@*/	/* p->fi->te undefined */
	rpmpsm psm = rpmpsmNew(ts, p, p->fi);
/*@=compdef =usereleased@*/
	/*
	 * If it died before the header was put in the rpmdb, we need
	 * do to something wacky which is add the header to the DB anyway.
	 * This will allow us to add the failed package as an erase
	 * to the rollback transaction.  This must be done because we
	 * want the the erase scriptlets to run, and the only way that
	 * is going is if the header is in the rpmdb.
	 */
assert(psm != NULL);
	psm->stepName = "failed";	/* XXX W2DO? */
	rc = rpmpsmStage(psm, PSM_RPMDB_ADD);
	psm = rpmpsmFree(psm, "_processFailedPackage");
    }
    return rc;
}
/*@=nullpass@*/

/*@-nullpass@*/
rpmRC rpmtsRollback(rpmts rbts, rpmprobFilterFlags ignoreSet, int running, rpmte rbte)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rbts, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const char * semfn = NULL;
    rpmRC rc = 0;
    rpmuint32_t arbgoal = rpmtsARBGoal(rbts);
    QVA_t ia = memset(alloca(sizeof(*ia)), 0, sizeof(*ia));
    time_t ttid;
    int xx;

    /* Don't attempt rollback's of rollback transactions */
    if ((rpmtsType(rbts) & RPMTRANS_TYPE_ROLLBACK) ||
	(rpmtsType(rbts) & RPMTRANS_TYPE_AUTOROLLBACK))
	return RPMRC_OK;

    if (arbgoal == 0xffffffff) 
	arbgoal = rpmtsGetTid(rbts);

    /* Don't attempt rollbacks if no goal is set. */
    if (!running && arbgoal == 0xffffffff)
	return RPMRC_OK;

    /* We need to remove an headers that were going to be removed so 
     * as to not foul up the regular rollback mechanism which will not 
     * handle properly a file being in the repackaged package directory
     * and also its header still in the DB.
     */
    {	rpmtsi tsi;
	rpmte te;

	/* XXX Insure an O_RDWR rpmdb. */
	xx = rpmtsOpenDB(rbts, O_RDWR);

	tsi = rpmtsiInit(rbts);
	while((te = rpmtsiNext(tsi, TR_REMOVED)) != NULL) {
	    if (te->isSource) continue;
	    if(!te->u.removed.dboffset)
		continue;
	    rc = rpmdbRemove(rpmtsGetRdb(rbts),
			rpmtsGetTid(rbts),
			te->u.removed.dboffset, NULL);
	    if (rc != RPMRC_OK) {
		rpmlog(RPMLOG_ERR, _("rpmdb erase failed. NEVRA: %s\n"),
			rpmteNEVRA(te));
		break;
	    }
	}
	tsi = rpmtsiFree(tsi);
	if (rc != RPMRC_OK) 
	    goto cleanup;
    }

    /* Process the failed package */
    rc = _processFailedPackage(rbts, rbte);
    if (rc != RPMRC_OK)
	goto cleanup;

    rpmtsEmpty(rbts);

    ttid = (time_t)arbgoal;
    rpmlog(RPMLOG_NOTICE, _("Rollback to %-24.24s (0x%08x)\n"),
	ctime(&ttid), arbgoal);

    /* Set the verify signature flags:
     *  - can't verify signatures/digests on repackaged packages.
     *  - header check are out.
     */
    {
	rpmVSFlags vsflags = rpmExpandNumeric("%{?_vsflags_erase}");
	vsflags |= _RPMVSF_NODIGESTS;
	vsflags |= _RPMVSF_NOSIGNATURES;
	vsflags |= RPMVSF_NOHDRCHK;
	vsflags |= RPMVSF_NEEDPAYLOAD;      
	xx = rpmtsSetVSFlags(rbts, vsflags); 
    }

    /* Set transaction flags to be the same as the running transaction */
    {
	rpmtransFlags tsFlags = rpmtsFlags(rbts);
	tsFlags &= ~RPMTRANS_FLAG_DIRSTASH;	/* No repackage of rollbacks */
	tsFlags &= ~RPMTRANS_FLAG_REPACKAGE;	/* No repackage of rollbacks */
	tsFlags |= RPMTRANS_FLAG_NOFDIGESTS;	/* Don't check file digests */
	tsFlags = rpmtsSetFlags(rbts, tsFlags);
    }

    /* Create install arguments structure */ 	
    ia->rbtid = arbgoal;
    /* transFlags/depFlags from rbts, (re-)set in rpmRollback(). */
    ia->transFlags = rpmtsFlags(rbts);
    ia->depFlags = rpmtsDFlags(rbts);
    /* XXX probFilter is normally set in main(). */
    ia->probFilter = ignoreSet;	/* XXX RPMPROB_FILTER_NONE? */
    /* XXX installInterfaceFlags is normally set in main(). */
    ia->installInterfaceFlags = INSTALL_UPGRADE | INSTALL_HASH ;

    /* rpmtsCheck and rpmtsOrder failures do not have links. */
    ia->no_rollback_links = 1;

    /* Create a file semaphore. */
    semfn = rpmExpand("%{?semaphore_backout}", NULL);
    if (semfn && *semfn) {
	FD_t fd = Fopen(semfn, "w.fdio");
	if (fd)
	    xx = Fclose(fd);
    }

/*@-compmempass@*/
    rc = rpmRollback(rbts, ia, NULL);
/*@=compmempass@*/

cleanup: 
    /* Remove the file semaphore. */
    if (semfn && *semfn)
	xx = Unlink(semfn);
    semfn = _free(semfn);

    return rc;
}
/*@=nullpass@*/

int rpmtsRun(rpmts ts, rpmps okProbs, rpmprobFilterFlags ignoreSet)
{
    static const char msg[] = "rpmtsRun";
    int ourrc = -1;	/* assume failure */
    uint32_t totalFileCount = 0;
    rpmps ps;
    rpmsx sx = NULL;
    uint32_t numRemoved;
    int rollbackFailures = 0;
    void * lock = NULL;
    int xx;

if (_rpmts_debug)
fprintf(stderr, "--> %s(%p,0x%x,0x%x) tsFlags 0x%x\n", msg, ts, (unsigned) okProbs, (unsigned) ignoreSet, rpmtsFlags(ts));

    /* XXX programmer error segfault avoidance. */
    if (rpmtsNElements(ts) <= 0) {
	rpmlog(RPMLOG_ERR,
	    _("Invalid number of transaction elements.\n"));
	return -1;
    }

    /* Don't acquire the transaction lock if testing. */
    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_TEST))
	lock = rpmtsAcquireLock(ts);

    rollbackFailures = rpmExpandNumeric("%{?_rollback_transaction_on_failure}");
    /* Don't rollback unless repackaging. */
    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_REPACKAGE))
	rollbackFailures = 0;
    /* Don't rollback if testing. */
    if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)
	rollbackFailures = 0;

    if (rpmtsType(ts) & (RPMTRANS_TYPE_ROLLBACK | RPMTRANS_TYPE_AUTOROLLBACK))
	rollbackFailures = 0;

    /* ===============================================
     * Setup flags and such, open the rpmdb in O_RDWR mode.
     */
    sx = NULL;
    if (rpmtsSetup(ts, ignoreSet, &sx))
        goto exit;

    /* ===============================================
     * For packages being installed:
     * - verify package epoch:version-release is newer.
     * - count files.
     * For packages being removed:
     * - count files.
     */

    totalFileCount = 0;
    ps = checkProblems(ts, &totalFileCount);
    ps = rpmpsFree(ps);

    /* ===============================================
     * Run pre-transaction scripts, but only if no known problems exist.
     */
    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPRETRANS) &&
       (!((rpmtsFlags(ts) & (RPMTRANS_FLAG_BUILD_PROBS|RPMTRANS_FLAG_TEST))
     	  || (rpmpsNumProblems(ts->probs) &&
		(okProbs == NULL || rpmpsTrim(ts->probs, okProbs))))))
    {
	rpmlog(RPMLOG_DEBUG, D_("running pre-transaction scripts\n"));
	xx = runTransScript(ts, RPMTAG_PRETRANS);
    }

    /* ===============================================
     * Compute file disposition for each package in transaction set.
     */
    numRemoved = 0;
    if (rpmtsPrepare(ts, sx, totalFileCount, &numRemoved))
	goto exit;

    /* ===============================================
     * If unfiltered problems exist, free memory and return.
     */
    if ((rpmtsFlags(ts) & RPMTRANS_FLAG_BUILD_PROBS)
     || (rpmpsNumProblems(ts->probs) &&
		(okProbs == NULL || rpmpsTrim(ts->probs, okProbs)))
       )
    {
	lock = rpmtsFreeLock(lock);
	if (sx != NULL) sx = rpmsxFree(sx);
	return ts->orderCount;
    }

    /* ===============================================
     * Save removed files before erasing.
     */
    if (rpmtsFlags(ts) & (RPMTRANS_FLAG_DIRSTASH | RPMTRANS_FLAG_REPACKAGE)) {
	xx = rpmtsRepackage(ts, numRemoved);
    }

#ifdef	NOTYET
    xx = rpmtxnBegin(rpmtsGetRdb(ts), NULL, &ts->txn);
#endif

    /* ===============================================
     * Install and remove packages.
     */
    ourrc = rpmtsProcess(ts, ignoreSet, rollbackFailures);

    /* ===============================================
     * Run post-transaction scripts unless disabled.
     */
    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPOSTTRANS) &&
	!(rpmtsFlags(ts) & RPMTRANS_FLAG_TEST))
    {

#if defined(RPM_VENDOR_MANDRIVA)
	if ((rpmtsFlags(ts) & _noTransTriggers) != _noTransTriggers)
	    rpmRunFileTriggers(rpmtsRootDir(ts));
#endif

	rpmlog(RPMLOG_DEBUG, D_("running post-transaction scripts\n"));
	xx = runTransScript(ts, RPMTAG_POSTTRANS);
    }

exit:
    xx = rpmtsFinish(ts, sx);

    lock = rpmtsFreeLock(lock);

    /*@-nullstate@*/ /* FIX: ts->flList may be NULL */
    if (ourrc) {
	if (ts->txn != NULL)
	    xx = rpmtxnAbort(ts->txn);
	ts->txn = NULL;
    	return -1;
    } else {
	if (ts->txn != NULL)
	    xx = rpmtxnCommit(ts->txn);
	ts->txn = NULL;
	xx = rpmtxnCheckpoint(rpmtsGetRdb(ts));
	return 0;
    }
    /*@=nullstate@*/
}
