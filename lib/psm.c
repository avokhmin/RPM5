/** \ingroup rpmts payload
 * \file lib/psm.c
 * Package state machine to handle a package from a transaction set.
 */

#include "system.h"

#include <rpmio_internal.h>
#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>
#include <rpmlua.h>

#include "cpio.h"
#include "fsm.h"		/* XXX CPIO_FOO/FSM_FOO constants */
#include "psm.h"

#define	_RPMEVR_INTERNAL
#include "rpmds.h"

#define _RPMFI_INTERNAL
#include "rpmfi.h"

#define	_RPMTE_INTERNAL
#include "rpmte.h"

#define	_RPMTS_INTERNAL		/* XXX ts->notify */
#include "rpmts.h"

#include <pkgio.h>
#include "signature.h"		/* signature constants */
#include "misc.h"		/* XXX stripTrailingChar() */
#include "rpmdb.h"		/* XXX for db_chrootDone */
#include "debug.h"

#define	_PSM_DEBUG	0
/*@unchecked@*/
int _psm_debug = _PSM_DEBUG;
/*@unchecked@*/
int _psm_threads = 0;

extern int _nolead;
extern int _nosigh;

/*@access FD_t @*/		/* XXX void ptr args */
/*@access rpmpsm @*/

/*@access rpmfi @*/
/*@access rpmte @*/	/* XXX rpmInstallSourcePackage */
/*@access rpmts @*/	/* XXX ts->notify */

/*@access rpmluav @*/

int rpmVersionCompare(Header first, Header second)
{
    const char * one, * two;
    int_32 * epochOne, * epochTwo;
    static int_32 zero = 0;
    int rc;

    if (!headerGetEntry(first, RPMTAG_EPOCH, NULL, &epochOne, NULL))
	epochOne = &zero;
    if (!headerGetEntry(second, RPMTAG_EPOCH, NULL, &epochTwo, NULL))
	epochTwo = &zero;

/*@-boundsread@*/
    if (*epochOne < *epochTwo)
	return -1;
    else if (*epochOne > *epochTwo)
	return 1;
/*@=boundsread@*/

    rc = headerGetEntry(first, RPMTAG_VERSION, NULL, &one, NULL);
    rc = headerGetEntry(second, RPMTAG_VERSION, NULL, &two, NULL);

    rc = rpmvercmp(one, two);
    if (rc)
	return rc;

    rc = headerGetEntry(first, RPMTAG_RELEASE, NULL, &one, NULL);
    rc = headerGetEntry(second, RPMTAG_RELEASE, NULL, &two, NULL);

    return rpmvercmp(one, two);
}

/**
 * Mark files in database shared with this package as "replaced".
 * @param psm		package state machine data
 * @return		0 always
 */
/*@-bounds@*/
static rpmRC markReplacedFiles(const rpmpsm psm)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const rpmts ts = psm->ts;
    rpmte te = psm->te;
    rpmfi fi = psm->fi;
    HGE_t hge = (HGE_t)fi->hge;
    sharedFileInfo replaced = (te ? te->replaced : NULL);
    sharedFileInfo sfi;
    rpmdbMatchIterator mi;
    Header h;
    int * offsets;
    unsigned int prev;
    int num, xx;

    if (!(rpmfiFC(fi) > 0 && replaced != NULL))
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

	if (!hge(h, RPMTAG_FILESTATES, NULL, &secStates, &count))
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

