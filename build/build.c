/** \ingroup rpmbuild
 * \file build/build.c
 *  Top-level build dispatcher.
 */

#include "system.h"

#include <rpmio_internal.h>
#include <rpmbuild.h>
#include "signature.h"		/* XXX rpmTempFile */

#include "debug.h"

/*@unchecked@*/
static int _build_debug = 0;

/**
 */
const char * getSourceDir(rpmfileAttrs attr)
    /*@globals rpmGlobalMacroContext @*/
{
    const char * dir = NULL;

    if (attr & RPMFILE_SOURCE)
        dir = "%{_sourcedir}/";
    else if (attr & RPMFILE_PATCH)
        dir = "%{_patchdir}/";
    else if (attr & RPMFILE_ICON)
        dir = "%{_icondir}/";

    return dir;
}

/*@access StringBuf @*/
/*@access urlinfo @*/		/* XXX compared with NULL */
/*@access FD_t @*/

/**
 */
static void doRmSource(Spec spec)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState  @*/
{
    struct Source *sp;
    int rc;
    
#if 0
    rc = Unlink(spec->specFile);
#endif

    for (sp = spec->sources; sp != NULL; sp = sp->next) {
	const char *dn, *fn;
    if (sp->flags & RPMFILE_GHOST)
        continue;
    if (! (dn = getSourceDir(sp->flags)))
        continue;
	fn = rpmGenPath(NULL, dn, sp->source);
	rc = Unlink(fn);
	fn = _free(fn);
    }
}

/*
 * @todo Single use by %%doc in files.c prevents static.
 */
