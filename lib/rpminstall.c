/** \ingroup rpmcli
 * \file lib/rpminstall.c
 */

#include "system.h"

#include <rpmcli.h>

#include "rpmts.h"		/* XXX ts->rpmdb */

#include "manifest.h"
#include "misc.h"	/* XXX for rpmGlob() */
#include "debug.h"

/*@access rpmTransactionSet @*/	/* XXX compared with NULL, ts->rpmdb */
/*@access rpmProblemSet @*/	/* XXX compared with NULL */
/*@access Header @*/		/* XXX compared with NULL */
/*@access rpmdb @*/		/* XXX compared with NULL */
/*@access FD_t @*/		/* XXX compared with NULL */
/*@access IDTX @*/
/*@access IDT @*/

/*@unchecked@*/
static int hashesPrinted = 0;

/*@unchecked@*/
int packagesTotal = 0;
/*@unchecked@*/
static int progressTotal = 0;
/*@unchecked@*/
static int progressCurrent = 0;

/**
 */
static void printHash(const unsigned long amount, const unsigned long total)
	/*@globals hashesPrinted, progressCurrent, fileSystem @*/
	/*@modifies hashesPrinted, progressCurrent, fileSystem @*/
{
    int hashesNeeded;
    int hashesTotal = 50;

    if (isatty (STDOUT_FILENO))
	hashesTotal = 44;

    if (hashesPrinted != hashesTotal) {
	hashesNeeded = hashesTotal * (total ? (((float) amount) / total) : 1);
	while (hashesNeeded > hashesPrinted) {
	    if (isatty (STDOUT_FILENO)) {
		int i;
		for (i = 0; i < hashesPrinted; i++) (void) putchar ('#');
		for (; i < hashesTotal; i++) (void) putchar (' ');
		fprintf(stdout, "(%3d%%)",
			(int)(100 * (total ? (((float) amount) / total) : 1)));
		for (i = 0; i < (hashesTotal + 6); i++) (void) putchar ('\b');
	    } else
		fprintf(stdout, "#");

	    hashesPrinted++;
	}
	(void) fflush(stdout);
	hashesPrinted = hashesNeeded;

	if (hashesPrinted == hashesTotal) {
	    int i;
	    progressCurrent++;
	    if (isatty(STDOUT_FILENO)) {
	        for (i = 1; i < hashesPrinted; i++) (void) putchar ('#');
		fprintf(stdout, " [%3d%%]", (int)(100 * (progressTotal ?
			(((float) progressCurrent) / progressTotal) : 1)));
	    }
	    fprintf(stdout, "\n");
	}
	(void) fflush(stdout);
    }
}

void * rpmShowProgress(/*@null@*/ const void * arg,
			const rpmCallbackType what,
			const unsigned long amount,
			const unsigned long total,
			/*@null@*/ fnpyKey key,
			/*@null@*/ void * data)
	/*@globals hashesPrinted, progressCurrent, progressTotal,
		fileSystem @*/
	/*@modifies hashesPrinted, progressCurrent, progressTotal,
		fileSystem @*/
{
    /*@-castexpose@*/
    Header h = (Header) arg;
    /*@=castexpose@*/
    char * s;
    int flags = (int) ((long)data);
    void * rc = NULL;
    /*@-assignexpose -abstract @*/
    const char * filename = (const char *)key;
    /*@=assignexpose =abstract @*/
    static FD_t fd = NULL;

    switch (what) {
    case RPMCALLBACK_INST_OPEN_FILE:
	if (filename == NULL || filename[0] == '\0')
	    return NULL;
	fd = Fopen(filename, "r.ufdio");
	/*@-type@*/ /* FIX: still necessary? */
	if (fd)
	    fd = fdLink(fd, "persist (showProgress)");
	/*@=type@*/
	return fd;
	/*@notreached@*/ break;

    case RPMCALLBACK_INST_CLOSE_FILE:
	/*@-type@*/ /* FIX: still necessary? */
	fd = fdFree(fd, "persist (showProgress)");
	/*@=type@*/
	if (fd) {
	    (void) Fclose(fd);
	    fd = NULL;
	}
	break;

    case RPMCALLBACK_INST_START:
	hashesPrinted = 0;
	if (h == NULL || !(flags & INSTALL_LABEL))
	    break;
	if (flags & INSTALL_HASH) {
	    s = headerSprintf(h, "%{NAME}",
				rpmTagTable, rpmHeaderFormats, NULL);
	    if (isatty (STDOUT_FILENO))
		fprintf(stdout, "%4d:%-23.23s", progressCurrent + 1, s);
	    else
		fprintf(stdout, "%-28.28s", s);
	    (void) fflush(stdout);
	    s = _free(s);
	} else {
	    s = headerSprintf(h, "%{NAME}-%{VERSION}-%{RELEASE}",
				  rpmTagTable, rpmHeaderFormats, NULL);
	    fprintf(stdout, "%s\n", s);
	    (void) fflush(stdout);
	    s = _free(s);
	}
	break;

    case RPMCALLBACK_TRANS_PROGRESS:
    case RPMCALLBACK_INST_PROGRESS:
	if (flags & INSTALL_PERCENT)
	    fprintf(stdout, "%%%% %f\n", (double) (total
				? ((((float) amount) / total) * 100)
				: 100.0));
	else if (flags & INSTALL_HASH)
	    printHash(amount, total);
	(void) fflush(stdout);
	break;

    case RPMCALLBACK_TRANS_START:
	hashesPrinted = 0;
	progressTotal = 1;
	progressCurrent = 0;
	if (!(flags & INSTALL_LABEL))
	    break;
	if (flags & INSTALL_HASH)
	    fprintf(stdout, "%-28s", _("Preparing..."));
	else
	    fprintf(stdout, "%s\n", _("Preparing packages for installation..."));
	(void) fflush(stdout);
	break;

    case RPMCALLBACK_TRANS_STOP:
	if (flags & INSTALL_HASH)
	    printHash(1, 1);	/* Fixes "preparing..." progress bar */
	progressTotal = packagesTotal;
	progressCurrent = 0;
	break;

    case RPMCALLBACK_UNINST_PROGRESS:
    case RPMCALLBACK_UNINST_START:
    case RPMCALLBACK_UNINST_STOP:
    case RPMCALLBACK_UNPACK_ERROR:
    case RPMCALLBACK_CPIO_ERROR:
	/* ignore */
	break;
    }

    return rc;
}	

