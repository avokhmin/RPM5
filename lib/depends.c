/** \ingroup rpmdep
 * \file lib/depends.c
 */

#include "system.h"

#include <rpmlib.h>
#include <rpmmacro.h>		/* XXX rpmGetPath */
#include <rpmpgp.h>		/* XXX pgpFreeDig */

#include "depends.h"
#include "rpmal.h"
#include "rpmdb.h"		/* XXX response cache needs dbiOpen et al. */

#include "debug.h"

/*@access dbiIndex@*/		/* XXX compared with NULL */
/*@access dbiIndexSet@*/	/* XXX compared with NULL */
/*@access Header@*/		/* XXX compared with NULL */
/*@access FD_t@*/		/* XXX compared with NULL */
/*@access rpmdb@*/		/* XXX compared with NULL */
/*@access rpmdbMatchIterator@*/		/* XXX compared with NULL */
/*@access rpmTransactionSet@*/
/*@access rpmDependencyConflict@*/

/*@access problemsSet@*/
/*@access orderListIndex@*/
/*@access tsortInfo@*/

/*@access availablePackage@*/

/*@unchecked@*/
static int _cacheDependsRC = 1;

/*@unchecked@*/
int _ts_debug = 0;

/**
 * Return formatted dependency string.
 * @param depend	type of dependency ("R" == Requires, "C" == Conflcts)
 * @param key		dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @return		formatted dependency (malloc'ed)
 */
static /*@only@*/ char * printDepend(const char * depend, const char * key,
		const char * keyEVR, int keyFlags)
	/*@*/
{
    char * tbuf, * t;
    size_t nb;

    nb = 0;
    if (depend)	nb += strlen(depend) + 1;
    if (key)	nb += strlen(key);
    if (keyFlags & RPMSENSE_SENSEMASK) {
	if (nb)	nb++;
	if (keyFlags & RPMSENSE_LESS)	nb++;
	if (keyFlags & RPMSENSE_GREATER) nb++;
	if (keyFlags & RPMSENSE_EQUAL)	nb++;
    }
    if (keyEVR && *keyEVR) {
	if (nb)	nb++;
	nb += strlen(keyEVR);
    }

    t = tbuf = xmalloc(nb + 1);
    if (depend) {
	while(*depend != '\0')	*t++ = *depend++;
	*t++ = ' ';
    }
    if (key)
	while(*key != '\0')	*t++ = *key++;
    if (keyFlags & RPMSENSE_SENSEMASK) {
	if (t != tbuf)	*t++ = ' ';
	if (keyFlags & RPMSENSE_LESS)	*t++ = '<';
	if (keyFlags & RPMSENSE_GREATER) *t++ = '>';
	if (keyFlags & RPMSENSE_EQUAL)	*t++ = '=';
    }
    if (keyEVR && *keyEVR) {
	if (t != tbuf)	*t++ = ' ';
	while(*keyEVR != '\0')	*t++ = *keyEVR++;
    }
    *t = '\0';
    return tbuf;
}

#ifdef	UNUSED
static /*@only@*/ const char *buildEVR(int_32 *e, const char *v, const char *r)
{
    const char *pEVR;
    char *p;

    pEVR = p = xmalloc(21 + strlen(v) + 1 + strlen(r) + 1);
    *p = '\0';
    if (e) {
	sprintf(p, "%d:", *e);
	while (*p)
	    p++;
    }
    (void) stpcpy( stpcpy( stpcpy(p, v) , "-") , r);
    return pEVR;
}
#endif

/**
 * Split EVR into epoch, version, and release components.
 * @param evr		[epoch:]version[-release] string
 * @retval *ep		pointer to epoch
 * @retval *vp		pointer to version
 * @retval *rp		pointer to release
 */
static void parseEVR(char * evr,
		/*@exposed@*/ /*@out@*/ const char ** ep,
		/*@exposed@*/ /*@out@*/ const char ** vp,
		/*@exposed@*/ /*@out@*/ const char ** rp)
	/*@modifies *ep, *vp, *rp @*/
{
    const char *epoch;
    const char *version;		/* assume only version is present */
    const char *release;
    char *s, *se;

    s = evr;
    while (*s && xisdigit(*s)) s++;	/* s points to epoch terminator */
    se = strrchr(s, '-');		/* se points to version terminator */

    if (*s == ':') {
	epoch = evr;
	*s++ = '\0';
	version = s;
	/*@-branchstate@*/
	if (*epoch == '\0') epoch = "0";
	/*@=branchstate@*/
    } else {
	epoch = NULL;	/* XXX disable epoch compare if missing */
	version = evr;
    }
    if (se) {
	*se++ = '\0';
	release = se;
    } else {
	release = NULL;
    }

    if (ep) *ep = epoch;
    if (vp) *vp = version;
    if (rp) *rp = release;
}

/*@-exportheadervar@*/
/*@observer@*/ /*@unchecked@*/
const char *rpmNAME = PACKAGE;

/*@observer@*/ /*@unchecked@*/
const char *rpmEVR = VERSION;

/*@unchecked@*/
int rpmFLAGS = RPMSENSE_EQUAL;
/*@=exportheadervar@*/

int rpmRangesOverlap(const char * AName, const char * AEVR, int AFlags,
	const char * BName, const char * BEVR, int BFlags)
{
    const char *aDepend = printDepend(NULL, AName, AEVR, AFlags);
    const char *bDepend = printDepend(NULL, BName, BEVR, BFlags);
    char *aEVR, *bEVR;
    const char *aE, *aV, *aR, *bE, *bV, *bR;
    int result;
    int sense;

    /* Different names don't overlap. */
    if (strcmp(AName, BName)) {
	result = 0;
	goto exit;
    }

    /* Same name. If either A or B is an existence test, always overlap. */
    if (!((AFlags & RPMSENSE_SENSEMASK) && (BFlags & RPMSENSE_SENSEMASK))) {
	result = 1;
	goto exit;
    }

    /* If either EVR is non-existent or empty, always overlap. */
    if (!(AEVR && *AEVR && BEVR && *BEVR)) {
	result = 1;
	goto exit;
    }

    /* Both AEVR and BEVR exist. */
    aEVR = xstrdup(AEVR);
    parseEVR(aEVR, &aE, &aV, &aR);
    bEVR = xstrdup(BEVR);
    parseEVR(bEVR, &bE, &bV, &bR);

    /* Compare {A,B} [epoch:]version[-release] */
    sense = 0;
    if (aE && *aE && bE && *bE)
	sense = rpmvercmp(aE, bE);
    else if (aE && *aE && atol(aE) > 0) {
	/* XXX legacy epoch-less requires/conflicts compatibility */
	rpmMessage(RPMMESS_DEBUG, _("the \"B\" dependency needs an epoch (assuming same as \"A\")\n\tA %s\tB %s\n"),
		aDepend, bDepend);
	sense = 0;
    } else if (bE && *bE && atol(bE) > 0)
	sense = -1;

    if (sense == 0) {
	sense = rpmvercmp(aV, bV);
	if (sense == 0 && aR && *aR && bR && *bR) {
	    sense = rpmvercmp(aR, bR);
	}
    }
    aEVR = _free(aEVR);
    bEVR = _free(bEVR);

    /* Detect overlap of {A,B} range. */
    result = 0;
    if (sense < 0 && ((AFlags & RPMSENSE_GREATER) || (BFlags & RPMSENSE_LESS))) {
	result = 1;
    } else if (sense > 0 && ((AFlags & RPMSENSE_LESS) || (BFlags & RPMSENSE_GREATER))) {
	result = 1;
    } else if (sense == 0 &&
	(((AFlags & RPMSENSE_EQUAL) && (BFlags & RPMSENSE_EQUAL)) ||
	 ((AFlags & RPMSENSE_LESS) && (BFlags & RPMSENSE_LESS)) ||
	 ((AFlags & RPMSENSE_GREATER) && (BFlags & RPMSENSE_GREATER)))) {
	result = 1;
    }

exit:
    rpmMessage(RPMMESS_DEBUG, _("  %s    A %s\tB %s\n"),
	(result ? _("YES") : _("NO ")), aDepend, bDepend);
    aDepend = _free(aDepend);
    bDepend = _free(bDepend);
    return result;
}