rpmRC rpmInstallSourcePackage(rpmts ts, void * _fd,
		const char ** specFilePtr, const char ** cookie)
{
    FD_t fd = _fd;
    int scareMem = 1;	/* XXX fi->h is needed */
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
    rpmRC rpmrc;
    int i;

    memset(psm, 0, sizeof(*psm));
    psm->ts = rpmtsLink(ts, "InstallSourcePackage");

    rpmrc = rpmReadPackageFile(ts, fd, "InstallSourcePackage", &h);
    switch (rpmrc) {
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

    rpmrc = RPMRC_OK;

    isSource =
	(headerIsEntry(h, RPMTAG_SOURCERPM) == 0 &&
	 headerIsEntry(h, RPMTAG_ARCH) != 0);

    if (!isSource) {
	rpmError(RPMERR_NOTSRPM, _("source package expected, binary found\n"));
	rpmrc = RPMRC_FAIL;
	goto exit;
    }

    (void) rpmtsAddInstallElement(ts, h, NULL, 0, NULL);

    fi = rpmfiNew(ts, h, RPMTAG_BASENAMES, scareMem);
    h = headerFree(h);

    if (fi == NULL) {	/* XXX can't happen */
	rpmrc = RPMRC_FAIL;
	goto exit;
    }

/*@-onlytrans@*/	/* FIX: te reference */
    fi->te = rpmtsElement(ts, 0);
/*@=onlytrans@*/
    if (fi->te == NULL) {	/* XXX can't happen */
	rpmrc = RPMRC_FAIL;
	goto exit;
    }

assert(fi->h != NULL);
    fi->te->h = headerLink(fi->h);
    fi->te->fd = fdLink(fd, "installSourcePackage");
    hge = fi->hge;
    hfd = fi->hfd;

    (void) headerMacrosLoad(fi->h);

    psm->fi = rpmfiLink(fi, NULL);
    /*@-assignexpose -usereleased @*/
    psm->te = fi->te;
    /*@=assignexpose =usereleased @*/

    if (cookie) {
	*cookie = NULL;
	if (hge(fi->h, RPMTAG_COOKIE, NULL, (void **) cookie, NULL))
	    *cookie = xstrdup(*cookie);
    }

    /* XXX FIXME: don't do per-file mapping, force global flags. */
    fi->fmapflags = _free(fi->fmapflags);
    fi->mapflags = CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID;

    fi->uid = getuid();
    fi->gid = getgid();
    fi->astriplen = 0;
    fi->striplen = 0;

    for (i = 0; i < fi->fc; i++)
	fi->actions[i] = FA_CREATE;

    i = fi->fc;

    if (fi->h != NULL) {	/* XXX can't happen */
	headerGetExtension(fi->h, RPMTAG_FILEPATHS, NULL, &fi->apath, NULL);

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
    rpmrc = rpmMkdirPath(_sourcedir, "sourcedir");
    if (rpmrc) {
	rpmrc = RPMRC_FAIL;
	goto exit;
    }
    if (Access(_sourcedir, W_OK)) {
	rpmError(RPMERR_CREATE, _("cannot write to %%%s %s\n"),
		"_sourcedir", _sourcedir);
	rpmrc = RPMRC_FAIL;
	goto exit;
    }

    _specdir = rpmGenPath(rpmtsRootDir(ts), "%{_specdir}", "");
    rpmrc = rpmMkdirPath(_specdir, "specdir");
    if (rpmrc) {
	rpmrc = RPMRC_FAIL;
	goto exit;
    }
    if (Access(_specdir, W_OK)) {
	rpmError(RPMERR_CREATE, _("cannot write to %%%s %s\n"),
		"_specdir", _specdir);
	rpmrc = RPMRC_FAIL;
	goto exit;
    }

    /* Build dnl/dil with {_sourcedir, _specdir} as values. */
    if (i < fi->fc) {
	int speclen = strlen(_specdir) + 2;
	int sourcelen = strlen(_sourcedir) + 2;
	char * t;

/*@i@*/	fi->dnl = hfd(fi->dnl, -1);

	fi->dc = 2;
	fi->dnl = xmalloc(fi->dc * sizeof(*fi->dnl)
			+ fi->fc * sizeof(*fi->dil)
			+ speclen + sourcelen);
	/*@-dependenttrans@*/
	fi->dil = (unsigned int *)(fi->dnl + fi->dc);
	/*@=dependenttrans@*/
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
	rpmrc = RPMRC_FAIL;
	goto exit;
    }

    psm->goal = PSM_PKGINSTALL;

    /*@-compmempass@*/	/* FIX: psm->fi->dnl should be owned. */
    rpmrc = rpmpsmStage(psm, PSM_PROCESS);

    (void) rpmpsmStage(psm, PSM_FINI);
    /*@=compmempass@*/

    if (rpmrc) rpmrc = RPMRC_FAIL;

exit:
    if (specFilePtr && specFile && rpmrc == RPMRC_OK)
	*specFilePtr = specFile;
    else
	specFile = _free(specFile);

    _specdir = _free(_specdir);
    _sourcedir = _free(_sourcedir);

    psm->fi = rpmfiFree(psm->fi);
    psm->te = NULL;

    if (h != NULL) h = headerFree(h);

    /*@-branchstate@*/
    if (fi != NULL) {
	fi->te->h = headerFree(fi->te->h);
	if (fi->te->fd != NULL)
	    (void) Fclose(fi->te->fd);
	fi->te->fd = NULL;
	fi->te = NULL;
	fi = rpmfiFree(fi);
    }
    /*@=branchstate@*/

    /* XXX nuke the added package(s). */
    rpmtsClean(ts);

    psm->ts = rpmtsFree(psm->ts);

    return rpmrc;
}

/*@observer@*/ /*@unchecked@*/
static char * SCRIPT_PATH = "PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/X11R6/bin";

/**
 * Return scriptlet name from tag.
 * @param tag		scriptlet tag
 * @return		name of scriptlet
 */
static /*@observer@*/ const char * tag2sln(int tag)
	/*@*/
{
    switch (tag) {
    case RPMTAG_PRETRANS:	return "%pretrans";
    case RPMTAG_TRIGGERPREIN:	return "%triggerprein";
    case RPMTAG_PREIN:		return "%pre";
    case RPMTAG_POSTIN:		return "%post";
    case RPMTAG_TRIGGERIN:	return "%triggerin";
    case RPMTAG_TRIGGERUN:	return "%triggerun";
    case RPMTAG_PREUN:		return "%preun";
    case RPMTAG_POSTUN:		return "%postun";
    case RPMTAG_POSTTRANS:	return "%posttrans";
    case RPMTAG_TRIGGERPOSTUN:	return "%triggerpostun";
    case RPMTAG_VERIFYSCRIPT:	return "%verify";
    }
    return "%unknownscript";
}

/**
 * Return scriptlet id from tag.
 * @param tag		scriptlet tag
 * @return		id of scriptlet
 */
static rpmScriptID tag2slx(int tag)
	/*@*/
{
    switch (tag) {
    case RPMTAG_PRETRANS:	return RPMSCRIPT_PRETRANS;
    case RPMTAG_TRIGGERPREIN:	return RPMSCRIPT_TRIGGERPREIN;
    case RPMTAG_PREIN:		return RPMSCRIPT_PREIN;
    case RPMTAG_POSTIN:		return RPMSCRIPT_POSTIN;
    case RPMTAG_TRIGGERIN:	return RPMSCRIPT_TRIGGERIN;
    case RPMTAG_TRIGGERUN:	return RPMSCRIPT_TRIGGERUN;
    case RPMTAG_PREUN:		return RPMSCRIPT_PREUN;
    case RPMTAG_POSTUN:		return RPMSCRIPT_POSTUN;
    case RPMTAG_POSTTRANS:	return RPMSCRIPT_POSTTRANS;
    case RPMTAG_TRIGGERPOSTUN:	return RPMSCRIPT_TRIGGERPOSTUN;
    case RPMTAG_VERIFYSCRIPT:	return RPMSCRIPT_VERIFY;
    }
    return RPMSCRIPT_UNKNOWN;
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
    const rpmts ts = psm->ts;
    rpmtime_t msecs;

    (void) rpmsqWait(&psm->sq);
    msecs = psm->sq.op.usecs/1000;
    (void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_SCRIPTLETS), &psm->sq.op);

    rpmMessage(RPMMESS_DEBUG,
	D_("%s: waitpid(%d) rc %d status %x secs %u.%03u\n"),
	psm->stepName, (unsigned)psm->sq.child,
	(unsigned)psm->sq.reaped, psm->sq.status,
	(unsigned)msecs/1000, (unsigned)msecs%1000);

    if (psm->sstates != NULL)
    {	int * ssp = psm->sstates + tag2slx(psm->scriptTag);
	*ssp &= ~0xffff;
	*ssp |= (psm->sq.status & 0xffff);
	*ssp |= RPMSCRIPT_STATE_REAPED;
    }

    return psm->sq.reaped;
}

#ifdef WITH_LUA
/**
 * Run internal Lua script.
 */
static rpmRC runLuaScript(rpmpsm psm, Header h, const char *sln,
		   int progArgc, const char **progArgv,
		   const char *script, int arg1, int arg2)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies psm, fileSystem, internalState @*/
{
    const rpmts ts = psm->ts;
    int rootFdno = -1;
    const char * NVRA = NULL;
    rpmRC rc = RPMRC_OK;
    int i;
    int xx;
    rpmlua lua = NULL; /* Global state. */
    rpmluav var;
    int * ssp = NULL;

    if (psm->sstates != NULL)
	ssp = psm->sstates + tag2slx(psm->scriptTag);
    if (ssp != NULL)
	*ssp |= (RPMSCRIPT_STATE_LUA|RPMSCRIPT_STATE_EXEC);

    xx = headerGetExtension(h, RPMTAG_NVRA, NULL, &NVRA, NULL);
assert(NVRA);

    /* Save the current working directory. */
/*@-nullpass@*/
    rootFdno = open(".", O_RDONLY, 0);
/*@=nullpass@*/

    /* Get into the chroot. */
    if (!rpmtsChrootDone(ts)) {
	const char *rootDir = rpmtsRootDir(ts);
	/*@-superuser -noeffect @*/
	if (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/') {
	    xx = Chroot(rootDir);
	/*@=superuser =noeffect @*/
	    xx = rpmtsSetChrootDone(ts, 1);
	}
    }

    /* All lua scripts run with CWD == "/". */
    xx = Chdir("/");

    /* Create arg variable */
    rpmluaPushTable(lua, "arg");
    var = rpmluavNew();
    rpmluavSetListMode(var, 1);
/*@+relaxtypes@*/
    if (progArgv) {
	for (i = 0; i < progArgc && progArgv[i]; i++) {
	    rpmluavSetValue(var, RPMLUAV_STRING, progArgv[i]);
	    rpmluaSetVar(lua, var);
	}
    }
    if (arg1 >= 0) {
	rpmluavSetValueNum(var, arg1);
	rpmluaSetVar(lua, var);
    }
    if (arg2 >= 0) {
	rpmluavSetValueNum(var, arg2);
	rpmluaSetVar(lua, var);
    }
/*@=relaxtypes@*/
/*@-moduncon@*/
    var = rpmluavFree(var);
/*@=moduncon@*/
    rpmluaPop(lua);

    {
	char buf[BUFSIZ];
	xx = snprintf(buf, BUFSIZ, "%s(%s)", sln, NVRA);
	xx = rpmluaRunScript(lua, script, buf);
	if (xx == -1)
	    rc = RPMRC_FAIL;
	if (ssp != NULL) {
	    *ssp &= ~0xffff;
	    *ssp |= (xx & 0xffff);
	    *ssp |= RPMSCRIPT_STATE_REAPED;
	}
    }

    rpmluaDelVar(lua, "arg");

    /* Get out of chroot. */
    if (rpmtsChrootDone(ts)) {
	const char *rootDir = rpmtsRootDir(ts);
	xx = fchdir(rootFdno);
	/*@-superuser -noeffect @*/
	if (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/') {
	    xx = Chroot(".");
	/*@=superuser =noeffect @*/
	    xx = rpmtsSetChrootDone(ts, 0);
	}
    } else
	xx = fchdir(rootFdno);

    xx = close(rootFdno);
    NVRA = _free(NVRA);

    return rc;
}
#endif

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
	/*@globals ldconfig_done, rpmGlobalMacroContext, h_errno,
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
    int xx;
    int i;
    int freePrefixes = 0;
    FD_t scriptFd;
    FD_t out;
    rpmRC rc = RPMRC_FAIL;	/* assume failure */
    const char * NVRA = NULL;
    int * ssp = NULL;

    if (psm->sstates != NULL)
	ssp = psm->sstates + tag2slx(psm->scriptTag);
    if (ssp != NULL)
	*ssp = RPMSCRIPT_STATE_UNKNOWN;

    if (progArgv == NULL && script == NULL)
	return RPMRC_OK;

    xx = headerGetExtension(h, RPMTAG_NVRA, NULL, &NVRA, NULL);
assert(NVRA);

    if (progArgv && strcmp(progArgv[0], "<lua>") == 0) {
#ifdef WITH_LUA
	rpmMessage(RPMMESS_DEBUG,
		D_("%s: %s(%s) running <lua> scriptlet.\n"),
		psm->stepName, tag2sln(psm->scriptTag), NVRA);
	rc = runLuaScript(psm, h, sln, progArgc, progArgv,
			    script, arg1, arg2);
#endif
	goto exit;
    }

    psm->sq.reaper = 1;

    /*
     * If a successor node, and ldconfig was just run, don't bother.
     */
    if (ldconfig_path && progArgv != NULL && psm->unorderedSuccessor) {
 	if (ldconfig_done && !strcmp(progArgv[0], ldconfig_path)) {
	    rpmMessage(RPMMESS_DEBUG,
		D_("%s: %s(%s) skipping redundant \"%s\".\n"),
		psm->stepName, tag2sln(psm->scriptTag), NVRA,
		progArgv[0]);
	    rc = RPMRC_OK;
	    goto exit;
	}
    }

    rpmMessage(RPMMESS_DEBUG,
		D_("%s: %s(%s) %ssynchronous scriptlet start\n"),
		psm->stepName, tag2sln(psm->scriptTag), NVRA,
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

    if (hge(h, RPMTAG_INSTPREFIXES, &ipt, &prefixes, &numPrefixes)) {
	freePrefixes = 1;
    } else if (hge(h, RPMTAG_INSTALLPREFIX, NULL, &oldPrefix, NULL)) {
	prefixes = &oldPrefix;
	numPrefixes = 1;
    } else {
	numPrefixes = 0;
    }

    maxPrefixLength = 0;
    if (prefixes != NULL)
    for (i = 0; i < numPrefixes; i++) {
	len = strlen(prefixes[i]);
	if (len > maxPrefixLength) maxPrefixLength = len;
    }
    prefixBuf = alloca(maxPrefixLength + 50);

    if (script) {
	const char * rootDir = rpmtsRootDir(ts);
	FD_t fd;

	/*@-branchstate@*/
	if (makeTempFile((!rpmtsChrootDone(ts) ? rootDir : "/"), &fn, &fd))
	    goto exit;
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
    if (out == NULL)	/* XXX can't happen */
	goto exit;

    /*@-branchstate@*/
    xx = rpmsqFork(&psm->sq);
    if (psm->sq.child == 0) {
	int pipes[2];
	int flag;
	int fdno;

	pipes[0] = pipes[1] = 0;
	/* Make stdin inaccessible */
	xx = pipe(pipes);
	xx = close(pipes[1]);
	xx = dup2(pipes[0], STDIN_FILENO);
	xx = close(pipes[0]);

	/* XXX Force FD_CLOEXEC on 1st 100 inherited fdno's. */
	for (fdno = 3; fdno < 100; fdno++) {
	    flag = fcntl(fdno, F_GETFD);
	    if (flag == -1 || (flag & FD_CLOEXEC))
		continue;
	    rpmMessage(RPMMESS_DEBUG,
			D_("%s: %s(%s)\tfdno(%d) missing FD_CLOEXEC\n"),
			psm->stepName, sln, NVRA,
			fdno);
	    xx = fcntl(fdno, F_SETFD, FD_CLOEXEC);
	    /* XXX W2DO? debug msg for inheirited fdno w/o FD_CLOEXEC */
	}

	if (scriptFd != NULL) {
	    int sfdno = Fileno(scriptFd);
	    int ofdno = Fileno(out);
	    if (sfdno != STDERR_FILENO)
		xx = dup2(sfdno, STDERR_FILENO);
	    if (ofdno != STDOUT_FILENO)
		xx = dup2(ofdno, STDOUT_FILENO);
	    /* make sure we don't close stdin/stderr/stdout by mistake! */
	    if (ofdno > STDERR_FILENO && ofdno != sfdno)
		xx = Fclose (out);
	    if (sfdno > STDERR_FILENO && ofdno != sfdno)
		xx = Fclose (scriptFd);
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

	if (prefixes != NULL)
	for (i = 0; i < numPrefixes; i++) {
	    sprintf(prefixBuf, "RPM_INSTALL_PREFIX%d=%s", i, prefixes[i]);
	    xx = doputenv(prefixBuf);

	    /* backwards compatibility */
	    if (i == 0) {
		sprintf(prefixBuf, "RPM_INSTALL_PREFIX=%s", prefixes[i]);
		xx = doputenv(prefixBuf);
	    }
	}

	{   const char * rootDir = rpmtsRootDir(ts);
	    if (!rpmtsChrootDone(ts) &&
		!(rootDir[0] == '/' && rootDir[1] == '\0'))
	    {
		/*@-superuser -noeffect @*/
		xx = Chroot(rootDir);
		/*@=superuser =noeffect @*/
	    }
	    xx = Chdir("/");
	    rpmMessage(RPMMESS_DEBUG, D_("%s: %s(%s)\texecv(%s) pid %d\n"),
			psm->stepName, sln, NVRA,
			argv[0], (unsigned)getpid());

	    /* XXX Don't mtrace into children. */
	    unsetenv("MALLOC_CHECK_");

	    if (ssp != NULL)
		*ssp |= RPMSCRIPT_STATE_EXEC;

	    /* Permit libselinux to do the scriptlet exec. */
	    if (rpmtsSELinuxEnabled(ts) == 1) {	
		if (ssp != NULL)
		    *ssp |= RPMSCRIPT_STATE_SELINUX;
/*@-moduncon@*/
		xx = rpm_execcon(0, argv[0], (char *const *)argv, environ);
/*@=moduncon@*/
	    } else {
/*@-nullstate@*/
		xx = execv(argv[0], (char *const *)argv);
/*@=nullstate@*/
	    }
	}

	if (ssp != NULL)
	    *ssp &= ~RPMSCRIPT_STATE_EXEC;

 	_exit(-1);
	/*@notreached@*/
    }
    /*@=branchstate@*/

    if (psm->sq.child == (pid_t)-1) {
        rpmError(RPMERR_FORK, _("Couldn't fork %s: %s\n"), sln, strerror(errno));
        goto exit;
    }

    (void) psmWait(psm);

  /* XXX filter order dependent multilib "other" arch helper error. */
  if (!(psm->sq.reaped >= 0 && !strcmp(argv[0], "/usr/sbin/glibc_post_upgrade") && WEXITSTATUS(psm->sq.status) == 110)) {
    if (psm->sq.reaped < 0) {
	rpmError(RPMERR_SCRIPT,
		_("%s(%s) scriptlet failed, waitpid(%d) rc %d: %s\n"),
		 sln, NVRA, psm->sq.child, psm->sq.reaped, strerror(errno));
	goto exit;
    } else
    if (!WIFEXITED(psm->sq.status) || WEXITSTATUS(psm->sq.status)) {
	if (WIFSIGNALED(psm->sq.status)) {
	    rpmError(RPMERR_SCRIPT,
                 _("%s(%s) scriptlet failed, signal %d\n"),
                 sln, NVRA, WTERMSIG(psm->sq.status));
	} else {
	    rpmError(RPMERR_SCRIPT,
		_("%s(%s) scriptlet failed, exit status %d\n"),
		sln, NVRA, WEXITSTATUS(psm->sq.status));
	}
	goto exit;
    }
  }

    rc = RPMRC_OK;

exit:
    if (freePrefixes) prefixes = hfd(prefixes, ipt);

    if (out)
	xx = Fclose(out);	/* XXX dup'd STDOUT_FILENO */

    /*@-branchstate@*/
    if (script) {
	if (!rpmIsDebug())
	    xx = Unlink(fn);
	fn = _free(fn);
    }
    /*@=branchstate@*/

    NVRA = _free(NVRA);

    return rc;
}

/**
 * Retrieve and run scriptlet from header.
 * @param psm		package state machine data
 * @return		rpmRC return code
 */
static rpmRC runInstScript(rpmpsm psm)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    void ** progArgv = NULL;
    int progArgc;
    const char * argv0 = NULL;
    const char ** argv;
    rpmTagType ptt, stt;
    const char * script;
    rpmRC rc = RPMRC_OK;
    int xx;

assert(fi->h != NULL);
    xx = hge(fi->h, psm->scriptTag, &stt, &script, NULL);
    xx = hge(fi->h, psm->progTag, &ptt, &progArgv, &progArgc);
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

    if (argv[0][0] == '%')
	argv[0] = argv0 = rpmExpand(argv[0], NULL);

    if (fi->h != NULL)	/* XXX can't happen */
    rc = runScript(psm, fi->h, tag2sln(psm->scriptTag), progArgc, argv,
		script, psm->scriptArg, -1);

exit:
    argv0 = _free(argv0);
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
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState@*/
	/*@modifies psm, sourceH, triggeredH, *triggersAlreadyRun,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    int scareMem = 0;
    const rpmts ts = psm->ts;
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    rpmds trigger = NULL;
    const char ** triggerScripts;
    const char ** triggerProgs;
    int_32 * triggerIndices;
    const char * sourceName;
    const char * triggerName;
    rpmRC rc = RPMRC_OK;
    int xx;
    int i;

    xx = hge(sourceH, RPMTAG_NAME, NULL, &sourceName, NULL);
    xx = hge(triggeredH, RPMTAG_NAME, NULL, &triggerName, NULL);

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
		       &triggerIndices, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERSCRIPTS, &tst,
		       &triggerScripts, NULL) &&
		hge(triggeredH, RPMTAG_TRIGGERSCRIPTPROG, &tpt,
		       &triggerProgs, NULL))
	    )
	    continue;

	{   int arg1;
	    int index;

	    arg1 = rpmdbCountPackages(rpmtsGetRdb(ts), triggerName);
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
	/*@globals rpmGlobalMacroContext, h_errno,
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
/* XXX: Might need to adjust instance counts for autorollback. */
    if (N) 		/* XXX can't happen */
	numPackage = rpmdbCountPackages(rpmtsGetRdb(ts), N)
				+ psm->countCorrection;
    if (numPackage < 0)
	return RPMRC_NOTFOUND;

    if (fi != NULL && fi->h != NULL)	/* XXX can't happen */
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
	/*@globals rpmGlobalMacroContext, h_errno,
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
    size_t nb;

    if (fi->h == NULL)	return rc;	/* XXX can't happen */

    if (!(	hge(fi->h, RPMTAG_TRIGGERNAME, &tnt,
			&triggerNames, &numTriggers) &&
		hge(fi->h, RPMTAG_TRIGGERINDEX, &tit,
			&triggerIndices, &numTriggerIndices))
	)
	return rc;

    nb = sizeof(*triggersRun) * numTriggerIndices;
    triggersRun = memset(alloca(nb), 0, nb);

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

/*@observer@*/
static const char * pkgStageString(pkgStage a)
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
/*@-internalglobs@*/
    psm->ts = rpmtsFree(psm->ts);
/*@=internalglobs@*/

    psm->sstates = _free(psm->sstates);

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

    psm->sstates = xcalloc(RPMSCRIPT_MAX, sizeof(*psm->sstates));

    return rpmpsmLink(psm, msg);
}

/**
 * Load a transaction id from a header.
 * @param h		header
 * @param tag		tag to load
 * @return		tag value (0 on failure)
 */
static uint_32 hLoadTID(Header h, int_32 tag)
	/*@*/
{
    uint_32 val = 0;
    int_32 typ = 0;
    void * ptr = NULL;
    int_32 cnt = 0;

    if (headerGetEntry(h, tag, &typ, &ptr, &cnt)
     && typ == RPM_INT32_TYPE && ptr != NULL && cnt == 1)
	val = *(uint_32 *)ptr;
    ptr = headerFreeData(ptr, typ);
    return val;
}

/**
 * Copy a tag from a source to a target header.
 * @param sh		source header
 * @param th		target header
 * @param tag		tag to copy
 * @return		0 always
 */
static int hCopyTag(Header sh, Header th, int_32 tag)
	/*@modifies th @*/
{
    int_32 typ = 0;
    void * ptr = NULL;
    int_32 cnt = 0;
    int xx = 1;

    if (headerGetEntry(sh, tag, &typ, &ptr, &cnt) && cnt > 0)
	xx = headerAddEntry(th, tag, typ, ptr, cnt);
assert(xx);
    ptr = headerFreeData(ptr, typ);
    return 0;
}

/**
 * Save backward link(s) of an upgrade chain into a header.
 * @param h		header
 * @param *blink	backward links
 * @return		0 always
 */
static int hSaveBlinks(Header h, const struct rpmChainLink_s * blink)
	/*@modifies h @*/
{
    /*@observer@*/
    static const char * chain_end = RPMTE_CHAIN_END;
    int ac;
    int xx = 1;

    ac = argvCount(blink->NEVRA);
    if (ac > 0)
	xx = headerAddEntry(h, RPMTAG_BLINKNEVRA, RPM_STRING_ARRAY_TYPE,
			argvData(blink->NEVRA), ac);
    else     /* XXX Add an explicit chain terminator on 1st install. */
	xx = headerAddEntry(h, RPMTAG_BLINKNEVRA, RPM_STRING_ARRAY_TYPE,
			&chain_end, 1);
assert(xx);
    
    ac = argvCount(blink->Pkgid);
    if (ac > 0) 
	xx = headerAddEntry(h, RPMTAG_BLINKPKGID, RPM_STRING_ARRAY_TYPE,
			argvData(blink->Pkgid), ac);
    else     /* XXX Add an explicit chain terminator on 1st install. */
	xx = headerAddEntry(h, RPMTAG_BLINKPKGID, RPM_STRING_ARRAY_TYPE,
			&chain_end, 1);
assert(xx);

    ac = argvCount(blink->Hdrid);
    if (ac > 0)
	xx = headerAddEntry(h, RPMTAG_BLINKHDRID, RPM_STRING_ARRAY_TYPE,
			argvData(blink->Hdrid), ac);
    else     /* XXX Add an explicit chain terminator on 1st install. */
	xx = headerAddEntry(h, RPMTAG_BLINKNEVRA, RPM_STRING_ARRAY_TYPE,
			&chain_end, 1);
assert(xx);

    return 0;
}

/**
 * Save forward link(s) of an upgrade chain into a header.
 * @param h		header
 * @param *flink	forward links
 * @return		0 always
 */
static int hSaveFlinks(Header h, const struct rpmChainLink_s * flink)
	/*@modifies h @*/
{
#ifdef	NOTYET
    /*@observer@*/
    static const char * chain_end = RPMTE_CHAIN_END;
#endif
    int ac;
    int xx = 1;

    /* Save backward links into header upgrade chain. */
    ac = argvCount(flink->NEVRA);
    if (ac > 0)
	xx = headerAddEntry(h, RPMTAG_FLINKNEVRA, RPM_STRING_ARRAY_TYPE,
			argvData(flink->NEVRA), ac);
#ifdef	NOTYET	/* XXX is an explicit flink terminator needed? */
    else     /* XXX Add an explicit chain terminator on 1st install. */
	xx = headerAddEntry(h, RPMTAG_FLINKNEVRA, RPM_STRING_ARRAY_TYPE,
			&chain_end, 1);
#endif
assert(xx);

    ac = argvCount(flink->Pkgid);
    if (ac > 0) 
	xx = headerAddEntry(h, RPMTAG_FLINKPKGID, RPM_STRING_ARRAY_TYPE,
			argvData(flink->Pkgid), ac);
#ifdef	NOTYET	/* XXX is an explicit flink terminator needed? */
    else     /* XXX Add an explicit chain terminator on 1st install. */
	xx = headerAddEntry(h, RPMTAG_FLINKPKGID, RPM_STRING_ARRAY_TYPE,
			&chain_end, 1);
#endif
assert(xx);

    ac = argvCount(flink->Hdrid);
    if (ac > 0)
	xx = headerAddEntry(h, RPMTAG_FLINKHDRID, RPM_STRING_ARRAY_TYPE,
			argvData(flink->Hdrid), ac);
#ifdef	NOTYET	/* XXX is an explicit flink terminator needed? */
    else     /* XXX Add an explicit chain terminator on 1st install. */
	xx = headerAddEntry(h, RPMTAG_FLINKNEVRA, RPM_STRING_ARRAY_TYPE,
			&chain_end, 1);
#endif
assert(xx);

    return 0;
}

/**
 * Add per-transaction data to an install header.
 * @param ts		transaction set
 * @param te		transaction element
 * @param fi		file info set
 * @return		0 always
 */
static int populateInstallHeader(const rpmts ts, const rpmte te, rpmfi fi)
	/*@modifies fi @*/
{
    uint_32 tscolor = rpmtsColor(ts);
    uint_32 tecolor = rpmteColor(te);
    int_32 installTime = (int_32) time(NULL);
    int fc = rpmfiFC(fi);
    const char * origin;
    int xx = 1;

assert(fi->h != NULL);
    if (fi->fstates != NULL && fc > 0)
	xx = headerAddEntry(fi->h, RPMTAG_FILESTATES, RPM_CHAR_TYPE,
				fi->fstates, fc);
assert(xx);

    xx = headerAddEntry(fi->h, RPMTAG_INSTALLTIME, RPM_INT32_TYPE,
			&installTime, 1);
assert(xx);

    xx = headerAddEntry(fi->h, RPMTAG_INSTALLCOLOR, RPM_INT32_TYPE,
			&tscolor, 1);
assert(xx);

    /* XXX FIXME: add preferred color at install. */

    xx = headerAddEntry(fi->h, RPMTAG_PACKAGECOLOR, RPM_INT32_TYPE,
				&tecolor, 1);
assert(xx);

    /* Add the header's origin (i.e. URL) */
    origin = headerGetOrigin(fi->h);
    if (origin != NULL)
	xx = headerAddEntry(fi->h, RPMTAG_PACKAGEORIGIN, RPM_STRING_TYPE,
				origin, 1);
assert(xx);

    /* XXX Don't clobber forward/backward upgrade chain on rollbacks */
    if (rpmtsType(ts) != RPMTRANS_TYPE_ROLLBACK)
	xx = hSaveBlinks(fi->h, &te->blink);

    return 0;
}

#if defined(HAVE_PTHREAD_H)
static void * rpmpsmThread(void * arg)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies arg, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    rpmpsm psm = arg;
/*@-unqualifiedtrans@*/
    return ((void *) rpmpsmStage(psm, psm->nstage));
/*@=unqualifiedtrans@*/
}
#endif

static int rpmpsmNext(rpmpsm psm, pkgStage nstage)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies psm, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    psm->nstage = nstage;
#if defined(HAVE_PTHREAD_H)
    if (_psm_threads)
	return rpmsqJoin( rpmsqThread(rpmpsmThread, psm) );
#endif
    return rpmpsmStage(psm, psm->nstage);
}

