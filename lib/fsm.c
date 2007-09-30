/** \ingroup payload
 * \file lib/fsm.c
 * File state machine to handle a payload from a package.
 */

#include "system.h"

#include <rpmio_internal.h>
#include <rpmlib.h>

#include "cpio.h"
#include "tar.h"

#include "fsm.h"
#define	fsmUNSAFE	fsmStage

#include "rpmerr.h"

#define	_RPMFI_INTERNAL
#include "rpmfi.h"
#include "rpmte.h"
#include "rpmts.h"
#include "rpmsq.h"

#include "ugid.h"		/* XXX unameToUid() and gnameToGid() */

#include "debug.h"

/*@access FD_t @*/	/* XXX void ptr args */
/*@access FSMI_t @*/
/*@access FSM_t @*/

/*@access rpmfi @*/

#define	alloca_strdup(_s)	strcpy(alloca(strlen(_s)+1), (_s))

#define	_FSM_DEBUG	0
/*@unchecked@*/
int _fsm_debug = _FSM_DEBUG;

/*@-exportheadervar@*/
/*@unchecked@*/
int _fsm_threads = 0;
/*@=exportheadervar@*/

/* XXX Failure to remove is not (yet) cause for failure. */
/*@-exportlocal -exportheadervar@*/
/*@unchecked@*/
int strict_erasures = 0;
/*@=exportlocal =exportheadervar@*/

rpmts fsmGetTs(const FSM_t fsm) {
    const FSMI_t iter = fsm->iter;
    /*@-compdef -refcounttrans -retexpose -usereleased @*/
    return (iter ? iter->ts : NULL);
    /*@=compdef =refcounttrans =retexpose =usereleased @*/
}

rpmfi fsmGetFi(const FSM_t fsm)
{
    const FSMI_t iter = fsm->iter;
    /*@-compdef -refcounttrans -retexpose -usereleased @*/
    return (iter ? iter->fi : NULL);
    /*@=compdef =refcounttrans =retexpose =usereleased @*/
}

#define	SUFFIX_RPMORIG	".rpmorig"
#define	SUFFIX_RPMSAVE	".rpmsave"
#define	SUFFIX_RPMNEW	".rpmnew"

/** \ingroup payload
 * Build path to file from file info, ornamented with subdir and suffix.
 * @param fsm		file state machine data
 * @param st		file stat info
 * @param subdir	subdir to use (NULL disables)
 * @param suffix	suffix to use (NULL disables)
 * @retval		path to file
 */
static /*@only@*//*@null@*/
const char * fsmFsPath(/*@special@*/ /*@null@*/ const FSM_t fsm,
		/*@null@*/ const struct stat * st,
		/*@null@*/ const char * subdir,
		/*@null@*/ const char * suffix)
	/*@uses fsm->dirName, fsm->baseName */
	/*@*/
{
    const char * s = NULL;

    if (fsm) {
	char * t;
	int nb;
	nb = strlen(fsm->dirName) +
	    (st && !S_ISDIR(st->st_mode) ? (subdir ? strlen(subdir) : 0) : 0) +
	    (st && !S_ISDIR(st->st_mode) ? (suffix ? strlen(suffix) : 0) : 0) +
	    strlen(fsm->baseName) + 1;
	s = t = xmalloc(nb);
	t = stpcpy(t, fsm->dirName);
	if (st && !S_ISDIR(st->st_mode))
	    if (subdir) t = stpcpy(t, subdir);
	t = stpcpy(t, fsm->baseName);
	if (st && !S_ISDIR(st->st_mode))
	    if (suffix) t = stpcpy(t, suffix);
    }
    return s;
}

/** \ingroup payload
 * Destroy file info iterator.
 * @param p		file info iterator
 * @retval		NULL always
 */
static /*@null@*/ void * mapFreeIterator(/*@only@*//*@null@*/ void * p)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    FSMI_t iter = p;
    if (iter) {
/*@-internalglobs@*/ /* XXX rpmswExit() */
	iter->ts = rpmtsFree(iter->ts);
/*@=internalglobs@*/
	iter->fi = rpmfiUnlink(iter->fi, "mapIterator");
    }
    return _free(p);
}

/** \ingroup payload
 * Create file info iterator.
 * @param ts		transaction set
 * @param fi		transaction element file info
 * @return		file info iterator
 */
static void *
mapInitIterator(rpmts ts, rpmfi fi)
	/*@modifies ts, fi @*/
{
    FSMI_t iter = NULL;

    iter = xcalloc(1, sizeof(*iter));
    iter->ts = rpmtsLink(ts, "mapIterator");
    iter->fi = rpmfiLink(fi, "mapIterator");
    iter->reverse = (rpmteType(fi->te) == TR_REMOVED && fi->action != FA_COPYOUT);
    iter->i = (iter->reverse ? (fi->fc - 1) : 0);
    iter->isave = iter->i;
    return iter;
}

/** \ingroup payload
 * Return next index into file info.
 * @param a		file info iterator
 * @return		next index, -1 on termination
 */
static int mapNextIterator(/*@null@*/ void * a)
	/*@*/
{
    FSMI_t iter = a;
    int i = -1;

    if (iter) {
	const rpmfi fi = iter->fi;
	if (iter->reverse) {
	    if (iter->i >= 0)	i = iter->i--;
	} else {
    	    if (iter->i < fi->fc)	i = iter->i++;
	}
	iter->isave = i;
    }
    return i;
}

/** \ingroup payload
 */
static int cpioStrCmp(const void * a, const void * b)
	/*@*/
{
    const char * aurl = *(const char **)a;
    const char * burl = *(const char **)b;
    const char * afn = NULL;
    const char * bfn = NULL;

    (void) urlPath(aurl, &afn);
    (void) urlPath(burl, &bfn);

    /* XXX Some 4+ year old rpm packages have basename only in payloads. */
#ifdef	VERY_OLD_BUGGY_RPM_PACKAGES
    if (strchr(afn, '/') == NULL)
	bfn = strrchr(bfn, '/') + 1;
#endif

    /* Match rpm-4.0 payloads with ./ prefixes. */
    if (afn[0] == '.' && afn[1] == '/')	afn += 2;
    if (bfn[0] == '.' && bfn[1] == '/')	bfn += 2;

    /* If either path is absolute, make it relative. */
    if (afn[0] == '/')	afn += 1;
    if (bfn[0] == '/')	bfn += 1;

    return strcmp(afn, bfn);
}

/** \ingroup payload
 * Locate archive path in file info.
 * @param iter		file info iterator
 * @param fsmPath	archive path
 * @return		index into file info, -1 if archive path was not found
 */
static int mapFind(/*@null@*/ FSMI_t iter, const char * fsmPath)
	/*@modifies iter @*/
{
    int ix = -1;

    if (iter) {
	const rpmfi fi = iter->fi;
	if (fi && fi->fc > 0 && fi->apath && fsmPath && *fsmPath) {
	    const char ** p = NULL;

	    if (fi->apath != NULL)
		p = bsearch(&fsmPath, fi->apath, fi->fc, sizeof(fsmPath),
			cpioStrCmp);
	    if (p) {
		iter->i = p - fi->apath;
		ix = mapNextIterator(iter);
	    }
	}
    }
    return ix;
}

/** \ingroup payload
 * Directory name iterator.
 */
typedef struct dnli_s {
    rpmfi fi;
/*@only@*/ /*@null@*/
    char * active;
    int reverse;
    int isave;
    int i;
} * DNLI_t;

/** \ingroup payload
 * Destroy directory name iterator.
 * @param a		directory name iterator
 * @retval		NULL always
 */
static /*@null@*/ void * dnlFreeIterator(/*@only@*//*@null@*/ const void * a)
	/*@modifies a @*/
{
    if (a) {
	DNLI_t dnli = (void *)a;
	if (dnli->active) free(dnli->active);
    }
    return _free(a);
}

/** \ingroup payload
 */
static inline int dnlCount(/*@null@*/ const DNLI_t dnli)
	/*@*/
{
    return (dnli ? dnli->fi->dc : 0);
}

/** \ingroup payload
 */
static inline int dnlIndex(/*@null@*/ const DNLI_t dnli)
	/*@*/
{
    return (dnli ? dnli->isave : -1);
}

/** \ingroup payload
 * Create directory name iterator.
 * @param fsm		file state machine data
 * @param reverse	traverse directory names in reverse order?
 * @return		directory name iterator
 */
/*@-usereleased@*/
static /*@only@*/ /*@null@*/
void * dnlInitIterator(/*@special@*/ const FSM_t fsm,
		int reverse)
	/*@uses fsm->iter @*/ 
	/*@*/
{
    rpmfi fi = fsmGetFi(fsm);
    const char * dnl;
    DNLI_t dnli;
    int i, j;

    if (fi == NULL)
	return NULL;
    dnli = xcalloc(1, sizeof(*dnli));
    dnli->fi = fi;
    dnli->reverse = reverse;
    dnli->i = (reverse ? fi->dc : 0);

    if (fi->dc) {
	dnli->active = xcalloc(fi->dc, sizeof(*dnli->active));

	/* Identify parent directories not skipped. */
	for (i = 0; i < fi->fc; i++)
            if (!XFA_SKIPPING(fi->actions[i])) dnli->active[fi->dil[i]] = 1;

	/* Exclude parent directories that are explicitly included. */
	for (i = 0; i < fi->fc; i++) {
	    int dil, dnlen, bnlen;

	    if (!S_ISDIR(fi->fmodes[i]))
		continue;

	    dil = fi->dil[i];
	    dnlen = strlen(fi->dnl[dil]);
	    bnlen = strlen(fi->bnl[i]);

	    for (j = 0; j < fi->dc; j++) {
		int jlen;

		if (!dnli->active[j] || j == dil)
		    /*@innercontinue@*/ continue;
		(void) urlPath(fi->dnl[j], &dnl);
		jlen = strlen(dnl);
		if (jlen != (dnlen+bnlen+1))
		    /*@innercontinue@*/ continue;
		if (strncmp(dnl, fi->dnl[dil], dnlen))
		    /*@innercontinue@*/ continue;
		if (strncmp(dnl+dnlen, fi->bnl[i], bnlen))
		    /*@innercontinue@*/ continue;
		if (dnl[dnlen+bnlen] != '/' || dnl[dnlen+bnlen+1] != '\0')
		    /*@innercontinue@*/ continue;
		/* This directory is included in the package. */
		dnli->active[j] = 0;
		/*@innerbreak@*/ break;
	    }
	}

	/* Print only once per package. */
	if (!reverse) {
	    j = 0;
	    for (i = 0; i < fi->dc; i++) {
		if (!dnli->active[i]) continue;
		if (j == 0) {
		    j = 1;
		    rpmMessage(RPMMESS_DEBUG,
	D_("========== Directories not explicitly included in package:\n"));
		}
		(void) urlPath(fi->dnl[i], &dnl);
		rpmMessage(RPMMESS_DEBUG, "%10d %s\n", i, dnl);
	    }
	    if (j)
		rpmMessage(RPMMESS_DEBUG, "==========\n");
	}
    }
    return dnli;
}
/*@=usereleased@*/

/** \ingroup payload
 * Return next directory name (from file info).
 * @param dnli		directory name iterator
 * @return		next directory name
 */
static /*@observer@*/ /*@null@*/
const char * dnlNextIterator(/*@null@*/ DNLI_t dnli)
	/*@modifies dnli @*/
{
    const char * dn = NULL;

    if (dnli) {
	rpmfi fi = dnli->fi;
	int i = -1;

	if (dnli->active)
	do {
	    i = (!dnli->reverse ? dnli->i++ : --dnli->i);
	} while (i >= 0 && i < fi->dc && !dnli->active[i]);

	if (i >= 0 && i < fi->dc)
	    dn = fi->dnl[i];
	else
	    i = -1;
	dnli->isave = i;
    }
    return dn;
}