typedef /*@only@*/ /*@null@*/ const char * str_t;

struct rpmEIU {
    Header h;
    FD_t fd;
    int numFailed;
    int numPkgs;
/*@only@*/
    str_t * pkgURL;
/*@dependent@*/ /*@null@*/
    str_t * fnp;
/*@only@*/
    char * pkgState;
    int prevx;
    int pkgx;
    int numRPMS;
    int numSRPMS;
/*@only@*/ /*@null@*/
    str_t * sourceURL;
    int isSource;
    int argc;
/*@only@*/ /*@null@*/
    str_t * argv;
/*@dependent@*/
    rpmRelocation * relocations;
    rpmRC rpmrc;
};

/** @todo Generalize --freshen policies. */
int rpmInstall(rpmTransactionSet ts,
		struct rpmInstallArguments_s * ia,
		const char ** fileArgv)
{
    struct rpmEIU * eiu = memset(alloca(sizeof(*eiu)), 0, sizeof(*eiu));
    rpmProblemSet ps;
    rpmprobFilterFlags probFilter;
    rpmRelocation * relocations;
/*@only@*/ /*@null@*/ const char * fileURL = NULL;
    int stopInstall = 0;
    const char ** av = NULL;
    int ac = 0;
    int rc;
    int xx;
    int i;

    if (fileArgv == NULL) goto exit;

    (void) rpmtsSetFlags(ts, ia->transFlags);
    probFilter = ia->probFilter;
    relocations = ia->relocations;

    ts->goal = TSM_INSTALL;
    ts->nodigests = (ia->qva_flags & VERIFY_DIGEST);
    ts->nosignatures = (ia->qva_flags & VERIFY_SIGNATURE);

    ts->dbmode = (rpmtsGetFlags(ts) & RPMTRANS_FLAG_TEST)
		? O_RDONLY : (O_RDWR|O_CREAT);

    {	int notifyFlags;
	notifyFlags = ia->installInterfaceFlags | (rpmIsVerbose() ? INSTALL_LABEL : 0 );
	xx = rpmtsSetNotifyCallback(ts,
			rpmShowProgress, (void *) ((long)notifyFlags));
    }

    if ((eiu->relocations = relocations) != NULL) {
	while (eiu->relocations->oldPath)
	    eiu->relocations++;
	if (eiu->relocations->newPath == NULL)
	    eiu->relocations = NULL;
    }

    /* Build fully globbed list of arguments in argv[argc]. */
    /*@-branchstate@*/
    /*@-temptrans@*/
    for (eiu->fnp = fileArgv; *eiu->fnp != NULL; eiu->fnp++) {
    /*@=temptrans@*/
	av = _free(av);	ac = 0;
	rc = rpmGlob(*eiu->fnp, &ac, &av);
	if (rc || ac == 0) continue;

	eiu->argv = xrealloc(eiu->argv, (eiu->argc+ac+1) * sizeof(*eiu->argv));
	memcpy(eiu->argv+eiu->argc, av, ac * sizeof(*av));
	eiu->argc += ac;
	eiu->argv[eiu->argc] = NULL;
    }
    /*@=branchstate@*/
    av = _free(av);	ac = 0;

restart:
    /* Allocate sufficient storage for next set of args. */
    if (eiu->pkgx >= eiu->numPkgs) {
	eiu->numPkgs = eiu->pkgx + eiu->argc;
	eiu->pkgURL = xrealloc(eiu->pkgURL,
			(eiu->numPkgs + 1) * sizeof(*eiu->pkgURL));
	memset(eiu->pkgURL + eiu->pkgx, 0,
			((eiu->argc + 1) * sizeof(*eiu->pkgURL)));
	eiu->pkgState = xrealloc(eiu->pkgState,
			(eiu->numPkgs + 1) * sizeof(*eiu->pkgState));
	memset(eiu->pkgState + eiu->pkgx, 0,
			((eiu->argc + 1) * sizeof(*eiu->pkgState)));
    }

    /* Retrieve next set of args, cache on local storage. */
    for (i = 0; i < eiu->argc; i++) {
	fileURL = _free(fileURL);
	fileURL = eiu->argv[i];
	eiu->argv[i] = NULL;

	switch (urlIsURL(fileURL)) {
	case URL_IS_FTP:
	case URL_IS_HTTP:
	{   const char *tfn;

	    if (rpmIsVerbose())
		fprintf(stdout, _("Retrieving %s\n"), fileURL);

	    {	char tfnbuf[64];
		const char * rootDir;
		rootDir = (ts->rootDir && *ts->rootDir) ? ts->rootDir : "";
		strcpy(tfnbuf, "rpm-xfer.XXXXXX");
		(void) mktemp(tfnbuf);
		tfn = rpmGenPath(rootDir, "%{_tmppath}/", tfnbuf);
	    }

	    /* XXX undefined %{name}/%{version}/%{release} here */
	    /* XXX %{_tmpdir} does not exist */
	    rpmMessage(RPMMESS_DEBUG, _(" ... as %s\n"), tfn);
	    rc = urlGetFile(fileURL, tfn);
	    if (rc < 0) {
		rpmMessage(RPMMESS_ERROR,
			_("skipping %s - transfer failed - %s\n"),
			fileURL, ftpStrerror(rc));
		eiu->numFailed++;
		eiu->pkgURL[eiu->pkgx] = NULL;
		tfn = _free(tfn);
		/*@switchbreak@*/ break;
	    }
	    eiu->pkgState[eiu->pkgx] = 1;
	    eiu->pkgURL[eiu->pkgx] = tfn;
	    eiu->pkgx++;
	}   /*@switchbreak@*/ break;
	case URL_IS_PATH:
	default:
	    eiu->pkgURL[eiu->pkgx] = fileURL;
	    fileURL = NULL;
	    eiu->pkgx++;
	    /*@switchbreak@*/ break;
	}
    }
    fileURL = _free(fileURL);

    if (eiu->numFailed) goto exit;

    /* Continue processing file arguments, building transaction set. */
    for (eiu->fnp = eiu->pkgURL+eiu->prevx;
	 *eiu->fnp != NULL;
	 eiu->fnp++, eiu->prevx++)
    {
	const char * fileName;

	rpmMessage(RPMMESS_DEBUG, "============== %s\n", *eiu->fnp);
	(void) urlPath(*eiu->fnp, &fileName);

	/* Try to read the header from a package file. */
	eiu->fd = Fopen(*eiu->fnp, "r.ufdio");
	if (eiu->fd == NULL || Ferror(eiu->fd)) {
	    rpmError(RPMERR_OPEN, _("open of %s failed: %s\n"), *eiu->fnp,
			Fstrerror(eiu->fd));
	    if (eiu->fd) {
		xx = Fclose(eiu->fd);
		eiu->fd = NULL;
	    }
	    eiu->numFailed++; *eiu->fnp = NULL;
	    continue;
	}

	ts->verify_legacy = 1;
	/*@-mustmod -nullstate @*/	/* LCL: segfault */
	eiu->rpmrc = rpmReadPackageFile(ts, eiu->fd, *eiu->fnp, &eiu->h);
	/*@=mustmod =nullstate @*/
	ts->verify_legacy = 0;
	eiu->isSource = headerIsEntry(eiu->h, RPMTAG_SOURCEPACKAGE);

	xx = Fclose(eiu->fd);
	eiu->fd = NULL;

	if (eiu->rpmrc == RPMRC_FAIL || eiu->rpmrc == RPMRC_SHORTREAD) {
	    eiu->numFailed++; *eiu->fnp = NULL;
	    continue;
	}

	if (eiu->isSource &&
		(eiu->rpmrc == RPMRC_OK || eiu->rpmrc == RPMRC_BADSIZE))
	{
	    rpmMessage(RPMMESS_DEBUG, "\tadded source package [%d]\n",
		eiu->numSRPMS);
	    eiu->sourceURL = xrealloc(eiu->sourceURL,
				(eiu->numSRPMS + 2) * sizeof(*eiu->sourceURL));
	    eiu->sourceURL[eiu->numSRPMS] = *eiu->fnp;
	    *eiu->fnp = NULL;
	    eiu->numSRPMS++;
	    eiu->sourceURL[eiu->numSRPMS] = NULL;
	    continue;
	}

	if (eiu->rpmrc == RPMRC_OK || eiu->rpmrc == RPMRC_BADSIZE) {

	    /* Open database RDWR for binary packages. */
	    /*@-nullstate@*/ /* FIX: ts->rootDir may be NULL? */
	    if (rpmtsOpenDB(ts, ts->dbmode)) {
		eiu->numFailed++;
		goto exit;
	    }
	    /*@=nullstate@*/ /* FIX: ts->rootDir may be NULL? */

	    if (eiu->relocations) {
		const char ** paths;
		int pft;
		int c;

		if (headerGetEntry(eiu->h, RPMTAG_PREFIXES, &pft,
				       (void **) &paths, &c) && (c == 1)) {
		    eiu->relocations->oldPath = xstrdup(paths[0]);
		    paths = headerFreeData(paths, pft);
		} else {
		    const char * name;
		    xx = headerNVR(eiu->h, &name, NULL, NULL);
		    rpmMessage(RPMMESS_ERROR,
			       _("package %s is not relocateable\n"), name);
		    eiu->numFailed++;
		    goto exit;
		    /*@notreached@*/
		}
	    }

	    /* On --freshen, verify package is installed and newer */
	    if (ia->installInterfaceFlags & INSTALL_FRESHEN) {
		rpmdbMatchIterator mi;
		const char * name;
		Header oldH;
		int count;

		xx = headerNVR(eiu->h, &name, NULL, NULL);
		/*@-nullstate@*/ /* FIX: ts->rootDir may be NULL? */
		mi = rpmtsInitIterator(ts, RPMTAG_NAME, name, 0);
		/*@=nullstate@*/ /* FIX: ts->rootDir may be NULL? */
		count = rpmdbGetIteratorCount(mi);
		while ((oldH = rpmdbNextIterator(mi)) != NULL) {
		    if (rpmVersionCompare(oldH, eiu->h) < 0)
			/*@innercontinue@*/ continue;
		    /* same or newer package already installed */
		    count = 0;
		    /*@innerbreak@*/ break;
		}
		mi = rpmdbFreeIterator(mi);
		if (count == 0) {
		    eiu->h = headerFree(eiu->h, "Install freshen");
		    continue;
		}
		/* Package is newer than those currently installed. */
	    }

	    /*@-nullstate@*/ /* FIX: ts->rootDir may be NULL? */
	    /*@-abstract@*/
	    rc = rpmtransAddPackage(ts, eiu->h, (fnpyKey)fileName,
			(ia->installInterfaceFlags & INSTALL_UPGRADE) != 0,
			relocations);
	    /*@=abstract@*/
	    /*@=nullstate@*/

	    /* XXX reference held by transaction set */
	    eiu->h = headerFree(eiu->h, "Install added");
	    if (eiu->relocations)
		eiu->relocations->oldPath = _free(eiu->relocations->oldPath);

	    switch(rc) {
	    case 0:
		rpmMessage(RPMMESS_DEBUG, "\tadded binary package [%d]\n",
			eiu->numRPMS);
		/*@switchbreak@*/ break;
	    case 1:
		rpmMessage(RPMMESS_ERROR,
			    _("error reading from file %s\n"), *eiu->fnp);
		eiu->numFailed++;
		goto exit;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    case 2:
		rpmMessage(RPMMESS_ERROR,
			    _("file %s requires a newer version of RPM\n"),
			    *eiu->fnp);
		eiu->numFailed++;
		goto exit;
		/*@notreached@*/ /*@switchbreak@*/ break;
	    }

	    eiu->numRPMS++;
	    continue;
	}

	if (eiu->rpmrc != RPMRC_BADMAGIC) {
	    rpmMessage(RPMMESS_ERROR, _("%s cannot be installed\n"), *eiu->fnp);
	    eiu->numFailed++; *eiu->fnp = NULL;
	    break;
	}

	/* Try to read a package manifest. */
	eiu->fd = Fopen(*eiu->fnp, "r.fpio");
	if (eiu->fd == NULL || Ferror(eiu->fd)) {
	    rpmError(RPMERR_OPEN, _("open of %s failed: %s\n"), *eiu->fnp,
			Fstrerror(eiu->fd));
	    if (eiu->fd) {
		xx = Fclose(eiu->fd);
		eiu->fd = NULL;
	    }
	    eiu->numFailed++; *eiu->fnp = NULL;
	    break;
	}

	/* Read list of packages from manifest. */
	rc = rpmReadPackageManifest(eiu->fd, &eiu->argc, &eiu->argv);
	if (rc)
	    rpmError(RPMERR_MANIFEST, _("%s: read manifest failed: %s\n"),
			*eiu->fnp, Fstrerror(eiu->fd));
	xx = Fclose(eiu->fd);
	eiu->fd = NULL;

	/* If successful, restart the query loop. */
	if (rc == 0) {
	    eiu->prevx++;
	    goto restart;
	}

	eiu->numFailed++; *eiu->fnp = NULL;
	break;
    }

    rpmMessage(RPMMESS_DEBUG, _("found %d source and %d binary packages\n"),
		eiu->numSRPMS, eiu->numRPMS);

    if (eiu->numFailed) goto exit;

    if (eiu->numRPMS && !(ia->installInterfaceFlags & INSTALL_NODEPS)) {

	/*@-nullstate@*/ /* FIX: ts->rootDir may be NULL? */
	if (rpmdepCheck(ts)) {
	    eiu->numFailed = eiu->numPkgs;
	    stopInstall = 1;
	}
	/*@=nullstate@*/

	ps = rpmtsGetProblems(ts);
	if (!stopInstall && ps) {
	    rpmMessage(RPMMESS_ERROR, _("Failed dependencies:\n"));
	    printDepProblems(stderr, ps);
	    eiu->numFailed = eiu->numPkgs;
	    stopInstall = 1;

	    /*@-branchstate@*/
	    if (ts->suggests != NULL && ts->nsuggests > 0) {
		rpmMessage(RPMMESS_NORMAL, _("    Suggested resolutions:\n"));
		for (i = 0; i < ts->nsuggests; i++) {
		    const char * str = ts->suggests[i];

		    if (str == NULL)
			break;

		    rpmMessage(RPMMESS_NORMAL, "\t%s\n", str);
		
		    ts->suggests[i] = NULL;
		    str = _free(str);
		}
		ts->suggests = _free(ts->suggests);
	    }
	    /*@=branchstate@*/
	}
	ps = rpmProblemSetFree(ps);
    }

    if (eiu->numRPMS && !(ia->installInterfaceFlags & INSTALL_NOORDER)) {
	/*@-nullstate@*/ /* FIX: ts->rootDir may be NULL? */
	if (rpmdepOrder(ts)) {
	    eiu->numFailed = eiu->numPkgs;
	    stopInstall = 1;
	}
	/*@=nullstate@*/
    }

    if (eiu->numRPMS && !stopInstall) {

	packagesTotal = eiu->numRPMS + eiu->numSRPMS;

	rpmMessage(RPMMESS_DEBUG, _("installing binary packages\n"));

	/*@-nullstate@*/ /* FIX: ts->rootDir may be NULL? */
	rc = rpmRunTransactions(ts, NULL, probFilter);
	/*@=nullstate@*/
	ps = rpmtsGetProblems(ts);

	if (rc < 0) {
	    eiu->numFailed += eiu->numRPMS;
	} else if (rc > 0 || ps) {
	    eiu->numFailed += rc;
	    rpmProblemSetPrint(stderr, ps);
	}
	ps = rpmProblemSetFree(ps);
    }

    if (eiu->numSRPMS && !stopInstall) {
	if (eiu->sourceURL != NULL)
	for (i = 0; i < eiu->numSRPMS; i++) {
	    if (eiu->sourceURL[i] == NULL) continue;
	    eiu->fd = Fopen(eiu->sourceURL[i], "r.ufdio");
	    if (eiu->fd == NULL || Ferror(eiu->fd)) {
		rpmMessage(RPMMESS_ERROR, _("cannot open file %s: %s\n"),
			   eiu->sourceURL[i], Fstrerror(eiu->fd));
		if (eiu->fd) {
		    xx = Fclose(eiu->fd);
		    eiu->fd = NULL;
		}
		continue;
	    }

	    if (!(rpmtsGetFlags(ts) & RPMTRANS_FLAG_TEST)) {
#if !defined(__LCLINT__) /* LCL: segfault */
		eiu->rpmrc = rpmInstallSourcePackage(ts, eiu->fd, NULL, NULL);
#endif
		if (eiu->rpmrc != RPMRC_OK) eiu->numFailed++;
	    }

	    xx = Fclose(eiu->fd);
	    eiu->fd = NULL;
	}
    }

exit:
    if (eiu->pkgURL != NULL)
    for (i = 0; i < eiu->numPkgs; i++) {
	if (eiu->pkgURL[i] == NULL) continue;
	if (eiu->pkgState[i] == 1)
	    (void) Unlink(eiu->pkgURL[i]);
	eiu->pkgURL[i] = _free(eiu->pkgURL[i]);
    }
    eiu->pkgState = _free(eiu->pkgState);
    eiu->pkgURL = _free(eiu->pkgURL);
    eiu->argv = _free(eiu->argv);

    return eiu->numFailed;
}

