/**
 * \file lib/problems.c
 */

#include "system.h"

#include <rpmlib.h>

#include "depends.h"
#include "rpmal.h"
#include "misc.h"
#include "debug.h"

/*@access Header@*/
/*@access rpmProblemSet@*/
/*@access rpmProblem@*/
/*@access rpmDependencyConflict@*/
/*@access availablePackage@*/

rpmProblemSet rpmProblemSetCreate(void)
{
    rpmProblemSet probs;

    probs = xcalloc(1, sizeof(*probs));	/* XXX memory leak */
    probs->numProblems = probs->numProblemsAlloced = 0;
    probs->probs = NULL;

    return probs;
}

void rpmProblemSetFree(rpmProblemSet tsprobs)
{
    int i;

    for (i = 0; i < tsprobs->numProblems; i++) {
	rpmProblem p = tsprobs->probs + i;
	p->h = headerFree(p->h);
	p->pkgNEVR = _free(p->pkgNEVR);
	p->altNEVR = _free(p->altNEVR);
	p->str1 = _free(p->str1);
    }
    tsprobs = _free(tsprobs);
}

void rpmProblemSetAppend(rpmProblemSet tsprobs, rpmProblemType type,
		const availablePackage alp,
		const char * dn, const char * bn,
		Header altH, unsigned long ulong1)
{
    rpmProblem p;
    char *t;

    if (tsprobs->numProblems == tsprobs->numProblemsAlloced) {
	if (tsprobs->numProblemsAlloced)
	    tsprobs->numProblemsAlloced *= 2;
	else
	    tsprobs->numProblemsAlloced = 2;
	tsprobs->probs = xrealloc(tsprobs->probs,
			tsprobs->numProblemsAlloced * sizeof(*tsprobs->probs));
    }

    p = tsprobs->probs + tsprobs->numProblems;
    tsprobs->numProblems++;
    memset(p, 0, sizeof(*p));
    p->type = type;
    /*@-assignexpose@*/
    p->key = alp->key;
    /*@=assignexpose@*/
    p->ulong1 = ulong1;
    p->ignoreProblem = 0;
    p->str1 = NULL;
    p->h = NULL;
    p->pkgNEVR = NULL;
    p->altNEVR = NULL;

    if (dn != NULL || bn != NULL) {
	t = xcalloc(1,	(dn != NULL ? strlen(dn) : 0) +
			(bn != NULL ? strlen(bn) : 0) + 1);
	p->str1 = t;
	if (dn != NULL) t = stpcpy(t, dn);
	if (bn != NULL) t = stpcpy(t, bn);
    }

    if (alp != NULL) {
	p->h = headerLink(alp->h);
	t = xcalloc(1,	strlen(alp->name) +
			strlen(alp->version) +
			strlen(alp->release) + sizeof("--"));
	p->pkgNEVR = t;
	t = stpcpy(t, alp->name);
	t = stpcpy(t, "-");
	t = stpcpy(t, alp->version);
	t = stpcpy(t, "-");
	t = stpcpy(t, alp->release);
    }

    if (altH != NULL) {
	const char * n, * v, * r;
	(void) headerNVR(altH, &n, &v, &r);
	t = xcalloc(1, strlen(n) + strlen(v) + strlen(r) + sizeof("--"));
	p->altNEVR = t;
	t = stpcpy(t, n);
	t = stpcpy(t, "-");
	t = stpcpy(t, v);
	t = stpcpy(t, "-");
	t = stpcpy(t, r);
    }
}

#define XSTRCMP(a, b) ((!(a) && !(b)) || ((a) && (b) && !strcmp((a), (b))))