#if defined(HAVE_PTHREAD_H)
static void * fsmThread(void * arg)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies arg, fileSystem, internalState @*/
{
    FSM_t fsm = arg;
/*@-unqualifiedtrans@*/
    return ((void *) ((long)fsmStage(fsm, fsm->nstage)));
/*@=unqualifiedtrans@*/
}
#endif

int fsmNext(FSM_t fsm, fileStage nstage)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    fsm->nstage = nstage;
#if defined(HAVE_PTHREAD_H)
    if (_fsm_threads)
	return rpmsqJoin( rpmsqThread(fsmThread, fsm) );
#endif
    return fsmStage(fsm, fsm->nstage);
}

/** \ingroup payload
 * Save hard link in chain.
 * @param fsm		file state machine data
 * @return		Is chain only partially filled?
 */
static int saveHardLink(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@uses fsm->links, fsm->ix, fsm->sb, fsm->goal, fsm->nsuffix @*/
	/*@defines fsm->li @*/
	/*@releases fsm->path @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    struct stat * st = &fsm->sb;
    int rc = 0;
    int ix = -1;
    int j;

    /* Find hard link set. */
    for (fsm->li = fsm->links; fsm->li; fsm->li = fsm->li->next) {
	if (fsm->li->sb.st_ino == st->st_ino && fsm->li->sb.st_dev == st->st_dev)
	    break;
    }

    /* New hard link encountered, add new link to set. */
    if (fsm->li == NULL) {
	fsm->li = xcalloc(1, sizeof(*fsm->li));
	fsm->li->next = NULL;
	fsm->li->sb = *st;	/* structure assignment */
	fsm->li->nlink = st->st_nlink;
	fsm->li->linkIndex = fsm->ix;
	fsm->li->createdPath = -1;

	fsm->li->filex = xcalloc(st->st_nlink, sizeof(fsm->li->filex[0]));
	memset(fsm->li->filex, -1, (st->st_nlink * sizeof(fsm->li->filex[0])));
	fsm->li->nsuffix = xcalloc(st->st_nlink, sizeof(*fsm->li->nsuffix));

	if (fsm->goal == FSM_PKGBUILD)
	    fsm->li->linksLeft = st->st_nlink;
	if (fsm->goal == FSM_PKGINSTALL)
	    fsm->li->linksLeft = 0;

	/*@-kepttrans@*/
	fsm->li->next = fsm->links;
	/*@=kepttrans@*/
	fsm->links = fsm->li;
    }

    if (fsm->goal == FSM_PKGBUILD) --fsm->li->linksLeft;
    fsm->li->filex[fsm->li->linksLeft] = fsm->ix;
    /*@-observertrans -dependenttrans@*/
    fsm->li->nsuffix[fsm->li->linksLeft] = fsm->nsuffix;
    /*@=observertrans =dependenttrans@*/
    if (fsm->goal == FSM_PKGINSTALL) fsm->li->linksLeft++;

    if (fsm->goal == FSM_PKGBUILD)
	return (fsm->li->linksLeft > 0);

    if (fsm->goal != FSM_PKGINSTALL)
	return 0;

    if (!(st->st_size || fsm->li->linksLeft == st->st_nlink))
	return 1;

    /* Here come the bits, time to choose a non-skipped file name. */
    {	rpmfi fi = fsmGetFi(fsm);

	for (j = fsm->li->linksLeft - 1; j >= 0; j--) {
	    ix = fsm->li->filex[j];
	    if (ix < 0 || XFA_SKIPPING(fi->actions[ix]))
		continue;
	    break;
	}
    }

    /* Are all links skipped or not encountered yet? */
    if (ix < 0 || j < 0)
	return 1;	/* XXX W2DO? */

    /* Save the non-skipped file name and map index. */
    fsm->li->linkIndex = j;
    fsm->path = _free(fsm->path);
    fsm->ix = ix;
    rc = fsmNext(fsm, FSM_MAP);
    return rc;
}

/** \ingroup payload
 * Destroy set of hard links.
 * @param li		set of hard links
 * @return		NULL always
 */
static /*@null@*/ void * freeHardLink(/*@only@*/ /*@null@*/ struct hardLink_s * li)
	/*@modifies li @*/
{
    if (li) {
	li->nsuffix = _free(li->nsuffix);	/* XXX elements are shared */
	li->filex = _free(li->filex);
    }
    return _free(li);
}

FSM_t newFSM(void)
{
    FSM_t fsm = xcalloc(1, sizeof(*fsm));
    return fsm;
}

FSM_t freeFSM(FSM_t fsm)
{
    if (fsm) {
	fsm->path = _free(fsm->path);
	while ((fsm->li = fsm->links) != NULL) {
	    fsm->links = fsm->li->next;
	    fsm->li->next = NULL;
	    fsm->li = freeHardLink(fsm->li);
	}
	fsm->dnlx = _free(fsm->dnlx);
	fsm->ldn = _free(fsm->ldn);
	fsm->iter = mapFreeIterator(fsm->iter);
    }
    return _free(fsm);
}

int fsmSetup(FSM_t fsm, fileStage goal, const char * afmt,
		const rpmts ts, const rpmfi fi, FD_t cfd,
		unsigned int * archiveSize, const char ** failedFile)
{
    size_t pos = 0;
    int rc, ec = 0;

/*@+voidabstract -nullpass@*/
if (_fsm_debug < 0)
fprintf(stderr, "--> fsmSetup(%p, 0x%x, \"%s\", %p, %p, %p, %p, %p)\n", fsm, goal, afmt, (void *)ts, fi, cfd, archiveSize, failedFile);
/*@=voidabstract =nullpass@*/

    if (fsm->headerRead == NULL) {
	if (afmt != NULL && (!strcmp(afmt, "tar") || !strcmp(afmt, "ustar"))) {
if (_fsm_debug < 0)
fprintf(stderr, "\ttar vectors set\n");
	    fsm->headerRead = &tarHeaderRead;
	    fsm->headerWrite = &tarHeaderWrite;
	    fsm->trailerWrite = &tarTrailerWrite;
	    fsm->blksize = TAR_BLOCK_SIZE;
	} else  {
if (_fsm_debug < 0)
fprintf(stderr, "\tcpio vectors set\n");
	    fsm->headerRead = &cpioHeaderRead;
	    fsm->headerWrite = &cpioHeaderWrite;
	    fsm->trailerWrite = &cpioTrailerWrite;
	    fsm->blksize = 4;
	}
    }

    fsm->goal = goal;
    if (cfd != NULL) {
	fsm->cfd = fdLink(cfd, "persist (fsm)");
	pos = fdGetCpioPos(fsm->cfd);
	fdSetCpioPos(fsm->cfd, 0);
    }
    fsm->iter = mapInitIterator(ts, fi);

    if (fsm->goal == FSM_PKGINSTALL || fsm->goal == FSM_PKGBUILD) {
	void * ptr;
	fi->archivePos = 0;
	ptr = rpmtsNotify(ts, fi->te,
		RPMCALLBACK_INST_START, fi->archivePos, fi->archiveSize);
    }

    /*@-assignexpose@*/
    fsm->archiveSize = archiveSize;
    if (fsm->archiveSize)
	*fsm->archiveSize = 0;
    fsm->failedFile = failedFile;
    if (fsm->failedFile)
	*fsm->failedFile = NULL;
    /*@=assignexpose@*/

    memset(fsm->sufbuf, 0, sizeof(fsm->sufbuf));
    if (fsm->goal == FSM_PKGINSTALL) {
	if (ts && rpmtsGetTid(ts) != -1)
	    sprintf(fsm->sufbuf, ";%08x", (unsigned)rpmtsGetTid(ts));
    }

    ec = fsm->rc = 0;
    rc = fsmUNSAFE(fsm, FSM_CREATE);
    if (rc && !ec) ec = rc;

    rc = fsmUNSAFE(fsm, fsm->goal);
    if (rc && !ec) ec = rc;

    if (fsm->archiveSize && ec == 0)
	*fsm->archiveSize = (fdGetCpioPos(fsm->cfd) - pos);

/*@-nullstate@*/ /* FIX: *fsm->failedFile may be NULL */
   return ec;
/*@=nullstate@*/
}

int fsmTeardown(FSM_t fsm)
{
    int rc = fsm->rc;

if (_fsm_debug < 0)
fprintf(stderr, "--> fsmTeardown(%p)\n", fsm);
    if (!rc)
	rc = fsmUNSAFE(fsm, FSM_DESTROY);

    fsm->iter = mapFreeIterator(fsm->iter);
    if (fsm->cfd != NULL) {
	fsm->cfd = fdFree(fsm->cfd, "persist (fsm)");
	fsm->cfd = NULL;
    }
    fsm->failedFile = NULL;
    return rc;
}

static int fsmMapFContext(FSM_t fsm)
	/*@modifies fsm @*/
{
    rpmts ts = fsmGetTs(fsm);
    rpmfi fi = fsmGetFi(fsm);

    /*
     * Find file security context (if not disabled).
     */
    fsm->fcontext = NULL;
    if (ts != NULL && rpmtsSELinuxEnabled(ts) == 1 &&
	!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOCONTEXTS))
    {
	security_context_t scon = NULL;

/*@-moduncon@*/
	if (matchpathcon(fsm->path, fsm->sb.st_mode, &scon) == 0 && scon != NULL)
	    fsm->fcontext = scon;
	else {
	    int i = fsm->ix;

	    /* Get file security context from package. */
	    if (fi && i >= 0 && i < fi->fc)
		fsm->fcontext = (fi->fcontexts ? fi->fcontexts[i] : NULL);
	}
/*@=moduncon@*/
    }
    return 0;
}

int fsmMapPath(FSM_t fsm)
{
    rpmfi fi = fsmGetFi(fsm);	/* XXX const except for fstates */
    int rc = 0;
    int i;

    fsm->osuffix = NULL;
    fsm->nsuffix = NULL;
    fsm->astriplen = 0;
    fsm->action = FA_UNKNOWN;
    fsm->mapFlags = fi->mapflags;

    i = fsm->ix;
    if (fi && i >= 0 && i < fi->fc) {

	fsm->astriplen = fi->astriplen;
	fsm->action = (fi->actions ? fi->actions[i] : fi->action);
	fsm->fflags = (fi->fflags ? fi->fflags[i] : fi->flags);
	fsm->mapFlags = (fi->fmapflags ? fi->fmapflags[i] : fi->mapflags);

	/* src rpms have simple base name in payload. */
	fsm->dirName = fi->dnl[fi->dil[i]];
	fsm->baseName = fi->bnl[i];

	switch (fsm->action) {
	case FA_SKIP:
	    break;
	case FA_UNKNOWN:
	    break;

	case FA_COPYOUT:
	    break;
	case FA_COPYIN:
	case FA_CREATE:
assert(rpmteType(fi->te) == TR_ADDED);
	    break;

	case FA_SKIPNSTATE:
	    if (fi->fstates && rpmteType(fi->te) == TR_ADDED)
		fi->fstates[i] = RPMFILE_STATE_NOTINSTALLED;
	    break;

	case FA_SKIPNETSHARED:
	    if (fi->fstates && rpmteType(fi->te) == TR_ADDED)
		fi->fstates[i] = RPMFILE_STATE_NETSHARED;
	    break;

	case FA_SKIPCOLOR:
	    if (fi->fstates && rpmteType(fi->te) == TR_ADDED)
		fi->fstates[i] = RPMFILE_STATE_WRONGCOLOR;
	    break;

	case FA_BACKUP:
	    if (!(fsm->fflags & RPMFILE_GHOST)) /* XXX Don't if %ghost file. */
	    switch (rpmteType(fi->te)) {
	    case TR_ADDED:
		fsm->osuffix = SUFFIX_RPMORIG;
		/*@innerbreak@*/ break;
	    case TR_REMOVED:
		fsm->osuffix = SUFFIX_RPMSAVE;
		/*@innerbreak@*/ break;
	    }
	    break;

	case FA_ALTNAME:
assert(rpmteType(fi->te) == TR_ADDED);
	    if (!(fsm->fflags & RPMFILE_GHOST)) /* XXX Don't if %ghost file. */
		fsm->nsuffix = SUFFIX_RPMNEW;
	    break;

	case FA_SAVE:
assert(rpmteType(fi->te) == TR_ADDED);
	    if (!(fsm->fflags & RPMFILE_GHOST)) /* XXX Don't if %ghost file. */
		fsm->osuffix = SUFFIX_RPMSAVE;
	    break;
	case FA_ERASE:
#if 0	/* XXX is this a genhdlist fix? */
	    assert(rpmteType(fi->te) == TR_REMOVED);
#endif
	    /*
	     * XXX TODO: %ghost probably shouldn't be removed, but that changes
	     * legacy rpm behavior.
	     */
	    break;
	default:
	    break;
	}

	if ((fsm->mapFlags & CPIO_MAP_PATH) || fsm->nsuffix) {
	    const struct stat * st = &fsm->sb;
	    fsm->path = _free(fsm->path);
	    fsm->path = fsmFsPath(fsm, st, fsm->subdir,
		(fsm->suffix ? fsm->suffix : fsm->nsuffix));
	}
    }
    return rc;
}