int rpmErase(rpmTransactionSet ts,
		const struct rpmInstallArguments_s * ia,
		const char ** argv)
{
    int count;
    const char ** arg;
    int numFailed = 0;
    int stopUninstall = 0;
    int numPackages = 0;
    rpmProblemSet ps;

    if (argv == NULL) return 0;

    (void) rpmtsSetFlags(ts, ia->transFlags);

#ifdef	NOTYET	/* XXX no callbacks on erase yet */
    {	int notifyFlags;
	notifyFlags = ia->eraseInterfaceFlags | (rpmIsVerbose() ? INSTALL_LABEL : 0 );
	xx = rpmtsSetNotifyCallback(ts,
			rpmShowProgress, (void *) ((long)notifyFlags)
    }
#endif

    ts->goal = TSM_ERASE;
    ts->nodigests = (ia->qva_flags & VERIFY_DIGEST);
    ts->nosignatures = (ia->qva_flags & VERIFY_SIGNATURE);

    /* XXX W2DO? O_EXCL??? */
    ts->dbmode = (rpmtsGetFlags(ts) & RPMTRANS_FLAG_TEST)
		? O_RDONLY : (O_RDWR|O_EXCL);

    (void) rpmtsOpenDB(ts, ts->dbmode);

    for (arg = argv; *arg; arg++) {
	rpmdbMatchIterator mi;

	/* XXX HACK to get rpmdbFindByLabel out of the API */
	mi = rpmtsInitIterator(ts, RPMDBI_LABEL, *arg, 0);
	count = rpmdbGetIteratorCount(mi);
	if (count <= 0) {
	    rpmMessage(RPMMESS_ERROR, _("package %s is not installed\n"), *arg);
	    numFailed++;
	} else if (!(count == 1 || (ia->eraseInterfaceFlags & UNINSTALL_ALLMATCHES))) {
	    rpmMessage(RPMMESS_ERROR, _("\"%s\" specifies multiple packages\n"),
			*arg);
	    numFailed++;
	} else {
	    Header h;	/* XXX iterator owns the reference */
	    while ((h = rpmdbNextIterator(mi)) != NULL) {
		unsigned int recOffset = rpmdbGetIteratorOffset(mi);
		if (recOffset) {
		    (void) rpmtransRemovePackage(ts, h, recOffset);
		    numPackages++;
		}
	    }
	}
	mi = rpmdbFreeIterator(mi);
    }

    if (!(ia->eraseInterfaceFlags & UNINSTALL_NODEPS)) {

	if (rpmdepCheck(ts)) {
	    numFailed = numPackages;
	    stopUninstall = 1;
	}

	ps = rpmtsGetProblems(ts);
	if (!stopUninstall && ps) {
	    rpmMessage(RPMMESS_ERROR, _("removing these packages would break "
			      "dependencies:\n"));
	    printDepProblems(stderr, ps);
	    numFailed += numPackages;
	    stopUninstall = 1;
	}
	ps = rpmProblemSetFree(ps);
    }

    if (!stopUninstall) {
	(void) rpmtsSetFlags(ts, (rpmtsGetFlags(ts) | RPMTRANS_FLAG_REVERSE));
	numFailed += rpmRunTransactions(ts, NULL, 0);
	ps = rpmtsGetProblems(ts);
	ps = rpmProblemSetFree(ps);
    }

    return numFailed;
}

int rpmInstallSource(rpmTransactionSet ts, const char * arg,
		const char ** specFilePtr, const char ** cookie)
{
    FD_t fd;
    int rc;

    fd = Fopen(arg, "r.ufdio");
    if (fd == NULL || Ferror(fd)) {
	rpmMessage(RPMMESS_ERROR, _("cannot open %s: %s\n"), arg, Fstrerror(fd));
	if (fd) (void) Fclose(fd);
	return 1;
    }

    if (rpmIsVerbose())
	fprintf(stdout, _("Installing %s\n"), arg);

    {
	rpmRC rpmrc = rpmInstallSourcePackage(ts, fd, specFilePtr, cookie);
	rc = (rpmrc == RPMRC_OK ? 0 : 1);
    }
    if (rc != 0) {
	rpmMessage(RPMMESS_ERROR, _("%s cannot be installed\n"), arg);
	/*@-unqualifiedtrans@*/
	if (specFilePtr && *specFilePtr)
	    *specFilePtr = _free(*specFilePtr);
	if (cookie && *cookie)
	    *cookie = _free(*cookie);
	/*@=unqualifiedtrans@*/
    }

    (void) Fclose(fd);

    return rc;
}

/*@unchecked@*/
static int reverse = -1;

/**
 */
static int IDTintcmp(const void * a, const void * b)
	/*@*/
{
    /*@-castexpose@*/
    return ( reverse * (((IDT)a)->val.u32 - ((IDT)b)->val.u32) );
    /*@=castexpose@*/
}

IDTX IDTXfree(IDTX idtx)
{
    if (idtx) {
	int i;
	if (idtx->idt)
	for (i = 0; i < idtx->nidt; i++) {
	    IDT idt = idtx->idt + i;
	    idt->h = headerFree(idt->h, "IDTXfree");
	    idt->key = _free(idt->key);
	}
	idtx->idt = _free(idtx->idt);
	idtx = _free(idtx);
    }
    return NULL;
}

IDTX IDTXnew(void)
{
    IDTX idtx = xcalloc(1, sizeof(*idtx));
    idtx->delta = 10;
    idtx->size = sizeof(*((IDT)0));
    return idtx;
}

IDTX IDTXgrow(IDTX idtx, int need)
{
    if (need < 0) return NULL;
    if (idtx == NULL)
	idtx = IDTXnew();
    if (need == 0) return idtx;

    if ((idtx->nidt + need) > idtx->alloced) {
	while (need > 0) {
	    idtx->alloced += idtx->delta;
	    need -= idtx->delta;
	}
	idtx->idt = xrealloc(idtx->idt, (idtx->alloced * idtx->size) );
    }
    return idtx;
}

IDTX IDTXsort(IDTX idtx)
{
    if (idtx != NULL && idtx->idt != NULL && idtx->nidt > 0)
	qsort(idtx->idt, idtx->nidt, idtx->size, IDTintcmp);
    return idtx;
}

IDTX IDTXload(rpmTransactionSet ts, rpmTag tag)
{
    IDTX idtx = NULL;
    rpmdbMatchIterator mi;
    HGE_t hge = (HGE_t) headerGetEntry;
    Header h;

    /*@-branchstate@*/
    mi = rpmtsInitIterator(ts, tag, NULL, 0);
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	rpmTagType type = RPM_NULL_TYPE;
	int_32 count = 0;
	int_32 * tidp;

	tidp = NULL;
	if (!hge(h, tag, &type, (void **)&tidp, &count) || tidp == NULL)
	    continue;

	if (type == RPM_INT32_TYPE && (*tidp == 0 || *tidp == -1))
	    continue;

	idtx = IDTXgrow(idtx, 1);
	if (idtx == NULL)
	    continue;
	if (idtx->idt == NULL)
	    continue;

	{   IDT idt;
	    /*@-nullderef@*/
	    idt = idtx->idt + idtx->nidt;
	    /*@=nullderef@*/
	    idt->h = headerLink(h, "IDTXload idt->h");
	    idt->key = NULL;
	    idt->instance = rpmdbGetIteratorOffset(mi);
	    idt->val.u32 = *tidp;
	}
	idtx->nidt++;
    }
    mi = rpmdbFreeIterator(mi);
    /*@=branchstate@*/

    return IDTXsort(idtx);
}