int doScript(Spec spec, int what, const char *name, StringBuf sb, int test)
{
    const char * rootURL = spec->rootURL;
    const char * rootDir;
    const char *scriptName = NULL;
    const char * buildDirURL = rpmGenPath(rootURL, "%{_builddir}", "");
    const char * buildScript;
    const char * buildCmd = NULL;
    const char * buildTemplate = NULL;
    const char * buildPost = NULL;
    const char * mTemplate = NULL;
    const char * mCmd = NULL;
    const char * mPost = NULL;
    int argc = 0;
    const char **argv = NULL;
    FILE * fp = NULL;
    urlinfo u = NULL;

    FD_t fd;
    FD_t xfd;
    int child;
    int status;
    int rc;
    
    /*@-branchstate@*/
    switch (what) {
    case RPMBUILD_PREP:
	name = "%prep";
	sb = spec->prep;
	mTemplate = "%{__spec_prep_template}";
	mPost = "%{__spec_prep_post}";
	mCmd = "%{__spec_prep_cmd}";
	break;
    case RPMBUILD_BUILD:
	name = "%build";
	sb = spec->build;
	mTemplate = "%{__spec_build_template}";
	mPost = "%{__spec_build_post}";
	mCmd = "%{__spec_build_cmd}";
	break;
    case RPMBUILD_INSTALL:
	name = "%install";
	sb = spec->install;
	mTemplate = "%{__spec_install_template}";
	mPost = "%{__spec_install_post}";
	mCmd = "%{__spec_install_cmd}";
	break;
    case RPMBUILD_CHECK:
	name = "%check";
	sb = spec->check;
	mTemplate = "%{__spec_check_template}";
	mPost = "%{__spec_check_post}";
	mCmd = "%{__spec_check_cmd}";
	break;
    case RPMBUILD_CLEAN:
	name = "%clean";
	sb = spec->clean;
	mTemplate = "%{__spec_clean_template}";
	mPost = "%{__spec_clean_post}";
	mCmd = "%{__spec_clean_cmd}";
	break;
    case RPMBUILD_RMBUILD:
	name = "--clean";
	mTemplate = "%{__spec_clean_template}";
	mPost = "%{__spec_clean_post}";
	mCmd = "%{__spec_clean_cmd}";
	break;
    case RPMBUILD_STRINGBUF:
    default:
	mTemplate = "%{___build_template}";
	mPost = "%{___build_post}";
	mCmd = "%{___build_cmd}";
	break;
    }
    if (name == NULL)	/* XXX shouldn't happen */
	name = "???";
    /*@=branchstate@*/

    if ((what != RPMBUILD_RMBUILD) && sb == NULL) {
	rc = 0;
	goto exit;
    }
    
    if (rpmTempFile(rootURL, &scriptName, &fd) || fd == NULL || Ferror(fd)) {
	rpmError(RPMERR_SCRIPT, _("Unable to open temp file.\n"));
	rc = RPMERR_SCRIPT;
	goto exit;
    }

    /*@-branchstate@*/
    if (fdGetFp(fd) == NULL)
	xfd = Fdopen(fd, "w.fpio");
    else
	xfd = fd;
    /*@=branchstate@*/

    /*@-type@*/ /* FIX: cast? */
    if ((fp = fdGetFp(xfd)) == NULL) {
	rc = RPMERR_SCRIPT;
	goto exit;
    }
    /*@=type@*/
    
    (void) urlPath(rootURL, &rootDir);
    /*@-branchstate@*/
    if (*rootDir == '\0') rootDir = "/";
    /*@=branchstate@*/

    (void) urlPath(scriptName, &buildScript);

    buildTemplate = rpmExpand(mTemplate, NULL);
    buildPost = rpmExpand(mPost, NULL);

    (void) fputs(buildTemplate, fp);

    if (what != RPMBUILD_PREP && what != RPMBUILD_RMBUILD && spec->buildSubdir)
	fprintf(fp, "cd '%s'\n", spec->buildSubdir);

    if (what == RPMBUILD_RMBUILD) {
	if (spec->buildSubdir)
	    fprintf(fp, "rm -rf '%s'\n", spec->buildSubdir);
    } else if (sb != NULL)
	fprintf(fp, "%s", getStringBuf(sb));

    (void) fputs(buildPost, fp);
    
    (void) Fclose(xfd);

    if (test) {
	rc = 0;
	goto exit;
    }
    
if (_build_debug)
fprintf(stderr, "*** rootURL %s buildDirURL %s\n", rootURL, buildDirURL);
/*@-boundsread@*/
    if (buildDirURL && buildDirURL[0] != '/' &&
	(urlSplit(buildDirURL, &u) != 0)) {
	rc = RPMERR_SCRIPT;
	goto exit;
    }
/*@=boundsread@*/
    if (u != NULL) {
	switch (u->urltype) {
	case URL_IS_HTTPS:
	case URL_IS_HTTP:
	case URL_IS_FTP:
if (_build_debug)
fprintf(stderr, "*** addMacros\n");
	    addMacro(spec->macros, "_remsh", NULL, "%{__remsh}", RMIL_SPEC);
	    addMacro(spec->macros, "_remhost", NULL, u->host, RMIL_SPEC);
	    if (strcmp(rootDir, "/"))
		addMacro(spec->macros, "_remroot", NULL, rootDir, RMIL_SPEC);
	    break;
	case URL_IS_UNKNOWN:
	case URL_IS_DASH:
	case URL_IS_PATH:
	case URL_IS_HKP:
	default:
	    break;
	}
    }

    buildCmd = rpmExpand(mCmd, " ", buildScript, NULL);
    (void) poptParseArgvString(buildCmd, &argc, &argv);

    rpmMessage(RPMMESS_NORMAL, _("Executing(%s): %s\n"), name, buildCmd);
    if (!(child = fork())) {

	/*@-mods@*/
	errno = 0;
	/*@=mods@*/
/*@-boundsread@*/
	(void) execvp(argv[0], (char *const *)argv);
/*@=boundsread@*/

	rpmError(RPMERR_SCRIPT, _("Exec of %s failed (%s): %s\n"),
		scriptName, name, strerror(errno));

	_exit(-1);
    }

    rc = waitpid(child, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmError(RPMERR_SCRIPT, _("Bad exit status from %s (%s)\n"),
		 scriptName, name);
	rc = RPMERR_SCRIPT;
    } else
	rc = 0;
    
exit:
    if (scriptName) {
	if (!rc)
	    (void) Unlink(scriptName);
	scriptName = _free(scriptName);
    }
    if (u != NULL) {
	switch (u->urltype) {
	case URL_IS_HTTPS:
	case URL_IS_HTTP:
	case URL_IS_FTP:
if (_build_debug)
fprintf(stderr, "*** delMacros\n");
	    delMacro(spec->macros, "_remsh");
	    delMacro(spec->macros, "_remhost");
	    if (strcmp(rootDir, "/"))
		delMacro(spec->macros, "_remroot");
	    break;
	case URL_IS_UNKNOWN:
	case URL_IS_DASH:
	case URL_IS_PATH:
	case URL_IS_HKP:
	default:
	    break;
	}
    }
    argv = _free(argv);
    buildCmd = _free(buildCmd);
    buildTemplate = _free(buildTemplate);
    buildPost = _free(buildPost);
    buildDirURL = _free(buildDirURL);

    return rc;
}