int fsmMapAttrs(FSM_t fsm)
{
    struct stat * st = &fsm->sb;
    rpmfi fi = fsmGetFi(fsm);
    int i = fsm->ix;

    if (fi && i >= 0 && i < fi->fc) {
	mode_t perms = (S_ISDIR(st->st_mode) ? fi->dperms : fi->fperms);
	mode_t finalMode = (fi->fmodes ? fi->fmodes[i] : perms);
	dev_t finalRdev = (fi->frdevs ? fi->frdevs[i] : 0);
	int_32 finalMtime = (fi->fmtimes ? fi->fmtimes[i] : 0);
	uid_t uid = fi->uid;
	gid_t gid = fi->gid;

	if (fi->fuser && unameToUid(fi->fuser[i], &uid)) {
	    if (fsm->goal == FSM_PKGINSTALL)
		rpmMessage(RPMMESS_WARNING,
		    _("user %s does not exist - using root\n"), fi->fuser[i]);
	    uid = 0;
	    finalMode &= ~S_ISUID;      /* turn off suid bit */
	}

	if (fi->fgroup && gnameToGid(fi->fgroup[i], &gid)) {
	    if (fsm->goal == FSM_PKGINSTALL)
		rpmMessage(RPMMESS_WARNING,
		    _("group %s does not exist - using root\n"), fi->fgroup[i]);
	    gid = 0;
	    finalMode &= ~S_ISGID;	/* turn off sgid bit */
	}

	if (fsm->mapFlags & CPIO_MAP_MODE)
	    st->st_mode = (st->st_mode & S_IFMT) | (finalMode & ~S_IFMT);
	if (fsm->mapFlags & CPIO_MAP_TYPE) {
	    st->st_mode = (st->st_mode & ~S_IFMT) | (finalMode & S_IFMT);
	    if ((S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
	    && st->st_nlink == 0)
		st->st_nlink = 1;
	    st->st_rdev = finalRdev;
	    st->st_mtime = finalMtime;
	}
	if (fsm->mapFlags & CPIO_MAP_UID)
	    st->st_uid = uid;
	if (fsm->mapFlags & CPIO_MAP_GID)
	    st->st_gid = gid;

	{   rpmts ts = fsmGetTs(fsm);

	    /*
	     * Set file digest (if not disabled).
	     */
	    if (ts != NULL && !(rpmtsFlags(ts) & RPMTRANS_FLAG_NOFDIGESTS)) {
		fsm->fdigestalgo = fi->digestalgo;
		fsm->fdigest = (fi->fdigests ? fi->fdigests[i] : NULL);
		fsm->digestlen = fi->digestlen;
		fsm->digest = (fi->digests ? (fi->digests + (fsm->digestlen * i)) : NULL);
	    } else {
		fsm->fdigestalgo = 0;
		fsm->fdigest = NULL;
		fsm->digestlen = 0;
		fsm->digest = NULL;
	    }
	}

    }
    return 0;
}

/** \ingroup payload
 * Create file from payload stream.
 * @param fsm		file state machine data
 * @return		0 on success
 */
/*@-compdef@*/
static int extractRegular(/*@special@*/ FSM_t fsm)
	/*@uses fsm->fdigest, fsm->digest, fsm->sb, fsm->wfd  @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    const struct stat * st = &fsm->sb;
    int left = st->st_size;
    int rc = 0;
    int xx;

    rc = fsmNext(fsm, FSM_WOPEN);
    if (rc)
	goto exit;

    if (st->st_size > 0 && (fsm->fdigest != NULL || fsm->digest != NULL))
	fdInitDigest(fsm->wfd, fsm->fdigestalgo, 0);

    while (left) {

	fsm->wrlen = (left > fsm->wrsize ? fsm->wrsize : left);
	rc = fsmNext(fsm, FSM_DREAD);
	if (rc)
	    goto exit;

	rc = fsmNext(fsm, FSM_WRITE);
	if (rc)
	    goto exit;

	left -= fsm->wrnb;

	/* Notify iff progress, completion is done elsewhere */
	if (!rc && left)
	    (void) fsmNext(fsm, FSM_NOTIFY);
    }

    xx = fsync(Fileno(fsm->wfd));

    if (st->st_size > 0 && (fsm->fdigest || fsm->digest)) {
	void * digest = NULL;
	int asAscii = (fsm->digest == NULL ? 1 : 0);

	(void) Fflush(fsm->wfd);
	fdFiniDigest(fsm->wfd, fsm->fdigestalgo, &digest, NULL, asAscii);

	if (digest == NULL) {
	    rc = CPIOERR_DIGEST_MISMATCH;
	    goto exit;
	}

	if (fsm->digest != NULL) {
	    if (memcmp(digest, fsm->digest, fsm->digestlen))
		rc = CPIOERR_DIGEST_MISMATCH;
	} else {
	    if (strcmp(digest, fsm->fdigest))
		rc = CPIOERR_DIGEST_MISMATCH;
	}
	digest = _free(digest);
    }

exit:
    (void) fsmNext(fsm, FSM_WCLOSE);
    return rc;
}
/*@=compdef@*/

/** \ingroup payload
 * Write next item to payload stream.
 * @param fsm		file state machine data
 * @param writeData	should data be written?
 * @return		0 on success
 */
/*@-compdef -compmempass@*/
static int writeFile(/*@special@*/ /*@partial@*/ FSM_t fsm, int writeData)
	/*@uses fsm->path, fsm->opath, fsm->sb, fsm->osb, fsm->cfd @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    const char * path = fsm->path;
    const char * opath = fsm->opath;
    struct stat * st = &fsm->sb;
    struct stat * ost = &fsm->osb;
    size_t left;
    int xx;
    int rc;

    st->st_size = (writeData ? ost->st_size : 0);

    if (S_ISDIR(st->st_mode)) {
	st->st_size = 0;
    } else if (S_ISLNK(st->st_mode)) {
	/*
	 * While linux puts the size of a symlink in the st_size field,
	 * I don't think that's a specified standard.
	 */
	/* XXX NUL terminated result in fsm->rdbuf, len in fsm->rdnb. */
	rc = fsmUNSAFE(fsm, FSM_READLINK);
	if (rc) goto exit;
	st->st_size = fsm->rdnb;
	fsm->lpath = xstrdup(fsm->rdbuf);	/* XXX save readlink return. */
    }

    if (fsm->mapFlags & CPIO_MAP_ABSOLUTE) {
	int nb = strlen(fsm->dirName) + strlen(fsm->baseName) + sizeof(".");
	char * t = alloca(nb);
	*t = '\0';
	fsm->path = t;
	if (fsm->mapFlags & CPIO_MAP_ADDDOT)
	    *t++ = '.';
	t = stpcpy( stpcpy(t, fsm->dirName), fsm->baseName);
    } else if (fsm->mapFlags & CPIO_MAP_PATH) {
	rpmfi fi = fsmGetFi(fsm);
	if (fi->apath) {
	    const char * apath = NULL;
	    (void) urlPath(fi->apath[fsm->ix], &apath);
	    fsm->path = apath + fi->striplen;
	} else
	    fsm->path = fi->bnl[fsm->ix];
    }

    rc = fsmNext(fsm, FSM_HWRITE);
    fsm->path = path;
    if (rc) goto exit;

    if (writeData && S_ISREG(st->st_mode)) {
#if defined(HAVE_MMAP)
	char * rdbuf = NULL;
	void * mapped = (void *)-1;
	size_t nmapped = 0;
	/* XXX 128 Mb resource cap for top(1) scrutiny, MADV_DONTNEED better. */
	int use_mmap = (st->st_size <= 0x07ffffff);
#endif

	rc = fsmNext(fsm, FSM_ROPEN);
	if (rc) goto exit;

	/* XXX unbuffered mmap generates *lots* of fdio debugging */
#if defined(HAVE_MMAP)
	if (use_mmap) {
	    mapped = mmap(NULL, st->st_size, PROT_READ, MAP_SHARED, Fileno(fsm->rfd), 0);
	    if (mapped != (void *)-1) {
		rdbuf = fsm->rdbuf;
		fsm->rdbuf = (char *) mapped;
		fsm->rdlen = nmapped = st->st_size;
#if defined(HAVE_MADVISE) && defined(MADV_DONTNEED)
		xx = madvise(mapped, nmapped, MADV_DONTNEED);
#endif
	    }
	}
#endif

	left = st->st_size;

	while (left) {
#if defined(HAVE_MMAP)
	  if (mapped != (void *)-1) {
	    fsm->rdnb = nmapped;
	  } else
#endif
	  {
	    fsm->rdlen = (left > fsm->rdsize ? fsm->rdsize : left),
	    rc = fsmNext(fsm, FSM_READ);
	    if (rc) goto exit;
	  }

	    /* XXX DWRITE uses rdnb for I/O length. */
	    rc = fsmNext(fsm, FSM_DWRITE);
	    if (rc) goto exit;

	    left -= fsm->wrnb;
	}

#if defined(HAVE_MMAP)
	if (mapped != (void *)-1) {
	    xx = msync(mapped, nmapped, MS_ASYNC);
#if defined(HAVE_MADVISE) && defined(MADV_DONTNEED)
	    xx = madvise(mapped, nmapped, MADV_DONTNEED);
#endif
/*@-noeffect@*/
	    xx = munmap(mapped, nmapped);
/*@=noeffect@*/
	    fsm->rdbuf = rdbuf;
	} else
#endif
	    xx = fsync(Fileno(fsm->rfd));

    }

    rc = fsmNext(fsm, FSM_PAD);
    if (rc) goto exit;

    rc = 0;

exit:
    if (fsm->rfd != NULL)
	(void) fsmNext(fsm, FSM_RCLOSE);
/*@-dependenttrans@*/
    fsm->opath = opath;
    fsm->path = path;
/*@=dependenttrans@*/
    return rc;
}
/*@=compdef =compmempass@*/

/** \ingroup payload
 * Write set of linked files to payload stream.
 * @param fsm		file state machine data
 * @return		0 on success
 */