static int rangeMatchesDepFlags (Header h,
		const char * reqName, const char * reqEVR, int reqFlags)
	/*@*/
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType pnt, pvt;
    const char ** provides;
    const char ** providesEVR;
    int_32 * provideFlags;
    int providesCount;
    int result;
    int i, xx;

    if (!(reqFlags & RPMSENSE_SENSEMASK) || !reqEVR || !strlen(reqEVR))
	return 1;

    /* Get provides information from header */
    /*
     * Rpm prior to 3.0.3 does not have versioned provides.
     * If no provides version info is available, match any requires.
     */
    if (!hge(h, RPMTAG_PROVIDEVERSION, &pvt,
		(void **) &providesEVR, &providesCount))
	return 1;

    xx = hge(h, RPMTAG_PROVIDEFLAGS, NULL, (void **) &provideFlags, NULL);

    if (!hge(h, RPMTAG_PROVIDENAME, &pnt, (void **) &provides, &providesCount))
    {
	providesEVR = hfd(providesEVR, pvt);
	return 0;	/* XXX should never happen */
    }

    result = 0;
    for (i = 0; i < providesCount; i++) {

	/* Filter out provides that came along for the ride. */
	if (strcmp(provides[i], reqName))
	    continue;

	result = rpmRangesOverlap(provides[i], providesEVR[i], provideFlags[i],
			reqName, reqEVR, reqFlags);

	/* If this provide matches the require, we're done. */
	if (result)
	    break;
    }

    provides = hfd(provides, pnt);
    providesEVR = hfd(providesEVR, pvt);

    return result;
}

int headerMatchesDepFlags(Header h,
		const char * reqName, const char * reqEVR, int reqFlags)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const char *name, *version, *release;
    int_32 * epoch;
    const char *pkgEVR;
    char *p;
    int pkgFlags = RPMSENSE_EQUAL;

    if (!((reqFlags & RPMSENSE_SENSEMASK) && reqEVR && *reqEVR))
	return 1;

    /* Get package information from header */
    (void) headerNVR(h, &name, &version, &release);

    pkgEVR = p = alloca(21 + strlen(version) + 1 + strlen(release) + 1);
    *p = '\0';
    if (hge(h, RPMTAG_EPOCH, NULL, (void **) &epoch, NULL)) {
	sprintf(p, "%d:", *epoch);
	while (*p != '\0')
	    p++;
    }
    (void) stpcpy( stpcpy( stpcpy(p, version) , "-") , release);

    return rpmRangesOverlap(name, pkgEVR, pkgFlags, reqName, reqEVR, reqFlags);
}

rpmTransactionSet XrpmtsUnlink(rpmTransactionSet ts, const char * msg, const char * fn, unsigned ln)
{
/*@-modfilesystem@*/
if (_ts_debug)
fprintf(stderr, "--> ts %p -- %d %s at %s:%u\n", ts, ts->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    ts->nrefs--;
    return NULL;
}

rpmTransactionSet XrpmtsLink(rpmTransactionSet ts, const char * msg, const char * fn, unsigned ln)
{
    ts->nrefs++;
/*@-modfilesystem@*/
if (_ts_debug)
fprintf(stderr, "--> ts %p ++ %d %s at %s:%u\n", ts, ts->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    /*@-refcounttrans@*/ return ts; /*@=refcounttrans@*/
}

int rpmtsCloseDB(rpmTransactionSet ts)
{
    int rc = 0;

    if (ts->rpmdb != NULL) {
	rc = rpmdbClose(ts->rpmdb);
	ts->rpmdb = NULL;
    }
    return rc;
}

int rpmtsOpenDB(rpmTransactionSet ts, int dbmode)
{
    int rc = 0;

    if (ts->rpmdb != NULL && ts->dbmode == dbmode)
	return 0;

    (void) rpmtsCloseDB(ts);

    /* XXX there's a lock race here. */

    ts->dbmode = dbmode;
    rc = rpmdbOpen(ts->rootDir, &ts->rpmdb, ts->dbmode, 0644);
    if (rc) {
	const char * dn;
	/*@-globs -mods@*/ /* FIX: rpmGlobalMacroContext */
	dn = rpmGetPath(ts->rootDir, "%{_dbpath}", NULL);
	/*@=globs =mods@*/
	rpmMessage(RPMMESS_ERROR,
			_("cannot open Packages database in %s\n"), dn);
	dn = _free(dn);
    }
    return rc;
}

rpmdbMatchIterator rpmtsInitIterator(const rpmTransactionSet ts, int rpmtag,
			const void * keyp, size_t keylen)
{
    /*@-mods -onlytrans -type@*/ /* FIX: rpmdb excision */
    return rpmdbInitIterator(ts->rpmdb, rpmtag, keyp, keylen);
    /*@=mods =onlytrans =type@*/
}

rpmTransactionSet rpmtransCreateSet(rpmdb db, const char * rootDir)
{
    rpmTransactionSet ts;
    int rootLen;

    /*@-branchstate@*/
    if (!rootDir) rootDir = "";
    /*@=branchstate@*/

    ts = xcalloc(1, sizeof(*ts));
    ts->filesystemCount = 0;
    ts->filesystems = NULL;
    ts->di = NULL;
    if (db != NULL) {
	/*@-assignexpose@*/
	ts->rpmdb = rpmdbLink(db, "tsCreate");
	/*@=assignexpose@*/
	ts->dbmode = db->db_mode;
    } else {
	ts->rpmdb = NULL;
	ts->dbmode = O_RDONLY;
    }
    ts->scriptFd = NULL;
    ts->id = (int_32) time(NULL);
    ts->delta = 5;

    ts->numRemovedPackages = 0;
    ts->allocedRemovedPackages = ts->delta;
    ts->removedPackages = xcalloc(ts->allocedRemovedPackages,
			sizeof(*ts->removedPackages));

    /* This canonicalizes the root */
    rootLen = strlen(rootDir);
    if (!(rootLen && rootDir[rootLen - 1] == '/')) {
	char * t;

	t = alloca(rootLen + 2);
	*t = '\0';
	(void) stpcpy( stpcpy(t, rootDir), "/");
	rootDir = t;
    }

    ts->rootDir = (rootDir != NULL ? xstrdup(rootDir) : xstrdup(""));
    ts->currDir = NULL;
    ts->chrootDone = 0;

    ts->addedPackages = alCreate(ts->delta);

    ts->availablePackages = alCreate(ts->delta);

    ts->orderAlloced = ts->delta;
    ts->orderCount = 0;
    ts->order = xcalloc(ts->orderAlloced, sizeof(*ts->order));

    ts->sig = NULL;
    ts->dig = NULL;

    ts->nrefs = 0;

    return rpmtsLink(ts, "tsCreate");
}

/**
 * Compare removed package instances (qsort/bsearch).
 * @param a		1st instance address
 * @param b		2nd instance address
 * @return		result of comparison
 */
static int intcmp(const void * a, const void * b)	/*@*/
{
    const int * aptr = a;
    const int * bptr = b;
    int rc = (*aptr - *bptr);
    return rc;
}

/**
 * Add removed package instance to ordered transaction set.
 * @param ts		transaction set
 * @param dboffset	rpm database instance
 * @param depends	installed package of pair (or -1 on erase)
 * @return		0 on success
 */
static int removePackage(rpmTransactionSet ts, int dboffset, int depends)
	/*@modifies ts @*/
{

    /* Filter out duplicate erasures. */
    if (ts->numRemovedPackages > 0 && ts->removedPackages != NULL) {
	if (bsearch(&dboffset, ts->removedPackages, ts->numRemovedPackages,
			sizeof(*ts->removedPackages), intcmp) != NULL)
	    return 0;
    }

    if (ts->numRemovedPackages == ts->allocedRemovedPackages) {
	ts->allocedRemovedPackages += ts->delta;
	ts->removedPackages = xrealloc(ts->removedPackages,
		sizeof(ts->removedPackages) * ts->allocedRemovedPackages);
    }

    if (ts->removedPackages != NULL) {	/* XXX can't happen. */
	ts->removedPackages[ts->numRemovedPackages++] = dboffset;
	qsort(ts->removedPackages, ts->numRemovedPackages,
			sizeof(*ts->removedPackages), intcmp);
    }

    if (ts->orderCount == ts->orderAlloced) {
	ts->orderAlloced += ts->delta;
	ts->order = xrealloc(ts->order, sizeof(*ts->order) * ts->orderAlloced);
    }

    ts->order[ts->orderCount].type = TR_REMOVED;
    ts->order[ts->orderCount].u.removed.dboffset = dboffset;
    ts->order[ts->orderCount++].u.removed.dependsOnIndex = depends;

    return 0;
}

static /*@only@*/ const char * hNVRstr(Header h, /*@out@*/ const char ** np )
	/*@modifies *np @*/
{
    const char * NVR, * n, * v, * r;
    char * t;

    (void) headerNVR(h, &n, &v, &r);
    NVR = t = xcalloc(1, strlen(n) + strlen(v) + strlen(r) + sizeof("--"));
    t = stpcpy(t, n);
    t = stpcpy(t, "-");
    t = stpcpy(t, v);
    t = stpcpy(t, "-");
    t = stpcpy(t, r);
    if (np)
	*np = n;
    return NVR;
}

int rpmtransAddPackage(rpmTransactionSet ts, Header h, FD_t fd,
			const void * key, int upgrade, rpmRelocation * relocs)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    const char * name = NULL;
    const char * addNVR = hNVRstr(h, &name);
    const char * pkgNVR = NULL;
    rpmTagType ont, ovt;
    availablePackage p;
    int count;
    const char ** obsoletes;
    int alNum;
    int xx;
    int ec = 0;
    int rc;
    int i = ts->orderCount;

    /*
     * Check for previously added versions with the same name.
     */
    i = ts->orderCount;
    for (i = 0; i < ts->orderCount; i++) {

	if ((p = alGetPkg(ts->addedPackages, i)) == NULL)
	    break;

	if (strcmp(p->name, name))
	    continue;
	rc = rpmVersionCompare(p->h, h);
	pkgNVR = hNVRstr(p->h, NULL);
	if (rc > 0) {
	    rpmMessage(RPMMESS_WARNING,
		_("newer package %s already added, skipping %s\n"),
		pkgNVR, addNVR);
	    goto exit;
	} else if (rc == 0) {
	    rpmMessage(RPMMESS_WARNING,
		_("package %s already added, ignoring\n"),
		pkgNVR);
	    goto exit;
	} else {
	    rpmMessage(RPMMESS_WARNING,
		_("older package %s already added, replacing with %s\n"),
		pkgNVR, addNVR);
	}
	break;
    }

    /* XXX Note: i == ts->orderCount here almost always. */

    if (i == ts->orderAlloced) {
	ts->orderAlloced += ts->delta;
	ts->order = xrealloc(ts->order, ts->orderAlloced * sizeof(*ts->order));
    }
    ts->order[i].type = TR_ADDED;

    {	availablePackage this =
		alAddPackage(ts->addedPackages, i, h, key, fd, relocs);
    	alNum = alGetPkgIndex(ts->addedPackages, this);
	ts->order[i].u.addedIndex = alNum;
    }

    /* XXX sanity check */
    if (alGetPkg(ts->addedPackages, alNum) == NULL)
	goto exit;

    if (i == ts->orderCount)
	ts->orderCount++;

    if (!upgrade)
	goto exit;

    /* XXX binary rpms always have RPMTAG_SOURCERPM, source rpms do not */
    if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE))
	goto exit;

    /* Do lazy (readonly?) open of rpm database. */
    if (ts->rpmdb == NULL) {
	if ((ec = rpmtsOpenDB(ts, ts->dbmode)) != 0)
	    goto exit;
    }

    {	rpmdbMatchIterator mi;
	Header h2;

	mi = rpmtsInitIterator(ts, RPMTAG_NAME, name, 0);
	while((h2 = rpmdbNextIterator(mi)) != NULL) {
	    /*@-branchstate@*/
	    if (rpmVersionCompare(h, h2))
		xx = removePackage(ts, rpmdbGetIteratorOffset(mi), alNum);
	    else {
		uint_32 *pp, multiLibMask = 0, oldmultiLibMask = 0;

		if (hge(h2, RPMTAG_MULTILIBS, NULL, (void **) &pp, NULL))
		    oldmultiLibMask = *pp;
		if (hge(h, RPMTAG_MULTILIBS, NULL, (void **) &pp, NULL))
		    multiLibMask = *pp;
		if (oldmultiLibMask && multiLibMask
		 && !(oldmultiLibMask & multiLibMask))
		{
		    availablePackage alp = alGetPkg(ts->addedPackages, alNum);
		    if (alp != NULL)
			alp->multiLib = multiLibMask;
		}
	    }
	    /*@=branchstate@*/
	}
	mi = rpmdbFreeIterator(mi);
    }

    if (hge(h, RPMTAG_OBSOLETENAME, &ont, (void **) &obsoletes, &count)) {
	const char ** obsoletesEVR;
	int_32 * obsoletesFlags;
	int j;

	xx = hge(h, RPMTAG_OBSOLETEVERSION, &ovt, (void **) &obsoletesEVR,
			NULL);
	xx = hge(h, RPMTAG_OBSOLETEFLAGS, NULL, (void **) &obsoletesFlags,
			NULL);

	for (j = 0; j < count; j++) {

	    /* XXX avoid self-obsoleting packages. */
	    if (!strcmp(name, obsoletes[j]))
		continue;

	  { rpmdbMatchIterator mi;
	    Header h2;

	    mi = rpmtsInitIterator(ts, RPMTAG_NAME, obsoletes[j], 0);

	    xx = rpmdbPruneIterator(mi,
		ts->removedPackages, ts->numRemovedPackages, 1);

	    while((h2 = rpmdbNextIterator(mi)) != NULL) {
		/*
		 * Rpm prior to 3.0.3 does not have versioned obsoletes.
		 * If no obsoletes version info is available, match all names.
		 */
		/*@-branchstate@*/
		if (obsoletesEVR == NULL ||
		    headerMatchesDepFlags(h2,
			obsoletes[j], obsoletesEVR[j], obsoletesFlags[j]))
		{
		    xx = removePackage(ts, rpmdbGetIteratorOffset(mi), alNum);
		}
		/*@=branchstate@*/
	    }
	    mi = rpmdbFreeIterator(mi);
	  }
	}

	obsoletesEVR = hfd(obsoletesEVR, ovt);
	obsoletes = hfd(obsoletes, ont);
    }
    ec = 0;