IDTX IDTXglob(rpmTransactionSet ts, const char * globstr, rpmTag tag)
{
    IDTX idtx = NULL;
    HGE_t hge = (HGE_t) headerGetEntry;
    Header h;
    int_32 * tidp;
    FD_t fd;
    const char ** av = NULL;
    int ac = 0;
    int rc;
    int xx;
    int i;

    av = NULL;	ac = 0;
    rc = rpmGlob(globstr, &ac, &av);

    if (rc == 0)
    for (i = 0; i < ac; i++) {
	rpmTagType type;
	int_32 count;
	int isSource;
	rpmRC rpmrc;

	fd = Fopen(av[i], "r.ufdio");
	if (fd == NULL || Ferror(fd)) {
            rpmError(RPMERR_OPEN, _("open of %s failed: %s\n"), av[i],
                        Fstrerror(fd));
	    if (fd) (void) Fclose(fd);
	    continue;
	}

	/*@-mustmod@*/	/* LCL: segfault */
	xx = rpmReadPackageFile(ts, fd, av[i], &h);
	/*@=mustmod@*/
	rpmrc = (xx ? RPMRC_FAIL : RPMRC_OK);		/* XXX HACK */
	isSource = headerIsEntry(h, RPMTAG_SOURCEPACKAGE);

	if (rpmrc != RPMRC_OK || isSource) {
	    (void) Fclose(fd);
	    continue;
	}

	tidp = NULL;
	/*@-branchstate@*/
	if (hge(h, tag, &type, (void **) &tidp, &count) && tidp) {

	    idtx = IDTXgrow(idtx, 1);
	    if (idtx == NULL || idtx->idt == NULL) {
		h = headerFree(h, "IDTXglob skip");
		(void) Fclose(fd);
		continue;
	    }
	    {	IDT idt;
		idt = idtx->idt + idtx->nidt;
		idt->h = headerLink(h, "IDTXglob idt->h");
		idt->key = av[i];
		av[i] = NULL;
		idt->instance = 0;
		idt->val.u32 = *tidp;
	    }
	    idtx->nidt++;
	}
	/*@=branchstate@*/

	h = headerFree(h, "IDTXglob next");
	(void) Fclose(fd);
    }

    for (i = 0; i < ac; i++)
	av[i] = _free(av[i]);
    av = _free(av);	ac = 0;

    return idtx;
}