int buildSpec(rpmts ts, Spec spec, int what, int test)
{
    int rc = 0;

    if (!spec->recursing && spec->BACount) {
	int x;
	/* When iterating over BANames, do the source    */
	/* packaging on the first run, and skip RMSOURCE altogether */
	if (spec->BASpecs != NULL)
	for (x = 0; x < spec->BACount; x++) {
/*@-boundsread@*/
	    if ((rc = buildSpec(ts, spec->BASpecs[x],
				(what & ~RPMBUILD_RMSOURCE) |
				(x ? 0 : (what & RPMBUILD_PACKAGESOURCE)),
				test))) {
		goto exit;
	    }
/*@=boundsread@*/
	}
    } else {
	if ((what & RPMBUILD_PREP) &&
	    (rc = doScript(spec, RPMBUILD_PREP, NULL, NULL, test)))
		goto exit;

	if ((what & RPMBUILD_BUILD) &&
	    (rc = doScript(spec, RPMBUILD_BUILD, NULL, NULL, test)))
		goto exit;

	if ((what & RPMBUILD_INSTALL) &&
	    (rc = doScript(spec, RPMBUILD_INSTALL, NULL, NULL, test)))
		goto exit;

	if ((what & RPMBUILD_CHECK) &&
	    (rc = doScript(spec, RPMBUILD_CHECK, NULL, NULL, test)))
		goto exit;

	if ((what & RPMBUILD_PACKAGESOURCE) &&
	    (rc = processSourceFiles(spec)))
		goto exit;

	if (((what & RPMBUILD_INSTALL) || (what & RPMBUILD_PACKAGEBINARY) ||
	    (what & RPMBUILD_FILECHECK)) &&
	    (rc = processBinaryFiles(spec, what & RPMBUILD_INSTALL, test)))
		goto exit;

	if (((what & RPMBUILD_PACKAGESOURCE) && !test) &&
	    (rc = packageSources(spec)))
		return rc;

	if (((what & RPMBUILD_PACKAGEBINARY) && !test) &&
	    (rc = packageBinaries(spec)))
		goto exit;
	
	if ((what & RPMBUILD_CLEAN) &&
	    (rc = doScript(spec, RPMBUILD_CLEAN, NULL, NULL, test)))
		goto exit;

	if ((what & RPMBUILD_RMBUILD) &&
	    (rc = doScript(spec, RPMBUILD_RMBUILD, NULL, NULL, test)))
		goto exit;
    }

    if (what & RPMBUILD_RMSOURCE)
	doRmSource(spec);

    if (what & RPMBUILD_RMSPEC)
	(void) Unlink(spec->specFile);

exit:
    if (rc && rpmlogGetNrecs() > 0) {
	rpmMessage(RPMMESS_NORMAL, _("\n\nRPM build errors:\n"));
	rpmlogPrint(NULL);
    }

    return rc;
}