static int writeLinkedFile(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@uses fsm->path, fsm->nsuffix, fsm->ix, fsm->li, fsm->failedFile @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    const char * path = fsm->path;
    const char * lpath = fsm->lpath;
    const char * nsuffix = fsm->nsuffix;
    int iterIndex = fsm->ix;
    int ec = 0;
    int rc;
    int i;
    const char * linkpath = NULL;
    int firstfile = 1;

    fsm->path = NULL;
    fsm->lpath = NULL;
    fsm->nsuffix = NULL;
    fsm->ix = -1;

    for (i = fsm->li->nlink - 1; i >= 0; i--) {

	if (fsm->li->filex[i] < 0) continue;

	fsm->ix = fsm->li->filex[i];
/*@-compdef@*/
	rc = fsmNext(fsm, FSM_MAP);
/*@=compdef@*/

	/* XXX tar and cpio have to do things differently. */
	if (fsm->headerWrite == tarHeaderWrite) {
	    if (firstfile) {
		const char * apath = NULL;
		char *t;
		(void) urlPath(fsm->path, &apath);
		/* Remove the buildroot prefix. */
		t = xmalloc(sizeof(".") + strlen(apath + fsm->astriplen));
		(void) stpcpy( stpcpy(t, "."), apath + fsm->astriplen);
		linkpath = t;
		firstfile = 0;
	    } else
		fsm->lpath = linkpath;

	    /* Write data after first link for tar. */
	    rc = writeFile(fsm, (fsm->lpath == NULL));
	} else {
	    /* Write data after last link for cpio. */
	    rc = writeFile(fsm, (i == 0));
	}
	if (fsm->failedFile && rc != 0 && *fsm->failedFile == NULL) {
	    ec = rc;
	    *fsm->failedFile = xstrdup(fsm->path);
	}

	fsm->path = _free(fsm->path);
	fsm->li->filex[i] = -1;
    }

/*@-dependenttrans@*/
    linkpath = _free(linkpath);
/*@=dependenttrans@*/
    fsm->ix = iterIndex;
    fsm->nsuffix = nsuffix;
    fsm->lpath = lpath;
    fsm->path = path;
    return ec;
}

/** \ingroup payload
 * Create pending hard links to existing file.
 * @param fsm		file state machine data
 * @return		0 on success
 */
/*@-compdef@*/
static int fsmMakeLinks(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@uses fsm->path, fsm->opath, fsm->nsuffix, fsm->ix, fsm->li @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    const char * path = fsm->path;
    const char * opath = fsm->opath;
    const char * nsuffix = fsm->nsuffix;
    int iterIndex = fsm->ix;
    int ec = 0;
    int rc;
    int i;

    fsm->path = NULL;
    fsm->opath = NULL;
    fsm->nsuffix = NULL;
    fsm->ix = -1;

    fsm->ix = fsm->li->filex[fsm->li->createdPath];
    rc = fsmNext(fsm, FSM_MAP);
    fsm->opath = fsm->path;
    fsm->path = NULL;
    for (i = 0; i < fsm->li->nlink; i++) {
	if (fsm->li->filex[i] < 0) continue;
	if (fsm->li->createdPath == i) continue;

	fsm->ix = fsm->li->filex[i];
	fsm->path = _free(fsm->path);
	rc = fsmNext(fsm, FSM_MAP);
	if (XFA_SKIPPING(fsm->action)) continue;

	rc = fsmUNSAFE(fsm, FSM_VERIFY);
	if (!rc) continue;
	if (!(rc == CPIOERR_ENOENT)) break;

	/* XXX link(fsm->opath, fsm->path) */
	rc = fsmNext(fsm, FSM_LINK);
	if (fsm->failedFile && rc != 0 && *fsm->failedFile == NULL) {
	    ec = rc;
	    *fsm->failedFile = xstrdup(fsm->path);
	}

	fsm->li->linksLeft--;
    }
    fsm->path = _free(fsm->path);
    fsm->opath = _free(fsm->opath);

    fsm->ix = iterIndex;
    fsm->nsuffix = nsuffix;
    fsm->path = path;
    fsm->opath = opath;
    return ec;
}
/*@=compdef@*/

/** \ingroup payload
 * Commit hard linked file set atomically.
 * @param fsm		file state machine data
 * @return		0 on success
 */
/*@-compdef@*/
static int fsmCommitLinks(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@uses fsm->path, fsm->nsuffix, fsm->ix, fsm->sb,
		fsm->li, fsm->links @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    const char * path = fsm->path;
    const char * nsuffix = fsm->nsuffix;
    int iterIndex = fsm->ix;
    struct stat * st = &fsm->sb;
    int rc = 0;
    int i;

    fsm->path = NULL;
    fsm->nsuffix = NULL;
    fsm->ix = -1;

    for (fsm->li = fsm->links; fsm->li; fsm->li = fsm->li->next) {
	if (fsm->li->sb.st_ino == st->st_ino && fsm->li->sb.st_dev == st->st_dev)
	    break;
    }

    for (i = 0; i < fsm->li->nlink; i++) {
	if (fsm->li->filex[i] < 0) continue;
	fsm->ix = fsm->li->filex[i];
	rc = fsmNext(fsm, FSM_MAP);
	if (!XFA_SKIPPING(fsm->action))
	    rc = fsmNext(fsm, FSM_COMMIT);
	fsm->path = _free(fsm->path);
	fsm->li->filex[i] = -1;
    }

    fsm->ix = iterIndex;
    fsm->nsuffix = nsuffix;
    fsm->path = path;
    return rc;
}
/*@=compdef@*/

/**
 * Remove (if created) directories not explicitly included in package.
 * @param fsm		file state machine data
 * @return		0 on success
 */
static int fsmRmdirs(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@uses fsm->path, fsm->dnlx, fsm->ldn, fsm->rdbuf, fsm->iter @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    const char * path = fsm->path;
    void * dnli = dnlInitIterator(fsm, 1);
    char * dn = fsm->rdbuf;
    int dc = dnlCount(dnli);
    int rc = 0;

    fsm->path = NULL;
    dn[0] = '\0';
    /*@-observertrans -dependenttrans@*/
    if (fsm->ldn != NULL && fsm->dnlx != NULL)
    while ((fsm->path = dnlNextIterator(dnli)) != NULL) {
	int dnlen = strlen(fsm->path);
	char * te;

	dc = dnlIndex(dnli);
	if (fsm->dnlx[dc] < 1 || fsm->dnlx[dc] >= dnlen)
	    continue;

	/* Copy to avoid const on fsm->path. */
	te = stpcpy(dn, fsm->path) - 1;
	fsm->path = dn;

	/* Remove generated directories. */
	/*@-usereleased@*/ /* LCL: te used after release? */
	do {
	    if (*te == '/') {
		*te = '\0';
/*@-compdef@*/
		rc = fsmNext(fsm, FSM_RMDIR);
/*@=compdef@*/
		*te = '/';
	    }
	    if (rc)
		/*@innerbreak@*/ break;
	    te--;
	} while ((te - fsm->path) > fsm->dnlx[dc]);
	/*@=usereleased@*/
    }
    dnli = dnlFreeIterator(dnli);
    /*@=observertrans =dependenttrans@*/

    fsm->path = path;
    return rc;
}

/**
 * Create (if necessary) directories not explicitly included in package.
 * @param fsm		file state machine data
 * @return		0 on success
 */
static int fsmMkdirs(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@uses fsm->path, fsm->sb, fsm->osb, fsm->rdbuf, fsm->iter,
		fsm->ldn, fsm->ldnlen, fsm->ldnalloc @*/
	/*@defines fsm->dnlx, fsm->ldn @*/
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    struct stat * st = &fsm->sb;
    struct stat * ost = &fsm->osb;
    const char * path = fsm->path;
    mode_t st_mode = st->st_mode;
    void * dnli = dnlInitIterator(fsm, 0);
    char * dn = fsm->rdbuf;
    int dc = dnlCount(dnli);
    int rc = 0;
    int i;
/*@-compdef@*/
    rpmts ts = fsmGetTs(fsm);
/*@=compdef@*/
    rpmsx sx = NULL;

    /* XXX Set file contexts on non-packaged dirs iff selinux enabled. */
    if (ts != NULL && rpmtsSELinuxEnabled(ts) == 1 &&
      !(rpmtsFlags(ts) & RPMTRANS_FLAG_NOCONTEXTS))
	sx = rpmtsREContext(ts);

    fsm->path = NULL;

    dn[0] = '\0';
    fsm->dnlx = (dc ? xcalloc(dc, sizeof(*fsm->dnlx)) : NULL);
    /*@-observertrans -dependenttrans@*/
    if (fsm->dnlx != NULL)
    while ((fsm->path = dnlNextIterator(dnli)) != NULL) {
	int dnlen = strlen(fsm->path);
	char * te;

	dc = dnlIndex(dnli);
	if (dc < 0) continue;
	fsm->dnlx[dc] = dnlen;
	if (dnlen <= 1)
	    continue;

	/*@-compdef -nullpass@*/	/* FIX: fsm->ldn not defined ??? */
	if (dnlen <= fsm->ldnlen && !strcmp(fsm->path, fsm->ldn))
	    continue;
	/*@=compdef =nullpass@*/

	/* Copy to avoid const on fsm->path. */
	(void) stpcpy(dn, fsm->path);
	fsm->path = dn;

	/* Assume '/' directory exists, "mkdir -p" for others if non-existent */
	(void) urlPath(dn, (const char **)&te);
	for (i = 1, te++; *te != '\0'; te++, i++) {
	    if (*te != '/')
		/*@innercontinue@*/ continue;

	    *te = '\0';

	    /* Already validated? */
	    /*@-usedef -compdef -nullpass -nullderef@*/
	    if (i < fsm->ldnlen &&
		(fsm->ldn[i] == '/' || fsm->ldn[i] == '\0') &&
		!strncmp(fsm->path, fsm->ldn, i))
	    {
		*te = '/';
		/* Move pre-existing path marker forward. */
		fsm->dnlx[dc] = (te - dn);
		/*@innercontinue@*/ continue;
	    }
	    /*@=usedef =compdef =nullpass =nullderef@*/

	    /* Validate next component of path. */
	    rc = fsmUNSAFE(fsm, FSM_LSTAT);
	    *te = '/';

	    /* Directory already exists? */
	    if (rc == 0 && S_ISDIR(ost->st_mode)) {
		/* Move pre-existing path marker forward. */
		fsm->dnlx[dc] = (te - dn);
	    } else if (rc == CPIOERR_ENOENT) {
		rpmfi fi = fsmGetFi(fsm);
		*te = '\0';
		st->st_mode = S_IFDIR | (fi->dperms & 07777);
		rc = fsmNext(fsm, FSM_MKDIR);
		if (!rc) {
		    /* XXX FIXME? only new dir will have context set. */
		    /* Get file security context from patterns. */
		    if (sx != NULL) {
			fsm->fcontext = rpmsxFContext(sx, fsm->path, st->st_mode);
			rc = fsmNext(fsm, FSM_LSETFCON);
		    }
		    if (fsm->fcontext == NULL)
			rpmMessage(RPMMESS_DEBUG,
			    D_("%s directory created with perms %04o, no context.\n"),
			    fsm->path, (unsigned)(st->st_mode & 07777));
		    else
			rpmMessage(RPMMESS_DEBUG,
			    D_("%s directory created with perms %04o, context %s.\n"),
			    fsm->path, (unsigned)(st->st_mode & 07777),
			    fsm->fcontext);
		    fsm->fcontext = NULL;
		}
		*te = '/';
	    }
	    if (rc)
		/*@innerbreak@*/ break;
	}
	if (rc) break;

	/* Save last validated path. */
/*@-compdef@*/ /* FIX: ldn/path annotations ? */
	if (fsm->ldnalloc < (dnlen + 1)) {
	    fsm->ldnalloc = dnlen + 100;
	    fsm->ldn = xrealloc(fsm->ldn, fsm->ldnalloc);
	}
	if (fsm->ldn != NULL) {	/* XXX can't happen */
	    strcpy(fsm->ldn, fsm->path);
 	    fsm->ldnlen = dnlen;
	}
/*@=compdef@*/
    }
    dnli = dnlFreeIterator(dnli);
    sx = rpmsxFree(sx);
    /*@=observertrans =dependenttrans@*/

    fsm->path = path;
    st->st_mode = st_mode;		/* XXX restore st->st_mode */
/*@-compdef@*/ /* FIX: ldn/path annotations ? */
    return rc;
/*@=compdef@*/
}

