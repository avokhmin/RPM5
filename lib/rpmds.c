/** \ingroup rpmdep
 * \file lib/rpmds.c
 */
#include "system.h"

#include <rpmlib.h>
#include "rpmps.h"

#define	_RPMDS_INTERNAL
#include "rpmds.h"

#include "debug.h"

/**
 * Enable noisy range comparison debugging message?
 */
/*@unchecked@*/
static int _noisy_range_comparison_debug_message = 0;

/*@unchecked@*/
int _rpmds_debug = 0;

rpmds XrpmdsUnlink(rpmds ds, const char * msg, const char * fn, unsigned ln)
{
    if (ds == NULL) return NULL;
/*@-modfilesystem@*/
if (_rpmds_debug && msg != NULL)
fprintf(stderr, "--> ds %p -- %d %s at %s:%u\n", ds, ds->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    ds->nrefs--;
    return NULL;
}

rpmds XrpmdsLink(rpmds ds, const char * msg, const char * fn, unsigned ln)
{
    if (ds == NULL) return NULL;
    ds->nrefs++;

/*@-modfilesystem@*/
if (_rpmds_debug && msg != NULL)
fprintf(stderr, "--> ds %p ++ %d %s at %s:%u\n", ds, ds->nrefs, msg, fn, ln);
/*@=modfilesystem@*/

    /*@-refcounttrans@*/ return ds; /*@=refcounttrans@*/
}

rpmds rpmdsFree(rpmds ds)
{
    HFD_t hfd = headerFreeData;
    rpmTag tagEVR, tagF;

    if (ds == NULL)
	return NULL;

    if (ds->nrefs > 1)
	return rpmdsUnlink(ds, ds->Type);

/*@-modfilesystem@*/
if (_rpmds_debug < 0)
fprintf(stderr, "*** ds %p\t%s[%d]\n", ds, ds->Type, ds->Count);
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
	ds->h = headerFree(ds->h, ds->Type);
    }
    /*@=branchstate@*/

    ds->DNEVR = _free(ds->DNEVR);

    (void) rpmdsUnlink(ds, ds->Type);
    /*@-refcounttrans -usereleased@*/
/*@-boundswrite@*/
    memset(ds, 0, sizeof(*ds));		/* XXX trash and burn */
/*@=boundswrite@*/
    ds = _free(ds);
    /*@=refcounttrans =usereleased@*/
    return NULL;
}

rpmds rpmdsNew(Header h, rpmTag tagN, int scareMem)
{
    HGE_t hge =
	(scareMem ? (HGE_t) headerGetEntryMinMemory : (HGE_t) headerGetEntry);
    rpmTag tagEVR, tagF;
    rpmds ds = NULL;
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
	ds->Type = Type;
	ds->h = (scareMem ? headerLink(h, ds->Type) : NULL);
	ds->i = -1;
	ds->DNEVR = NULL;
	ds->tagN = tagN;
	ds->N = N;
	ds->Nt = Nt;
	ds->Count = Count;

	xx = hge(h, tagEVR, &ds->EVRt, (void **) &ds->EVR, NULL);
	xx = hge(h, tagF, &ds->Ft, (void **) &ds->Flags, NULL);
/*@-boundsread@*/
	if (!scareMem && ds->Flags != NULL)
	    ds->Flags = memcpy(xmalloc(ds->Count * sizeof(*ds->Flags)),
                                ds->Flags, ds->Count * sizeof(*ds->Flags));
/*@=boundsread@*/

/*@-modfilesystem@*/
if (_rpmds_debug < 0)
fprintf(stderr, "*** ds %p\t%s[%d]\n", ds, ds->Type, ds->Count);
/*@=modfilesystem@*/

    }
    /*@=branchstate@*/

exit:
    /*@-nullstate@*/ /* FIX: ds->Flags may be NULL */
    return rpmdsLink(ds, (ds ? ds->Type : NULL));
    /*@=nullstate@*/
}

char * rpmdsNewDNEVR(const char * dspfx, const rpmds ds)
{
    char * tbuf, * t;
    size_t nb;

    nb = 0;
    if (dspfx)	nb += strlen(dspfx) + 1;
/*@-boundsread@*/
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
/*@=boundsread@*/

/*@-boundswrite@*/
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
/*@=boundswrite@*/
    return tbuf;
}