int rpmProblemSetTrim(rpmProblemSet tsprobs, rpmProblemSet filter)
{
    rpmProblem t;
    rpmProblem f;
    int gotProblems = 0;

    if (tsprobs == NULL || tsprobs->numProblems == 0)
	return 0;

    if (filter == NULL)
	return (tsprobs->numProblems == 0 ? 0 : 1);

    t = tsprobs->probs;
    f = filter->probs;

    /*@-branchstate@*/
    while ((f - filter->probs) < filter->numProblems) {
	if (!f->ignoreProblem) {
	    f++;
	    continue;
	}
	while ((t - tsprobs->probs) < tsprobs->numProblems) {
	    /*@-nullpass@*/	/* LCL: looks good to me */
	    if (f->h == t->h && f->type == t->type && t->key == f->key &&
		     XSTRCMP(f->str1, t->str1))
		/*@innerbreak@*/ break;
	    /*@=nullpass@*/
	    t++;
	    gotProblems = 1;
	}

	/* XXX This can't happen, but let's be sane in case it does. */
	if ((t - tsprobs->probs) == tsprobs->numProblems)
	    break;

	t->ignoreProblem = f->ignoreProblem;
	t++, f++;
    }
    /*@=branchstate@*/

    if ((t - tsprobs->probs) < tsprobs->numProblems)
	gotProblems = 1;

    return gotProblems;
}

/* XXX FIXME: merge into problems */
/* XXX used in verify.c rpmlibprov.c */
void printDepFlags(FILE * fp, const char * version, int flags)
{
    if (flags)
	fprintf(fp, " ");

    if (flags & RPMSENSE_LESS) 
	fprintf(fp, "<");
    if (flags & RPMSENSE_GREATER)
	fprintf(fp, ">");
    if (flags & RPMSENSE_EQUAL)
	fprintf(fp, "=");

    if (flags)
	fprintf(fp, " %s", version);
}

static int sameProblem(const rpmDependencyConflict ap,
		const rpmDependencyConflict bp)
	/*@*/
{

    if (ap->sense != bp->sense)
	return 1;

    if (ap->byName && bp->byName && strcmp(ap->byName, bp->byName))
	return 1;
    if (ap->byVersion && bp->byVersion && strcmp(ap->byVersion, bp->byVersion))
	return 1;
    if (ap->byRelease && bp->byRelease && strcmp(ap->byRelease, bp->byRelease))
	return 1;

    if (ap->needsName && bp->needsName && strcmp(ap->needsName, bp->needsName))
	return 1;
    if (ap->needsVersion && bp->needsVersion && strcmp(ap->needsVersion, bp->needsVersion))
	return 1;
    if (ap->needsFlags && bp->needsFlags && ap->needsFlags != bp->needsFlags)
	return 1;

    return 0;
}

/* XXX FIXME: merge into problems */
void printDepProblems(FILE * fp,
		const rpmDependencyConflict conflicts, int numConflicts)
{
    int i;

    for (i = 0; i < numConflicts; i++) {
	int j;

	/* Filter already displayed problems. */
	for (j = 0; j < i; j++) {
	    if (!sameProblem(conflicts + i, conflicts + j))
		/*@innerbreak@*/ break;
	}
	if (j < i)
	    continue;

	fprintf(fp, "\t%s", conflicts[i].needsName);
	if (conflicts[i].needsFlags)
	    printDepFlags(fp, conflicts[i].needsVersion, 
			  conflicts[i].needsFlags);

	if (conflicts[i].sense == RPMDEP_SENSE_REQUIRES) 
	    fprintf(fp, _(" is needed by %s-%s-%s\n"), conflicts[i].byName, 
		    conflicts[i].byVersion, conflicts[i].byRelease);
	else
	    fprintf(fp, _(" conflicts with %s-%s-%s\n"), conflicts[i].byName, 
		    conflicts[i].byVersion, conflicts[i].byRelease);
    }
}

#if !defined(HAVE_VSNPRINTF)
/*@-shadow -bufferoverflowhigh @*/
static inline int vsnprintf(/*@out@*/ char * buf, /*@unused@*/ int nb,
	const char * fmt, va_list ap)
{
    return vsprintf(buf, fmt, ap);
}
/*@=shadow =bufferoverflowhigh @*/
#endif
#if !defined(HAVE_SNPRINTF)
static inline int snprintf(/*@out@*/ char * buf, int nb, const char * fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    /*@=modunconnomods@*/
    rc = vsnprintf(buf, nb, fmt, ap);
    /*@=modunconnomods@*/
    va_end(ap);
    return rc;
}
#endif