#ifdef	NOTYET
/**
 * Check for file on disk.
 * @param fsm		file state machine data
 * @return		0 on success
 */
static int fsmStat(/*@special@*/ /*@partial@*/ FSM_t fsm)
	/*@globals fileSystem, internalState @*/
	/*@modifies fsm, fileSystem, internalState @*/
{
    int rc = 0;

    if (fsm->path != NULL) {
	int saveernno = errno;
	rc = fsmUNSAFE(fsm, (!(fsm->mapFlags & CPIO_FOLLOW_SYMLINKS)
			? FSM_LSTAT : FSM_STAT));
	if (rc == CPIOERR_ENOENT) {
	    errno = saveerrno;
	    rc = 0;
	    fsm->exists = 0;
	} else if (rc == 0) {
	    fsm->exists = 1;
	}
    } else {
	/* Skip %ghost files on build. */
	fsm->exists = 0;
    }
    return rc;
}
#endif

#define	IS_DEV_LOG(_x)	\
	((_x) != NULL && strlen(_x) >= (sizeof("/dev/log")-1) && \
	!strncmp((_x), "/dev/log", sizeof("/dev/log")-1) && \
	((_x)[sizeof("/dev/log")-1] == '\0' || \
	 (_x)[sizeof("/dev/log")-1] == ';'))