/**
 * @todo Packages w/o files never get a callback, hence don't get displayed
 * on install with -v.
 */
/*@-bounds -nullpass@*/ /* FIX: testing null annotation for fi->h */
rpmRC rpmpsmStage(rpmpsm psm, pkgStage stage)
{
    const rpmts ts = psm->ts;
    uint_32 tscolor = rpmtsColor(ts);
    rpmfi fi = psm->fi;
    HGE_t hge = fi->hge;
    HAE_t hae = fi->hae;
    HFD_t hfd = (fi->hfd ? fi->hfd : headerFreeData);
    rpmRC rc = psm->rc;
    int saveerrno;
    int xx;

/*@-branchstate@*/
    switch (stage) {
    case PSM_UNKNOWN:
	break;
    case PSM_INIT:
	rpmMessage(RPMMESS_DEBUG, D_("%s: %s has %d files, test = %d\n"),
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

	/* Adjust package count on rollback downgrade. */
assert(psm->te != NULL);
	if (rpmtsType(ts) == RPMTRANS_TYPE_AUTOROLLBACK &&
	    (psm->goal & ~(PSM_PKGSAVE|PSM_PKGERASE)))
	{
	    if (psm->te->downgrade)
		psm->npkgs_installed--;
	}

	if (psm->goal == PSM_PKGINSTALL) {
	    int fc = rpmfiFC(fi);
	    const char * hdrid;

	    /* Add per-transaction data to install header. */
	    xx = populateInstallHeader(ts, psm->te, fi);

	    psm->scriptArg = psm->npkgs_installed + 1;

assert(psm->mi == NULL);
	    hdrid = rpmteHdrid(psm->te);
	    if (hdrid != NULL) {
		/* XXX should use RPMTAG_HDRID not RPMTAG_SHA1HEADER */
		psm->mi = rpmtsInitIterator(ts, RPMTAG_SHA1HEADER, hdrid, 0);
	    } else {
		psm->mi = rpmtsInitIterator(ts, RPMTAG_NAME, rpmteN(psm->te),0);
		xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_EPOCH, RPMMIRE_STRCMP,
			rpmteE(psm->te));
		xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_VERSION, RPMMIRE_STRCMP,
			rpmteV(psm->te));
		xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_RELEASE, RPMMIRE_STRCMP,
			rpmteR(psm->te));
		if (tscolor) {
		    xx = rpmdbSetIteratorRE(psm->mi,RPMTAG_ARCH, RPMMIRE_STRCMP,
			rpmteA(psm->te));
		    xx = rpmdbSetIteratorRE(psm->mi, RPMTAG_OS, RPMMIRE_STRCMP,
			rpmteO(psm->te));
		}
	    }

	    while ((psm->oh = rpmdbNextIterator(psm->mi)) != NULL) {
		fi->record = rpmdbGetIteratorOffset(psm->mi);
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
	     * Old format relocatable packages need the entire default
	     * prefix stripped to form the cpio list, while all other packages
	     * need the leading / stripped.
	     */
	    {   const char * p;
		xx = hge(fi->h, RPMTAG_DEFAULTPREFIX, NULL, &p, NULL);
		fi->striplen = (xx ? strlen(p) + 1 : 1);
	    }
	    fi->mapflags =
		CPIO_MAP_PATH | CPIO_MAP_MODE | CPIO_MAP_UID | CPIO_MAP_GID | (fi->mapflags & CPIO_SBIT_CHECK);
	
	    if (headerIsEntry(fi->h, RPMTAG_ORIGBASENAMES))
		headerGetExtension(fi->h, RPMTAG_ORIGPATHS, NULL, &fi->apath, NULL);
	    else
		headerGetExtension(fi->h, RPMTAG_FILEPATHS, NULL, &fi->apath, NULL);
	
	    if (fi->fuser == NULL)
		xx = hge(fi->h, RPMTAG_FILEUSERNAME, NULL,
				&fi->fuser, NULL);
	    if (fi->fgroup == NULL)
		xx = hge(fi->h, RPMTAG_FILEGROUPNAME, NULL,
				&fi->fgroup, NULL);
	    rc = RPMRC_OK;
	}
	if (psm->goal == PSM_PKGERASE || psm->goal == PSM_PKGSAVE) {
	    psm->scriptArg = psm->npkgs_installed - 1;
	
	    /* Retrieve installed header. */
	    rc = rpmpsmNext(psm, PSM_RPMDB_LOAD);
if (rc == RPMRC_OK)
if (psm->te)
psm->te->h = headerLink(fi->h);
	}
	if (psm->goal == PSM_PKGSAVE) {
	    /* Open output package for writing. */
	    {	char tiddn[32];
		const char * bfmt;
		const char * pkgdn;
		const char * pkgbn;
		char * pkgdn_buf;

		xx = snprintf(tiddn, sizeof(tiddn), "%d", rpmtsGetTid(ts));
		bfmt = rpmGetPath(tiddn, "/", "%{_repackage_name_fmt}", NULL);
		pkgbn = headerSprintf(fi->h, bfmt,
					rpmTagTable, rpmHeaderFormats, NULL);
		bfmt = _free(bfmt);
		psm->pkgURL = rpmGenPath("%{?_repackage_root}",
					 "%{?_repackage_dir}",
					pkgbn);
		pkgbn = _free(pkgbn);
		(void) urlPath(psm->pkgURL, &psm->pkgfn);
		pkgdn_buf = xstrdup(psm->pkgfn);
		pkgdn = dirname(pkgdn_buf);
		rc = rpmMkdirPath(pkgdn, "_repackage_dir");
		pkgdn_buf = _free(pkgdn_buf);
		if (rc == RPMRC_FAIL)
		    break;
		psm->fd = Fopen(psm->pkgfn, "w.fdio");
		if (psm->fd == NULL || Ferror(psm->fd)) {
		    rc = RPMRC_FAIL;
		    break;
		}
	    }
	}
	break;
    case PSM_PRE:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

/* XXX insure that trigger index is opened before entering chroot. */
#ifdef	NOTYET
 { static int oneshot = 0;
   dbiIndex dbi;
   if (!oneshot) {
     dbi = dbiOpen(rpmtsGetRdb(ts), RPMTAG_TRIGGERNAME, 0);
     oneshot++;
   }
 }
#endif

	/* Change root directory if requested and not already done. */
	rc = rpmpsmNext(psm, PSM_CHROOT_IN);

	if (psm->goal == PSM_PKGINSTALL) {
	    psm->scriptTag = RPMTAG_PREIN;
	    psm->progTag = RPMTAG_PREINPROG;
	    psm->sense = RPMSENSE_TRIGGERPREIN;
	    psm->countCorrection = 0;	/* XXX is this correct?!? */

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERPREIN)) {

		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPRE)) {
		rc = rpmpsmNext(psm, PSM_SCRIPT);
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
		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;

		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPREUN))
		rc = rpmpsmNext(psm, PSM_SCRIPT);
	}
	if (psm->goal == PSM_PKGSAVE) {
	    int noArchiveSize = 0;
	    const char * origin;

	    /* Regenerate original header. */
	    {	void * uh = NULL;
		int_32 uht, uhc;

		/* Save originnal header's origin (i.e. URL) */
		origin = NULL;
		xx = headerGetEntry(fi->h, RPMTAG_PACKAGEORIGIN, NULL,
			&origin, NULL);

		/* Retrieve original header blob. */
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
		        if (ptr) xx = hae(psm->oh, tag, type, ptr, count);
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
	    rc = rpmpsmNext(psm, PSM_RPMIO_FLAGS);
	    /*@=nullstate@*/

	    /* Write the lead section into the package. */
	    if (!_nolead) {
		static const char item[] = "Lead";
		const char * NEVR = rpmteNEVR(psm->te);
		size_t nb = rpmpkgSizeof(item, NULL);
	
		if (nb == 0)
		    rc = RPMRC_FAIL;
		else {
		    void * l = alloca(nb);
		    memset(l, 0, nb);
		    rc = rpmpkgWrite(item, psm->fd, l, &NEVR);
		}
		if (rc != RPMRC_OK) {
		    rpmError(RPMERR_NOSPACE, _("Unable to write package: %s\n"),
				Fstrerror(psm->fd));
		    break;
		}
	    }

	    /* Write the signature section into the package. */
	    /* XXX rpm-4.1 and later has archive size in signature header. */
	    if (!_nosigh) {
		static const char item[] = "Signature";
		Header sigh = headerRegenSigHeader(fi->h, noArchiveSize);
		/* Reallocate the signature into one contiguous region. */
		sigh = headerReload(sigh, RPMTAG_HEADERSIGNATURES);
		if (sigh == NULL) {
		    rpmError(RPMERR_NOSPACE, _("Unable to reload signature header\n"));
		    rc = RPMRC_FAIL;
		    break;
		}
		rc = rpmpkgWrite(item, psm->fd, sigh, NULL);
		sigh = headerFree(sigh);
		if (rc != RPMRC_OK) {
		    break;
		}
	    }

	    /* Add remove transaction id to header. */
	    if (psm->oh != NULL)
	    {	int_32 tid = rpmtsGetTid(ts);

		xx = hae(psm->oh, RPMTAG_REMOVETID, RPM_INT32_TYPE,
				&tid, 1);

		/* Add original header's origin (i.e. URL) */
		if (origin != NULL)
		    xx = hae(psm->oh, RPMTAG_PACKAGEORIGIN, RPM_STRING_TYPE,
				origin, 1);

		/* Copy upgrade chain link tags. */
		xx = hCopyTag(fi->h, psm->oh, RPMTAG_INSTALLTID);
		xx = hCopyTag(fi->h, psm->oh, RPMTAG_BLINKPKGID);
		xx = hCopyTag(fi->h, psm->oh, RPMTAG_BLINKHDRID);
		xx = hCopyTag(fi->h, psm->oh, RPMTAG_BLINKNEVRA);

assert(psm->te != NULL);
		xx = hSaveFlinks(psm->oh, &psm->te->flink);
	    }

	    /* Write the metadata section into the package. */
	    rc = headerWrite(psm->fd, psm->oh);
	    if (rc) break;
	}
	break;
    case PSM_PROCESS:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

	if (psm->goal == PSM_PKGINSTALL) {

	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)	break;

	    /* XXX Synthesize callbacks for packages with no files. */
	    if (rpmfiFC(fi) <= 0) {
		void * ptr;
		ptr = rpmtsNotify(ts, fi->te, RPMCALLBACK_INST_START, 0, 100);
		ptr = rpmtsNotify(ts, fi->te, RPMCALLBACK_INST_PROGRESS, 100, 100);
		break;
	    }

	    /* Retrieve type of payload compression. */
	    rc = rpmpsmNext(psm, PSM_RPMIO_FLAGS);

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

	    rc = fsmSetup(fi->fsm, FSM_PKGINSTALL, psm->payload_format, ts, fi,
			psm->cfd, NULL, &psm->failedFile);
	    (void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_UNCOMPRESS),
			fdstat_op(psm->cfd, FDSTAT_READ));
	    (void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_DIGEST),
			fdstat_op(psm->cfd, FDSTAT_DIGEST));
	    xx = fsmTeardown(fi->fsm);

	    saveerrno = errno; /* XXX FIXME: Fclose with libio destroys errno */
	    xx = Fclose(psm->cfd);
	    psm->cfd = NULL;
	    /*@-mods@*/
	    errno = saveerrno; /* XXX FIXME: Fclose with libio destroys errno */
	    /*@=mods@*/

	    if (!rc)
		rc = rpmpsmNext(psm, PSM_COMMIT);

	    /* XXX make sure progress is closed out */
	    psm->what = RPMCALLBACK_INST_PROGRESS;
	    psm->amount = (fi->archiveSize ? fi->archiveSize : 100);
	    psm->total = psm->amount;
	    xx = rpmpsmNext(psm, PSM_NOTIFY);

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
		xx = rpmpsmNext(psm, PSM_NOTIFY);

		break;
	    }
	}
	if (psm->goal == PSM_PKGERASE) {
	    int fc = rpmfiFC(fi);

	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)	break;
	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY)	break;

	    psm->what = RPMCALLBACK_UNINST_START;
	    psm->amount = fc;
	    psm->total = (fc ? fc : 100);
	    xx = rpmpsmNext(psm, PSM_NOTIFY);

	    if (fc > 0) {
		rc = fsmSetup(fi->fsm, FSM_PKGERASE, psm->payload_format, ts, fi,
			NULL, NULL, &psm->failedFile);
		xx = fsmTeardown(fi->fsm);
	    }

	    psm->what = RPMCALLBACK_UNINST_STOP;
	    psm->amount = (fc ? fc : 100);
	    psm->total = (fc ? fc : 100);
	    xx = rpmpsmNext(psm, PSM_NOTIFY);

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

	    rc = fsmSetup(fi->fsm, FSM_PKGBUILD, psm->payload_format, ts, fi,
			psm->cfd, NULL, &psm->failedFile);
	    (void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_COMPRESS),
			fdstat_op(psm->cfd, FDSTAT_WRITE));
	    (void) rpmswAdd(rpmtsOp(ts, RPMTS_OP_DIGEST),
			fdstat_op(psm->cfd, FDSTAT_DIGEST));
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
	    xx = rpmpsmNext(psm, PSM_NOTIFY);

	    fi->action = action;
	    fi->actions = actions;
	}
	break;
    case PSM_POST:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

	if (psm->goal == PSM_PKGINSTALL) {

	    /*
	     * If this header has already been installed, remove it from
	     * the database before adding the new header.
	     */
	    if (fi->record && !(rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY)) {
		rc = rpmpsmNext(psm, PSM_RPMDB_REMOVE);
		if (rc) break;
	    }

	    rc = rpmpsmNext(psm, PSM_RPMDB_ADD);
	    if (rc) break;

	    psm->scriptTag = RPMTAG_POSTIN;
	    psm->progTag = RPMTAG_POSTINPROG;
	    psm->sense = RPMSENSE_TRIGGERIN;
	    psm->countCorrection = 0;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPOST)) {
		rc = rpmpsmNext(psm, PSM_SCRIPT);
		if (rc) break;
	    }
	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERIN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
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
		rc = rpmpsmNext(psm, PSM_SCRIPT);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERPOSTUN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_APPLYONLY))
		rc = rpmpsmNext(psm, PSM_RPMDB_REMOVE);
	}
	if (psm->goal == PSM_PKGSAVE) {
	}

	/* Restore root directory if changed. */
	xx = rpmpsmNext(psm, PSM_CHROOT_OUT);
	break;
    case PSM_UNDO:
	break;
    case PSM_FINI:
	/* Restore root directory if changed. */
	xx = rpmpsmNext(psm, PSM_CHROOT_OUT);

	if (psm->fd != NULL) {
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
	    xx = rpmpsmNext(psm, PSM_NOTIFY);
	    /*@=nullstate@*/
	}

