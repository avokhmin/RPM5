/** \ingroup rpmdep
 * \file lib/rpmds.c
 */
#include "system.h"
#include <rpmlib.h>

#include "rpmds.h"

#include "debug.h"

/*@access alKey@*/

/*@access rpmFNSet @*/

/*@unchecked@*/
static int _fns_debug = 0;

/*@-shadow@*/	/* XXX copy from depends.c for now. */
static char * hGetNEVR(Header h, /*@out@*/ const char ** np)
	/*@modifies *np @*/
{
    const char * n, * v, * r;
    char * NVR, * t;

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
/*@=shadow@*/

rpmFNSet fnsFree(rpmFNSet fns)
{
    HFD_t hfd = headerFreeData;

    if (fns == NULL)
	return NULL;

/*@-modfilesystem@*/
if (_fns_debug)
fprintf(stderr, "*** fns %p -- %s[%d]\n", fns, fns->Type, fns->fc);
/*@=modfilesystem@*/

    /*@-branchstate@*/
    if (fns->fc > 0) {
	fns->bnl = hfd(fns->bnl, -1);
	fns->dnl = hfd(fns->dnl, -1);

	fns->flinks = hfd(fns->flinks, -1);
	fns->flangs = hfd(fns->flangs, -1);
	fns->fmd5s = hfd(fns->fmd5s, -1);

	fns->fstates = _free(fns->fstates);

	/*@-evalorder@*/
	if (fns->h != NULL) {
	    fns->h = headerFree(fns->h, "fnsFree");
	} else {
	    fns->fmtimes = _free(fns->fmtimes);
	    fns->fmodes = _free(fns->fmodes);
	    fns->fflags = _free(fns->fflags);
	    fns->fsizes = _free(fns->fsizes);
	    fns->frdevs = _free(fns->frdevs);
	    fns->dil = _free(fns->dil);
	}
	/*@=evalorder@*/
    }
    /*@=branchstate@*/

    memset(fns, 0, sizeof(*fns));		/* XXX trash and burn */
    fns = _free(fns);
    return NULL;
}

#define	_fdupe(_fns, _data)	\
    if ((_fns)->_data != NULL)	\
	(_fns)->_data = memcpy(xmalloc((_fns)->fc * sizeof(*(_fns)->_data)), \
			(_fns)->_data, (_fns)->fc * sizeof(*(_fns)->_data))

rpmFNSet fnsNew(Header h, rpmTag tagN, int scareMem)
{
    HGE_t hge =
	(scareMem ? (HGE_t) headerGetEntryMinMemory : (HGE_t) headerGetEntry);
    rpmFNSet fns = NULL;
    const char * Type;
    const char ** N;
    rpmTagType Nt;
    int_32 Count;

    if (tagN == RPMTAG_BASENAMES) {
	Type = "Files";
    } else
	goto exit;

    /*@-branchstate@*/
    if (hge(h, tagN, &Nt, (void **) &N, &Count)
     && N != NULL && Count > 0)
    {
	int xx;

	fns = xcalloc(1, sizeof(*fns));
	fns->h = headerLink(h, "fnsNew");
	fns->i = -1;
	fns->Type = Type;
	fns->tagN = tagN;
	fns->bnl = N;
	fns->fc = Count;

	xx = hge(h, RPMTAG_DIRNAMES, NULL, (void **) &fns->dnl, &fns->dc);

	xx = hge(h, RPMTAG_FILELINKTOS, NULL, (void **) &fns->flinks, NULL);
	xx = hge(h, RPMTAG_FILELANGS, NULL, (void **) &fns->flangs, NULL);
	xx = hge(h, RPMTAG_FILEMD5S, NULL, (void **) &fns->fmd5s, NULL);

	xx = hge(h, RPMTAG_FILEMTIMES, NULL, (void **) &fns->fmtimes, NULL);
	xx = hge(h, RPMTAG_FILEMODES, NULL, (void **) &fns->fmodes, NULL);
	xx = hge(h, RPMTAG_FILEFLAGS, NULL, (void **) &fns->fflags, NULL);
	xx = hge(h, RPMTAG_FILESIZES, NULL, (void **) &fns->fsizes, NULL);
	xx = hge(h, RPMTAG_FILERDEVS, NULL, (void **) &fns->frdevs, NULL);
	xx = hge(h, RPMTAG_DIRINDEXES, NULL, (void **) &fns->dil, NULL);

	xx = hge(h, RPMTAG_FILESTATES, NULL, (void **) &fns->fstates, NULL);
	_fdupe(fns, fstates);
	if (xx == 0 || fns->fstates == NULL)
	    fns->fstates = xcalloc(fns->fc, sizeof(*fns->fstates));

	if (!scareMem) {
	    _fdupe(fns, fmtimes);
	    _fdupe(fns, fmodes);
	    _fdupe(fns, fflags);
	    _fdupe(fns, fsizes);
	    _fdupe(fns, frdevs);
	    _fdupe(fns, dil);
	    fns->h = headerFree(fns->h, "fnsNew");
	}

/*@-modfilesystem@*/
if (_fns_debug)
fprintf(stderr, "*** fns %p ++ %s[%d]\n", fns, fns->Type, fns->fc);
/*@=modfilesystem@*/

    }
    /*@-branchstate@*/

exit:
    /*@-nullret@*/ /* FIX: fns->{dil,fflags} may be NULL. */
/*@i@*/ return fns;
    /*@=nullret@*/
}

/*@access rpmDepSet @*/

/*@unchecked@*/
static int _ds_debug = 0;

rpmDepSet dsFree(rpmDepSet ds)
{
    HFD_t hfd = headerFreeData;
    rpmTag tagEVR, tagF;

    if (ds == NULL)
	return NULL;

/*@-modfilesystem@*/
if (_ds_debug)
fprintf(stderr, "*** ds %p --\t%s[%d]\n", ds, ds->Type, ds->Count);
/*@=modfilesystem@*/


    if (ds->tagN == RPMTAG_PROVIDENAME) {
	tagEVR = RPMTAG_PROVIDEVERSION;
	tagF = RPMTAG_PROVIDEFLAGS;
    } else
    if (ds->tagN == RPMTAG_REQUIRENAME) {
	tagEVR = RPMTAG_REQUIREVERSION;
	tagF = RPMTAG_REQUIREFLAGS;
    } else
    if (ds->tagN == RPMTAG_CONFLICTNAME) {
	tagEVR = RPMTAG_CONFLICTVERSION;
	tagF = RPMTAG_CONFLICTFLAGS;
    } else
    if (ds->tagN == RPMTAG_OBSOLETENAME) {
	tagEVR = RPMTAG_OBSOLETEVERSION;
	tagF = RPMTAG_OBSOLETEFLAGS;
    } else
    if (ds->tagN == RPMTAG_TRIGGERNAME) {
	tagEVR = RPMTAG_TRIGGERVERSION;
	tagF = RPMTAG_TRIGGERFLAGS;
    } else
	return NULL;

    /*@-branchstate@*/
    if (ds->Count > 0) {
	ds->N = hfd(ds->N, ds->Nt);
	ds->EVR = hfd(ds->EVR, ds->EVRt);
	/*@-evalorder@*/
	ds->Flags = (ds->h != NULL ? hfd(ds->Flags, ds->Ft) : _free(ds->Flags));
	/*@=evalorder@*/
	ds->h = headerFree(ds->h, "dsFree");
    }
    /*@=branchstate@*/

    ds->DNEVR = _free(ds->DNEVR);

    memset(ds, 0, sizeof(*ds));		/* XXX trash and burn */
    ds = _free(ds);
    return NULL;
}

rpmDepSet dsNew(Header h, rpmTag tagN, int scareMem)
{
    HGE_t hge =
	(scareMem ? (HGE_t) headerGetEntryMinMemory : (HGE_t) headerGetEntry);
    rpmTag tagEVR, tagF;
    rpmDepSet ds = NULL;
    const char * Type;
    const char ** N;
    rpmTagType Nt;
    int_32 Count;

    if (tagN == RPMTAG_PROVIDENAME) {
	Type = "Provides";
	tagEVR = RPMTAG_PROVIDEVERSION;
	tagF = RPMTAG_PROVIDEFLAGS;
    } else
    if (tagN == RPMTAG_REQUIRENAME) {
	Type = "Requires";
	tagEVR = RPMTAG_REQUIREVERSION;
	tagF = RPMTAG_REQUIREFLAGS;
    } else
    if (tagN == RPMTAG_CONFLICTNAME) {
	Type = "Conflicts";
	tagEVR = RPMTAG_CONFLICTVERSION;
	tagF = RPMTAG_CONFLICTFLAGS;
    } else
    if (tagN == RPMTAG_OBSOLETENAME) {
	Type = "Obsoletes";
	tagEVR = RPMTAG_OBSOLETEVERSION;
	tagF = RPMTAG_OBSOLETEFLAGS;
    } else
    if (tagN == RPMTAG_TRIGGERNAME) {
	Type = "Trigger";
	tagEVR = RPMTAG_TRIGGERVERSION;
	tagF = RPMTAG_TRIGGERFLAGS;
    } else
	goto exit;

    /*@-branchstate@*/
    if (hge(h, tagN, &Nt, (void **) &N, &Count)
     && N != NULL && Count > 0)
    {
	int xx;

	ds = xcalloc(1, sizeof(*ds));
	ds->h = (scareMem ? headerLink(h, "dsNew") : NULL);
	ds->i = -1;
	ds->Type = Type;
	ds->DNEVR = NULL;
	ds->tagN = tagN;
	ds->N = N;
	ds->Nt = Nt;
	ds->Count = Count;

	xx = hge(h, tagEVR, &ds->EVRt, (void **) &ds->EVR, NULL);
	xx = hge(h, tagF, &ds->Ft, (void **) &ds->Flags, NULL);
	if (!scareMem && ds->Flags != NULL)
	    ds->Flags = memcpy(xmalloc(ds->Count * sizeof(*ds->Flags)),
                                ds->Flags, ds->Count * sizeof(*ds->Flags));

/*@-modfilesystem@*/
if (_ds_debug)
fprintf(stderr, "*** ds %p ++\t%s[%d]\n", ds, ds->Type, ds->Count);
/*@=modfilesystem@*/

    }
    /*@=branchstate@*/

exit:
    /*@-nullret@*/ /* FIX: ds->Flags may be NULL. */
/*@i@*/ return ds;
    /*@=nullret@*/
}

char * dsDNEVR(const char * dspfx, const rpmDepSet ds)
{
    char * tbuf, * t;
    size_t nb;

    nb = 0;
    if (dspfx)	nb += strlen(dspfx) + 1;
    if (ds->N[ds->i])	nb += strlen(ds->N[ds->i]);
    if (ds->Flags[ds->i] & RPMSENSE_SENSEMASK) {
	if (nb)	nb++;
	if (ds->Flags[ds->i] & RPMSENSE_LESS)	nb++;
	if (ds->Flags[ds->i] & RPMSENSE_GREATER) nb++;
	if (ds->Flags[ds->i] & RPMSENSE_EQUAL)	nb++;
    }
    if (ds->EVR[ds->i] && *ds->EVR[ds->i]) {
	if (nb)	nb++;
	nb += strlen(ds->EVR[ds->i]);
    }

    t = tbuf = xmalloc(nb + 1);
    if (dspfx) {
	t = stpcpy(t, dspfx);
	*t++ = ' ';
    }
    if (ds->N[ds->i])
	t = stpcpy(t, ds->N[ds->i]);
    if (ds->Flags[ds->i] & RPMSENSE_SENSEMASK) {
	if (t != tbuf)	*t++ = ' ';
	if (ds->Flags[ds->i] & RPMSENSE_LESS)	*t++ = '<';
	if (ds->Flags[ds->i] & RPMSENSE_GREATER) *t++ = '>';
	if (ds->Flags[ds->i] & RPMSENSE_EQUAL)	*t++ = '=';
    }
    if (ds->EVR[ds->i] && *ds->EVR[ds->i]) {
	if (t != tbuf)	*t++ = ' ';
	t = stpcpy(t, ds->EVR[ds->i]);
    }
    *t = '\0';
    return tbuf;
}

int dsiGetCount(rpmDepSet ds)
{
    return (ds != NULL ? ds->Count : 0);
}

int dsiGetIx(rpmDepSet ds)
{
    return (ds != NULL ? ds->i : -1);
}

int dsiSetIx(rpmDepSet ds, int ix)
{
    int i = -1;

    if (ds != NULL) {
	i = ds->i;
	ds->i = ix;
    }
    return i;
}

const char * dsiGetDNEVR(rpmDepSet ds)
{
    const char * DNEVR = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->DNEVR != NULL)
	    DNEVR = ds->DNEVR;
    }
    return DNEVR;
}