exit:
    pkgNVR = _free(pkgNVR);
    addNVR = _free(addNVR);
    return ec;
}

void rpmtransAvailablePackage(rpmTransactionSet ts, Header h, const void * key)
{
    availablePackage al;
    al = alAddPackage(ts->availablePackages, -1, h, key, NULL, NULL);
}

int rpmtransRemovePackage(rpmTransactionSet ts, int dboffset)
{
    return removePackage(ts, dboffset, -1);
}

/*@-nullstate@*/ /* FIX: better annotations */
void rpmtransClean(rpmTransactionSet ts)
{
    if (ts) {
	HFD_t hfd = headerFreeData;
	if (ts->sig != NULL)
	    ts->sig = hfd(ts->sig, ts->sigtype);
	if (ts->dig != NULL)
	    ts->dig = pgpFreeDig(ts->dig);
    }
}

rpmTransactionSet rpmtransFree(rpmTransactionSet ts)
{
    if (ts) {

	(void) rpmtsUnlink(ts, "tsCreate");

	/*@-usereleased@*/
	if (ts->nrefs > 0)
	    return NULL;

	ts->addedPackages = alFree(ts->addedPackages);
	ts->availablePackages = alFree(ts->availablePackages);
	ts->di = _free(ts->di);
	ts->removedPackages = _free(ts->removedPackages);
	ts->order = _free(ts->order);
	/*@-type@*/ /* FIX: cast? */
	if (ts->scriptFd != NULL) {
	    ts->scriptFd =
		fdFree(ts->scriptFd, "rpmtransSetScriptFd (rpmtransFree");
	    ts->scriptFd = NULL;
	}
	/*@=type@*/
	ts->rootDir = _free(ts->rootDir);
	ts->currDir = _free(ts->currDir);

	rpmtransClean(ts);

	(void) rpmtsCloseDB(ts);

	/*@-refcounttrans@*/ ts = _free(ts); /*@=refcounttrans@*/
	/*@=usereleased@*/
    }
    return NULL;
}
/*@=nullstate@*/

rpmDependencyConflict rpmdepFreeConflicts(rpmDependencyConflict conflicts,
		int numConflicts)
{
    int i;

    if (conflicts)
    for (i = 0; i < numConflicts; i++) {
	conflicts[i].byHeader = headerFree(conflicts[i].byHeader);
	conflicts[i].byName = _free(conflicts[i].byName);
	conflicts[i].byVersion = _free(conflicts[i].byVersion);
	conflicts[i].byRelease = _free(conflicts[i].byRelease);
	conflicts[i].needsName = _free(conflicts[i].needsName);
	conflicts[i].needsVersion = _free(conflicts[i].needsVersion);
	conflicts[i].suggestedPackages = _free(conflicts[i].suggestedPackages);
    }

    return (conflicts = _free(conflicts));
}

/**
 * Check added package file lists for first package that has a provide.
 * @todo Eliminate.
 * @param al		available list
 * @param keyType	type of dependency
 * @param keyDepend	dependency string representation
 * @param keyName	dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @return		available package pointer
 */
