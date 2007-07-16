/** \ingroup rpmbuild
 * \file build/parsePrep.c
 *  Parse %prep section from spec file.
 */

#include "system.h"

#include <rpmio_internal.h>
#include <rpmbuild.h>
#include "debug.h"

/*@access StringBuf @*/	/* compared with NULL */

/* These have to be global to make up for stupid compilers */
/*@unchecked@*/
    static int leaveDirs, skipDefaultAction;
/*@unchecked@*/
    static int createDir, quietly;
/*@unchecked@*/ /*@observer@*/ /*@null@*/
    static const char * dirName = NULL;
/*@unchecked@*/ /*@observer@*/
    static struct poptOption optionsTable[] = {
	    { NULL, 'a', POPT_ARG_STRING, NULL, 'a',	NULL, NULL},
	    { NULL, 'b', POPT_ARG_STRING, NULL, 'b',	NULL, NULL},
	    { NULL, 'c', 0, &createDir, 0,		NULL, NULL},
	    { NULL, 'D', 0, &leaveDirs, 0,		NULL, NULL},
	    { NULL, 'n', POPT_ARG_STRING, &dirName, 0,	NULL, NULL},
	    { NULL, 'T', 0, &skipDefaultAction, 0,	NULL, NULL},
	    { NULL, 'q', 0, &quietly, 0,		NULL, NULL},
	    { 0, 0, 0, 0, 0,	NULL, NULL}
    };

/**
 * Check that file owner and group are known.
 * @param urlfn		file url
 * @return		0 on success
 */
static int checkOwners(const char * urlfn)
	/*@globals h_errno, fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    struct stat sb;

    if (Lstat(urlfn, &sb)) {
	rpmError(RPMERR_BADSPEC, _("Bad source: %s: %s\n"),
		urlfn, strerror(errno));
	return RPMERR_BADSPEC;
    }
    if (!getUname(sb.st_uid) || !getGname(sb.st_gid)) {
	rpmError(RPMERR_BADSPEC, _("Bad owner/group: %s\n"), urlfn);
	return RPMERR_BADSPEC;
    }

    return 0;
}

/**
 * Expand %setup macro into %prep scriptlet.
 * @param spec		build info
 * @param c		source index
 * @param quietly	should -vv be omitted from tar?
 * @return		expanded %setup macro (NULL on error)
 */
/*@-boundswrite@*/
/*@observer@*/
static const char *doUntar(Spec spec, int c, int quietly)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const char *fn, *Lurlfn;
    static char buf[BUFSIZ];
    char *taropts;
    char *t = NULL;
    struct Source *sp;
    rpmCompressedMagic compressed = COMPRESSED_NOT;
    int urltype;
    const char *tar;

    for (sp = spec->sources; sp != NULL; sp = sp->next) {
	if ((sp->flags & RPMFILE_SOURCE) && (sp->num == c)) {
	    break;
	}
    }
    if (sp == NULL) {
	rpmError(RPMERR_BADSPEC, _("No source number %d\n"), c);
	return NULL;
    }

    /*@-internalglobs@*/ /* FIX: shrug */
    taropts = ((rpmIsVerbose() && !quietly) ? "-xvvf" : "-xf");
    /*@=internalglobs@*/

    Lurlfn = rpmGenPath(NULL, "%{_sourcedir}/", sp->source);

    /* XXX On non-build parse's, file cannot be stat'd or read */
    if (!spec->force && (isCompressed(Lurlfn, &compressed) || checkOwners(Lurlfn))) {
	Lurlfn = _free(Lurlfn);
	return NULL;
    }

    fn = NULL;
    urltype = urlPath(Lurlfn, &fn);
    switch (urltype) {
    case URL_IS_HTTPS:	/* XXX WRONG WRONG WRONG */
    case URL_IS_HTTP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_FTP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_HKP:	/* XXX WRONG WRONG WRONG */
    case URL_IS_PATH:
    case URL_IS_UNKNOWN:
	break;
    case URL_IS_DASH:
	Lurlfn = _free(Lurlfn);
	return NULL;
	/*@notreached@*/ break;
    }

    tar = rpmGetPath("%{__tar}", NULL);
    if (strcmp(tar, "%{__tar}") == 0)
        tar = xstrdup("tar");

    if (compressed != COMPRESSED_NOT) {
	const char *zipper;
	int needtar = 1;

	switch (compressed) {
	case COMPRESSED_NOT:	/* XXX can't happen */
	case COMPRESSED_OTHER:
	    t = "%{__gzip} -dc";
	    break;
	case COMPRESSED_BZIP2:
	    t = "%{__bzip2} -dc";
	    break;
	case COMPRESSED_LZOP:
	    t = "%{__lzop} -dc";
	    break;
	case COMPRESSED_LZMA:
	    t = "%{__lzma} -dc";
	    break;
	case COMPRESSED_ZIP:
	    if (rpmIsVerbose() && !quietly)
		t = "%{__unzip}";
	    else
		t = "%{__unzip} -qq";
	    needtar = 0;
	    break;
	}
	zipper = rpmGetPath(t, NULL);
	buf[0] = '\0';
	t = stpcpy(buf, zipper);
	zipper = _free(zipper);
	*t++ = ' ';
	*t++ = '\'';
	t = stpcpy(t, fn);
	*t++ = '\'';
	if (needtar) {
	    t = stpcpy(t, " | ");
	    t = stpcpy(t, tar);
	    t = stpcpy(t, " ");
	    t = stpcpy(t, taropts);
	    t = stpcpy(t, " -");
        }
	t = stpcpy(t,
		"\n"
		"STATUS=$?\n"
		"if [ $STATUS -ne 0 ]; then\n"
		"  exit $STATUS\n"
		"fi");
    } else {
	buf[0] = '\0';
	t = stpcpy(buf, tar);
	t = stpcpy(t, " ");
	t = stpcpy(t, taropts);
	*t++ = ' ';
	t = stpcpy(t, fn);
    }

    tar = _free(tar);
    Lurlfn = _free(Lurlfn);
    return buf;
}
/*@=boundswrite@*/