const char * dsiGetN(rpmDepSet ds)
{
    const char * N = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->N != NULL)
	    N = ds->N[ds->i];
    }
    return N;
}

const char * dsiGetEVR(rpmDepSet ds)
{
    const char * EVR = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->EVR != NULL)
	    EVR = ds->EVR[ds->i];
    }
    return EVR;
}

int_32 dsiGetFlags(rpmDepSet ds)
{
    int_32 Flags = 0;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	if (ds->Flags != NULL)
	    Flags = ds->Flags[ds->i];
    }
    return Flags;
}

void dsiNotify(rpmDepSet ds, const char * where, int rc)
{
    if (!(ds != NULL && ds->i >= 0 && ds->i < ds->Count))
	return;
    if (!(ds->Type != NULL && ds->DNEVR != NULL))
	return;

    rpmMessage(RPMMESS_DEBUG, "%9s: %-45s %-s %s\n", ds->Type,
		(!strcmp(ds->DNEVR, "cached") ? ds->DNEVR : ds->DNEVR+2),
		(rc ? _("NO ") : _("YES")),
		(where != NULL ? where : ""));
}

int dsiNext(/*@null@*/ rpmDepSet ds)
	/*@modifies ds @*/
{
    int i = -1;

    if (ds != NULL && ++ds->i >= 0) {
	if (ds->i < ds->Count) {
	    char t[2];
	    i = ds->i;
	    ds->DNEVR = _free(ds->DNEVR);
	    t[0] = ((ds->Type != NULL) ? ds->Type[0] : '\0');
	    t[1] = '\0';
	    /*@-nullstate@*/
	    ds->DNEVR = dsDNEVR(t, ds);
	    /*@=nullstate@*/

	} else
	    ds->i = -1;
    }

/*@-modfilesystem -nullderef -nullpass @*/
if (_ds_debug && i != -1)
fprintf(stderr, "*** ds %p\t%s[%d]: %s\n", ds, (ds && ds->Type ? ds->Type : "?Type?"), i, (ds->DNEVR ? ds->DNEVR : "?DNEVR?"));
/*@=modfilesystem =nullderef =nullpass @*/

    return i;
}