static inline /*@only@*/ /*@null@*/ availablePackage
alSatisfiesDepend(const availableList al,
		const char * keyType, const char * keyDepend,
		const char * keyName, const char * keyEVR, int keyFlags)
	/*@*/
{
    availablePackage ret;
    availablePackage * tmp =
	alAllSatisfiesDepend(al, keyType, keyDepend, keyName, keyEVR, keyFlags);

    if (tmp) {
	ret = tmp[0];
	tmp = _free(tmp);
	return ret;
    }
    return NULL;
 }

/**
 * Check key for an unsatisfied dependency.
 * @todo Eliminate rpmrc provides.
 * @param al		available list
 * @param keyType	type of dependency
 * @param keyDepend	dependency string representation
 * @param keyName	dependency name string
 * @param keyEVR	dependency [epoch:]version[-release] string
 * @param keyFlags	dependency logical range qualifiers
 * @retval suggestion	possible package(s) to resolve dependency
 * @return		0 if satisfied, 1 if not satisfied, 2 if error
 */
static int unsatisfiedDepend(rpmTransactionSet ts,
		const char * keyType, const char * keyDepend,
		const char * keyName, const char * keyEVR, int keyFlags,
		/*@null@*/ /*@out@*/ availablePackage ** suggestion)
	/*@globals _cacheDependsRC, fileSystem @*/
	/*@modifies ts, *suggestion, _cacheDependsRC, fileSystem @*/
{
    rpmdbMatchIterator mi;
    Header h;
    int rc = 0;	/* assume dependency is satisfied */

    if (suggestion) *suggestion = NULL;

    /*
     * Check if dbiOpen/dbiPut failed (e.g. permissions), we can't cache.
     */
    if (_cacheDependsRC) {
	dbiIndex dbi;
	dbi = dbiOpen(ts->rpmdb, RPMDBI_DEPENDS, 0);
	if (dbi == NULL)
	    _cacheDependsRC = 0;
	else {
	    DBC * dbcursor = NULL;
	    size_t keylen = strlen(keyDepend);
	    void * datap = NULL;
	    size_t datalen = 0;
	    int xx;
	    xx = dbiCopen(dbi, &dbcursor, 0);
	    /*@-mods@*/		/* FIX: keyDepends mod undocumented. */
	    xx = dbiGet(dbi, dbcursor, (void **)&keyDepend, &keylen, &datap, &datalen, 0);
	    /*@=mods@*/
	    if (xx == 0 && datap && datalen == 4) {
		memcpy(&rc, datap, datalen);
		rpmMessage(RPMMESS_DEBUG, _("%s: %-45s %-s (cached)\n"),
			keyType, keyDepend, (rc ? _("NO ") : _("YES")));
		xx = dbiCclose(dbi, NULL, 0);

		if (suggestion && rc == 1)
		    *suggestion = alAllSatisfiesDepend(ts->availablePackages,
				NULL, NULL, keyName, keyEVR, keyFlags);

		return rc;
	    }
	    xx = dbiCclose(dbi, dbcursor, 0);
	}
    }

#if defined(DYING) || defined(__LCLINT__)
  { static /*@observer@*/ const char noProvidesString[] = "nada";
    static /*@observer@*/ const char * rcProvidesString = noProvidesString;
    const char * start;
    int i;

    if (rcProvidesString == noProvidesString)
	rcProvidesString = rpmGetVar(RPMVAR_PROVIDES);

    if (rcProvidesString != NULL && !(keyFlags & RPMSENSE_SENSEMASK)) {
	i = strlen(keyName);
	/*@-observertrans -mayaliasunique@*/
	while ((start = strstr(rcProvidesString, keyName))) {
	/*@=observertrans =mayaliasunique@*/
	    if (xisspace(start[i]) || start[i] == '\0' || start[i] == ',') {
		rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (rpmrc provides)\n"),
			keyType, keyDepend+2);
		goto exit;
	    }
	    rcProvidesString = start + 1;
	}
    }
  }
#endif

    /*
     * New features in rpm packaging implicitly add versioned dependencies
     * on rpmlib provides. The dependencies look like "rpmlib(YaddaYadda)".
     * Check those dependencies now.
     */
    if (!strncmp(keyName, "rpmlib(", sizeof("rpmlib(")-1)) {
	if (rpmCheckRpmlibProvides(keyName, keyEVR, keyFlags)) {
	    rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (rpmlib provides)\n"),
			keyType, keyDepend+2);
	    goto exit;
	}
	goto unsatisfied;
    }

    if (alSatisfiesDepend(ts->addedPackages, keyType, keyDepend,
		keyName, keyEVR, keyFlags))
    {
	goto exit;
    }

    /* XXX only the installer does not have the database open here. */
    if (ts->rpmdb != NULL) {
	if (*keyName == '/') {
	    /* keyFlags better be 0! */

	    mi = rpmtsInitIterator(ts, RPMTAG_BASENAMES, keyName, 0);

	    (void) rpmdbPruneIterator(mi,
			ts->removedPackages, ts->numRemovedPackages, 1);

	    while ((h = rpmdbNextIterator(mi)) != NULL) {
		rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (db files)\n"),
			keyType, keyDepend+2);
		mi = rpmdbFreeIterator(mi);
		goto exit;
	    }
	    mi = rpmdbFreeIterator(mi);
	}

	mi = rpmtsInitIterator(ts, RPMTAG_PROVIDENAME, keyName, 0);
	(void) rpmdbPruneIterator(mi,
			ts->removedPackages, ts->numRemovedPackages, 1);
	while ((h = rpmdbNextIterator(mi)) != NULL) {
	    if (rangeMatchesDepFlags(h, keyName, keyEVR, keyFlags)) {
		rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (db provides)\n"),
			keyType, keyDepend+2);
		mi = rpmdbFreeIterator(mi);
		goto exit;
	    }
	}
	mi = rpmdbFreeIterator(mi);

#if defined(DYING) || defined(__LCLINT__)
	mi = rpmtsInitIterator(ts, RPMTAG_NAME, keyName, 0);
	(void) rpmdbPruneIterator(mi,
			ts->removedPackages, ts->numRemovedPackages, 1);
	while ((h = rpmdbNextIterator(mi)) != NULL) {
	    if (rangeMatchesDepFlags(h, keyName, keyEVR, keyFlags)) {
		rpmMessage(RPMMESS_DEBUG, _("%s: %-45s YES (db package)\n"),
			keyType, keyDepend+2);
		mi = rpmdbFreeIterator(mi);
		goto exit;
	    }
	}
	mi = rpmdbFreeIterator(mi);
#endif

    }

    if (suggestion)
	*suggestion = alAllSatisfiesDepend(ts->availablePackages, NULL, NULL,
				keyName, keyEVR, keyFlags);

unsatisfied:
    rpmMessage(RPMMESS_DEBUG, _("%s: %-45s NO\n"), keyType, keyDepend+2);
    rc = 1;	/* dependency is unsatisfied */

exit:
    /*
     * If dbiOpen/dbiPut fails (e.g. permissions), we can't cache.
     */
    if (_cacheDependsRC) {
	dbiIndex dbi;
	dbi = dbiOpen(ts->rpmdb, RPMDBI_DEPENDS, 0);
	if (dbi == NULL) {
	    _cacheDependsRC = 0;
	} else {
	    DBC * dbcursor = NULL;
	    int xx;
	    xx = dbiCopen(dbi, &dbcursor, DBI_WRITECURSOR);
	    xx = dbiPut(dbi, dbcursor, keyDepend, strlen(keyDepend), &rc, sizeof(rc), 0);
	    if (xx)
		_cacheDependsRC = 0;
#if 0	/* XXX NOISY */
	    else
		rpmMessage(RPMMESS_DEBUG, _("%s: (%s, %s) added to Depends cache.\n"), keyType, keyDepend, (rc ? _("NO ") : _("YES")));
#endif
	    xx = dbiCclose(dbi, dbcursor, DBI_WRITECURSOR);
	}
    }
    return rc;
}

/**
 * Check header requires/conflicts against against installed+added packages.
 * @param ts		transaction set
 * @param psp		dependency problems
 * @param h		header to check
 * @param keyName	dependency name
 * @param multiLib	skip multilib colored dependencies?
 * @return		0 no problems found
 */