rpmds rpmdsThis(Header h, rpmTag tagN, int_32 Flags)
{
    HGE_t hge = (HGE_t) headerGetEntryMinMemory;
    rpmds ds = NULL;
    const char * Type;
    const char * n, * v, * r;
    int_32 * ep;
    const char ** N, ** EVR;
    char * t;
    int xx;

    if (tagN == RPMTAG_PROVIDENAME) {
	Type = "Provides";
    } else
    if (tagN == RPMTAG_REQUIRENAME) {
	Type = "Requires";
    } else
    if (tagN == RPMTAG_CONFLICTNAME) {
	Type = "Conflicts";
    } else
    if (tagN == RPMTAG_OBSOLETENAME) {
	Type = "Obsoletes";
    } else
    if (tagN == RPMTAG_TRIGGERNAME) {
	Type = "Trigger";
    } else
	goto exit;

    xx = headerNVR(h, &n, &v, &r);
    ep = NULL;
    xx = hge(h, RPMTAG_EPOCH, NULL, (void **)&ep, NULL);

    t = xmalloc(sizeof(*N) + strlen(n) + 1);
/*@-boundswrite@*/
    N = (const char **) t;
    t += sizeof(*N);
    N[0] = t;
    t = stpcpy(t, n);

    t = xmalloc(sizeof(*EVR) +
		(ep ? 20 : 0) + strlen(v) + strlen(r) + sizeof("-"));
    EVR = (const char **) t;
    t += sizeof(*EVR);
    EVR[0] = t;
    if (ep) {
	sprintf(t, "%d:", *ep);
	t += strlen(t);
    }
    t = stpcpy( stpcpy( stpcpy( t, v), "-"), r);
/*@=boundswrite@*/

    ds = xcalloc(1, sizeof(*ds));
    ds->h = NULL;
    ds->Type = Type;
    ds->tagN = tagN;
    ds->Count = 1;
    ds->N = N;
    ds->Nt = -1;	/* XXX to insure that hfd will free */
    ds->EVR = EVR;
    ds->EVRt = -1;	/* XXX to insure that hfd will free */
/*@-boundswrite@*/
    ds->Flags = xmalloc(sizeof(*ds->Flags));	ds->Flags[0] = Flags;
/*@=boundswrite@*/
    ds->i = 0;
    {	char pre[2];
/*@-boundsread@*/
	pre[0] = ds->Type[0];
/*@=boundsread@*/
	pre[1] = '\0';
	/*@-nullstate@*/ /* LCL: ds->Type may be NULL ??? */
	ds->DNEVR = rpmdsNewDNEVR(pre, ds);
	/*@=nullstate@*/
    }

exit:
    return rpmdsLink(ds, (ds ? ds->Type : NULL));
}

rpmds rpmdsSingle(rpmTag tagN, const char * N, const char * EVR, int_32 Flags)
{
    rpmds ds = NULL;
    const char * Type;

    if (tagN == RPMTAG_PROVIDENAME) {
	Type = "Provides";
    } else
    if (tagN == RPMTAG_REQUIRENAME) {
	Type = "Requires";
    } else
    if (tagN == RPMTAG_CONFLICTNAME) {
	Type = "Conflicts";
    } else
    if (tagN == RPMTAG_OBSOLETENAME) {
	Type = "Obsoletes";
    } else
    if (tagN == RPMTAG_TRIGGERNAME) {
	Type = "Trigger";
    } else
	goto exit;

    ds = xcalloc(1, sizeof(*ds));
    ds->h = NULL;
    ds->Type = Type;
    ds->tagN = tagN;
    ds->Count = 1;
    /*@-assignexpose@*/
/*@-boundswrite@*/
    ds->N = xmalloc(sizeof(*ds->N));		ds->N[0] = N;
    ds->Nt = -1;	/* XXX to insure that hfd will free */
    ds->EVR = xmalloc(sizeof(*ds->EVR));	ds->EVR[0] = EVR;
    ds->EVRt = -1;	/* XXX to insure that hfd will free */
    /*@=assignexpose@*/
    ds->Flags = xmalloc(sizeof(*ds->Flags));	ds->Flags[0] = Flags;
/*@=boundswrite@*/
    ds->i = 0;
    {	char t[2];
/*@-boundsread@*/
	t[0] = ds->Type[0];
/*@=boundsread@*/
	t[1] = '\0';
	ds->DNEVR = rpmdsNewDNEVR(t, ds);
    }

exit:
    return rpmdsLink(ds, (ds ? ds->Type : NULL));
}