/**
 * Parse %setup macro.
 * @todo FIXME: Option -q broken when not immediately after %setup.
 * @param spec		build info
 * @param line		current line from spec file
 * @return		0 on success
 */
static int doSetupMacro(Spec spec, char *line)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies spec->buildSubdir, spec->macros, spec->prep,
		rpmGlobalMacroContext, fileSystem, internalState @*/
{
    char buf[BUFSIZ];
    StringBuf before;
    StringBuf after;
    poptContext optCon;
    int argc;
    const char ** argv;
    int arg;
    const char * optArg;
    int rc;
    int num;

    /*@-mods@*/
    leaveDirs = skipDefaultAction = 0;
    createDir = quietly = 0;
    dirName = NULL;
    /*@=mods@*/

    if ((rc = poptParseArgvString(line, &argc, &argv))) {
	rpmError(RPMERR_BADSPEC, _("Error parsing %%setup: %s\n"),
			poptStrerror(rc));
	return RPMERR_BADSPEC;
    }

    before = newStringBuf();
    after = newStringBuf();

    optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
    while ((arg = poptGetNextOpt(optCon)) > 0) {
	optArg = poptGetOptArg(optCon);

	/* We only parse -a and -b here */

	if (parseNum(optArg, &num)) {
	    rpmError(RPMERR_BADSPEC, _("line %d: Bad arg to %%setup: %s\n"),
		     spec->lineNum, (optArg ? optArg : "???"));
	    before = freeStringBuf(before);
	    after = freeStringBuf(after);
	    optCon = poptFreeContext(optCon);
	    argv = _free(argv);
	    return RPMERR_BADSPEC;
	}

	{   const char *chptr = doUntar(spec, num, quietly);
	    if (chptr == NULL)
		return RPMERR_BADSPEC;

	    appendLineStringBuf((arg == 'a' ? after : before), chptr);
	}
    }

    if (arg < -1) {
	rpmError(RPMERR_BADSPEC, _("line %d: Bad %%setup option %s: %s\n"),
		 spec->lineNum,
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS), 
		 poptStrerror(arg));
	before = freeStringBuf(before);
	after = freeStringBuf(after);
	optCon = poptFreeContext(optCon);
	argv = _free(argv);
	return RPMERR_BADSPEC;
    }

    if (dirName) {
	spec->buildSubdir = xstrdup(dirName);
    } else {
	const char *name, *version;
	(void) headerNVR(spec->packages->header, &name, &version, NULL);
	sprintf(buf, "%s-%s", name, version);
	spec->buildSubdir = xstrdup(buf);
    }
    addMacro(spec->macros, "buildsubdir", NULL, spec->buildSubdir, RMIL_SPEC);
    
    optCon = poptFreeContext(optCon);
    argv = _free(argv);

    /* cd to the build dir */
    {	const char * buildDirURL = rpmGenPath(spec->rootURL, "%{_builddir}", "");
	const char *buildDir;

	(void) urlPath(buildDirURL, &buildDir);
	sprintf(buf, "cd '%s'", buildDir);
	appendLineStringBuf(spec->prep, buf);
	buildDirURL = _free(buildDirURL);
    }
    
    /* delete any old sources */
    if (!leaveDirs) {
	sprintf(buf, "rm -rf '%s'", spec->buildSubdir);
	appendLineStringBuf(spec->prep, buf);
    }

    /* if necessary, create and cd into the proper dir */
    if (createDir) {
	sprintf(buf, MKDIR_P " '%s'\ncd '%s'",
		spec->buildSubdir, spec->buildSubdir);
	appendLineStringBuf(spec->prep, buf);
    }

    /* do the default action */
   if (!createDir && !skipDefaultAction) {
	const char *chptr = doUntar(spec, 0, quietly);
	if (!chptr)
	    return RPMERR_BADSPEC;
	appendLineStringBuf(spec->prep, chptr);
    }

    appendStringBuf(spec->prep, getStringBuf(before));
    before = freeStringBuf(before);

    if (!createDir) {
	sprintf(buf, "cd '%s'", spec->buildSubdir);
	appendLineStringBuf(spec->prep, buf);
    }

    if (createDir && !skipDefaultAction) {
	const char * chptr = doUntar(spec, 0, quietly);
	if (chptr == NULL)
	    return RPMERR_BADSPEC;
	appendLineStringBuf(spec->prep, chptr);
    }
    
    appendStringBuf(spec->prep, getStringBuf(after));
    after = freeStringBuf(after);

    /* XXX FIXME: owner & group fixes were conditioned on !geteuid() */
    /* Fix the owner, group, and permissions of the setup build tree */
    {	/*@observer@*/ static const char *fixmacs[] =
		{ "%{_fixowner}", "%{_fixgroup}", "%{_fixperms}", NULL };
	const char ** fm;

	for (fm = fixmacs; *fm; fm++) {
	    const char *fix;
/*@-boundsread@*/
	    fix = rpmExpand(*fm, " .", NULL);
	    if (fix && *fix != '%')
		appendLineStringBuf(spec->prep, fix);
	    fix = _free(fix);
/*@=boundsread@*/
	}
    }
    
    return 0;
}