/*@-compmempass@*/
int fsmStage(FSM_t fsm, fileStage stage)
{
#ifdef	UNUSED
    fileStage prevStage = fsm->stage;
    const char * const prev = fileStageString(prevStage);
#endif
    const char * const cur = fileStageString(stage);
    struct stat * st = &fsm->sb;
    struct stat * ost = &fsm->osb;
    int saveerrno = errno;
    int rc = fsm->rc;
    size_t left;
    int i;

#define	_fafilter(_a)	\
    (!((_a) == FA_CREATE || (_a) == FA_ERASE || (_a) == FA_COPYIN || (_a) == FA_COPYOUT) \
	? fileActionString(_a) : "")

    if (stage & FSM_DEAD) {
	/* do nothing */
    } else if (stage & FSM_INTERNAL) {
	if (_fsm_debug && !(stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s %06o%3d (%4d,%4d)%12lu %s %s\n",
		cur,
		(unsigned)st->st_mode, (int)st->st_nlink,
		(int)st->st_uid, (int)st->st_gid, (unsigned long)st->st_size,
		(fsm->path ? fsm->path : ""),
		_fafilter(fsm->action));
    } else {
	const char * apath = NULL;
	if (fsm->path)
	    (void) urlPath(fsm->path, &apath);
	fsm->stage = stage;
	if (_fsm_debug || !(stage & FSM_VERBOSE))
	    rpmMessage(RPMMESS_DEBUG, "%-8s  %06o%3d (%4d,%4d)%12lu %s %s\n",
		cur,
		(unsigned)st->st_mode, (int)st->st_nlink,
		(int)st->st_uid, (int)st->st_gid, (unsigned long)st->st_size,
		(fsm->path ? apath + fsm->astriplen : ""),
		_fafilter(fsm->action));
    }
#undef	_fafilter

    switch (stage) {
    case FSM_UNKNOWN:
	break;
    case FSM_PKGINSTALL:
	while (1) {
	    /* Clean fsm, free'ing memory. Read next archive header. */
	    rc = fsmUNSAFE(fsm, FSM_INIT);

	    /* Exit on end-of-payload. */
	    if (rc == CPIOERR_HDR_TRAILER) {
		rc = 0;
		/*@loopbreak@*/ break;
	    }

	    /* Exit on error. */
	    if (rc) {
		fsm->postpone = 1;
		(void) fsmNext(fsm, FSM_UNDO);
		/*@loopbreak@*/ break;
	    }

	    /* Extract file from archive. */
	    rc = fsmNext(fsm, FSM_PROCESS);
	    if (rc) {
		(void) fsmNext(fsm, FSM_UNDO);
		/*@loopbreak@*/ break;
	    }

	    /* Notify on success. */
	    (void) fsmNext(fsm, FSM_NOTIFY);

	    rc = fsmNext(fsm, FSM_FINI);
	    if (rc) {
		/*@loopbreak@*/ break;
	    }
	}
	break;
    case FSM_PKGERASE:
    case FSM_PKGCOMMIT:
	while (1) {
	    /* Clean fsm, free'ing memory. */
	    rc = fsmUNSAFE(fsm, FSM_INIT);

	    /* Exit on end-of-payload. */
	    if (rc == CPIOERR_HDR_TRAILER) {
		rc = 0;
		/*@loopbreak@*/ break;
	    }

	    /* Rename/erase next item. */
	    if (fsmNext(fsm, FSM_FINI))
		/*@loopbreak@*/ break;
	}
	break;
    case FSM_PKGBUILD:
	while (1) {

	    rc = fsmUNSAFE(fsm, FSM_INIT);

	    /* Exit on end-of-payload. */
	    if (rc == CPIOERR_HDR_TRAILER) {
		rc = 0;
		/*@loopbreak@*/ break;
	    }

	    /* Exit on error. */
	    if (rc) {
		fsm->postpone = 1;
		(void) fsmNext(fsm, FSM_UNDO);
		/*@loopbreak@*/ break;
	    }

	    /* Copy file into archive. */
	    rc = fsmNext(fsm, FSM_PROCESS);
	    if (rc) {
		(void) fsmNext(fsm, FSM_UNDO);
		/*@loopbreak@*/ break;
	    }

	    /* Notify on success. */
	    (void) fsmNext(fsm, FSM_NOTIFY);

	    if (fsmNext(fsm, FSM_FINI))
		/*@loopbreak@*/ break;
	}

	/* Flush partial sets of hard linked files. */
	if (!(fsm->mapFlags & CPIO_ALL_HARDLINKS)) {
	    int nlink, j;
	    while ((fsm->li = fsm->links) != NULL) {
		fsm->links = fsm->li->next;
		fsm->li->next = NULL;

		/* Re-calculate link count for archive header. */
		for (j = -1, nlink = 0, i = 0; i < fsm->li->nlink; i++) {
		    if (fsm->li->filex[i] < 0)
			/*@innercontinue@*/ continue;
		    nlink++;
		    if (j == -1) j = i;
		}
		/* XXX force the contents out as well. */
		if (j != 0) {
		    fsm->li->filex[0] = fsm->li->filex[j];
		    fsm->li->filex[j] = -1;
		}
		fsm->li->sb.st_nlink = nlink;

		fsm->sb = fsm->li->sb;	/* structure assignment */
		fsm->osb = fsm->sb;	/* structure assignment */

		if (!rc) rc = writeLinkedFile(fsm);

		fsm->li = freeHardLink(fsm->li);
	    }
	}

	if (!rc)
	    rc = fsmNext(fsm, FSM_TRAILER);

	break;
    case FSM_CREATE:
	{   rpmts ts = fsmGetTs(fsm);
#define	_tsmask	(RPMTRANS_FLAG_PKGCOMMIT | RPMTRANS_FLAG_COMMIT)
	    fsm->commit = ((ts && (rpmtsFlags(ts) & _tsmask) &&
			fsm->goal != FSM_PKGCOMMIT) ? 0 : 1);
#undef _tsmask
	}
	fsm->path = _free(fsm->path);
	fsm->lpath = _free(fsm->lpath);
	fsm->opath = _free(fsm->opath);
	fsm->dnlx = _free(fsm->dnlx);

	fsm->ldn = _free(fsm->ldn);
	fsm->ldnalloc = fsm->ldnlen = 0;

	fsm->rdsize = fsm->wrsize = 0;
	fsm->rdbuf = fsm->rdb = _free(fsm->rdb);
	fsm->wrbuf = fsm->wrb = _free(fsm->wrb);
	if (fsm->goal == FSM_PKGINSTALL || fsm->goal == FSM_PKGBUILD) {
	    fsm->rdsize = 8 * BUFSIZ;
	    fsm->rdbuf = fsm->rdb = xmalloc(fsm->rdsize);
	    fsm->wrsize = 8 * BUFSIZ;
	    fsm->wrbuf = fsm->wrb = xmalloc(fsm->wrsize);
	}

	fsm->mkdirsdone = 0;
	fsm->ix = -1;
	fsm->links = NULL;
	fsm->li = NULL;
	errno = 0;	/* XXX get rid of EBADF */

	/* Detect and create directories not explicitly in package. */
	if (fsm->goal == FSM_PKGINSTALL) {
/*@-compdef@*/
	    rc = fsmNext(fsm, FSM_MKDIRS);
/*@=compdef@*/
	    if (!rc) fsm->mkdirsdone = 1;
	}

	break;
    case FSM_INIT:
	fsm->path = _free(fsm->path);
	fsm->lpath = _free(fsm->lpath);
	fsm->postpone = 0;
	fsm->diskchecked = fsm->exists = 0;
	fsm->subdir = NULL;
	fsm->suffix = (fsm->sufbuf[0] != '\0' ? fsm->sufbuf : NULL);
	fsm->action = FA_UNKNOWN;
	fsm->osuffix = NULL;
	fsm->nsuffix = NULL;

	if (fsm->goal == FSM_PKGINSTALL) {
	    /* Read next header from payload, checking for end-of-payload. */
	    rc = fsmUNSAFE(fsm, FSM_NEXT);
	}
	if (rc) break;

	/* Identify mapping index. */
	fsm->ix = ((fsm->goal == FSM_PKGINSTALL)
		? mapFind(fsm->iter, fsm->path) : mapNextIterator(fsm->iter));

if (!(fsmGetFi(fsm)->mapflags & CPIO_PAYLOAD_LIST)) {
	/* Detect end-of-loop and/or mapping error. */
if (!(fsmGetFi(fsm)->mapflags & CPIO_PAYLOAD_EXTRACT)) {
	if (fsm->ix < 0) {
	    if (fsm->goal == FSM_PKGINSTALL) {
#if 0
		rpmMessage(RPMMESS_WARNING,
		    _("archive file %s was not found in header file list\n"),
			fsm->path);
#endif
		if (fsm->failedFile && *fsm->failedFile == NULL)
		    *fsm->failedFile = xstrdup(fsm->path);
		rc = CPIOERR_UNMAPPED_FILE;
	    } else {
		rc = CPIOERR_HDR_TRAILER;
	    }
	    break;
	}
}

	/* On non-install, mode must be known so that dirs don't get suffix. */
	if (fsm->goal != FSM_PKGINSTALL) {
	    rpmfi fi = fsmGetFi(fsm);
	    st->st_mode = fi->fmodes[fsm->ix];
	}
}

	/* Generate file path. */
	rc = fsmNext(fsm, FSM_MAP);
	if (rc) break;

	/* Perform lstat/stat for disk file. */
#ifdef	NOTYET
	rc = fsmStat(fsm);
#else
	if (fsm->path != NULL &&
	    !(fsm->goal == FSM_PKGINSTALL && S_ISREG(st->st_mode)))
	{
	    rc = fsmUNSAFE(fsm, (!(fsm->mapFlags & CPIO_FOLLOW_SYMLINKS)
			? FSM_LSTAT : FSM_STAT));
	    if (rc == CPIOERR_ENOENT) {
		errno = saveerrno;
		rc = 0;
		fsm->exists = 0;
	    } else if (rc == 0) {
		fsm->exists = 1;
	    }
	} else {
	    /* Skip %ghost files on build. */
	    fsm->exists = 0;
	}
#endif
	fsm->diskchecked = 1;
	if (rc) break;

	/* On non-install, the disk file stat is what's remapped. */
	if (fsm->goal != FSM_PKGINSTALL)
	    *st = *ost;			/* structure assignment */

	/* Remap file perms, owner, and group. */
	rc = fsmMapAttrs(fsm);
	if (rc) break;

	fsm->postpone = XFA_SKIPPING(fsm->action);
	if (fsm->goal == FSM_PKGINSTALL || fsm->goal == FSM_PKGBUILD) {
	    /*@-evalorder@*/ /* FIX: saveHardLink can modify fsm */
	    if (!(S_ISDIR(st->st_mode) || S_ISLNK(st->st_mode))
	     && (st->st_nlink > 1 || fsm->lpath != NULL))
		fsm->postpone = saveHardLink(fsm);
	    /*@=evalorder@*/
	}
if (fsmGetFi(fsm)->mapflags & CPIO_PAYLOAD_LIST) fsm->postpone = 1;
	break;
    case FSM_PRE:
	break;
    case FSM_MAP:
	rc = fsmMapPath(fsm);
	break;
    case FSM_MKDIRS:
	rc = fsmMkdirs(fsm);
	break;
    case FSM_RMDIRS:
	if (fsm->dnlx)
	    rc = fsmRmdirs(fsm);
	break;
    case FSM_PROCESS:
	if (fsm->postpone) {
	    if (fsm->goal == FSM_PKGINSTALL) {
		/* XXX Skip over file body, archive headers already done. */
		if (S_ISREG(st->st_mode))
		    rc = fsmNext(fsm, FSM_EAT);
	    }
	    break;
	}

	if (fsm->goal == FSM_PKGBUILD) {
	    if (fsm->fflags & RPMFILE_GHOST) /* XXX Don't if %ghost file. */
		break;
	    if (!S_ISDIR(st->st_mode) && st->st_nlink > 1) {
		struct hardLink_s * li, * prev;

if (!(fsm->mapFlags & CPIO_ALL_HARDLINKS)) break;
		rc = writeLinkedFile(fsm);
		if (rc) break;	/* W2DO? */

		for (li = fsm->links, prev = NULL; li; prev = li, li = li->next)
		     if (li == fsm->li)
			/*@loopbreak@*/ break;

		if (prev == NULL)
		    fsm->links = fsm->li->next;
		else
		    prev->next = fsm->li->next;
		fsm->li->next = NULL;
		fsm->li = freeHardLink(fsm->li);
	    } else {
		rc = writeFile(fsm, 1);
	    }
	    break;
	}

	if (fsm->goal != FSM_PKGINSTALL)
	    break;

	if (S_ISREG(st->st_mode) && fsm->lpath != NULL) {
	    const char * opath = fsm->opath;
	    char * t = xmalloc(strlen(fsm->lpath+1) + strlen(fsm->suffix) + 1);
	    (void) stpcpy(t, fsm->lpath+1);
	     fsm->opath = t;
	    /* XXX link(fsm->opath, fsm->path) */
	    rc = fsmNext(fsm, FSM_LINK);
	    if (fsm->failedFile && rc != 0 && *fsm->failedFile == NULL) {
		*fsm->failedFile = xstrdup(fsm->path);
	    }
	    fsm->opath = _free(fsm->opath);
	    fsm->opath = opath;
	    break;	/* XXX so that delayed hard links get skipped. */
	}
	if (S_ISREG(st->st_mode)) {
	    const char * path = fsm->path;
	    if (fsm->osuffix)
		fsm->path = fsmFsPath(fsm, st, NULL, NULL);
	    rc = fsmUNSAFE(fsm, FSM_VERIFY);

	    if (rc == 0 && fsm->osuffix) {
		const char * opath = fsm->opath;
		fsm->opath = fsm->path;
		fsm->path = fsmFsPath(fsm, st, NULL, fsm->osuffix);
		rc = fsmNext(fsm, FSM_RENAME);
		if (!rc)
		    rpmMessage(RPMMESS_WARNING,
			_("%s saved as %s\n"),
				(fsm->opath ? fsm->opath : ""),
				(fsm->path ? fsm->path : ""));
		fsm->path = _free(fsm->path);
		fsm->opath = opath;
	    }

	    /*@-dependenttrans@*/
	    fsm->path = path;
	    /*@=dependenttrans@*/
	    if (!(rc == CPIOERR_ENOENT)) return rc;
	    rc = extractRegular(fsm);
	} else if (S_ISDIR(st->st_mode)) {
	    mode_t st_mode = st->st_mode;
	    rc = fsmUNSAFE(fsm, FSM_VERIFY);
	    if (rc == CPIOERR_ENOENT) {
		st->st_mode &= ~07777; 		/* XXX abuse st->st_mode */
		st->st_mode |=  00700;
		rc = fsmNext(fsm, FSM_MKDIR);
		st->st_mode = st_mode;		/* XXX restore st->st_mode */
	    }
	} else if (S_ISLNK(st->st_mode)) {
assert(fsm->lpath != NULL);
	    /*@=dependenttrans@*/
	    rc = fsmUNSAFE(fsm, FSM_VERIFY);
	    if (rc == CPIOERR_ENOENT)
		rc = fsmNext(fsm, FSM_SYMLINK);
	} else if (S_ISFIFO(st->st_mode)) {
	    mode_t st_mode = st->st_mode;
	    /* This mimics cpio S_ISSOCK() behavior but probably isnt' right */
	    rc = fsmUNSAFE(fsm, FSM_VERIFY);
	    if (rc == CPIOERR_ENOENT) {
		st->st_mode = 0000;		/* XXX abuse st->st_mode */
		rc = fsmNext(fsm, FSM_MKFIFO);
		st->st_mode = st_mode;		/* XXX restore st->st_mode */
	    }
	} else if (S_ISCHR(st->st_mode) ||
		   S_ISBLK(st->st_mode) ||
    /*@-unrecog@*/ S_ISSOCK(st->st_mode) /*@=unrecog@*/)
	{
	    rc = fsmUNSAFE(fsm, FSM_VERIFY);
	    if (rc == CPIOERR_ENOENT)
		rc = fsmNext(fsm, FSM_MKNOD);
	} else {
	    /* XXX Repackaged payloads may be missing files. */
	    if (fsm->repackaged)
		break;

	    /* XXX Special case /dev/log, which shouldn't be packaged anyways */
	    if (!IS_DEV_LOG(fsm->path))
		rc = CPIOERR_UNKNOWN_FILETYPE;
	}
	if (!S_ISDIR(st->st_mode) && st->st_nlink > 1) {
	    fsm->li->createdPath = fsm->li->linkIndex;
	    rc = fsmMakeLinks(fsm);
	}
	break;
    case FSM_POST:
	break;
    case FSM_MKLINKS:
	rc = fsmMakeLinks(fsm);
	break;
    case FSM_NOTIFY:		/* XXX move from fsm to psm -> tsm */
	if (fsm->goal == FSM_PKGINSTALL || fsm->goal == FSM_PKGBUILD) {
	    rpmts ts = fsmGetTs(fsm);
	    rpmfi fi = fsmGetFi(fsm);
	    void * ptr;
	    unsigned long long archivePos = fdGetCpioPos(fsm->cfd);
	    if (archivePos > fi->archivePos) {
		fi->archivePos = archivePos;
		ptr = rpmtsNotify(ts, fi->te, RPMCALLBACK_INST_PROGRESS,
			fi->archivePos, fi->archiveSize);
	    }
	}
	break;
    case FSM_UNDO:
	if (fsm->postpone)
	    break;
	if (fsm->goal == FSM_PKGINSTALL) {
	    /* XXX only erase if temp fn w suffix is in use */
	    if (fsm->sufbuf[0] != '\0')
		(void) fsmNext(fsm,
		    (S_ISDIR(st->st_mode) ? FSM_RMDIR : FSM_UNLINK));

#ifdef	NOTYET	/* XXX remove only dirs just created, not all. */
	    if (fsm->dnlx)
		(void) fsmNext(fsm, FSM_RMDIRS);
#endif
	    errno = saveerrno;
	}
	if (fsm->failedFile && *fsm->failedFile == NULL)
	    *fsm->failedFile = xstrdup(fsm->path);
	break;
    case FSM_FINI:
	if (!fsm->postpone && fsm->commit) {
	    if (fsm->goal == FSM_PKGINSTALL)
		rc = ((!S_ISDIR(st->st_mode) && st->st_nlink > 1)
			? fsmCommitLinks(fsm) : fsmNext(fsm, FSM_COMMIT));
	    if (fsm->goal == FSM_PKGCOMMIT)
		rc = fsmNext(fsm, FSM_COMMIT);
	    if (fsm->goal == FSM_PKGERASE)
		rc = fsmNext(fsm, FSM_COMMIT);
	}
	fsm->path = _free(fsm->path);
	fsm->lpath = _free(fsm->lpath);
	fsm->opath = _free(fsm->opath);
	memset(st, 0, sizeof(*st));
	memset(ost, 0, sizeof(*ost));
	break;
    case FSM_COMMIT:
	/* Rename pre-existing modified or unmanaged file. */
	if (fsm->osuffix && fsm->diskchecked &&
	  (fsm->exists || (fsm->goal == FSM_PKGINSTALL && S_ISREG(st->st_mode))))
	{
	    const char * opath = fsm->opath;
	    const char * path = fsm->path;
	    fsm->opath = fsmFsPath(fsm, st, NULL, NULL);
	    fsm->path = fsmFsPath(fsm, st, NULL, fsm->osuffix);
	    rc = fsmNext(fsm, FSM_RENAME);
	    if (!rc) {
		rpmMessage(RPMMESS_WARNING, _("%s saved as %s\n"),
				(fsm->opath ? fsm->opath : ""),
				(fsm->path ? fsm->path : ""));
	    }
	    fsm->path = _free(fsm->path);
	    fsm->path = path;
	    fsm->opath = _free(fsm->opath);
	    fsm->opath = opath;
	}

	/* Remove erased files. */
	if (fsm->goal == FSM_PKGERASE) {
	    if (fsm->action == FA_ERASE) {
		rpmfi fi = fsmGetFi(fsm);
		if (S_ISDIR(st->st_mode)) {
		    rc = fsmNext(fsm, FSM_RMDIR);
		    if (!rc) break;
		    switch (rc) {
		    case CPIOERR_ENOENT: /* XXX rmdir("/") linux 2.2.x kernel hack */
		    case CPIOERR_ENOTEMPTY:
	/* XXX make sure that build side permits %missingok on directories. */
			if (fsm->fflags & RPMFILE_MISSINGOK)
			    /*@innerbreak@*/ break;

			/* XXX common error message. */
			rpmError(
			    (strict_erasures ? RPMERR_RMDIR : RPMDEBUG_RMDIR),
			    _("%s rmdir of %s failed: Directory not empty\n"), 
				rpmfiTypeString(fi), fsm->path);
			/*@innerbreak@*/ break;
		    default:
			rpmError(
			    (strict_erasures ? RPMERR_RMDIR : RPMDEBUG_RMDIR),
				_("%s rmdir of %s failed: %s\n"),
				rpmfiTypeString(fi), fsm->path, strerror(errno));
			/*@innerbreak@*/ break;
		    }
		} else {
		    rc = fsmNext(fsm, FSM_UNLINK);
		    if (!rc) break;
		    switch (rc) {
		    case CPIOERR_ENOENT:
			if (fsm->fflags & RPMFILE_MISSINGOK)
			    /*@innerbreak@*/ break;
			/*@fallthrough@*/
		    default:
			rpmError(
			    (strict_erasures ? RPMERR_UNLINK : RPMDEBUG_UNLINK),
				_(" %s: unlink of %s failed: %s\n"),
				rpmfiTypeString(fi), fsm->path, strerror(errno));
			/*@innerbreak@*/ break;
		    }
		}
	    }
	    /* XXX Failure to remove is not (yet) cause for failure. */
	    if (!strict_erasures) rc = 0;
	    break;
	}

	/* XXX Special case /dev/log, which shouldn't be packaged anyways */
if (!(fsmGetFi(fsm)->mapflags & CPIO_PAYLOAD_EXTRACT)) {
	if (!S_ISSOCK(st->st_mode) && !IS_DEV_LOG(fsm->path)) {
	    /* Rename temporary to final file name. */
	    if (!S_ISDIR(st->st_mode) &&
		(fsm->subdir || fsm->suffix || fsm->nsuffix))
	    {
		fsm->opath = fsm->path;
		fsm->path = fsmFsPath(fsm, st, NULL, fsm->nsuffix);
		rc = fsmNext(fsm, FSM_RENAME);
		if (rc)
			(void) Unlink(fsm->opath);
		else if (fsm->nsuffix) {
		    const char * opath = fsmFsPath(fsm, st, NULL, NULL);
		    rpmMessage(RPMMESS_WARNING, _("%s created as %s\n"),
				(opath ? opath : ""),
				(fsm->path ? fsm->path : ""));
		    opath = _free(opath);
		}
		fsm->opath = _free(fsm->opath);
	    }
	    /*
	     * Set file security context (if not disabled).
	     */
	    if (!rc && !getuid()) {
		rc = fsmMapFContext(fsm);
		if (!rc)
		    rc = fsmNext(fsm, FSM_LSETFCON);
		fsm->fcontext = NULL;
	    }
	    if (S_ISLNK(st->st_mode)) {
		if (!rc && !getuid())
		    rc = fsmNext(fsm, FSM_LCHOWN);
	    } else {
		if (!rc && !getuid())
		    rc = fsmNext(fsm, FSM_CHOWN);
		if (!rc)
		    rc = fsmNext(fsm, FSM_CHMOD);
		if (!rc) {
		    time_t mtime = st->st_mtime;
		    rpmfi fi = fsmGetFi(fsm);
		    if (fi->fmtimes)
			st->st_mtime = fi->fmtimes[fsm->ix];
		    rc = fsmNext(fsm, FSM_UTIME);
		    st->st_mtime = mtime;
		}
	    }
	}
}

	/* Notify on success. */
	if (!rc)		rc = fsmNext(fsm, FSM_NOTIFY);
	else if (fsm->failedFile && *fsm->failedFile == NULL) {
	    *fsm->failedFile = fsm->path;
	    fsm->path = NULL;
	}
	break;
    case FSM_DESTROY:
	fsm->path = _free(fsm->path);

	/* Check for hard links missing from payload. */
	while ((fsm->li = fsm->links) != NULL) {
	    fsm->links = fsm->li->next;
	    fsm->li->next = NULL;
	    if (fsm->goal == FSM_PKGINSTALL &&
			fsm->commit && fsm->li->linksLeft)
	    {
		for (i = 0 ; i < fsm->li->linksLeft; i++) {
		    if (fsm->li->filex[i] < 0)
			/*@innercontinue@*/ continue;
		    rc = CPIOERR_MISSING_HARDLINK;
		    if (fsm->failedFile && *fsm->failedFile == NULL) {
			fsm->ix = fsm->li->filex[i];
			if (!fsmNext(fsm, FSM_MAP)) {
			    *fsm->failedFile = fsm->path;
			    fsm->path = NULL;
			}
		    }
		    /*@loopbreak@*/ break;
		}
	    }
	    if (fsm->goal == FSM_PKGBUILD &&
		(fsm->mapFlags & CPIO_ALL_HARDLINKS))
	    {
		rc = CPIOERR_MISSING_HARDLINK;
            }
	    fsm->li = freeHardLink(fsm->li);
	}
	fsm->ldn = _free(fsm->ldn);
	fsm->ldnalloc = fsm->ldnlen = 0;
	fsm->rdbuf = fsm->rdb = _free(fsm->rdb);
	fsm->wrbuf = fsm->wrb = _free(fsm->wrb);
	break;
    case FSM_VERIFY:
	if (fsm->diskchecked && !fsm->exists) {
	    rc = CPIOERR_ENOENT;
	    break;
	}
	if (S_ISREG(st->st_mode)) {
	    char * path = alloca(strlen(fsm->path) + sizeof("-RPMDELETE"));
	    (void) stpcpy( stpcpy(path, fsm->path), "-RPMDELETE");
	    /*
	     * XXX HP-UX (and other os'es) don't permit unlink on busy
	     * XXX files.
	     */
	    fsm->opath = fsm->path;
	    fsm->path = path;
	    rc = fsmNext(fsm, FSM_RENAME);
	    if (!rc)
		    (void) fsmNext(fsm, FSM_UNLINK);
	    else
		    rc = CPIOERR_UNLINK_FAILED;
	    fsm->path = fsm->opath;
	    fsm->opath = NULL;
	    return (rc ? rc : CPIOERR_ENOENT);	/* XXX HACK */
	    /*@notreached@*/ break;
	} else if (S_ISDIR(st->st_mode)) {
	    if (S_ISDIR(ost->st_mode))		return 0;
	    if (S_ISLNK(ost->st_mode)) {
		rc = fsmUNSAFE(fsm, FSM_STAT);
		if (rc == CPIOERR_ENOENT) rc = 0;
		if (rc) break;
		errno = saveerrno;
		if (S_ISDIR(ost->st_mode))	return 0;
	    }
	} else if (S_ISLNK(st->st_mode)) {
	    if (S_ISLNK(ost->st_mode)) {
	/* XXX NUL terminated result in fsm->rdbuf, len in fsm->rdnb. */
		rc = fsmUNSAFE(fsm, FSM_READLINK);
		errno = saveerrno;
		if (rc) break;
		if (!strcmp(fsm->lpath, fsm->rdbuf))	return 0;
	    }
	} else if (S_ISFIFO(st->st_mode)) {
	    if (S_ISFIFO(ost->st_mode))		return 0;
	} else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode)) {
	    if ((S_ISCHR(ost->st_mode) || S_ISBLK(ost->st_mode)) &&
		(ost->st_rdev == st->st_rdev))	return 0;
	} else if (S_ISSOCK(st->st_mode)) {
	    if (S_ISSOCK(ost->st_mode))		return 0;
	}
	    /* XXX shouldn't do this with commit/undo. */
	rc = 0;
	if (fsm->stage == FSM_PROCESS) rc = fsmNext(fsm, FSM_UNLINK);
	if (rc == 0)	rc = CPIOERR_ENOENT;
	return (rc ? rc : CPIOERR_ENOENT);	/* XXX HACK */
	/*@notreached@*/ break;

    case FSM_UNLINK:
	/* XXX Remove setuid/setgid bits on possibly hard linked files. */
	if (fsm->mapFlags & CPIO_SBIT_CHECK) {
	    struct stat stb;
	    if (Lstat(fsm->path, &stb) == 0 && S_ISREG(stb.st_mode) && (stb.st_mode & 06000) != 0) {
		/* XXX rc = fsmNext(fsm, FSM_CHMOD); instead */
		(void)Chmod(fsm->path, stb.st_mode & 0777);
	    }
	}
	rc = Unlink(fsm->path);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s) %s\n", cur,
		fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0)
	    rc = (errno == ENOENT ? CPIOERR_ENOENT : CPIOERR_UNLINK_FAILED);
	break;
    case FSM_RENAME:
	/* XXX Remove setuid/setgid bits on possibly hard linked files. */
	if (fsm->mapFlags & CPIO_SBIT_CHECK) {
	    struct stat stb;
	    if (Lstat(fsm->path, &stb) == 0 && S_ISREG(stb.st_mode) && (stb.st_mode & 06000) != 0) {
		/* XXX rc = fsmNext(fsm, FSM_CHMOD); instead */
		(void)Chmod(fsm->path, stb.st_mode & 0777);
	    }
	}
	rc = Rename(fsm->opath, fsm->path);
	/* XXX Repackaged payloads may be missing files. */
	if (fsm->repackaged)
	    rc = 0;