int rpmdsCount(const rpmds ds)
{
    return (ds != NULL ? ds->Count : 0);
}

int rpmdsIx(const rpmds ds)
{
    return (ds != NULL ? ds->i : -1);
}

int rpmdsSetIx(rpmds ds, int ix)
{
    int i = -1;

    if (ds != NULL) {
	i = ds->i;
	ds->i = ix;
    }
    return i;
}

const char * rpmdsDNEVR(const rpmds ds)
{
    const char * DNEVR = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
/*@-boundsread@*/
	if (ds->DNEVR != NULL)
	    DNEVR = ds->DNEVR;
/*@=boundsread@*/
    }
    return DNEVR;
}

const char * rpmdsN(const rpmds ds)
{
    const char * N = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
/*@-boundsread@*/
	if (ds->N != NULL)
	    N = ds->N[ds->i];
/*@=boundsread@*/
    }
    return N;
}

const char * rpmdsEVR(const rpmds ds)
{
    const char * EVR = NULL;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
/*@-boundsread@*/
	if (ds->EVR != NULL)
	    EVR = ds->EVR[ds->i];
/*@=boundsread@*/
    }
    return EVR;
}

int_32 rpmdsFlags(const rpmds ds)
{
    int_32 Flags = 0;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
/*@-boundsread@*/
	if (ds->Flags != NULL)
	    Flags = ds->Flags[ds->i];
/*@=boundsread@*/
    }
    return Flags;
}

rpmTag rpmdsTagN(const rpmds ds)
{
    rpmTag tagN = 0;

    if (ds != NULL && ds->i >= 0 && ds->i < ds->Count) {
	tagN = ds->tagN;
    }
    return tagN;
}

void rpmdsNotify(rpmds ds, const char * where, int rc)
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

int rpmdsNext(/*@null@*/ rpmds ds)
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
	    ds->DNEVR = rpmdsNewDNEVR(t, ds);
	    /*@=nullstate@*/

	} else
	    ds->i = -1;

/*@-modfilesystem @*/
if (_rpmds_debug  < 0 && i != -1)
fprintf(stderr, "*** ds %p\t%s[%d]: %s\n", ds, (ds->Type ? ds->Type : "?Type?"), i, (ds->DNEVR ? ds->DNEVR : "?DNEVR?"));
/*@=modfilesystem @*/

    }

    return i;
}

rpmds rpmdsInit(/*@null@*/ rpmds ds)
	/*@modifies ds @*/
{
    if (ds != NULL)
	ds->i = -1;
    /*@-refcounttrans@*/
    return ds;
    /*@=refcounttrans@*/
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
/*@-boundsread@*/
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
/*@=boundsread@*/
    if (se) {
/*@-boundswrite@*/
	*se++ = '\0';
/*@=boundswrite@*/
	release = se;
    } else {
	release = NULL;
    }

/*@-boundswrite@*/
    if (ep) *ep = epoch;
    if (vp) *vp = version;
    if (rp) *rp = release;
/*@=boundswrite@*/
}

int rpmdsCompare(const rpmds A, const rpmds B)
{
    const char *aDepend = (A->DNEVR != NULL ? xstrdup(A->DNEVR+2) : "");
    const char *bDepend = (B->DNEVR != NULL ? xstrdup(B->DNEVR+2) : "");
    char *aEVR, *bEVR;
    const char *aE, *aV, *aR, *bE, *bV, *bR;
    int result;
    int sense;

/*@-boundsread@*/
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
/*@=boundsread@*/
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
    if (_noisy_range_comparison_debug_message)
    rpmMessage(RPMMESS_DEBUG, _("  %s    A %s\tB %s\n"),
	(result ? _("YES") : _("NO ")), aDepend, bDepend);
    aDepend = _free(aDepend);
    bDepend = _free(bDepend);
    return result;
}