static int checkPackageDeps(rpmTransactionSet ts, problemsSet psp,
		Header h, const char * keyName, uint_32 multiLib)
	/*@globals fileSystem @*/
	/*@modifies ts, h, psp, fileSystem */
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmTagType rnt, rvt;
    rpmTagType cnt, cvt;
    const char * name, * version, * release;
    const char ** requires;
    const char ** requiresEVR = NULL;
    int_32 * requireFlags = NULL;
    int requiresCount = 0;
    const char ** conflicts;
    const char ** conflictsEVR = NULL;
    int_32 * conflictFlags = NULL;
    int conflictsCount = 0;
    rpmTagType type;
    int i, rc, xx;
    int ourrc = 0;
    availablePackage * suggestion;

    xx = headerNVR(h, &name, &version, &release);

    if (!hge(h, RPMTAG_REQUIRENAME, &rnt, (void **) &requires, &requiresCount))
    {
	requiresCount = 0;
	rvt = RPM_STRING_ARRAY_TYPE;
    } else {
	xx = hge(h, RPMTAG_REQUIREFLAGS, NULL, (void **) &requireFlags, NULL);
	xx = hge(h, RPMTAG_REQUIREVERSION, &rvt, (void **) &requiresEVR, NULL);
    }

    for (i = 0; i < requiresCount && !ourrc; i++) {
	const char * keyDepend;

	/* Filter out requires that came along for the ride. */
	if (keyName && strcmp(keyName, requires[i]))
	    continue;

	/* If this requirement comes from the core package only, not libraries,
	   then if we're installing the libraries only, don't count it in. */
	if (multiLib && !isDependsMULTILIB(requireFlags[i]))
	    continue;

	keyDepend = printDepend("R",
		requires[i], requiresEVR[i], requireFlags[i]);

	rc = unsatisfiedDepend(ts, " Requires", keyDepend,
		requires[i], requiresEVR[i], requireFlags[i], &suggestion);

	switch (rc) {
	case 0:		/* requirements are satisfied. */
	    /*@switchbreak@*/ break;
	case 1:		/* requirements are not satisfied. */
	    rpmMessage(RPMMESS_DEBUG, _("package %s-%s-%s require not satisfied: %s\n"),
		    name, version, release, keyDepend+2);

	    if (psp->num == psp->alloced) {
		psp->alloced += 5;
		psp->problems = xrealloc(psp->problems, sizeof(*psp->problems) *
			    psp->alloced);
	    }

	    {	rpmDependencyConflict pp = psp->problems + psp->num;
		pp->byHeader = headerLink(h);
		pp->byName = xstrdup(name);
		pp->byVersion = xstrdup(version);
		pp->byRelease = xstrdup(release);
		pp->needsName = xstrdup(requires[i]);
		pp->needsVersion = xstrdup(requiresEVR[i]);
		pp->needsFlags = requireFlags[i];
		pp->sense = RPMDEP_SENSE_REQUIRES;

		if (suggestion) {
		    int j;
		    for (j = 0; suggestion[j]; j++)
			{};
		    pp->suggestedPackages =
			xmalloc( (j + 1) * sizeof(*pp->suggestedPackages) );
		    for (j = 0; suggestion[j]; j++)
			pp->suggestedPackages[j] = suggestion[j]->key;
		    pp->suggestedPackages[j] = NULL;
		} else {
		    pp->suggestedPackages = NULL;
		}
	    }

	    psp->num++;
	    /*@switchbreak@*/ break;
	case 2:		/* something went wrong! */
	default:
	    ourrc = 1;
	    /*@switchbreak@*/ break;
	}
	keyDepend = _free(keyDepend);
    }

    if (requiresCount) {
	requiresEVR = hfd(requiresEVR, rvt);
	requires = hfd(requires, rnt);
    }

    if (!hge(h, RPMTAG_CONFLICTNAME, &cnt, (void **)&conflicts, &conflictsCount))
    {
	conflictsCount = 0;
	cvt = RPM_STRING_ARRAY_TYPE;
    } else {
	xx = hge(h, RPMTAG_CONFLICTFLAGS, &type,
		(void **) &conflictFlags, &conflictsCount);
	xx = hge(h, RPMTAG_CONFLICTVERSION, &cvt,
		(void **) &conflictsEVR, &conflictsCount);
    }

    for (i = 0; i < conflictsCount && !ourrc; i++) {
	const char * keyDepend;

	/* Filter out conflicts that came along for the ride. */
	if (keyName && strcmp(keyName, conflicts[i]))
	    continue;

	/* If this requirement comes from the core package only, not libraries,
	   then if we're installing the libraries only, don't count it in. */
	if (multiLib && !isDependsMULTILIB(conflictFlags[i]))
	    continue;

	keyDepend = printDepend("C", conflicts[i], conflictsEVR[i], conflictFlags[i]);

	rc = unsatisfiedDepend(ts, "Conflicts", keyDepend,
		conflicts[i], conflictsEVR[i], conflictFlags[i], NULL);

	/* 1 == unsatisfied, 0 == satsisfied */
	switch (rc) {
	case 0:		/* conflicts exist. */
	    rpmMessage(RPMMESS_DEBUG, _("package %s conflicts: %s\n"),
		    name, keyDepend+2);

	    if (psp->num == psp->alloced) {
		psp->alloced += 5;
		psp->problems = xrealloc(psp->problems,
					sizeof(*psp->problems) * psp->alloced);
	    }

	    {	rpmDependencyConflict pp = psp->problems + psp->num;
		pp->byHeader = headerLink(h);
		pp->byName = xstrdup(name);
		pp->byVersion = xstrdup(version);
		pp->byRelease = xstrdup(release);
		pp->needsName = xstrdup(conflicts[i]);
		pp->needsVersion = xstrdup(conflictsEVR[i]);
		pp->needsFlags = conflictFlags[i];
		pp->sense = RPMDEP_SENSE_CONFLICTS;
		pp->suggestedPackages = NULL;
	    }

	    psp->num++;
	    /*@switchbreak@*/ break;
	case 1:		/* conflicts don't exist. */
	    /*@switchbreak@*/ break;
	case 2:		/* something went wrong! */
	default:
	    ourrc = 1;
	    /*@switchbreak@*/ break;
	}
	keyDepend = _free(keyDepend);
    }

    if (conflictsCount) {
	conflictsEVR = hfd(conflictsEVR, cvt);
	conflicts = hfd(conflicts, cnt);
    }

    return ourrc;
}

/**
 * Check dependency against installed packages.
 * Adding: check name/provides key against each conflict match,
 * Erasing: check name/provides/filename key against each requiredby match.
 * @param ts		transaction set
 * @param psp		dependency problems
 * @param key		dependency name
 * @param mi		rpm database iterator
 * @return		0 no problems found
 */
static int checkPackageSet(rpmTransactionSet ts, problemsSet psp,
		const char * key, /*@only@*/ /*@null@*/ rpmdbMatchIterator mi)
	/*@globals fileSystem @*/
	/*@modifies ts, mi, psp, fileSystem @*/
{
    Header h;
    int rc = 0;

    (void) rpmdbPruneIterator(mi,
		ts->removedPackages, ts->numRemovedPackages, 1);
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	if (checkPackageDeps(ts, psp, h, key, 0)) {
	    rc = 1;
	    break;
	}
    }
    mi = rpmdbFreeIterator(mi);

    return rc;
}

/**
 * Erasing: check name/provides/filename key against requiredby matches.
 * @param ts		transaction set
 * @param psp		dependency problems
 * @param key		requires name
 * @return		0 no problems found
 */
static int checkDependentPackages(rpmTransactionSet ts,
			problemsSet psp, const char * key)
	/*@globals fileSystem @*/
	/*@modifies ts, psp, fileSystem @*/
{
    rpmdbMatchIterator mi;
    mi = rpmtsInitIterator(ts, RPMTAG_REQUIRENAME, key, 0);
    return checkPackageSet(ts, psp, key, mi);
}

/**
 * Adding: check name/provides key against conflicts matches.
 * @param ts		transaction set
 * @param psp		dependency problems
 * @param key		conflicts name
 * @return		0 no problems found
 */
static int checkDependentConflicts(rpmTransactionSet ts,
		problemsSet psp, const char * key)
	/*@globals fileSystem @*/
	/*@modifies ts, psp, fileSystem @*/
{
    int rc = 0;

    if (ts->rpmdb) {	/* XXX is this necessary? */
	rpmdbMatchIterator mi;
	mi = rpmtsInitIterator(ts, RPMTAG_CONFLICTNAME, key, 0);
	rc = checkPackageSet(ts, psp, key, mi);
    }

    return rc;
}

/*
 * XXX Hack to remove known Red Hat dependency loops, will be removed
 * as soon as rpm's legacy permits.
 */
#define	DEPENDENCY_WHITEOUT

