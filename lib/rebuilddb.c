#include "system.h"

#include <rpmlib.h>
#include <rpmmacro.h>	/* XXX for rpmGetPath */

#include "rpmdb.h"

/** */
int rpmdbRebuild(const char * rootdir)
{
    rpmdb olddb, newdb;
    const char * dbpath = NULL;
    const char * rootdbpath = NULL;
    const char * newdbpath = NULL;
    const char * newrootdbpath = NULL;
    const char * tfn;
    int recnum; 
    Header h;
    int nocleanup = 1;
    int failed = 0;
    int rc = 0;
    int unique = 1;	/* XXX always eliminate duplicate entries */

    tfn = rpmGetPath("%{_dbpath}", NULL);
    if (!(tfn && tfn[0] != '%')) {
	rpmMessage(RPMMESS_DEBUG, _("no dbpath has been set"));
	rc = 1;
	goto exit;
    }
    dbpath = rootdbpath = rpmGetPath(rootdir, tfn, NULL);
    if (!(rootdir[0] == '/' && rootdir[1] == '\0'))
	dbpath += strlen(rootdir);
    xfree(tfn);

    tfn = rpmGetPath("%{_dbpath_rebuild}", NULL);
    if (!(tfn && tfn[0] != '%' && strcmp(tfn, dbpath))) {
	char pidbuf[20];
	char *t;
	sprintf(pidbuf, "rebuilddb.%d", (int) getpid());
	t = xmalloc(strlen(dbpath) + strlen(pidbuf) + 1);
	stpcpy(stpcpy(t, dbpath), pidbuf);
	if (tfn) xfree(tfn);
	tfn = t;
	nocleanup = 0;
    }
    newdbpath = newrootdbpath = rpmGetPath(rootdir, tfn, NULL);
    if (!(rootdir[0] == '/' && rootdir[1] == '\0'))
	newdbpath += strlen(rootdir);
    xfree(tfn);

    rpmMessage(RPMMESS_DEBUG, _("rebuilding database %s into %s\n"),
	rootdbpath, newrootdbpath);

    if (!access(newrootdbpath, F_OK)) {
	rpmError(RPMERR_MKDIR, _("temporary database %s already exists"),
	      newrootdbpath);
	rc = 1;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("creating directory: %s\n"), newrootdbpath);
    if (Mkdir(newrootdbpath, 0755)) {
	rpmError(RPMERR_MKDIR, _("error creating directory %s: %s"),
	      newrootdbpath, strerror(errno));
	rc = 1;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("opening old database\n"));
    if (openDatabase(rootdir, dbpath, &olddb, O_RDONLY, 0644, 
		     RPMDB_FLAG_MINIMAL)) {
	rc = 1;
	goto exit;
    }

    rpmMessage(RPMMESS_DEBUG, _("opening new database\n"));
    if (openDatabase(rootdir, newdbpath, &newdb, O_RDWR | O_CREAT, 0644, 0)) {
	rc = 1;
	goto exit;
    }

    recnum = rpmdbFirstRecNum(olddb);
    while (recnum > 0) {
	if ((h = rpmdbGetRecord(olddb, recnum)) == NULL) {
	    rpmError(RPMERR_INTERNAL,
			_("record number %d in database is bad -- skipping it"),
			recnum);
	    break;
	} else {
	    /* let's sanity check this record a bit, otherwise just skip it */
	    if (headerIsEntry(h, RPMTAG_NAME) &&
		headerIsEntry(h, RPMTAG_VERSION) &&
		headerIsEntry(h, RPMTAG_RELEASE) &&
		headerIsEntry(h, RPMTAG_BUILDTIME)) {
		dbiIndexSet matches;
		int skip;

		/* XXX always eliminate duplicate entries */
		if (unique && !rpmdbFindByHeader(newdb, h, &matches)) {
		    const char * name, * version, * release;
		    headerNVR(h, &name, &version, &release);

		    rpmError(RPMERR_INTERNAL,
			_("duplicated database entry: %s-%s-%s -- skipping."),
			name, version, release);
		    skip = 1;
		    dbiFreeIndexRecord(matches);
		} else
		    skip = 0;

		if (skip == 0 && rpmdbAdd(newdb, h)) {
		    rpmError(RPMERR_INTERNAL,
			_("cannot add record originally at %d"), recnum);
		    failed = 1;
		    break;
		}
	    } else {
		rpmError(RPMERR_INTERNAL,
			_("record number %d in database is bad -- skipping."), 
			recnum);
	    }

	    headerFree(h);
	}
	recnum = rpmdbNextRecNum(olddb, recnum);
    }

    rpmdbClose(olddb);
    rpmdbClose(newdb);

    if (failed) {
	rpmMessage(RPMMESS_NORMAL, _("failed to rebuild database; original database "
		"remains in place\n"));

	rpmdbRemoveDatabase(rootdir, newdbpath);
	rc = 1;
	goto exit;
    } else if (!nocleanup) {
	if (rpmdbMoveDatabase(rootdir, newdbpath, dbpath)) {
	    rpmMessage(RPMMESS_ERROR, _("failed to replace old database with new "
			"database!\n"));
	    rpmMessage(RPMMESS_ERROR, _("replaces files in %s with files from %s "
			"to recover"), dbpath, newdbpath);
	    rc = 1;
	    goto exit;
	}
	if (Rmdir(newrootdbpath))
	    rpmMessage(RPMMESS_ERROR, _("failed to remove directory %s: %s\n"),
			newrootdbpath, strerror(errno));
    }
    rc = 0;

exit:
    if (rootdbpath)		xfree(rootdbpath);
    if (newrootdbpath)	xfree(newrootdbpath);

    return rc;
}