void rpmdsProblem(rpmps ps, const char * pkgNEVR, const rpmds ds,
	const fnpyKey * suggestedKeys, int adding)
{
    const char * Name =  rpmdsN(ds);
    const char * DNEVR = rpmdsDNEVR(ds);
    const char * EVR = rpmdsEVR(ds);
    rpmProblemType type;
    fnpyKey key;

    if (ps == NULL) return;

    /*@-branchstate@*/
    if (Name == NULL) Name = "?N?";
    if (EVR == NULL) EVR = "?EVR?";
    if (DNEVR == NULL) DNEVR = "? ?N? ?OP? ?EVR?";
    /*@=branchstate@*/

    rpmMessage(RPMMESS_DEBUG, _("package %s has unsatisfied %s: %s\n"),
	    pkgNEVR, ds->Type, DNEVR+2);

    switch ((unsigned)DNEVR[0]) {
    case 'C':	type = RPMPROB_CONFLICT;	break;
    default:
    case 'R':	type = RPMPROB_REQUIRES;	break;
    }

    key = (suggestedKeys ? suggestedKeys[0] : NULL);
    rpmpsAppend(ps, type, pkgNEVR, key, NULL, NULL, DNEVR, adding);
}

int rangeMatchesDepFlags (Header h, const rpmds req)
{
    int scareMem = 1;
    rpmds provides = NULL;
    int result = 0;

/*@-boundsread@*/
    if (!(req->Flags[req->i] & RPMSENSE_SENSEMASK) || !req->EVR[req->i] || *req->EVR[req->i] == '\0')
	return 1;
/*@=boundsread@*/

    /* Get provides information from header */
    provides = rpmdsInit(rpmdsNew(h, RPMTAG_PROVIDENAME, scareMem));
    if (provides == NULL)
	goto exit;	/* XXX should never happen */

    /*
     * Rpm prior to 3.0.3 did not have versioned provides.
     * If no provides version info is available, match any/all requires
     * with same name.
     */
    if (provides->EVR == NULL) {
	result = 1;
	goto exit;
    }

    result = 0;
    if (provides != NULL)
    while (rpmdsNext(provides) >= 0) {

	/* Filter out provides that came along for the ride. */
/*@-boundsread@*/
	if (strcmp(provides->N[provides->i], req->N[req->i]))
	    continue;
/*@=boundsread@*/

	result = rpmdsCompare(provides, req);

	/* If this provide matches the require, we're done. */
	if (result)
	    break;
    }

exit:
    provides = rpmdsFree(provides);

    return result;
}

int headerMatchesDepFlags(const Header h, const rpmds req)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const char * pkgN, * v, * r;
    int_32 * epoch;
    const char * pkgEVR;
    char * t;
    int_32 pkgFlags = RPMSENSE_EQUAL;
    rpmds pkg;
    int rc = 1;	/* XXX assume match, names already match here */

/*@-boundsread@*/
    if (!((req->Flags[req->i] & RPMSENSE_SENSEMASK) && req->EVR[req->i] && *req->EVR[req->i]))
	return rc;
/*@=boundsread@*/

    /* Get package information from header */
    (void) headerNVR(h, &pkgN, &v, &r);

/*@-boundswrite@*/
    t = alloca(21 + strlen(v) + 1 + strlen(r) + 1);
    pkgEVR = t;
    *t = '\0';
    if (hge(h, RPMTAG_EPOCH, NULL, (void **) &epoch, NULL)) {
	sprintf(t, "%d:", *epoch);
	while (*t != '\0')
	    t++;
    }
    (void) stpcpy( stpcpy( stpcpy(t, v) , "-") , r);
/*@=boundswrite@*/

    if ((pkg = rpmdsSingle(RPMTAG_PROVIDENAME, pkgN, pkgEVR, pkgFlags)) != NULL) {
	rc = rpmdsCompare(pkg, req);
	pkg = rpmdsFree(pkg);
    }

    return rc;
}