#if defined(DEPENDENCY_WHITEOUT)
/*@observer@*/ /*@unchecked@*/
static struct badDeps_s {
/*@observer@*/ /*@null@*/ const char * pname;
/*@observer@*/ /*@null@*/ const char * qname;
} badDeps[] = {
    { "libtermcap", "bash" },
    { "modutils", "vixie-cron" },
    { "ypbind", "yp-tools" },
    { "ghostscript-fonts", "ghostscript" },
    /* 7.2 only */
    { "libgnomeprint15", "gnome-print" },
    { "nautilus", "nautilus-mozilla" },
    /* 7.1 only */
    { "arts", "kdelibs-sound" },
    /* 7.0 only */
    { "pango-gtkbeta-devel", "pango-gtkbeta" },
    { "XFree86", "Mesa" },
    { "compat-glibc", "db2" },
    { "compat-glibc", "db1" },
    { "pam", "initscripts" },
    { "initscripts", "sysklogd" },
    /* 6.2 */
    { "egcs-c++", "libstdc++" },
    /* 6.1 */
    { "pilot-link-devel", "pilot-link" },
    /* 5.2 */
    { "pam", "pamconfig" },
    { NULL, NULL }
};

static int ignoreDep(const availablePackage p,
		const availablePackage q)
	/*@*/
{
    struct badDeps_s * bdp = badDeps;

    while (bdp->pname != NULL && bdp->qname != NULL) {
	if (!strcmp(p->name, bdp->pname) && !strcmp(q->name, bdp->qname))
	    return 1;
	bdp++;
    }
    return 0;
}
#endif

/**
 * Recursively mark all nodes with their predecessors.
 * @param tsi		successor chain
 * @param q		predecessor
 */
static void markLoop(/*@special@*/ tsortInfo tsi, availablePackage q)
	/*@globals internalState @*/
	/*@uses tsi @*/
	/*@modifies internalState @*/
{
    availablePackage p;

    while (tsi != NULL && (p = tsi->tsi_suc) != NULL) {
	tsi = tsi->tsi_next;
	if (p->tsi.tsi_pkg != NULL)
	    continue;
	/*@-assignexpose@*/
	p->tsi.tsi_pkg = q;
	/*@=assignexpose@*/
	if (p->tsi.tsi_next != NULL)
	    markLoop(p->tsi.tsi_next, p);
    }
}

static inline /*@observer@*/ const char * const identifyDepend(int_32 f)
	/*@*/
{
    if (isLegacyPreReq(f))
	return "PreReq:";
    f = _notpre(f);
    if (f & RPMSENSE_SCRIPT_PRE)
	return "Requires(pre):";
    if (f & RPMSENSE_SCRIPT_POST)
	return "Requires(post):";
    if (f & RPMSENSE_SCRIPT_PREUN)
	return "Requires(preun):";
    if (f & RPMSENSE_SCRIPT_POSTUN)
	return "Requires(postun):";
    if (f & RPMSENSE_SCRIPT_VERIFY)
	return "Requires(verify):";
    if (f & RPMSENSE_FIND_REQUIRES)
	return "Requires(auto):";
    return "Requires:";
}

/**
 * Find (and eliminate co-requisites) "q <- p" relation in dependency loop.
 * Search all successors of q for instance of p. Format the specific relation,
 * (e.g. p contains "Requires: q"). Unlink and free co-requisite (i.e.
 * pure Requires: dependencies) successor node(s).
 * @param q		sucessor (i.e. package required by p)
 * @param p		predecessor (i.e. package that "Requires: q")
 * @param zap		max. no. of co-requisites to remove (-1 is all)?
 * @retval nzaps	address of no. of relations removed
 * @return		(possibly NULL) formatted "q <- p" releation (malloc'ed)
 */
static /*@owned@*/ /*@null@*/ const char *
zapRelation(availablePackage q, availablePackage p,
		int zap, /*@in@*/ /*@out@*/ int * nzaps)
	/*@modifies q, p, *nzaps @*/
{
    tsortInfo tsi_prev;
    tsortInfo tsi;
    const char *dp = NULL;

    for (tsi_prev = &q->tsi, tsi = q->tsi.tsi_next;
	 tsi != NULL;
	/* XXX Note: the loop traverses "not found", break on "found". */
	/*@-nullderef@*/
	 tsi_prev = tsi, tsi = tsi->tsi_next)
	/*@=nullderef@*/
    {
	int j;

	if (tsi->tsi_suc != p)
	    continue;
	if (p->requires == NULL) continue;	/* XXX can't happen */
	if (p->requireFlags == NULL) continue;	/* XXX can't happen */
	if (p->requiresEVR == NULL) continue;	/* XXX can't happen */

	j = tsi->tsi_reqx;
	dp = printDepend( identifyDepend(p->requireFlags[j]),
		p->requires[j], p->requiresEVR[j], p->requireFlags[j]);

	/*
	 * Attempt to unravel a dependency loop by eliminating Requires's.
	 */
	/*@-branchstate@*/
	if (zap && !(p->requireFlags[j] & RPMSENSE_PREREQ)) {
	    rpmMessage(RPMMESS_DEBUG,
			_("removing %s-%s-%s \"%s\" from tsort relations.\n"),
			p->name, p->version, p->release, dp);
	    p->tsi.tsi_count--;
	    if (tsi_prev) tsi_prev->tsi_next = tsi->tsi_next;
	    tsi->tsi_next = NULL;
	    tsi->tsi_suc = NULL;
	    tsi = _free(tsi);
	    if (nzaps)
		(*nzaps)++;
	    if (zap)
		zap--;
	}
	/*@=branchstate@*/
	/* XXX Note: the loop traverses "not found", get out now! */
	break;
    }
    return dp;
}

/**
 * Record next "q <- p" relation (i.e. "p" requires "q").
 * @param ts		transaction set
 * @param p		predecessor (i.e. package that "Requires: q")
 * @param selected	boolean package selected array
 * @param j		relation index
 * @return		0 always
 */
static inline int addRelation( const rpmTransactionSet ts,
		availablePackage p, unsigned char * selected, int j)
	/*@modifies p, *selected @*/
{
    availablePackage q;
    tsortInfo tsi;
    int matchNum;

    if (!p->requires || !p->requiresEVR || !p->requireFlags)
	return 0;

    q = alSatisfiesDepend(ts->addedPackages, NULL, NULL,
		p->requires[j], p->requiresEVR[j], p->requireFlags[j]);

    /* Ordering depends only on added package relations. */
    if (q == NULL)
	return 0;

    /* Avoid rpmlib feature dependencies. */
    if (!strncmp(p->requires[j], "rpmlib(", sizeof("rpmlib(")-1))
	return 0;

#if defined(DEPENDENCY_WHITEOUT)
    /* Avoid certain dependency relations. */
    if (ignoreDep(p, q))
	return 0;
#endif

    /* Avoid redundant relations. */
    /* XXX TODO: add control bit. */
    matchNum = alGetPkgIndex(ts->addedPackages, q);
    if (selected[matchNum] != 0)
	return 0;
    selected[matchNum] = 1;

    /* T3. Record next "q <- p" relation (i.e. "p" requires "q"). */
    p->tsi.tsi_count++;			/* bump p predecessor count */
    if (p->depth <= q->depth)		/* Save max. depth in dependency tree */
	p->depth = q->depth + 1;

    tsi = xmalloc(sizeof(*tsi));
    /*@-assignexpose@*/
    tsi->tsi_suc = p;
    /*@=assignexpose@*/
    tsi->tsi_reqx = j;
    tsi->tsi_next = q->tsi.tsi_next;
    q->tsi.tsi_next = tsi;
    q->tsi.tsi_qcnt++;			/* bump q successor count */
    return 0;
}

/**
 * Compare ordered list entries by index (qsort/bsearch).
 * @param a		1st ordered list entry
 * @param b		2nd ordered list entry
 * @return		result of comparison
 */
static int orderListIndexCmp(const void * one, const void * two)	/*@*/
{
    /*@-castexpose@*/
    int a = ((const orderListIndex)one)->alIndex;
    int b = ((const orderListIndex)two)->alIndex;
    /*@=castexpose@*/
    return (a - b);
}

/**
 * Add element to list sorting by initial successor count.
 * @param p		new element
 * @retval qp		address of first element
 * @retval rp		address of last element
 */
static void addQ(availablePackage p,
		/*@in@*/ /*@out@*/ availablePackage * qp,
		/*@in@*/ /*@out@*/ availablePackage * rp)
	/*@modifies p, *qp, *rp @*/
{
    availablePackage q, qprev;

    if ((*rp) == NULL) {	/* 1st element */
	(*rp) = (*qp) = p;
	return;
    }
    for (qprev = NULL, q = (*qp); q != NULL; qprev = q, q = q->tsi.tsi_suc) {
	if (q->tsi.tsi_qcnt <= p->tsi.tsi_qcnt)
	    break;
    }
    /*@-assignexpose@*/
    if (qprev == NULL) {	/* insert at beginning of list */
	p->tsi.tsi_suc = q;
	(*qp) = p;		/* new head */
    } else if (q == NULL) {	/* insert at end of list */
	qprev->tsi.tsi_suc = p;
	(*rp) = p;		/* new tail */
    } else {			/* insert between qprev and q */
	p->tsi.tsi_suc = q;
	qprev->tsi.tsi_suc = p;
    }
    /*@=assignexpose@*/
}