#if defined(ETXTBSY)
	if (rc && errno == ETXTBSY) {
	    char * path = alloca(strlen(fsm->path) + sizeof("-RPMDELETE"));
	    (void) stpcpy( stpcpy(path, fsm->path), "-RPMDELETE");
	    /*
	     * XXX HP-UX (and other os'es) don't permit rename to busy
	     * XXX files.
	     */
	    rc = Rename(fsm->path, path);
	    if (!rc) rc = Rename(fsm->opath, fsm->path);
	}
#endif
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %s) %s\n", cur,
		fsm->opath, fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_RENAME_FAILED;
	break;
    case FSM_MKDIR:
	rc = Mkdir(fsm->path, (st->st_mode & 07777));
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, 0%04o) %s\n", cur,
		fsm->path, (unsigned)(st->st_mode & 07777),
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_MKDIR_FAILED;
	break;
    case FSM_RMDIR:
	rc = Rmdir(fsm->path);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s) %s\n", cur,
		fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0)
	    switch (errno) {
	    case ENOENT:        rc = CPIOERR_ENOENT;    /*@switchbreak@*/ break;
	    case ENOTEMPTY:     rc = CPIOERR_ENOTEMPTY; /*@switchbreak@*/ break;
	    default:            rc = CPIOERR_RMDIR_FAILED; /*@switchbreak@*/ break;
	    }
	break;
    case FSM_LSETFCON:
      {	const char * fsmpath = NULL;
	if (fsm->fcontext == NULL || *fsm->fcontext == '\0'
	 || !strcmp(fsm->fcontext, "<<none>>"))
	    break;
	(void) urlPath(fsm->path, &fsmpath);	/* XXX fsm->path */
	rc = lsetfilecon(fsmpath, (security_context_t)fsm->fcontext);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %s) %s\n", cur,
		fsm->path, fsm->fcontext,
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0) rc = (errno == EOPNOTSUPP ? 0 : CPIOERR_LSETFCON_FAILED);
      }	break;
    case FSM_CHOWN:
	rc = Chown(fsm->path, st->st_uid, st->st_gid);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %d, %d) %s\n", cur,
		fsm->path, (int)st->st_uid, (int)st->st_gid,
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_CHOWN_FAILED;
	break;
    case FSM_LCHOWN:
#if ! CHOWN_FOLLOWS_SYMLINK
	rc = Lchown(fsm->path, st->st_uid, st->st_gid);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %d, %d) %s\n", cur,
		fsm->path, (int)st->st_uid, (int)st->st_gid,
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_CHOWN_FAILED;
#endif
	break;
    case FSM_CHMOD:
	rc = Chmod(fsm->path, (st->st_mode & 07777));
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, 0%04o) %s\n", cur,
		fsm->path, (unsigned)(st->st_mode & 07777),
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_CHMOD_FAILED;
	break;
    case FSM_UTIME:
	{   struct utimbuf stamp;
	    stamp.actime = st->st_mtime;
	    stamp.modtime = st->st_mtime;
	    rc = Utime(fsm->path, &stamp);
	    if (_fsm_debug && (stage & FSM_SYSCALL))
		rpmMessage(RPMMESS_DEBUG, " %8s (%s, 0x%x) %s\n", cur,
			fsm->path, (unsigned)st->st_mtime,
			(rc < 0 ? strerror(errno) : ""));
	    if (rc < 0)	rc = CPIOERR_UTIME_FAILED;
	}
	break;
    case FSM_SYMLINK:
	rc = Symlink(fsm->lpath, fsm->path);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %s) %s\n", cur,
		fsm->lpath, fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_SYMLINK_FAILED;
	break;
    case FSM_LINK:
	rc = Link(fsm->opath, fsm->path);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %s) %s\n", cur,
		fsm->opath, fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_LINK_FAILED;
	break;
    case FSM_MKFIFO:
	rc = Mkfifo(fsm->path, (st->st_mode & 07777));
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, 0%04o) %s\n", cur,
		fsm->path, (unsigned)(st->st_mode & 07777),
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_MKFIFO_FAILED;
	break;
    case FSM_MKNOD:
	/*@-unrecog -portability @*/ /* FIX: check S_IFIFO or dev != 0 */
	rc = Mknod(fsm->path, (st->st_mode & ~07777), st->st_rdev);
	/*@=unrecog =portability @*/
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, 0%o, 0x%x) %s\n", cur,
		fsm->path, (unsigned)(st->st_mode & ~07777),
		(unsigned)st->st_rdev,
		(rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_MKNOD_FAILED;
	break;
    case FSM_LSTAT:
	rc = Lstat(fsm->path, ost);
	if (_fsm_debug && (stage & FSM_SYSCALL) && rc && errno != ENOENT)
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, ost) %s\n", cur,
		fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0) {
	    rc = (errno == ENOENT ? CPIOERR_ENOENT : CPIOERR_LSTAT_FAILED);
	    memset(ost, 0, sizeof(*ost));	/* XXX s390x hackery */
	}
	break;
    case FSM_STAT:
	rc = Stat(fsm->path, ost);
	if (_fsm_debug && (stage & FSM_SYSCALL) && rc && errno != ENOENT)
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, ost) %s\n", cur,
		fsm->path, (rc < 0 ? strerror(errno) : ""));
	if (rc < 0) {
	    rc = (errno == ENOENT ? CPIOERR_ENOENT : CPIOERR_STAT_FAILED);
	    memset(ost, 0, sizeof(*ost));	/* XXX s390x hackery */
	}
	break;
    case FSM_READLINK:
	/* XXX NUL terminated result in fsm->rdbuf, len in fsm->rdnb. */
	rc = Readlink(fsm->path, fsm->rdbuf, fsm->rdsize - 1);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, rdbuf, %d) %s\n", cur,
		fsm->path, (int)(fsm->rdsize -1), (rc < 0 ? strerror(errno) : ""));
	if (rc < 0)	rc = CPIOERR_READLINK_FAILED;
	else {
	    fsm->rdnb = rc;
	    fsm->rdbuf[fsm->rdnb] = '\0';
	    rc = 0;
	}
	break;
    case FSM_CHROOT:
	break;

    case FSM_NEXT:
	rc = fsmUNSAFE(fsm, FSM_HREAD);
	if (rc) break;
	if (!strcmp(fsm->path, CPIO_TRAILER)) { /* Detect end-of-payload. */
	    fsm->path = _free(fsm->path);
	    rc = CPIOERR_HDR_TRAILER;
	}
	if (!rc)
	    rc = fsmNext(fsm, FSM_POS);
	break;
    case FSM_EAT:
	for (left = st->st_size; left > 0; left -= fsm->rdnb) {
	    fsm->wrlen = (left > fsm->wrsize ? fsm->wrsize : left);
	    rc = fsmNext(fsm, FSM_DREAD);
	    if (rc)
		/*@loopbreak@*/ break;
	}
	break;
    case FSM_POS:
	left = (fsm->blksize - (fdGetCpioPos(fsm->cfd) % fsm->blksize)) % fsm->blksize;
	if (left) {
	    fsm->wrlen = left;
	    (void) fsmNext(fsm, FSM_DREAD);
	}
	break;
    case FSM_PAD:
	left = (fsm->blksize - (fdGetCpioPos(fsm->cfd) % fsm->blksize)) % fsm->blksize;
	if (left) {
	    memset(fsm->rdbuf, 0, left);
	    /* XXX DWRITE uses rdnb for I/O length. */
	    fsm->rdnb = left;
	    (void) fsmNext(fsm, FSM_DWRITE);
	}
	break;
    case FSM_TRAILER:
	rc = (*fsm->trailerWrite) (fsm);	/* Write payload trailer. */
	break;
    case FSM_HREAD:
	rc = fsmNext(fsm, FSM_POS);
	if (!rc)
	    rc = (*fsm->headerRead) (fsm, st);	/* Read next payload header. */
	break;
    case FSM_HWRITE:
	rc = (*fsm->headerWrite) (fsm, st);	/* Write next payload header. */
	break;
    case FSM_DREAD:
	fsm->rdnb = Fread(fsm->wrbuf, sizeof(*fsm->wrbuf), fsm->wrlen, fsm->cfd);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %d, cfd)\trdnb %d\n",
		cur, (fsm->wrbuf == fsm->wrb ? "wrbuf" : "mmap"),
		(int)fsm->wrlen, (int)fsm->rdnb);
	if (fsm->rdnb != fsm->wrlen || Ferror(fsm->cfd))
	    rc = CPIOERR_READ_FAILED;
	if (fsm->rdnb > 0)
	    fdSetCpioPos(fsm->cfd, fdGetCpioPos(fsm->cfd) + fsm->rdnb);
	break;
    case FSM_DWRITE:
	fsm->wrnb = Fwrite(fsm->rdbuf, sizeof(*fsm->rdbuf), fsm->rdnb, fsm->cfd);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, %d, cfd)\twrnb %d\n",
		cur, (fsm->rdbuf == fsm->rdb ? "rdbuf" : "mmap"),
		(int)fsm->rdnb, (int)fsm->wrnb);
	if (fsm->rdnb != fsm->wrnb || Ferror(fsm->cfd))
	    rc = CPIOERR_WRITE_FAILED;
	if (fsm->wrnb > 0)
	    fdSetCpioPos(fsm->cfd, fdGetCpioPos(fsm->cfd) + fsm->wrnb);
	break;

    case FSM_ROPEN:
	fsm->rfd = Fopen(fsm->path, "r.fdio");
	if (fsm->rfd == NULL || Ferror(fsm->rfd)) {
	    if (fsm->rfd != NULL)	(void) fsmNext(fsm, FSM_RCLOSE);
	    fsm->rfd = NULL;
	    rc = CPIOERR_OPEN_FAILED;
	    break;
	}
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, \"r\") rfd %p rdbuf %p\n", cur,
		fsm->path, fsm->rfd, fsm->rdbuf);
	break;
    case FSM_READ:
	fsm->rdnb = Fread(fsm->rdbuf, sizeof(*fsm->rdbuf), fsm->rdlen, fsm->rfd);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (rdbuf, %d, rfd)\trdnb %d\n",
		cur, (int)fsm->rdlen, (int)fsm->rdnb);
	if (fsm->rdnb != fsm->rdlen || Ferror(fsm->rfd))
	    rc = CPIOERR_READ_FAILED;
	break;
    case FSM_RCLOSE:
	if (fsm->rfd != NULL) {
	    if (_fsm_debug && (stage & FSM_SYSCALL))
		rpmMessage(RPMMESS_DEBUG, " %8s (%p)\n", cur, fsm->rfd);
	    (void) rpmswAdd(rpmtsOp(fsmGetTs(fsm), RPMTS_OP_DIGEST),
			fdstat_op(fsm->rfd, FDSTAT_DIGEST));
	    (void) Fclose(fsm->rfd);
	    errno = saveerrno;
	}
	fsm->rfd = NULL;
	break;
    case FSM_WOPEN:
	fsm->wfd = Fopen(fsm->path, "w.fdio");
	if (fsm->wfd == NULL || Ferror(fsm->wfd)) {
	    if (fsm->wfd != NULL)	(void) fsmNext(fsm, FSM_WCLOSE);
	    fsm->wfd = NULL;
	    rc = CPIOERR_OPEN_FAILED;
	}
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (%s, \"w\") wfd %p wrbuf %p\n", cur,
		fsm->path, fsm->wfd, fsm->wrbuf);
	break;
    case FSM_WRITE:
	fsm->wrnb = Fwrite(fsm->wrbuf, sizeof(*fsm->wrbuf), fsm->rdnb, fsm->wfd);
	if (_fsm_debug && (stage & FSM_SYSCALL))
	    rpmMessage(RPMMESS_DEBUG, " %8s (wrbuf, %d, wfd)\twrnb %d\n",
		cur, (int)fsm->rdnb, (int)fsm->wrnb);
	if (fsm->rdnb != fsm->wrnb || Ferror(fsm->wfd))
	    rc = CPIOERR_WRITE_FAILED;
	break;
    case FSM_WCLOSE:
	if (fsm->wfd != NULL) {
	    if (_fsm_debug && (stage & FSM_SYSCALL))
		rpmMessage(RPMMESS_DEBUG, " %8s (%p)\n", cur, fsm->wfd);
	    (void) rpmswAdd(rpmtsOp(fsmGetTs(fsm), RPMTS_OP_DIGEST),
			fdstat_op(fsm->wfd, FDSTAT_DIGEST));
	    (void) Fclose(fsm->wfd);
	    errno = saveerrno;
	}
	fsm->wfd = NULL;
	break;

    default:
	break;
    }

    if (!(stage & FSM_INTERNAL)) {
	fsm->rc = (rc == CPIOERR_HDR_TRAILER ? 0 : rc);
    }
    return rc;
}
/*@=compmempass@*/

/*@observer@*/ const char * fileActionString(fileAction a)
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
    case FA_SKIPCOLOR:	return "skipcolor";
    default:		return "???";
    }
    /*@notreached@*/
}

/*@observer@*/ const char * fileStageString(fileStage a) {
    switch(a) {
    case FSM_UNKNOWN:	return "unknown";

    case FSM_PKGINSTALL:return "INSTALL";
    case FSM_PKGERASE:	return "ERASE";
    case FSM_PKGBUILD:	return "BUILD";
    case FSM_PKGCOMMIT:	return "COMMIT";
    case FSM_PKGUNDO:	return "UNDO";

    case FSM_CREATE:	return "create";
    case FSM_INIT:	return "init";
    case FSM_MAP:	return "map";
    case FSM_MKDIRS:	return "mkdirs";
    case FSM_RMDIRS:	return "rmdirs";
    case FSM_PRE:	return "pre";
    case FSM_PROCESS:	return "process";
    case FSM_POST:	return "post";
    case FSM_MKLINKS:	return "mklinks";
    case FSM_NOTIFY:	return "notify";
    case FSM_UNDO:	return "undo";
    case FSM_FINI:	return "fini";
    case FSM_COMMIT:	return "commit";
    case FSM_DESTROY:	return "destroy";
    case FSM_VERIFY:	return "verify";

    case FSM_UNLINK:	return "Unlink";
    case FSM_RENAME:	return "Rename";
    case FSM_MKDIR:	return "Mkdir";
    case FSM_RMDIR:	return "rmdir";
    case FSM_LSETFCON:	return "lsetfcon";
    case FSM_CHOWN:	return "chown";
    case FSM_LCHOWN:	return "lchown";
    case FSM_CHMOD:	return "chmod";
    case FSM_UTIME:	return "utime";
    case FSM_SYMLINK:	return "symlink";
    case FSM_LINK:	return "Link";
    case FSM_MKFIFO:	return "mkfifo";
    case FSM_MKNOD:	return "mknod";
    case FSM_LSTAT:	return "Lstat";
    case FSM_STAT:	return "Stat";
    case FSM_READLINK:	return "Readlink";
    case FSM_CHROOT:	return "chroot";

    case FSM_NEXT:	return "next";
    case FSM_EAT:	return "eat";
    case FSM_POS:	return "pos";
    case FSM_PAD:	return "pad";
    case FSM_TRAILER:	return "trailer";
    case FSM_HREAD:	return "hread";
    case FSM_HWRITE:	return "hwrite";
    case FSM_DREAD:	return "Fread";
    case FSM_DWRITE:	return "Fwrite";

    case FSM_ROPEN:	return "Fopen";
    case FSM_READ:	return "Fread";
    case FSM_RCLOSE:	return "Fclose";
    case FSM_WOPEN:	return "Fopen";
    case FSM_WRITE:	return "Fwrite";
    case FSM_WCLOSE:	return "Fclose";

    default:		return "???";
    }
    /*@noteached@*/
}