const char * rpmProblemString(const rpmProblem prob)
{
/*@observer@*/ const char * pkgNEVR = (prob->pkgNEVR ? prob->pkgNEVR : "");
/*@observer@*/ const char * altNEVR = (prob->altNEVR ? prob->altNEVR : "");
/*@observer@*/ const char * str1 = (prob->str1 ? prob->str1 : "");
    int nb =	strlen(pkgNEVR) + strlen(str1) + strlen(altNEVR) + 100;
    char * buf = xmalloc(nb+1);
    int rc;

    switch (prob->type) {
    case RPMPROB_BADARCH:
	rc = snprintf(buf, nb,
		_("package %s is for a different architecture"),
		pkgNEVR);
	break;
    case RPMPROB_BADOS:
	rc = snprintf(buf, nb,
		_("package %s is for a different operating system"),
		pkgNEVR);
	break;
    case RPMPROB_PKG_INSTALLED:
	rc = snprintf(buf, nb,
		_("package %s is already installed"),
		pkgNEVR);
	break;
    case RPMPROB_BADRELOCATE:
	rc = snprintf(buf, nb,
		_("path %s in package %s is not relocateable"),
		str1, pkgNEVR);
	break;
    case RPMPROB_NEW_FILE_CONFLICT:
	rc = snprintf(buf, nb,
		_("file %s conflicts between attempted installs of %s and %s"),
		str1, pkgNEVR, altNEVR);
	break;
    case RPMPROB_FILE_CONFLICT:
	rc = snprintf(buf, nb,
	    _("file %s from install of %s conflicts with file from package %s"),
		str1, pkgNEVR, altNEVR);
	break;
    case RPMPROB_OLDPACKAGE:
	rc = snprintf(buf, nb,
		_("package %s (which is newer than %s) is already installed"),
		altNEVR, pkgNEVR);
	break;
    case RPMPROB_DISKSPACE:
	rc = snprintf(buf, nb,
	    _("installing package %s needs %ld%cb on the %s filesystem"),
		pkgNEVR,
		prob->ulong1 > (1024*1024)
		    ? (prob->ulong1 + 1024 * 1024 - 1) / (1024 * 1024)
		    : (prob->ulong1 + 1023) / 1024,
		prob->ulong1 > (1024*1024) ? 'M' : 'K',
		str1);
	break;
    case RPMPROB_DISKNODES:
	rc = snprintf(buf, nb,
	    _("installing package %s needs %ld inodes on the %s filesystem"),
		pkgNEVR, (long)prob->ulong1, str1);
	break;
    case RPMPROB_BADPRETRANS:
	rc = snprintf(buf, nb,
		_("package %s pre-transaction syscall(s): %s failed: %s"),
		pkgNEVR, str1, strerror(prob->ulong1));
	break;
    case RPMPROB_REQUIRES:
    case RPMPROB_CONFLICT:
    default:
	rc = snprintf(buf, nb,
		_("unknown error %d encountered while manipulating package %s"),
		prob->type, pkgNEVR);
	break;
    }

    buf[nb] = '\0';
    return buf;
}

void rpmProblemPrint(FILE *fp, rpmProblem prob)
{
    const char * msg = rpmProblemString(prob);
    fprintf(fp, "%s\n", msg);
    msg = _free(msg);
}

void rpmProblemSetPrint(FILE *fp, rpmProblemSet tsprobs)
{
    int i;

    if (tsprobs == NULL)
	return;

    if (fp == NULL)
	fp = stderr;

    for (i = 0; i < tsprobs->numProblems; i++) {
	rpmProblem myprob = tsprobs->probs + i;
	if (!myprob->ignoreProblem)
	    rpmProblemPrint(fp, myprob);
    }
}