int rpmdepOrder(rpmTransactionSet ts)
{
    int npkgs = alGetSize(ts->addedPackages);
    int chainsaw = ts->transFlags & RPMTRANS_FLAG_CHAINSAW;
    availablePackage p;
    availablePackage q;
    availablePackage r;
    tsortInfo tsi;
    tsortInfo tsi_next;
    int * ordering = alloca(sizeof(*ordering) * (npkgs + 1));
    int orderingCount = 0;
    unsigned char * selected = alloca(sizeof(*selected) * (npkgs + 1));
    int loopcheck;
    transactionElement newOrder;
    int newOrderCount = 0;
    orderListIndex orderList;
    int nrescans = 10;
    int _printed = 0;
    int qlen;
    int i, j;

    alMakeIndex(ts->addedPackages);
    alMakeIndex(ts->availablePackages);

    /* T1. Initialize. */
    loopcheck = npkgs;

    /* Record all relations. */
    rpmMessage(RPMMESS_DEBUG, _("========== recording tsort relations\n"));
    for (i = 0; i < npkgs; i++) {
	int matchNum;

	if ((p = alGetPkg(ts->addedPackages, i)) == NULL)
	    break;

	if (p->requiresCount <= 0)
	    continue;

	memset(selected, 0, sizeof(*selected) * npkgs);

	/* Avoid narcisstic relations. */
	matchNum = alGetPkgIndex(ts->addedPackages, p);
	selected[matchNum] = 1;

	/* T2. Next "q <- p" relation. */

	/* First, do pre-requisites. */
	for (j = 0; j < p->requiresCount; j++) {

	    if (p->requireFlags == NULL) /* XXX can't happen */
		/*@innercontinue@*/ continue;

	    /* Skip if not %pre/%post requires or legacy prereq. */

	    if (isErasePreReq(p->requireFlags[j]) ||
		!( isInstallPreReq(p->requireFlags[j]) ||
		   isLegacyPreReq(p->requireFlags[j]) ))
		/*@innercontinue@*/ continue;

	    /* T3. Record next "q <- p" relation (i.e. "p" requires "q"). */
	    (void) addRelation(ts, p, selected, j);

	}

	/* Then do co-requisites. */
	for (j = 0; j < p->requiresCount; j++) {

	    if (p->requireFlags == NULL) /* XXX can't happen */
		/*@innercontinue@*/ continue;

	    /* Skip if %pre/%post requires or legacy prereq. */

	    if (isErasePreReq(p->requireFlags[j]) ||
		 ( isInstallPreReq(p->requireFlags[j]) ||
		   isLegacyPreReq(p->requireFlags[j]) ))
		/*@innercontinue@*/ continue;

	    /* T3. Record next "q <- p" relation (i.e. "p" requires "q"). */
	    (void) addRelation(ts, p, selected, j);

	}
    }

    /* Save predecessor count. */
    for (i = 0; i < npkgs; i++) {

	if ((p = alGetPkg(ts->addedPackages, i)) == NULL)
	    break;

	p->npreds = p->tsi.tsi_count;
    }

    /* T4. Scan for zeroes. */
    rpmMessage(RPMMESS_DEBUG, _("========== tsorting packages (order, #predecessors, #succesors, depth)\n"));

rescan:
    q = r = NULL;
    qlen = 0;
    for (i = 0; i < npkgs; i++) {

	if ((p = alGetPkg(ts->addedPackages, i)) == NULL)
	    break;

	/* Prefer packages in presentation order. */
	if (!chainsaw)
	    p->tsi.tsi_qcnt = (npkgs - i);

	if (p->tsi.tsi_count != 0)
	    continue;
	p->tsi.tsi_suc = NULL;
	addQ(p, &q, &r);
	qlen++;
    }

    /* T5. Output front of queue (T7. Remove from queue.) */
    for (; q != NULL; q = q->tsi.tsi_suc) {

	rpmMessage(RPMMESS_DEBUG, "%5d%5d%5d%3d %*s %s-%s-%s\n",
			orderingCount, q->npreds, q->tsi.tsi_qcnt, q->depth,
			2*q->depth, "",
			q->name, q->version, q->release);
	ordering[orderingCount++] = alGetPkgIndex(ts->addedPackages, q);
	qlen--;
	loopcheck--;

	/* T6. Erase relations. */
	tsi_next = q->tsi.tsi_next;
	q->tsi.tsi_next = NULL;
	while ((tsi = tsi_next) != NULL) {
	    tsi_next = tsi->tsi_next;
	    tsi->tsi_next = NULL;
	    p = tsi->tsi_suc;
	    if (p && (--p->tsi.tsi_count) <= 0) {
		/* XXX TODO: add control bit. */
		p->tsi.tsi_suc = NULL;
		/*@-nullstate@*/	/* FIX: q->tsi.tsi_u.suc may be NULL */
		addQ(p, &q->tsi.tsi_suc, &r);
		/*@=nullstate@*/
		qlen++;
	    }
	    tsi = _free(tsi);
	}
	if (!_printed && loopcheck == qlen && q->tsi.tsi_suc != NULL) {
	    _printed++;
	    rpmMessage(RPMMESS_DEBUG,
		_("========== successors only (presentation order)\n"));
	}
    }

    /* T8. End of process. Check for loops. */
    if (loopcheck != 0) {
	int nzaps;

	/* T9. Initialize predecessor chain. */
	nzaps = 0;
	for (i = 0; i < npkgs; i++) {

	    if ((q = alGetPkg(ts->addedPackages, i)) == NULL)
		break;

	    q->tsi.tsi_pkg = NULL;
	    q->tsi.tsi_reqx = 0;
	    /* Mark packages already sorted. */
	    if (q->tsi.tsi_count == 0)
		q->tsi.tsi_count = -1;
	}

	/* T10. Mark all packages with their predecessors. */
	for (i = 0; i < npkgs; i++) {

	    if ((q = alGetPkg(ts->addedPackages, i)) == NULL)
		break;

	    if ((tsi = q->tsi.tsi_next) == NULL)
		continue;
	    q->tsi.tsi_next = NULL;
	    markLoop(tsi, q);
	    q->tsi.tsi_next = tsi;
	}

	/* T11. Print all dependency loops. */
	/*@-branchstate@*/
	for (i = 0; i < npkgs; i++) {
	    int printed;

	    if ((r = alGetPkg(ts->addedPackages, i)) == NULL)
		break;

	    printed = 0;

	    /* T12. Mark predecessor chain, looking for start of loop. */
	    for (q = r->tsi.tsi_pkg; q != NULL; q = q->tsi.tsi_pkg) {
		if (q->tsi.tsi_reqx)
		    /*@innerbreak@*/ break;
		q->tsi.tsi_reqx = 1;
	    }

	    /* T13. Print predecessor chain from start of loop. */
	    while ((p = q) != NULL && (q = p->tsi.tsi_pkg) != NULL) {
		const char * dp;
		char buf[4096];

		/* Unchain predecessor loop. */
		p->tsi.tsi_pkg = NULL;

		if (!printed) {
		    rpmMessage(RPMMESS_DEBUG, _("LOOP:\n"));
		    printed = 1;
		}

		/* Find (and destroy if co-requisite) "q <- p" relation. */
		dp = zapRelation(q, p, 1, &nzaps);

		/* Print next member of loop. */
		sprintf(buf, "%s-%s-%s", p->name, p->version, p->release);
		rpmMessage(RPMMESS_DEBUG, "    %-40s %s\n", buf,
			(dp ? dp : "not found!?!"));

		dp = _free(dp);
	    }

	    /* Walk (and erase) linear part of predecessor chain as well. */
	    for (p = r, q = r->tsi.tsi_pkg;
		 q != NULL;
		 p = q, q = q->tsi.tsi_pkg)
	    {
		/* Unchain linear part of predecessor loop. */
		p->tsi.tsi_pkg = NULL;
		p->tsi.tsi_reqx = 0;
	    }
	}
	/*@=branchstate@*/

	/* If a relation was eliminated, then continue sorting. */
	/* XXX TODO: add control bit. */
	if (nzaps && nrescans-- > 0) {
	    rpmMessage(RPMMESS_DEBUG, _("========== continuing tsort ...\n"));
	    goto rescan;
	}
	return 1;
    }

    /*
     * The order ends up as installed packages followed by removed packages,
     * with removes for upgrades immediately following the installation of
     * the new package. This would be easier if we could sort the
     * addedPackages array, but we store indexes into it in various places.
     */
    orderList = xcalloc(npkgs, sizeof(*orderList));
    for (i = 0, j = 0; i < ts->orderCount; i++) {
	if (ts->order[i].type == TR_ADDED) {
	    orderList[j].alIndex = ts->order[i].u.addedIndex;
	    orderList[j].orIndex = i;
	    j++;
	}
    }
    assert(j <= npkgs);

    qsort(orderList, npkgs, sizeof(*orderList), orderListIndexCmp);

    newOrder = xcalloc(ts->orderCount, sizeof(*newOrder));
    for (i = 0, newOrderCount = 0; i < orderingCount; i++) {
	struct orderListIndex_s key;
	orderListIndex needle;

	key.alIndex = ordering[i];
	needle = bsearch(&key, orderList, npkgs, sizeof(key),orderListIndexCmp);
	/* bsearch should never, ever fail */
	if (needle == NULL) continue;

	/*@-assignexpose@*/
	newOrder[newOrderCount++] = ts->order[needle->orIndex];
	/*@=assignexpose@*/
	for (j = needle->orIndex + 1; j < ts->orderCount; j++) {
	    if (ts->order[j].type == TR_REMOVED &&
		ts->order[j].u.removed.dependsOnIndex == needle->alIndex) {
		/*@-assignexpose@*/
		newOrder[newOrderCount++] = ts->order[j];
		/*@=assignexpose@*/
	    } else
		/*@innerbreak@*/ break;
	}
    }

    for (i = 0; i < ts->orderCount; i++) {
	if (ts->order[i].type == TR_REMOVED &&
	    ts->order[i].u.removed.dependsOnIndex == -1)  {
	    /*@-assignexpose@*/
	    newOrder[newOrderCount++] = ts->order[i];
	    /*@=assignexpose@*/
	}
    }
    assert(newOrderCount == ts->orderCount);

    ts->order = _free(ts->order);
    ts->order = newOrder;
    ts->orderAlloced = ts->orderCount;
    orderList = _free(orderList);

    return 0;
}

