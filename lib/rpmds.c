/** \ingroup rpmdep
 * \file lib/rpmds.c
 */
#include "system.h"

#ifdef	DYING
#include <rpmlib.h>
#include "rpmds.h"
#else
#include "psm.h"		/* XXX rpmTransactionType */
#endif

#include "debug.h"

/*@access alKey@*/

/*@access rpmFNSet @*/
/*@access transactionElement @*/

/*@unchecked@*/
static int _fi_debug = -1;

/**
 * Wrapper to free(3), hides const compilation noise, permit NULL, return NULL.
 * @param p		memory to free
 * @return		NULL always
 */
/*@unused@*/ static /*@null@*/
void * _xfree(/*@only@*/ /*@null@*/ /*@out@*/ const void * p)
	/*@modifies p @*/
{
    if (p != NULL)	free((void *)p);
    return NULL;
}

rpmFNSet XrpmfnsUnlink(rpmFNSet fns, const char * msg, const char * fn, unsigned ln)
{
    if (fns == NULL) return NULL;
/*@-modfilesystem@*/
if (_fi_debug && msg != NULL)
fprintf(stderr, "--> fi %p -- %d %s at %s:%u\n", fns, fns->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    fns->nrefs--;
    return NULL;
}

rpmFNSet XrpmfnsLink(rpmFNSet fns, const char * msg, const char * fn, unsigned ln)
{
    if (fns == NULL) return NULL;
    fns->nrefs++;
/*@-modfilesystem@*/
if (_fi_debug && msg != NULL)
fprintf(stderr, "--> fi %p ++ %d %s at %s:%u\n", fns, fns->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    /*@-refcounttrans@*/ return fns; /*@=refcounttrans@*/
}

rpmFNSet fiFree(rpmFNSet fi, int freefimem)
{
    HFD_t hfd = headerFreeData;

    if (fi == NULL) return NULL;

    if (fi->nrefs > 1)
	return rpmfnsUnlink(fi, fi->Type);

/*@-modfilesystem@*/
if (_fi_debug < 0)
fprintf(stderr, "*** fi %p\t%s[%d]\n", fi, fi->Type, fi->fc);
/*@=modfilesystem@*/

    /*@-branchstate@*/
    if (fi->fc > 0) {
	fi->bnl = hfd(fi->bnl, -1);
	fi->dnl = hfd(fi->dnl, -1);

	fi->flinks = hfd(fi->flinks, -1);
	fi->flangs = hfd(fi->flangs, -1);
	fi->fmd5s = hfd(fi->fmd5s, -1);

	fi->fuser = hfd(fi->fuser, -1);
	fi->fuids = _xfree(fi->fuids);
	fi->fgroup = hfd(fi->fgroup, -1);
	fi->fgids = _xfree(fi->fgids);

	fi->fstates = _xfree(fi->fstates);

	/*@-evalorder@*/
	if (!fi->keep_header && fi->h == NULL) {
	    fi->fmtimes = _xfree(fi->fmtimes);
	    fi->fmodes = _xfree(fi->fmodes);
	    fi->fflags = _xfree(fi->fflags);
	    fi->fsizes = _xfree(fi->fsizes);
	    fi->frdevs = _xfree(fi->frdevs);
	    fi->dil = _xfree(fi->dil);
	}
	/*@=evalorder@*/
    }
    /*@=branchstate@*/

    fi->fsm = freeFSM(fi->fsm);

    fi->apath = _xfree(fi->apath);
    fi->fmapflags = _xfree(fi->fmapflags);

    fi->obnl = hfd(fi->obnl, -1);
    fi->odnl = hfd(fi->odnl, -1);

    fi->actions = _xfree(fi->actions);
    fi->replacedSizes = _xfree(fi->replacedSizes);
    fi->replaced = _xfree(fi->replaced);

    fi->h = headerFree(fi->h, fi->Type);

    /*@-nullstate -refcounttrans -usereleased@*/
    (void) rpmfnsUnlink(fi, fi->Type);
    memset(fi, 0, sizeof(*fi));		/* XXX trash and burn */
    /*@-branchstate@*/
    if (freefimem)
	fi = _xfree(fi);
    /*@=branchstate@*/
    /*@=nullstate =refcounttrans =usereleased@*/

    return NULL;
}

#define	_fdupe(_fi, _data)	\
    if ((_fi)->_data != NULL)	\
	(_fi)->_data = memcpy(xmalloc((_fi)->fc * sizeof(*(_fi)->_data)), \
			(_fi)->_data, (_fi)->fc * sizeof(*(_fi)->_data))

rpmFNSet fiNew(rpmTransactionSet ts, rpmFNSet fi,
		Header h, rpmTag tagN, int scareMem)
{
    HGE_t hge =
	(scareMem ? (HGE_t) headerGetEntryMinMemory : (HGE_t) headerGetEntry);
    const char * Type;
    uint_32 * uip;
    int malloced = 0;
    int len;
    int xx;
    int i;

    if (tagN == RPMTAG_BASENAMES) {
	Type = "Files";
    } else {
	Type = "?Type?";
	goto exit;
    }

    /*@-branchstate@*/
    if (fi == NULL) {
	fi = xcalloc(1, sizeof(*fi));
	malloced = 0;	/* XXX always return with memory alloced. */
    }
    /*@=branchstate@*/

    fi->magic = TFIMAGIC;
    fi->Type = Type;
    fi->i = -1;
    fi->tagN = tagN;

    fi->hge = hge;
    fi->hae = (HAE_t) headerAddEntry;
    fi->hme = (HME_t) headerModifyEntry;
    fi->hre = (HRE_t) headerRemoveEntry;
    fi->hfd = headerFreeData;

    fi->h = (scareMem ? headerLink(h, fi->Type) : NULL);

    if (fi->fsm == NULL)
	fi->fsm = newFSM();

    /* 0 means unknown */
    xx = hge(h, RPMTAG_ARCHIVESIZE, NULL, (void **) &uip, NULL);
    fi->archiveSize = (xx ? *uip : 0);

    if (!hge(h, RPMTAG_BASENAMES, NULL, (void **) &fi->bnl, &fi->fc)) {
	/*@-branchstate@*/
	if (malloced) {
	    if (scareMem && fi->h)
		fi->h = headerFree(fi->h, fi->Type);
	    fi->fsm = freeFSM(fi->fsm);
	    /*@-refcounttrans@*/
	    fi = _xfree(fi);
	    /*@=refcounttrans@*/
	} else {
	    fi->fc = 0;
	    fi->dc = 0;
	}
	/*@=branchstate@*/
	goto exit;
    }
    xx = hge(h, RPMTAG_DIRNAMES, NULL, (void **) &fi->dnl, &fi->dc);
    xx = hge(h, RPMTAG_DIRINDEXES, NULL, (void **) &fi->dil, NULL);
    xx = hge(h, RPMTAG_FILEMODES, NULL, (void **) &fi->fmodes, NULL);
    xx = hge(h, RPMTAG_FILEFLAGS, NULL, (void **) &fi->fflags, NULL);
    xx = hge(h, RPMTAG_FILESIZES, NULL, (void **) &fi->fsizes, NULL);
    xx = hge(h, RPMTAG_FILESTATES, NULL, (void **) &fi->fstates, NULL);
    if (xx == 0 || fi->fstates == NULL)
	fi->fstates = xcalloc(fi->fc, sizeof(*fi->fstates));
    else if (!scareMem)
	_fdupe(fi, fstates);

    fi->action = FA_UNKNOWN;
    fi->flags = 0;
    if (fi->actions == NULL)
	fi->actions = xcalloc(fi->fc, sizeof(*fi->actions));
    fi->keep_header = (scareMem ? 1 : 0);

    /* XXX TR_REMOVED needs CPIO_MAP_{ABSOLUTE,ADDDOT} CPIO_ALL_HARDLINKS */
    fi->mapflags =
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;

    xx = hge(h, RPMTAG_FILELINKTOS, NULL, (void **) &fi->flinks, NULL);
    xx = hge(h, RPMTAG_FILELANGS, NULL, (void **) &fi->flangs, NULL);

    xx = hge(h, RPMTAG_FILEMD5S, NULL, (void **) &fi->fmd5s, NULL);

    /* XXX TR_REMOVED doesn;t need fmtimes or frdevs */
    xx = hge(h, RPMTAG_FILEMTIMES, NULL, (void **) &fi->fmtimes, NULL);
    xx = hge(h, RPMTAG_FILERDEVS, NULL, (void **) &fi->frdevs, NULL);
    fi->replacedSizes = xcalloc(fi->fc, sizeof(*fi->replacedSizes));

    xx = hge(h, RPMTAG_FILEUSERNAME, NULL, (void **) &fi->fuser, NULL);
    fi->fuids = NULL;
    xx = hge(h, RPMTAG_FILEGROUPNAME, NULL, (void **) &fi->fgroup, NULL);
    fi->fgids = NULL;

    if (ts != NULL)
    if (fi != NULL)
    if (fi->te != NULL && fi->te->type == TR_ADDED) {
	Header foo;
	fi->actions = xcalloc(fi->fc, sizeof(*fi->actions));
	/*@-type@*/
	foo = relocateFileList(ts, fi, h, fi->actions);
	/*@=type@*/
	fi->h = headerFree(fi->h, "fiNew fi->h");
	fi->h = headerLink(foo, "fiNew fi->h = foo");
	foo = headerFree(foo, "fiNew foo");
    }

    if (!scareMem) {
	_fdupe(fi, fmtimes);
	_fdupe(fi, frdevs);
	_fdupe(fi, fsizes);
	_fdupe(fi, fflags);
	_fdupe(fi, fmodes);
	_fdupe(fi, dil);
	fi->h = headerFree(fi->h, fi->Type);
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

exit:
/*@-modfilesystem@*/
if (_fi_debug < 0)
fprintf(stderr, "*** fi %p\t%s[%d]\n", fi, Type, (fi ? fi->fc : 0));
/*@=modfilesystem@*/

    /*@-nullstate@*/ /* FIX: TFI/rpmFNSet null annotations */
    return rpmfnsLink(fi, (fi ? fi->Type : NULL));
    /*@=nullstate@*/
}

/*@access rpmDepSet @*/

/*@unchecked@*/
static int _ds_debug = 0;

rpmDepSet XrpmdsUnlink(rpmDepSet ds, const char * msg, const char * fn, unsigned ln)
{
    if (ds == NULL) return NULL;
/*@-modfilesystem@*/
if (_ds_debug && msg != NULL)
fprintf(stderr, "--> ds %p -- %d %s at %s:%u\n", ds, ds->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    ds->nrefs--;
    return NULL;
}

rpmDepSet XrpmdsLink(rpmDepSet ds, const char * msg, const char * fn, unsigned ln)
{
    if (ds == NULL) return NULL;
    ds->nrefs++;
/*@-modfilesystem@*/
if (_ds_debug && msg != NULL)
fprintf(stderr, "--> ds %p ++ %d %s at %s:%u\n", ds, ds->nrefs, msg, fn, ln);
/*@=modfilesystem@*/
    /*@-refcounttrans@*/ return ds; /*@=refcounttrans@*/
}

rpmDepSet dsFree(rpmDepSet ds)
{
    HFD_t hfd = headerFreeData;
    rpmTag tagEVR, tagF;

    if (ds == NULL)
	return NULL;

    if (ds->nrefs > 1)
	return rpmdsUnlink(ds, ds->Type);

/*@-modfilesystem@*/
if (_ds_debug < 0)
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
    memset(ds, 0, sizeof(*ds));		/* XXX trash and burn */
    ds = _free(ds);
    /*@=refcounttrans =usereleased@*/
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
	if (!scareMem && ds->Flags != NULL)
	    ds->Flags = memcpy(xmalloc(ds->Count * sizeof(*ds->Flags)),
                                ds->Flags, ds->Count * sizeof(*ds->Flags));

/*@-modfilesystem@*/
if (_ds_debug < 0)
fprintf(stderr, "*** ds %p\t%s[%d]\n", ds, ds->Type, ds->Count);
/*@=modfilesystem@*/

    }
    /*@=branchstate@*/

exit:
    /*@-nullstate@*/ /* FIX: ds->Flags may be NULL */
    return rpmdsLink(ds, (ds ? ds->Type : NULL));
    /*@=nullstate@*/
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

rpmDepSet dsThis(Header h, rpmTag tagN, int_32 Flags)
{
    HGE_t hge = (HGE_t) headerGetEntryMinMemory;
    rpmDepSet ds = NULL;
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

    ds = xcalloc(1, sizeof(*ds));
    ds->h = NULL;
    ds->Type = Type;
    ds->tagN = tagN;
    ds->Count = 1;
    ds->N = N;
    ds->EVR = EVR;
    ds->Flags = xmalloc(sizeof(*ds->Flags));	ds->Flags[0] = Flags;
    ds->i = 0;
    {	char pre[2];
	pre[0] = ds->Type[0];
	pre[1] = '\0';
	/*@-nullstate@*/ /* LCL: ds->Type may be NULL ??? */
	ds->DNEVR = dsDNEVR(pre, ds);
	/*@=nullstate@*/
    }

exit:
    return rpmdsLink(ds, (ds ? ds->Type : NULL));
}

rpmDepSet dsSingle(rpmTag tagN, const char * N, const char * EVR, int_32 Flags)
{
    rpmDepSet ds = NULL;
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
    ds->N = xmalloc(sizeof(*ds->N));		ds->N[0] = N;
    ds->EVR = xmalloc(sizeof(*ds->EVR));	ds->EVR[0] = EVR;
    /*@=assignexpose@*/
    ds->Flags = xmalloc(sizeof(*ds->Flags));	ds->Flags[0] = Flags;
    ds->i = 0;
    {	char t[2];
	t[0] = ds->Type[0];
	t[1] = '\0';
	/*@-nullstate@*/ /* LCL: ds->Type may be NULL ??? */
	ds->DNEVR = dsDNEVR(t, ds);
	/*@=nullstate@*/
    }

exit:
    return rpmdsLink(ds, (ds ? ds->Type : NULL));
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

/*@-modfilesystem @*/
if (_ds_debug  < 0 && i != -1)
fprintf(stderr, "*** ds %p\t%s[%d]: %s\n", ds, (ds->Type ? ds->Type : "?Type?"), i, (ds->DNEVR ? ds->DNEVR : "?DNEVR?"));
/*@=modfilesystem @*/

    }

    return i;
}

rpmDepSet dsiInit(/*@returned@*/ /*@null@*/ rpmDepSet ds)
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

void dsProblem(rpmProblemSet tsprobs, const char * pkgNEVR, const rpmDepSet ds,
		const fnpyKey * suggestedKeys)
{
    const char * Name =  dsiGetN(ds);
    const char * DNEVR = dsiGetDNEVR(ds);
    const char * EVR = dsiGetEVR(ds);
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

int headerMatchesDepFlags(const Header h, const rpmDepSet req)
{
    HGE_t hge = (HGE_t)headerGetEntryMinMemory;
    const char * pkgN, * v, * r;
    int_32 * epoch;
    const char * pkgEVR;
    char * t;
    int_32 pkgFlags = RPMSENSE_EQUAL;
    rpmDepSet pkg;
    int rc = 1;	/* XXX assume match as names should be the same already here */

    if (!((req->Flags[req->i] & RPMSENSE_SENSEMASK) && req->EVR[req->i] && *req->EVR[req->i]))
	return rc;

    /* Get package information from header */
    (void) headerNVR(h, &pkgN, &v, &r);

    pkgEVR = t = alloca(21 + strlen(v) + 1 + strlen(r) + 1);
    *t = '\0';
    if (hge(h, RPMTAG_EPOCH, NULL, (void **) &epoch, NULL)) {
	sprintf(t, "%d:", *epoch);
	while (*t != '\0')
	    t++;
    }
    (void) stpcpy( stpcpy( stpcpy(t, v) , "-") , r);

    if ((pkg = dsSingle(RPMTAG_PROVIDENAME, pkgN, pkgEVR, pkgFlags)) != NULL) {
	rc = dsCompare(pkg, req);
	pkg = dsFree(pkg);
    }

    return rc;
}
