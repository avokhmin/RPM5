#include "system.h"

#include <sys/file.h>
#include <signal.h>
#include <sys/signal.h>

#include <rpmlib.h>
#include <rpmurl.h>
#include <rpmmacro.h>	/* XXX for rpmGetPath */

#include "dbindex.h"
/*@access dbiIndexSet@*/
/*@access dbiIndexRecord@*/

#include "falloc.h"
#include "fprint.h"
#include "misc.h"
#include "rpmdb.h"

extern int _noDirTokens;

#define	_DBI_FLAGS	0
#define	_DBI_PERMS	0644
#define	_DBI_MAJOR	-1

struct _dbiIndex rpmdbi[] = {
    { "packages.rpm", DBI_RECNO, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_PACKAGES		0
    { "nameindex.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_NAME		1
    { "fileindex.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_FILE		2
    { "groupindex.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_GROUP		3
    { "requiredby.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_REQUIREDBY	4
    { "providesindex.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_PROVIDES		5
    { "conflictsindex.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_CONFLICTS	6
    { "triggerindex.rpm", DBI_HASH, _DBI_FLAGS, _DBI_PERMS, _DBI_MAJOR,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL },
#define	RPMDBI_TRIGGER		7
    { NULL }
#define	RPMDBI_MAX		8
};

/* XXX the signal handling in here is not thread safe */

/* the requiredbyIndex isn't stricly necessary. In a perfect world, we could
   have each header keep a list of packages that need it. However, we
   can't reserve space in the header for extra information so all of the
   required packages would move in the database every time a package was
   added or removed. Instead, each package (or virtual package) name
   keeps a list of package offsets of packages that might depend on this
   one. Version numbers still need verification, but it gets us in the
   right area w/o a linear search through the database. */

struct rpmdb_s {
    FD_t pkgs;
    dbiIndex nameIndex;
    dbiIndex fileIndex;
    dbiIndex groupIndex;
    dbiIndex providesIndex;
    dbiIndex requiredbyIndex;
    dbiIndex conflictsIndex;
    dbiIndex triggerIndex;
};

static sigset_t signalMask;

static void blockSignals(void)
{
    sigset_t newMask;

    sigfillset(&newMask);		/* block all signals */
    sigprocmask(SIG_BLOCK, &newMask, &signalMask);
}

static void unblockSignals(void)
{
    sigprocmask(SIG_SETMASK, &signalMask, NULL);
}

static int openDbFile(const char * prefix, const char * dbpath, int dbix,
	 int justCheck, int mode, dbiIndex * dbip)
{
    dbiIndex dbi;
    char * filename, * fn;
    int len;

    if (dbix < 0 || dbix >= RPMDBI_MAX)
	return 1;
    if (dbip == NULL)
	return 1;
    *dbip = NULL;

    dbi = rpmdbi + dbix;
    len = (prefix ? strlen(prefix) : 0) +
		strlen(dbpath) + strlen(dbi->dbi_basename) + 1;
    fn = filename = alloca(len);
    *fn = '\0';
    switch (urlIsURL(dbpath)) {
    case URL_IS_UNKNOWN:
	if (prefix && *prefix &&
	    !(prefix[0] == '/' && prefix[1] == '\0' && dbpath[0] == '/'))
		fn = stpcpy(fn, prefix);
	break;
    default:
	break;
    }
    fn = stpcpy(fn, dbpath);
    if (fn > filename && !(fn[-1] == '/' || dbi->dbi_basename[0] == '/'))
	fn = stpcpy(fn, "/");
    fn = stpcpy(fn, dbi->dbi_basename);

    if (!justCheck || !rpmfileexists(filename)) {
	if ((*dbip = dbiOpenIndex(filename, mode, dbi->dbi_perms, dbi->dbi_type)) == NULL)
	    return 1;
    }

    return 0;
}

static /*@only@*/ rpmdb newRpmdb(void)
{
    rpmdb db = xmalloc(sizeof(*db));
    db->pkgs = NULL;
    db->nameIndex = NULL;
    db->fileIndex = NULL;
    db->groupIndex = NULL;
    db->providesIndex = NULL;
    db->requiredbyIndex = NULL;
    db->conflictsIndex = NULL;
    db->triggerIndex = NULL;
    return db;
}

int openDatabase(const char * prefix, const char * dbpath, rpmdb *rpmdbp, int mode, 
		 int perms, int flags)
{
    char * filename;
    rpmdb db;
    int i, rc;
    struct flock lockinfo;
    int justcheck = flags & RPMDB_FLAG_JUSTCHECK;
    int minimal = flags & RPMDB_FLAG_MINIMAL;
    const char * akey;

    if (mode & O_WRONLY) 
	return 1;

    if (!(perms & 0600))	/* XXX sanity */
	perms = 0644;

    /* we should accept NULL as a valid prefix */
    if (!prefix) prefix="";

    i = strlen(dbpath);
    if (dbpath[i - 1] != '/') {
	filename = alloca(i + 2);
	strcpy(filename, dbpath);
	filename[i] = '/';
	filename[i + 1] = '\0';
	dbpath = filename;
    }
    
    filename = alloca(strlen(prefix) + strlen(dbpath) + 40);
    *filename = '\0';

    switch (urlIsURL(dbpath)) {
    case URL_IS_UNKNOWN:
	strcat(filename, prefix); 
	break;
    default:
	break;
    }
    strcat(filename, dbpath);
    (void)rpmCleanPath(filename);

    rpmMessage(RPMMESS_DEBUG, _("opening database mode 0x%x in %s\n"),
	mode, filename);

    if (filename[strlen(filename)-1] != '/')
	strcat(filename, "/");
    strcat(filename, "packages.rpm");

    db = newRpmdb();

    if (!justcheck || !rpmfileexists(filename)) {
	db->pkgs = fadOpen(filename, mode, perms);
	if (Ferror(db->pkgs)) {
	    rpmError(RPMERR_DBOPEN, _("failed to open %s: %s\n"), filename,
		Fstrerror(db->pkgs));
	    return 1;
	}

	/* try and get a lock - this is released by the kernel when we close
	   the file */
	lockinfo.l_whence = 0;
	lockinfo.l_start = 0;
	lockinfo.l_len = 0;
	
	if (mode & O_RDWR) {
	    lockinfo.l_type = F_WRLCK;
	    if (Fcntl(db->pkgs, F_SETLK, (void *) &lockinfo)) {
		rpmError(RPMERR_FLOCK, _("cannot get %s lock on database"), 
			 _("exclusive"));
		rpmdbClose(db);
		return 1;
	    } 
	} else {
	    lockinfo.l_type = F_RDLCK;
	    if (Fcntl(db->pkgs, F_SETLK, (void *) &lockinfo)) {
		rpmError(RPMERR_FLOCK, _("cannot get %s lock on database"), 
			 _("shared"));
		rpmdbClose(db);
		return 1;
	    } 
	}
    }

    rc = openDbFile(prefix, dbpath, RPMDBI_NAME, justcheck, mode,
		    &db->nameIndex);

    if (minimal) {
	*rpmdbp = xmalloc(sizeof(struct rpmdb_s));
	if (rpmdbp)
	    *rpmdbp = db;	/* structure assignment */
	else
	    rpmdbClose(db);
	return 0;
    }

    if (!rc)
	rc = openDbFile(prefix, dbpath, RPMDBI_FILE, justcheck, mode,
			&db->fileIndex);

    /* We used to store the fileindexes as complete paths, rather then
       plain basenames. Let's see which version we are... */
    /*
     * XXX FIXME: db->fileindex can be NULL under pathological (e.g. mixed
     * XXX db1/db2 linkage) conditions.
     */
    if (!justcheck && !dbiGetFirstKey(db->fileIndex, &akey)) {
	if (strchr(akey, '/')) {
	    rpmError(RPMERR_OLDDB, _("old format database is present; "
			"use --rebuilddb to generate a new format database"));
	    rc |= 1;
	}
	xfree(akey);
    }

    if (!rc)
	rc = openDbFile(prefix, dbpath, RPMDBI_GROUP, justcheck, mode,
			&db->groupIndex);
    if (!rc)
	rc = openDbFile(prefix, dbpath, RPMDBI_REQUIREDBY, justcheck, mode,
			&db->requiredbyIndex);
    if (!rc)
	rc = openDbFile(prefix, dbpath, RPMDBI_PROVIDES, justcheck, mode,
			&db->providesIndex);
    if (!rc)
	rc = openDbFile(prefix, dbpath, RPMDBI_CONFLICTS, justcheck, mode,
			&db->conflictsIndex);
    if (!rc)
	rc = openDbFile(prefix, dbpath, RPMDBI_TRIGGER, justcheck, mode,
			&db->triggerIndex);

    if (rc || justcheck || rpmdbp == NULL)
	rpmdbClose(db);
     else
	*rpmdbp = db;

     return rc;
}

static int doRpmdbOpen (const char * prefix, /*@out@*/ rpmdb * rpmdbp,
			int mode, int perms, int flags)
{
    const char * dbpath = rpmGetPath("%{_dbpath}", NULL);
    int rc;

    if (!(dbpath && dbpath[0] != '%')) {
	rpmMessage(RPMMESS_DEBUG, _("no dbpath has been set"));
	rc = 1;
    } else
    	rc = openDatabase(prefix, dbpath, rpmdbp, mode, perms, flags);
    xfree(dbpath);
    return rc;
}

int rpmdbOpenForTraversal(const char * prefix, rpmdb * rpmdbp)
{
    return doRpmdbOpen(prefix, rpmdbp, O_RDONLY, 0644, RPMDB_FLAG_MINIMAL);
}

int rpmdbOpen (const char * prefix, rpmdb *rpmdbp, int mode, int perms)
{
    return doRpmdbOpen(prefix, rpmdbp, mode, perms, 0);
}

int rpmdbInit (const char * prefix, int perms)
{
    rpmdb db;
    return doRpmdbOpen(prefix, &db, (O_CREAT | O_RDWR), perms, RPMDB_FLAG_JUSTCHECK);
}

void rpmdbClose (rpmdb db)
{
    if (db->pkgs != NULL) Fclose(db->pkgs);
    if (db->fileIndex) dbiCloseIndex(db->fileIndex);
    if (db->groupIndex) dbiCloseIndex(db->groupIndex);
    if (db->nameIndex) dbiCloseIndex(db->nameIndex);
    if (db->providesIndex) dbiCloseIndex(db->providesIndex);
    if (db->requiredbyIndex) dbiCloseIndex(db->requiredbyIndex);
    if (db->conflictsIndex) dbiCloseIndex(db->conflictsIndex);
    if (db->triggerIndex) dbiCloseIndex(db->triggerIndex);
    free(db);
}

int rpmdbFirstRecNum(rpmdb db) {
    return fadFirstOffset(db->pkgs);
}

int rpmdbNextRecNum(rpmdb db, unsigned int lastOffset) {
    /* 0 at end */
    return fadNextOffset(db->pkgs, lastOffset);
}

static Header doGetRecord(rpmdb db, unsigned int offset, int pristine)
{
    Header h;
    const char ** fileNames;
    int fileCount = 0;
    int i;

    (void)Fseek(db->pkgs, offset, SEEK_SET);

    h = headerRead(db->pkgs, HEADER_MAGIC_NO);

    if (pristine || h == NULL) return h;

    /* the RPM used to build much of RH 5.1 could produce packages whose
       file lists did not have leading /'s. Now is a good time to fix
       that */

    /* If this tag isn't present, either no files are in the package or
       we're dealing with a package that has just the compressed file name
       list */
    if (!headerGetEntryMinMemory(h, RPMTAG_OLDFILENAMES, NULL, 
			   (void **) &fileNames, &fileCount)) return h;

    for (i = 0; i < fileCount; i++) 
	if (*fileNames[i] != '/') break;

    if (i == fileCount) {
	free(fileNames);
    } else {	/* bad header -- let's clean it up */
	const char ** newFileNames = alloca(sizeof(*newFileNames) * fileCount);
	for (i = 0; i < fileCount; i++) {
	    char * newFileName = alloca(strlen(fileNames[i]) + 2);
	    if (*fileNames[i] != '/') {
		newFileName[0] = '/';
		newFileName[1] = '\0';
	    } else
		newFileName[0] = '\0';
	    strcat(newFileName, fileNames[i]);
	    newFileNames[i] = newFileName;
	}

	free(fileNames);

	headerModifyEntry(h, RPMTAG_OLDFILENAMES, RPM_STRING_ARRAY_TYPE, 
			  newFileNames, fileCount);
    }

    /* The file list was moved to a more compressed format which not
       only saves memory (nice), but gives fingerprinting a nice, fat
       speed boost (very nice). Go ahead and convert old headers to
       the new style (this is a noop for new headers) */
    compressFilelist(h);

    return h;
}

Header rpmdbGetRecord(rpmdb db, unsigned int offset)
{
    return doGetRecord(db, offset, 0);
}

int rpmdbFindByFile(rpmdb db, const char * filespec, dbiIndexSet * matches)
{
    const char * dirName;
    const char * baseName;
    fingerPrint fp1, fp2;
    dbiIndexSet allMatches = NULL;
    int i, rc;
    fingerPrintCache fpc;

    *matches = NULL;
    if ((baseName = strrchr(filespec, '/')) != NULL) {
    	char * t;
	size_t len;

    	len = baseName - filespec + 1;
	t = strncpy(alloca(len + 1), filespec, len);
	t[len] = '\0';
	dirName = t;
	baseName++;
    } else {
	dirName = "";
	baseName = filespec;
    }

    fpc = fpCacheCreate(20);
    fp1 = fpLookup(fpc, dirName, baseName, 1);

    rc = dbiSearchIndex(db->fileIndex, baseName, &allMatches);
    if (rc) {
	dbiFreeIndexSet(allMatches);
	allMatches = NULL;
	fpCacheFree(fpc);
	return rc;
    }

    *matches = dbiCreateIndexSet();
    i = 0;
    while (i < dbiIndexSetCount(allMatches)) {
	const char ** baseNames, ** dirNames;
	int_32 * dirIndexes;
	unsigned int recoff = dbiIndexRecordOffset(allMatches, i);
	unsigned int prevoff;
	Header h;

	if ((h = rpmdbGetRecord(db, recoff)) == NULL) {
	    i++;
	    continue;
	}

	headerGetEntryMinMemory(h, RPMTAG_BASENAMES, NULL, 
				(void **) &baseNames, NULL);
	headerGetEntryMinMemory(h, RPMTAG_DIRINDEXES, NULL, 
				(void **) &dirIndexes, NULL);
	headerGetEntryMinMemory(h, RPMTAG_DIRNAMES, NULL, 
				(void **) &dirNames, NULL);

	do {
	    int num = dbiIndexRecordFileNumber(allMatches, i);

	    fp2 = fpLookup(fpc, dirNames[dirIndexes[num]], baseNames[num], 1);
	    if (FP_EQUAL(fp1, fp2))
		dbiAppendIndexRecord(*matches, dbiIndexRecordOffset(allMatches, i), dbiIndexRecordFileNumber(allMatches, i));

	    prevoff = recoff;
	    i++;
	    recoff = dbiIndexRecordOffset(allMatches, i);
	} while (i < dbiIndexSetCount(allMatches) && 
		(i == 0 || recoff == prevoff));

	free(baseNames);
	free(dirNames);
	headerFree(h);
    }

    if (allMatches) {
	dbiFreeIndexSet(allMatches);
	allMatches = NULL;
    }

    fpCacheFree(fpc);

    if (dbiIndexSetCount(*matches) == 0) {
	dbiFreeIndexSet(*matches);
	*matches = NULL; 
	return 1;
    }

    return 0;
}

int rpmdbFindByProvides(rpmdb db, const char * filespec, dbiIndexSet * matches) {
    return dbiSearchIndex(db->providesIndex, filespec, matches);
}

int rpmdbFindByRequiredBy(rpmdb db, const char * filespec, dbiIndexSet * matches) {
    return dbiSearchIndex(db->requiredbyIndex, filespec, matches);
}

int rpmdbFindByConflicts(rpmdb db, const char * filespec, dbiIndexSet * matches) {
    return dbiSearchIndex(db->conflictsIndex, filespec, matches);
}

int rpmdbFindByTriggeredBy(rpmdb db, const char * filespec, dbiIndexSet * matches) {
    return dbiSearchIndex(db->triggerIndex, filespec, matches);
}

int rpmdbFindByGroup(rpmdb db, const char * group, dbiIndexSet * matches) {
    return dbiSearchIndex(db->groupIndex, group, matches);
}

int rpmdbFindPackage(rpmdb db, const char * name, dbiIndexSet * matches) {
    return dbiSearchIndex(db->nameIndex, name, matches);
}

static void removeIndexEntry(dbiIndex dbi, const char * key, dbiIndexRecord rec,
		             int tolerant, const char * idxName)
{
    dbiIndexSet matches = NULL;
    int rc;
    
    rc = dbiSearchIndex(dbi, key, &matches);
    switch (rc) {
      case 0:
	if (dbiRemoveIndexRecord(matches, rec) && !tolerant) {
	    rpmError(RPMERR_DBCORRUPT, _("package %s not listed in %s"),
		  key, idxName);
	} else {
	    dbiUpdateIndex(dbi, key, matches);
	       /* errors from above will be reported from dbindex.c */
	}
	break;
      case 1:
	if (!tolerant) 
	    rpmError(RPMERR_DBCORRUPT, _("package %s not found in %s"), 
			key, idxName);
	break;
      case 2:
	break;   /* error message already generated from dbindex.c */
    }
    if (matches) {
	dbiFreeIndexSet(matches);
	matches = NULL;
    }
}

int rpmdbRemove(rpmdb db, unsigned int offset, int tolerant)
{
    Header h;
    char * name, * group;
    int type;
    unsigned int count;
    dbiIndexRecord rec;
    char ** baseNames, ** providesList, ** requiredbyList;
    char ** conflictList, ** triggerList;
    int i;


    h = rpmdbGetRecord(db, offset);
    if (h == NULL) {
	rpmError(RPMERR_DBCORRUPT, _("cannot read header at %d for uninstall"),
	      offset);
	return 1;
    }

    rec = dbiReturnIndexRecordInstance(offset, 0);

    blockSignals();

    if (!headerGetEntry(h, RPMTAG_NAME, &type, (void **) &name, &count)) {
	rpmError(RPMERR_DBCORRUPT, _("package has no name"));
    } else {
	rpmMessage(RPMMESS_DEBUG, _("removing name index\n"));
	removeIndexEntry(db->nameIndex, name, rec, tolerant, "name index");
    }

    if (!headerGetEntry(h, RPMTAG_GROUP, &type, (void **) &group, &count)) {
	rpmMessage(RPMMESS_DEBUG, _("package has no group\n"));
    } else {
	rpmMessage(RPMMESS_DEBUG, _("removing group index\n"));
	removeIndexEntry(db->groupIndex, group, rec, tolerant, "group index");
    }

    if (headerGetEntry(h, RPMTAG_PROVIDENAME, &type, (void **) &providesList, 
	 &count)) {
	for (i = 0; i < count; i++) {
	    rpmMessage(RPMMESS_DEBUG, _("removing provides index for %s\n"), 
		    providesList[i]);
	    removeIndexEntry(db->providesIndex, providesList[i], rec, tolerant, 
			     "providesfile index");
	}
	free(providesList);
    }

    if (headerGetEntry(h, RPMTAG_REQUIRENAME, &type, (void **) &requiredbyList, 
	 &count)) {
	/* There could be dups in requiredByLIst, and the list is sorted.
	   Rather then sort the list, be tolerant of missing entries
	   as they should just indicate duplicated requirements. */

	for (i = 0; i < count; i++) {
	    rpmMessage(RPMMESS_DEBUG, _("removing requiredby index for %s\n"), 
		    requiredbyList[i]);
	    removeIndexEntry(db->requiredbyIndex, requiredbyList[i], rec, 
			     1, "requiredby index");
	}
	free(requiredbyList);
    }

    if (headerGetEntry(h, RPMTAG_TRIGGERNAME, &type, (void **) &triggerList, 
	 &count)) {
	/* triggerList often contains duplicates */
	for (i = 0; i < count; i++) {
	    rpmMessage(RPMMESS_DEBUG, _("removing trigger index for %s\n"), 
		       triggerList[i]);
	    removeIndexEntry(db->triggerIndex, triggerList[i], rec, 
			     1, "trigger index");
	}
	free(triggerList);
    }

    if (headerGetEntry(h, RPMTAG_CONFLICTNAME, &type, (void **) &conflictList, 
	 &count)) {
	for (i = 0; i < count; i++) {
	    rpmMessage(RPMMESS_DEBUG, _("removing conflict index for %s\n"), 
		    conflictList[i]);
	    removeIndexEntry(db->conflictsIndex, conflictList[i], rec, 
			     tolerant, "conflict index");
	}
	free(conflictList);
    }

    if (headerGetEntry(h, RPMTAG_BASENAMES, &type, (void **) &baseNames, 
	 &count)) {
	for (i = 0; i < count; i++) {
	    rpmMessage(RPMMESS_DEBUG, _("removing file index for %s\n"), 
			baseNames[i]);
	    /* structure assignment */
	    rec = dbiReturnIndexRecordInstance(offset, i);
	    removeIndexEntry(db->fileIndex, baseNames[i], rec, tolerant, 
			     "file index");
	}
	free(baseNames);
    } else {
	rpmMessage(RPMMESS_DEBUG, _("package has no files\n"));
    }

    fadFree(db->pkgs, offset);

    dbiSyncIndex(db->nameIndex);
    dbiSyncIndex(db->groupIndex);
    dbiSyncIndex(db->fileIndex);

    unblockSignals();

    dbiFreeIndexRecordInstance(rec);
    headerFree(h);

    return 0;
}

static int addIndexEntry(dbiIndex dbi, const char *index, unsigned int offset,
		         unsigned int fileNumber)
{
    dbiIndexSet set = NULL;
    int rc;

    rc = dbiSearchIndex(dbi, index, &set);
    switch (rc) {
    case -1:			/* error */
	if (set) {
	    dbiFreeIndexSet(set);
	    set = NULL;
	}
	return 1;
	/*@notreached@*/ break;
    case 1:			/* new item */
	set = dbiCreateIndexSet();
	break;
    default:
	break;
    }

    dbiAppendIndexRecord(set, offset, fileNumber);
    if (dbiUpdateIndex(dbi, index, set))
	exit(EXIT_FAILURE);	/* XXX W2DO? return 1; */

    if (set) {
	dbiFreeIndexSet(set);
	set = NULL;
    }

    return 0;
}

int rpmdbAdd(rpmdb db, Header dbentry)
{
    unsigned int dboffset;
    unsigned int i, j;
    const char ** baseNames;
    const char ** providesList;
    const char ** requiredbyList;
    const char ** conflictList;
    const char ** triggerList;
    const char * name;
    const char * group;
    int count = 0, providesCount = 0, requiredbyCount = 0, conflictCount = 0;
    int triggerCount = 0;
    int type;
    int newSize;
    int rc = 0;

    headerGetEntry(dbentry, RPMTAG_NAME, &type, (void **) &name, &count);
    headerGetEntry(dbentry, RPMTAG_GROUP, &type, (void **) &group, &count);

    if (!group) group = "Unknown";

    count = 0;

    headerGetEntry(dbentry, RPMTAG_BASENAMES, &type, (void **) 
		    &baseNames, &count);

    if (_noDirTokens) {
	const char ** newBaseNames;
	char * data;
	int len;
	len = count * sizeof(*baseNames);
	for (i = 0; i < count; i++)
	    len += strlen(baseNames[i]) + 1;
	newBaseNames = xmalloc(len);
	data = (char *) newBaseNames + count;
	for (i = 0; i < count; i++) {
	    newBaseNames[i] = data;
	    data = stpcpy(data, baseNames[i]);
	    *data++ = '\0';
	}
	expandFilelist(dbentry);
    }

    headerGetEntry(dbentry, RPMTAG_PROVIDENAME, &type, (void **) &providesList, 
	           &providesCount);
    headerGetEntry(dbentry, RPMTAG_REQUIRENAME, &type, 
		   (void **) &requiredbyList, &requiredbyCount);
    headerGetEntry(dbentry, RPMTAG_CONFLICTNAME, &type, 
		   (void **) &conflictList, &conflictCount);
    headerGetEntry(dbentry, RPMTAG_TRIGGERNAME, &type, 
		   (void **) &triggerList, &triggerCount);

    blockSignals();

    newSize = headerSizeof(dbentry, HEADER_MAGIC_NO);
    dboffset = fadAlloc(db->pkgs, newSize);
    if (!dboffset) {
	rc = 1;
    } else {
	(void)Fseek(db->pkgs, dboffset, SEEK_SET);
	fdSetContentLength(db->pkgs, newSize);
	rc = headerWrite(db->pkgs, dbentry, HEADER_MAGIC_NO);
	fdSetContentLength(db->pkgs, -1);
    }

    if (rc) {
	rpmError(RPMERR_DBCORRUPT, _("cannot allocate space for database"));
	goto exit;
    }

    /* Now update the appropriate indexes */
    if (addIndexEntry(db->nameIndex, name, dboffset, 0))
	rc = 1;
    if (addIndexEntry(db->groupIndex, group, dboffset, 0))
	rc = 1;

    for (i = 0; i < triggerCount; i++) {
	/* don't add duplicates */
	for (j = 0; j < i; j++)
	    if (!strcmp(triggerList[i], triggerList[j])) break;
	if (j == i)
	    rc += addIndexEntry(db->triggerIndex, triggerList[i], dboffset, 0);
    }

    for (i = 0; i < conflictCount; i++)
	rc += addIndexEntry(db->conflictsIndex, conflictList[i], dboffset, 0);

    for (i = 0; i < requiredbyCount; i++)
	rc += addIndexEntry(db->requiredbyIndex, requiredbyList[i], 
			    dboffset, 0);

    for (i = 0; i < providesCount; i++)
	rc += addIndexEntry(db->providesIndex, providesList[i], dboffset, 0);

    for (i = 0; i < count; i++) {
	rc += addIndexEntry(db->fileIndex, baseNames[i], dboffset, i);
    }

    dbiSyncIndex(db->nameIndex);
    dbiSyncIndex(db->groupIndex);
    dbiSyncIndex(db->fileIndex);
    dbiSyncIndex(db->providesIndex);
    dbiSyncIndex(db->requiredbyIndex);
    dbiSyncIndex(db->triggerIndex);

exit:
    unblockSignals();

    if (requiredbyCount) free(requiredbyList);
    if (providesCount) free(providesList);
    if (conflictCount) free(conflictList);
    if (triggerCount) free(triggerList);
    if (count) free(baseNames);

    return rc;
}

int rpmdbUpdateRecord(rpmdb db, int offset, Header newHeader)
{
    Header oldHeader;
    int oldSize, newSize;
    int rc = 0;

    oldHeader = doGetRecord(db, offset, 1);
    if (oldHeader == NULL) {
	rpmError(RPMERR_DBCORRUPT, _("cannot read header at %d for update"),
		offset);
	return 1;
    }

    oldSize = headerSizeof(oldHeader, HEADER_MAGIC_NO);
    headerFree(oldHeader);

    if (_noDirTokens)
	expandFilelist(newHeader);

    newSize = headerSizeof(newHeader, HEADER_MAGIC_NO);
    if (oldSize != newSize) {
	rpmMessage(RPMMESS_DEBUG, _("header changed size!"));
	if (rpmdbRemove(db, offset, 1))
	    return 1;

	if (rpmdbAdd(db, newHeader)) 
	    return 1;
    } else {
	blockSignals();

	(void)Fseek(db->pkgs, offset, SEEK_SET);
	fdSetContentLength(db->pkgs, newSize);
	rc = headerWrite(db->pkgs, newHeader, HEADER_MAGIC_NO);
	fdSetContentLength(db->pkgs, -1);

	unblockSignals();
    }

    return rc;
}

void rpmdbRemoveDatabase(const char * rootdir, const char * dbpath)
{ 
    int i;
    char * filename;

    i = strlen(dbpath);
    if (dbpath[i - 1] != '/') {
	filename = alloca(i);
	strcpy(filename, dbpath);
	filename[i] = '/';
	filename[i + 1] = '\0';
	dbpath = filename;
    }
    
    filename = alloca(strlen(rootdir) + strlen(dbpath) + 40);

    {	dbiIndex dbi;
	for (dbi = rpmdbi; dbi->dbi_basename != NULL; dbi++) {
	    sprintf(filename, "%s/%s/%s", rootdir, dbpath, dbi->dbi_basename);
	    unlink(filename);
	}
    }

    sprintf(filename, "%s/%s", rootdir, dbpath);
    rmdir(filename);

}

int rpmdbMoveDatabase(const char * rootdir, const char * olddbpath, const char * newdbpath)
{
    int i;
    char * ofilename, * nfilename;
    int rc = 0;
 
    i = strlen(olddbpath);
    if (olddbpath[i - 1] != '/') {
	ofilename = alloca(i + 2);
	strcpy(ofilename, olddbpath);
	ofilename[i] = '/';
	ofilename[i + 1] = '\0';
	olddbpath = ofilename;
    }
    
    i = strlen(newdbpath);
    if (newdbpath[i - 1] != '/') {
	nfilename = alloca(i + 2);
	strcpy(nfilename, newdbpath);
	nfilename[i] = '/';
	nfilename[i + 1] = '\0';
	newdbpath = nfilename;
    }
    
    ofilename = alloca(strlen(rootdir) + strlen(olddbpath) + 40);
    nfilename = alloca(strlen(rootdir) + strlen(newdbpath) + 40);

    {	dbiIndex dbi;
	for (dbi = rpmdbi; dbi->dbi_basename != NULL; dbi++) {
	    sprintf(ofilename, "%s/%s/%s", rootdir, olddbpath, dbi->dbi_basename);
	    sprintf(nfilename, "%s/%s/%s", rootdir, newdbpath, dbi->dbi_basename);
	    if (Rename(ofilename, nfilename)) rc = 1;
	}
    }

    return rc;
}

struct intMatch {
    unsigned int recOffset;
    unsigned int fileNumber;
    int fpNum;
};

static int intMatchCmp(const void * one, const void * two)
{
    const struct intMatch * a = one;
    const struct intMatch * b = two;

    if (a->recOffset < b->recOffset)
	return -1;
    else if (a->recOffset > b->recOffset)
	return 1;

    return 0;
}

int rpmdbFindFpList(rpmdb db, fingerPrint * fpList, dbiIndexSet * matchList, 
		    int numItems)
{
    int numIntMatches = 0;
    int intMatchesAlloced = numItems;
    struct intMatch * intMatches;
    int i, j;
    int start, end;
    int num;
    int_32 fc;
    const char ** dirNames, ** baseNames;
    const char ** fullBaseNames;
    int_32 * dirIndexes, * fullDirIndexes;
    fingerPrintCache fpc;

    /* this may be worth batching by baseName, but probably not as
       baseNames are quite unique as it is */

    intMatches = xcalloc(intMatchesAlloced, sizeof(*intMatches));

    /* Gather all matches from the database */
    for (i = 0; i < numItems; i++) {
	dbiIndexSet matches = NULL;
	switch (dbiSearchIndex(db->fileIndex, fpList[i].baseName, &matches)) {
	default:
	    break;
	case 2:
	    if (matches) {
		dbiFreeIndexSet(matches);
		matches = NULL;
	    }
	    free(intMatches);
	    return 1;
	    /*@notreached@*/ break;
	case 0:
	    if ((numIntMatches + dbiIndexSetCount(matches)) >= intMatchesAlloced) {
		intMatchesAlloced += dbiIndexSetCount(matches);
		intMatchesAlloced += intMatchesAlloced / 5;
		intMatches = xrealloc(intMatches, 
				     sizeof(*intMatches) * intMatchesAlloced);
	    }

	    for (j = 0; j < dbiIndexSetCount(matches); j++) {
		
		intMatches[numIntMatches].recOffset = dbiIndexRecordOffset(matches, j);
		intMatches[numIntMatches].fileNumber = dbiIndexRecordFileNumber(matches, j);
		intMatches[numIntMatches].fpNum = i;
		numIntMatches++;
	    }

	    break;
	}
	if (matches) {
	    dbiFreeIndexSet(matches);
	    matches = NULL;
	}
    }

    qsort(intMatches, numIntMatches, sizeof(*intMatches), intMatchCmp);
    /* intMatches is now sorted by (recnum, filenum) */

    for (i = 0; i < numItems; i++)
	matchList[i] = dbiCreateIndexSet();

    fpc = fpCacheCreate(numIntMatches);

    /* For each set of files matched in a package ... */
    for (start = 0; start < numIntMatches; start = end) {
	struct intMatch * im;
	Header h;
	fingerPrint * fps;

	im = intMatches + start;

	/* Find the end of the set of matched files in this package. */
	for (end = start + 1; end < numIntMatches; end++) {
	    if (im->recOffset != intMatches[end].recOffset)
		break;
	}
	num = end - start;

	/* Compute fingerprints for each file match in this package. */
	h = rpmdbGetRecord(db, im->recOffset);
	if (h == NULL) {
	    free(intMatches);
	    return 1;
	}

	headerGetEntryMinMemory(h, RPMTAG_DIRNAMES, NULL, 
			    (void **) &dirNames, NULL);
	headerGetEntryMinMemory(h, RPMTAG_BASENAMES, NULL, 
			    (void **) &fullBaseNames, &fc);
	headerGetEntryMinMemory(h, RPMTAG_DIRINDEXES, NULL, 
			    (void **) &fullDirIndexes, NULL);

	baseNames = xcalloc(num, sizeof(*baseNames));
	dirIndexes = xcalloc(num, sizeof(*dirIndexes));
	for (i = 0; i < num; i++) {
	    baseNames[i] = fullBaseNames[im[i].fileNumber];
	    dirIndexes[i] = fullDirIndexes[im[i].fileNumber];
	}

	fps = xcalloc(num, sizeof(*fps));
	fpLookupList(fpc, dirNames, baseNames, dirIndexes, num, fps);

	free(dirNames);
	free(fullBaseNames);
	free(baseNames);
	free(dirIndexes);

	/* Add db (recnum,filenum) to list for fingerprint matches. */
	for (i = 0; i < num; i++) {
	    j = im[i].fpNum;
	    if (FP_EQUAL_DIFFERENT_CACHE(fps[i], fpList[j]))
		dbiAppendIndexRecord(matchList[j], im[i].recOffset, im[i].fileNumber);
	}

	headerFree(h);

	free(fps);
    }

    fpCacheFree(fpc);
    free(intMatches);

    return 0;
}