/**
 * Close a single database index.
 * @param db		rpm database
 * @param rpmtag	rpm tag
 * @return              0 on success
 */
static int rpmdbCloseDBI(/*@null@*/ rpmdb db, int rpmtag)
	/*@globals fileSystem @*/
	/*@modifies db, fileSystem @*/
{
    int dbix;
    int rc = 0;

    if (db == NULL || db->_dbi == NULL || dbiTags == NULL)
	return 0;

    for (dbix = 0; dbix < dbiTagsMax; dbix++) {
	if (dbiTags[dbix] != rpmtag)
	    continue;
	if (db->_dbi[dbix] != NULL) {
	    int xx;
	    /*@-unqualifiedtrans@*/		/* FIX: double indirection. */
	    xx = dbiClose(db->_dbi[dbix], 0);
	    if (xx && rc == 0) rc = xx;
	    db->_dbi[dbix] = NULL;
	    /*@=unqualifiedtrans@*/
	}
	break;
    }
    return rc;
}

int rpmdepCheck(rpmTransactionSet ts,
		rpmDependencyConflict * conflicts, int * numConflicts)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    HFD_t hfd = headerFreeData;
    rpmdbMatchIterator mi = NULL;
    Header h = NULL;
    availablePackage p;
    problemsSet ps = NULL;
    int npkgs;
    int closeatexit = 0;
    int i, j, xx;
    int rc;

    /* Do lazy, readonly, open of rpm database. */
    if (ts->rpmdb == NULL) {
	if ((rc = rpmtsOpenDB(ts, ts->dbmode)) != 0)
	    goto exit;
	closeatexit = 1;
    }

    npkgs = alGetSize(ts->addedPackages);

    ps = xcalloc(1, sizeof(*ps));
    ps->alloced = 5;
    ps->num = 0;
    ps->problems = xcalloc(ps->alloced, sizeof(*ps->problems));

    *conflicts = NULL;
    *numConflicts = 0;

    alMakeIndex(ts->addedPackages);
    alMakeIndex(ts->availablePackages);

    /*
     * Look at all of the added packages and make sure their dependencies
     * are satisfied.
     */
    for (i = 0; i < npkgs; i++)
    {
	if ((p = alGetPkg(ts->addedPackages, i)) == NULL)
	    break;

        rpmMessage(RPMMESS_DEBUG,  "========== +++ %s-%s-%s\n" ,
		p->name, p->version, p->release);
	rc = checkPackageDeps(ts, ps, p->h, NULL, p->multiLib);
	if (rc)
	    goto exit;

	/* Adding: check name against conflicts matches. */
	rc = checkDependentConflicts(ts, ps, p->name);
	if (rc)
	    goto exit;

	if (p->providesCount == 0 || p->provides == NULL)
	    continue;

	rc = 0;
	for (j = 0; j < p->providesCount; j++) {
	    /* Adding: check provides key against conflicts matches. */
	    if (!checkDependentConflicts(ts, ps, p->provides[j]))
		/*@innercontinue@*/ continue;
	    rc = 1;
	    /*@innerbreak@*/ break;
	}
	if (rc)
	    goto exit;
    }

    /*
     * Look at the removed packages and make sure they aren't critical.
     */
    /*@-branchstate@*/
    if (ts->numRemovedPackages > 0) {
      mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES, NULL, 0);
      xx = rpmdbAppendIterator(mi,
			ts->removedPackages, ts->numRemovedPackages);
      while ((h = rpmdbNextIterator(mi)) != NULL) {

	{   const char * name, * version, * release;
	    xx = headerNVR(h, &name, &version, &release);
	    rpmMessage(RPMMESS_DEBUG,  "========== --- %s-%s-%s\n" ,
		name, version, release);

	    /* Erasing: check name against requiredby matches. */
	    rc = checkDependentPackages(ts, ps, name);
	    if (rc)
		goto exit;
	}

	{   const char ** provides;
	    int providesCount;
	    rpmTagType pnt;

	    if (hge(h, RPMTAG_PROVIDENAME, &pnt, (void **) &provides,
				&providesCount))
	    {
		rc = 0;
		for (j = 0; j < providesCount; j++) {
		    /* Erasing: check provides against requiredby matches. */
		    if (!checkDependentPackages(ts, ps, provides[j]))
			/*@innercontinue@*/ continue;
		    rc = 1;
		    /*@innerbreak@*/ break;
		}
		provides = hfd(provides, pnt);
		if (rc)
		    goto exit;
	    }
	}

	{   const char ** baseNames, ** dirNames;
	    int_32 * dirIndexes;
	    rpmTagType dnt, bnt;
	    int fileCount;
	    char * fileName = NULL;
	    int fileAlloced = 0;
	    int len;

	    if (hge(h, RPMTAG_BASENAMES, &bnt, (void **) &baseNames, &fileCount))
	    {
		xx = hge(h, RPMTAG_DIRNAMES, &dnt, (void **) &dirNames, NULL);
		xx = hge(h, RPMTAG_DIRINDEXES, NULL, (void **) &dirIndexes,
				NULL);
		rc = 0;
		for (j = 0; j < fileCount; j++) {
		    len = strlen(baseNames[j]) + 1 +
			  strlen(dirNames[dirIndexes[j]]);
		    if (len > fileAlloced) {
			fileAlloced = len * 2;
			fileName = xrealloc(fileName, fileAlloced);
		    }
		    *fileName = '\0';
		    (void) stpcpy( stpcpy(fileName, dirNames[dirIndexes[j]]) , baseNames[j]);
		    /* Erasing: check filename against requiredby matches. */
		    if (!checkDependentPackages(ts, ps, fileName))
			/*@innercontinue@*/ continue;
		    rc = 1;
		    /*@innerbreak@*/ break;
		}

		fileName = _free(fileName);
		baseNames = hfd(baseNames, bnt);
		dirNames = hfd(dirNames, dnt);
		if (rc)
		    goto exit;
	    }
	}

      }
      mi = rpmdbFreeIterator(mi);
    }
    /*@=branchstate@*/

    if (ps->num) {
	*conflicts = ps->problems;
	ps->problems = NULL;
	*numConflicts = ps->num;
    }
    rc = 0;

exit:
    mi = rpmdbFreeIterator(mi);
    if (ps) {
	ps->problems = _free(ps->problems);
	ps = _free(ps);
    }
    /*@-branchstate@*/
    if (closeatexit)
	xx = rpmtsCloseDB(ts);
    else if (_cacheDependsRC)
	xx = rpmdbCloseDBI(ts->rpmdb, RPMDBI_DEPENDS);
    /*@=branchstate@*/
    return rc;
}