rpmDepSet dsiInit(/*@returned@*/ /*@null@*/ rpmDepSet ds)
	/*@modifies ds @*/
{
    if (ds != NULL)
	ds->i = -1;
    return ds;
}

/**
 * Split EVR into epoch, version, and release components.
 * @param evr		[epoch:]version[-release] string
 * @retval *ep		pointer to epoch
 * @retval *vp		pointer to version
 * @retval *rp		pointer to release
 */
static
void parseEVR(char * evr,
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

int dsCompare(const rpmDepSet A, const rpmDepSet B)
{
    const char *aDepend = (A->DNEVR != NULL ? xstrdup(A->DNEVR+2) : "");
    const char *bDepend = (B->DNEVR != NULL ? xstrdup(B->DNEVR+2) : "");
    char *aEVR, *bEVR;
    const char *aE, *aV, *aR, *bE, *bV, *bR;
    int result;
    int sense;

    /* Different names don't overlap. */
    if (strcmp(A->N[A->i], B->N[B->i])) {
	result = 0;
	goto exit;
    }

    /* Same name. If either A or B is an existence test, always overlap. */
    if (!((A->Flags[A->i] & RPMSENSE_SENSEMASK) && (B->Flags[B->i] & RPMSENSE_SENSEMASK))) {
	result = 1;
	goto exit;
    }

    /* If either EVR is non-existent or empty, always overlap. */
    if (!(A->EVR[A->i] && *A->EVR[A->i] && B->EVR[B->i] && *B->EVR[B->i])) {
	result = 1;
	goto exit;
    }

    /* Both AEVR and BEVR exist. */
    aEVR = xstrdup(A->EVR[A->i]);
    parseEVR(aEVR, &aE, &aV, &aR);
    bEVR = xstrdup(B->EVR[B->i]);
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
    if (sense < 0 && ((A->Flags[A->i] & RPMSENSE_GREATER) || (B->Flags[B->i] & RPMSENSE_LESS))) {
	result = 1;
    } else if (sense > 0 && ((A->Flags[A->i] & RPMSENSE_LESS) || (B->Flags[B->i] & RPMSENSE_GREATER))) {
	result = 1;
    } else if (sense == 0 &&
	(((A->Flags[A->i] & RPMSENSE_EQUAL) && (B->Flags[B->i] & RPMSENSE_EQUAL)) ||
	 ((A->Flags[A->i] & RPMSENSE_LESS) && (B->Flags[B->i] & RPMSENSE_LESS)) ||
	 ((A->Flags[A->i] & RPMSENSE_GREATER) && (B->Flags[B->i] & RPMSENSE_GREATER)))) {
	result = 1;
    }

exit:
    rpmMessage(RPMMESS_DEBUG, _("  %s    A %s\tB %s\n"),
	(result ? _("YES") : _("NO ")), aDepend, bDepend);
    aDepend = _free(aDepend);
    bDepend = _free(bDepend);
    return result;
}

void dsProblem(rpmProblemSet tsprobs, Header h, const rpmDepSet ds,
		const fnpyKey * suggestedKeys)
{
    const char * Name =  dsiGetN(ds);
    const char * DNEVR = dsiGetDNEVR(ds);
    const char * EVR = dsiGetEVR(ds);
    char * pkgNEVR = hGetNEVR(h, NULL);
    rpmProblemType type;
    fnpyKey key;

    if (tsprobs == NULL) return;

    /*@-branchstate@*/
    if (Name == NULL) Name = "?N?";
    if (EVR == NULL) EVR = "?EVR?";
    if (DNEVR == NULL) DNEVR = "? ?N? ?OP? ?EVR?";
    /*@=branchstate@*/

    rpmMessage(RPMMESS_DEBUG, _("package %s has unsatisfied %s: %s\n"),
	    pkgNEVR, ds->Type, DNEVR+2);

    type = (DNEVR[0] == 'C' && DNEVR[1] == ' ')
		? RPMPROB_CONFLICT : RPMPROB_REQUIRES;
    key = (suggestedKeys ? suggestedKeys[0] : NULL);
    rpmProblemSetAppend(tsprobs, type, pkgNEVR, key,
			NULL, NULL, DNEVR, 0);
    pkgNEVR = _free(pkgNEVR);
}

int rangeMatchesDepFlags (Header h, const rpmDepSet req)
{
    int scareMem = 1;
    rpmDepSet provides = NULL;
    int result = 0;

    if (!(req->Flags[req->i] & RPMSENSE_SENSEMASK) || !req->EVR[req->i] || *req->EVR[req->i] == '\0')
	return 1;

    /* Get provides information from header */
    provides = dsiInit(dsNew(h, RPMTAG_PROVIDENAME, scareMem));
    if (provides == NULL)
	goto exit;	/* XXX should never happen */

    /*
     * Rpm prior to 3.0.3 did not have versioned provides.
     * If no provides version info is available, match any requires.
     */
    if (provides->EVR == NULL) {
	result = 1;
	goto exit;
    }

    result = 0;
    if (provides != NULL)
    while (dsiNext(provides) >= 0) {

	/* Filter out provides that came along for the ride. */
	if (strcmp(provides->N[provides->i], req->N[req->i]))
	    continue;

	result = dsCompare(provides, req);

	/* If this provide matches the require, we're done. */
	if (result)
	    break;
    }

exit:
    provides = dsFree(provides);

    return result;
}

int headerMatchesDepFlags(Header h, const rpmDepSet req)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const char *name, *version, *release;
    int_32 * epoch;
    const char * pkgEVR;
    char * p;
    int_32 pkgFlags = RPMSENSE_EQUAL;
    rpmDepSet pkg = memset(alloca(sizeof(*pkg)), 0, sizeof(*pkg));
    int rc;

    if (!((req->Flags[req->i] & RPMSENSE_SENSEMASK) && req->EVR[req->i] && *req->EVR[req->i]))
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

    /*@-compmempass@*/ /* FIX: move pkg immediate variables from stack */
    pkg->i = -1;
    pkg->Type = "Provides";
    pkg->tagN = RPMTAG_PROVIDENAME;
    pkg->DNEVR = NULL;
    /*@-immediatetrans@*/
    pkg->N = &name;
    pkg->EVR = &pkgEVR;
    pkg->Flags = &pkgFlags;
    /*@=immediatetrans@*/
    pkg->Count = 1;
    (void) dsiNext(dsiInit(pkg));

    rc = dsCompare(pkg, req);

    pkg->DNEVR = _free(pkg->DNEVR);
    /*@=compmempass@*/

    return rc;
}