/**
 * Check that all sources/patches/icons exist locally, fetching if necessary.
 */
static int prepFetch(Spec spec)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/
{
    const char *Lmacro, *Lurlfn = NULL;
    const char *Rmacro, *Rurlfn = NULL;
    struct Source *sp;
    struct stat st;
    rpmRC rpmrc;
    int ec, rc;

    /* XXX insure that %{_sourcedir} exists */
    rpmrc = RPMRC_OK;
    Lurlfn = rpmGenPath(NULL, "%{?_sourcedir}", NULL);
    if (Lurlfn != NULL && *Lurlfn != '\0')
	rpmrc = rpmMkdirPath(Lurlfn, "_sourcedir");
    Lurlfn = _free(Lurlfn);
    if (rpmrc != RPMRC_OK)
	return -1;

    /* XXX insure that %{_patchdir} exists */
    rpmrc = RPMRC_OK;
    Lurlfn = rpmGenPath(NULL, "%{?_patchdir}", NULL);
    if (Lurlfn != NULL && *Lurlfn != '\0')
	rpmrc = rpmMkdirPath(Lurlfn, "_patchdir");
    Lurlfn = _free(Lurlfn);
    if (rpmrc != RPMRC_OK)
	return -1;

    /* XXX insure that %{_icondir} exists */
    rpmrc = RPMRC_OK;
    Lurlfn = rpmGenPath(NULL, "%{?_icondir}", NULL);
    if (Lurlfn != NULL && *Lurlfn != '\0')
	rpmrc = rpmMkdirPath(Lurlfn, "_icondir");
    Lurlfn = _free(Lurlfn);
    if (rpmrc != RPMRC_OK)
	return -1;

/*@-branchstate@*/
    ec = 0;
    for (sp = spec->sources; sp != NULL; sp = sp->next) {

	if (sp->flags & RPMFILE_SOURCE) {
	    Rmacro = "%{_Rsourcedir}/";
	    Lmacro = "%{_sourcedir}/";
	} else
	if (sp->flags & RPMFILE_PATCH) {
	    Rmacro = "%{_Rpatchdir}/";
	    Lmacro = "%{_patchdir}/";
	} else
	if (sp->flags & RPMFILE_ICON) {
	    Rmacro = "%{_Ricondir}/";
	    Lmacro = "%{_icondir}/";
	} else
	    continue;

	Lurlfn = rpmGenPath(NULL, Lmacro, sp->source);
	rc = Lstat(Lurlfn, &st);
	if (rc == 0)
	    goto bottom;
	if (errno != ENOENT) {
	    ec++;
	    rpmError(RPMERR_BADFILENAME, _("Missing %s%d %s: %s\n"),
		((sp->flags & RPMFILE_SOURCE) ? "Source" : "Patch"),
		sp->num, sp->source, strerror(ENOENT));
	    goto bottom;
	}

	Rurlfn = rpmGenPath(NULL, Rmacro, sp->source);
	if (Rurlfn == NULL || *Rurlfn == '%' || !strcmp(Lurlfn, Rurlfn)) {
	    rpmError(RPMERR_BADFILENAME, _("file %s missing: %s\n"),
		Lurlfn, strerror(errno));
	    ec++;
	    goto bottom;
	}

	rc = urlGetFile(Rurlfn, Lurlfn);
	if (rc != 0) {
	    rpmError(RPMERR_BADFILENAME, _("Fetching %s failed: %s\n"),
		Rurlfn, ftpStrerror(rc));
	    ec++;
	    goto bottom;
	}

bottom:
	Lurlfn = _free(Lurlfn);
	Rurlfn = _free(Rurlfn);
    }
/*@=branchstate@*/

    return ec;
}