/*@-branchstate@*/
	if (psm->goal == PSM_PKGERASE || psm->goal == PSM_PKGSAVE) {
if (psm->te != NULL)
if (psm->te->h != NULL)
psm->te->h = headerFree(psm->te->h);
	    if (fi->h != NULL)
		fi->h = headerFree(fi->h);
 	}
/*@=branchstate@*/
	psm->oh = headerFree(psm->oh);
	psm->pkgURL = _free(psm->pkgURL);
	psm->rpmio_flags = _free(psm->rpmio_flags);
	psm->payload_format = _free(psm->payload_format);
	psm->failedFile = _free(psm->failedFile);

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

	rc = rpmpsmNext(psm, PSM_INIT);
	if (!rc) rc = rpmpsmNext(psm, PSM_PRE);
	if (!rc) rc = rpmpsmNext(psm, PSM_PROCESS);
	if (!rc) rc = rpmpsmNext(psm, PSM_POST);
	xx = rpmpsmNext(psm, PSM_FINI);
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

	rc = fsmSetup(fi->fsm, FSM_PKGCOMMIT, psm->payload_format, ts, fi,
			NULL, NULL, &psm->failedFile);
	xx = fsmTeardown(fi->fsm);
	break;

    case PSM_CHROOT_IN:
    {	const char * rootDir = rpmtsRootDir(ts);
	/* Change root directory if requested and not already done. */
	if (rootDir != NULL && !(rootDir[0] == '/' && rootDir[1] == '\0')
	 && !rpmtsChrootDone(ts) && !psm->chrootDone)
	{
	    static int _pw_loaded = 0;
	    static int _gr_loaded = 0;

	    if (!_pw_loaded) {
		(void)getpwnam("root");
		endpwent();
		_pw_loaded++;
	    }
	    if (!_gr_loaded) {
		(void)getgrnam("root");
		endgrent();
		_gr_loaded++;
	    }

	    xx = Chdir("/");
	    /*@-superuser@*/
	    if (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/')
		rc = Chroot(rootDir);
	    /*@=superuser@*/
	    psm->chrootDone = 1;
	    (void) rpmtsSetChrootDone(ts, 1);
	}
    }	break;
    case PSM_CHROOT_OUT:
	/* Restore root directory if changed. */
	if (psm->chrootDone) {
	    const char * rootDir = rpmtsRootDir(ts);
	    const char * currDir = rpmtsCurrDir(ts);
	    /*@-superuser@*/
	    if (rootDir != NULL && strcmp(rootDir, "/") && *rootDir == '/')
		rc = Chroot(".");
	    /*@=superuser@*/
	    psm->chrootDone = 0;
	    (void) rpmtsSetChrootDone(ts, 0);
	    if (currDir != NULL)	/* XXX can't happen */
		xx = Chdir(currDir);
	}
	break;
    case PSM_SCRIPT:	/* Run current package scriptlets. */
	rc = runInstScript(psm);
	break;
    case PSM_TRIGGERS:
	/* Run triggers in other package(s) this package sets off. */
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;
	rc = runTriggers(psm);
	break;
    case PSM_IMMED_TRIGGERS:
	/* Run triggers in this package other package(s) set off. */
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;
	rc = runImmedTriggers(psm);
	break;

    case PSM_RPMIO_FLAGS:
    {	const char * payload_compressor = NULL;
	const char * payload_format = NULL;
	char * t;

	/*@-branchstate@*/
	if (!hge(fi->h, RPMTAG_PAYLOADCOMPRESSOR, NULL,
			    &payload_compressor, NULL))
	    payload_compressor = "gzip";
	/*@=branchstate@*/
	psm->rpmio_flags = t = xmalloc(sizeof("w9.gzdio"));
	*t = '\0';
	t = stpcpy(t, ((psm->goal == PSM_PKGSAVE) ? "w9" : "r"));
	if (!strcmp(payload_compressor, "gzip"))
	    t = stpcpy(t, ".gzdio");
	if (!strcmp(payload_compressor, "bzip2"))
	    t = stpcpy(t, ".bzdio");
	if (!strcmp(payload_compressor, "lzma"))
	    t = stpcpy(t, ".lzdio");

	/*@-branchstate@*/
	if (!hge(fi->h, RPMTAG_PAYLOADFORMAT, NULL,
			    &payload_format, NULL)
	 || !(!strcmp(payload_format, "tar") || !strcmp(payload_format, "ustar")))
	    payload_format = "cpio";
	/*@=branchstate@*/
	psm->payload_format = xstrdup(payload_format);
	rc = RPMRC_OK;
    }	break;

    case PSM_RPMDB_LOAD:
assert(psm->mi == NULL);
	psm->mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES,
				&fi->record, sizeof(fi->record));
	fi->h = rpmdbNextIterator(psm->mi);
	if (fi->h != NULL)
	    fi->h = headerLink(fi->h);
	psm->mi = rpmdbFreeIterator(psm->mi);

	if (fi->h != NULL) {
	    (void) headerSetInstance(fi->h, fi->record);
	    rc = RPMRC_OK;
	} else
	    rc = RPMRC_FAIL;
	break;
    case PSM_RPMDB_ADD:
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;
	if (fi->isSource)	break;	/* XXX never add SRPM's */
	if (fi->h == NULL)	break;	/* XXX can't happen */

	/* Add header to db, doing header check if requested */
	/* XXX rollback headers propagate the previous transaction id. */
	{   uint_32 tid = ((rpmtsType(ts) == RPMTRANS_TYPE_ROLLBACK)
		? hLoadTID(fi->h, RPMTAG_INSTALLTID) : rpmtsGetTid(ts));
	    (void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DBADD), 0);
	    if (!(rpmtsVSFlags(ts) & RPMVSF_NOHDRCHK))
		rc = rpmdbAdd(rpmtsGetRdb(ts), tid, fi->h, ts);
	    else
		rc = rpmdbAdd(rpmtsGetRdb(ts), tid, fi->h, NULL);
	    (void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DBADD), 0);
	}

	if (rc != RPMRC_OK) break;

assert(psm->te != NULL);
	/* Mark non-rollback elements as installed. */
	if (rpmtsType(ts) != RPMTRANS_TYPE_ROLLBACK)
	    psm->te->installed = 1;

	/* Set the database instance for (possible) rollbacks. */
	rpmteSetDBInstance(psm->te, headerGetInstance(fi->h));

	break;
    case PSM_RPMDB_REMOVE:
    {	
	if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)	break;

	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DBREMOVE), 0);
	rc = rpmdbRemove(rpmtsGetRdb(ts), rpmtsGetTid(ts), fi->record, NULL);
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DBREMOVE), 0);

	if (rc != RPMRC_OK) break;

	/* Forget the offset of a successfully removed header. */
	if (psm->te != NULL)	/* XXX can't happen */
	    psm->te->u.removed.dboffset = 0;

    }	break;

    default:
	break;
/*@i@*/    }
/*@=branchstate@*/

/*@-nullstate@*/	/* FIX: psm->oh and psm->fi->h may be NULL. */
    return rc;
/*@=nullstate@*/
}
/*@=bounds =nullpass@*/