/** @todo Transaction handling, more, needs work. */
int rpmRollback(rpmTransactionSet ts,
		/*@unused@*/ struct rpmInstallArguments_s * ia,
		const char ** argv)
{
#ifdef	NOTYET
    rpmdb db = NULL;
    rpmTransactionSet ts = NULL;
    rpmProblemSet ps;
    int ifmask= (INSTALL_UPGRADE|INSTALL_FRESHEN|INSTALL_INSTALL|INSTALL_ERASE);
    unsigned thistid = 0xffffffff;
    unsigned prevtid;
    time_t tid;
#endif
    IDTX itids = NULL;
    IDTX rtids = NULL;
    IDT rp;
    int nrids = 0;
    IDT ip;
    int niids = 0;
    int rc = 0;

    if (argv != NULL && *argv != NULL) {
	rc = -1;
	goto exit;
    }

    itids = IDTXload(ts, RPMTAG_INSTALLTID);
    if (itids != NULL) {
	ip = itids->idt;
	niids = itids->nidt;
    } else {
	ip = NULL;
	niids = 0;
    }

    {	const char * globstr = rpmExpand("%{_repackage_dir}/*.rpm", NULL);
	if (globstr == NULL || *globstr == '%') {
	    globstr = _free(globstr);
	    rc = -1;
	    goto exit;
	}
	rtids = IDTXglob(ts, globstr, RPMTAG_REMOVETID);
	if (rtids != NULL) {
	    rp = rtids->idt;
	    nrids = rtids->nidt;
	} else {
	    rp = NULL;
	    nrids = 0;
	}
	globstr = _free(globstr);
    }

#ifdef	NOTYET
    {	int notifyFlags;
	notifyFlags = ia->installInterfaceFlags | (rpmIsVerbose() ? INSTALL_LABEL : 0 );
	xx = rpmtsSetNotifyCallback(ts,
			rpmShowProgress, (void *) ((long)notifyFlags));
    }

    (void) rpmtsSetFlags(ts, ia->transFlags);

    /* Run transactions until rollback goal is achieved. */
    do {
	prevtid = thistid;
	rc = 0;
	packagesTotal = 0;
	ia->installInterfaceFlags &= ~ifmask;

	/* Find larger of the remaining install/erase transaction id's. */
	thistid = 0;
	if (ip != NULL && ip->val.u32 > thistid)
	    thistid = ip->val.u32;
	if (rp != NULL && rp->val.u32 > thistid)
	    thistid = rp->val.u32;

	/* If we've achieved the rollback goal, then we're done. */
	if (thistid == 0 || thistid < ia->rbtid)
	    break;

	/* Install the previously erased packages for this transaction. */
	while (rp != NULL && rp->val.u32 == thistid) {

	    rpmMessage(RPMMESS_DEBUG, "\t+++ %s\n", rp->key);

	    rc = rpmtransAddPackage(ts, rp->h, (fnpyKey)rp->key,
			       0, ia->relocations);
	    if (rc != 0)
		goto exit;

	    packagesTotal++;
	    if (!(ia->installInterfaceFlags & ifmask))
		ia->installInterfaceFlags |= INSTALL_UPGRADE;

#ifdef	NOTYET
	    rp->h = headerFree(rp->h);
#endif
	    nrids--;
	    if (nrids > 0)
		rp++;
	    else
		rp = NULL;
	}

	/* Erase the previously installed packages for this transaction. */
	while (ip != NULL && ip->val.u32 == thistid) {

	    rpmMessage(RPMMESS_DEBUG,
			"\t--- rpmdb instance #%u\n", ip->instance);

	    rc = rpmtransRemovePackage(ts, ip->instance);
	    if (rc != 0)
		goto exit;

	    packagesTotal++;
	    if (!(ia->installInterfaceFlags & ifmask))
		ia->installInterfaceFlags |= INSTALL_ERASE;

#ifdef	NOTYET
	    ip->instance = 0;
#endif
	    niids--;
	    if (niids > 0)
		ip++;
	    else
		ip = NULL;
	}

	/* Anything to do? */
	if (packagesTotal <= 0)
	    break;

	tid = (time_t)thistid;
	rpmMessage(RPMMESS_DEBUG, _("rollback %d packages to %s"),
			packagesTotal, ctime(&tid));

	rc = rpmdepCheck(ts);
	ps = rpmtsGetProblems(ts);
	if (rc != 0 && ps) {
	    rpmMessage(RPMMESS_ERROR, _("Failed dependencies:\n"));
	    printDepProblems(stderr, ps);
	    ps = rpmProblemSetFree(ps);
	    goto exit;
	}
	ps = rpmProblemSetFree(ps);

	rc = rpmdepOrder(ts);
	if (rc != 0)
	    goto exit;

	rc = rpmRunTransactions(ts, NULL,
		(ia->probFilter|RPMPROB_FILTER_OLDPACKAGE));
	ps = rpmtsGetProblems(ts);
	if (rc > 0)
	    rpmProblemSetPrint(stderr, ps);
	ps = rpmProblemSetFree(ps);
	if (rc)
	    goto exit;

	/* Clean up after successful rollback. */
	if (!rpmIsDebug()) {
	    int i;
	    for (i = 0; i < rtids->nidt; i++) {
		IDT rrp = rtids->idt + i;
		if (rrp->val.u32 != thistid)
		    continue;
		(void) unlink(rrp->key);
	    }
	}

    } while (1);
#endif

exit:

    rtids = IDTXfree(rtids);
    itids = IDTXfree(itids);

    return rc;
}