int parsePrep(Spec spec, int verify)
{
    int nextPart, res, rc;
    StringBuf sb;
    char **lines, **saveLines;
    char *cp;

    if (spec->prep != NULL) {
	rpmError(RPMERR_BADSPEC, _("line %d: second %%prep\n"), spec->lineNum);
	return RPMERR_BADSPEC;
    }

    spec->prep = newStringBuf();

    /* There are no options to %prep */
    if ((rc = readLine(spec, STRIP_NOTHING)) > 0)
	return PART_NONE;
    if (rc)
	return rc;

    /* Check to make sure that all sources/patches are present. */
    if (verify) {
	rc = prepFetch(spec);
	if (rc)
	    return RPMERR_BADSPEC;
    }
    
    sb = newStringBuf();
    
    while (! (nextPart = isPart(spec->line))) {
	/* Need to expand the macros inline.  That way we  */
	/* can give good line number information on error. */
	appendStringBuf(sb, spec->line);
	if ((rc = readLine(spec, STRIP_NOTHING)) > 0) {
	    nextPart = PART_NONE;
	    break;
	}
	if (rc)
	    return rc;
    }

    saveLines = splitString(getStringBuf(sb), strlen(getStringBuf(sb)), '\n');
    /*@-usereleased@*/
    for (lines = saveLines; *lines; lines++) {
	res = 0;
	for (cp = *lines; *cp == ' ' || *cp == '\t'; cp++)
	    ;
/*@-boundsread@*/
	if (! strncmp(cp, "%setup", sizeof("%setup")-1))
	    res = doSetupMacro(spec, cp);
	else
	    appendLineStringBuf(spec->prep, *lines);
/*@=boundsread@*/
	if (res && !spec->force) {
	    freeSplitString(saveLines);
	    sb = freeStringBuf(sb);
	    return res;
	}
    }
    /*@=usereleased@*/

    freeSplitString(saveLines);
    sb = freeStringBuf(sb);

    return nextPart;
}
