#include "system.h"

#include <sys/file.h>
#include <signal.h>
#include <sys/signal.h>

#include <rpmlib.h>
#include <rpmurl.h>	/* XXX for assert.h */
#include <rpmmacro.h>	/* XXX for rpmGetPath/rpmGenPath */

#include "rpmdb.h"
/*@access dbiIndexSet@*/
/*@access dbiIndexRecord@*/
/*@access rpmdbMatchIterator@*/

#include "fprint.h"
#include "misc.h"

extern int _noDirTokens;

int _useDbiMajor = 3;
int _filterDbDups = 0;	/* Filter duplicate entries ? (bug in pre rpm-3.0.4) */

#define	_DBI_FLAGS	0
#define	_DBI_PERMS	0644
#define	_DBI_MAJOR	-1

static int dbiTagsMax = 0;
static int *dbiTags = NULL;

/**
 * Return dbi index used for rpm tag.
 * @param rpmtag	rpm header tag
 * @return dbi index, -1 on error
 */
static int dbiTagToDbix(int rpmtag)
{
    int dbix;

    if (!(dbiTagsMax > 0 && dbiTags))
	return -1;
    for (dbix = 0; dbix < dbiTagsMax; dbix++) {
	if (rpmtag == dbiTags[dbix])
	    return dbix;
    }
    return -1;
}

static void dbiTagsInit(void)
{
    static const char * _dbiTagStr_default =
	"Packages:Name:Basenames:Group:Requirename:Providename:Conflictname:Triggername";
    char * dbiTagStr;
    char * o, * oe;
    int rpmtag;

    dbiTagStr = rpmExpand("%{_dbi_tags}", NULL);
    if (!(dbiTagStr && *dbiTagStr && *dbiTagStr != '%')) {
	xfree(dbiTagStr);
	dbiTagStr = xstrdup(_dbiTagStr_default);
    }

    if (dbiTagsMax || dbiTags) {
	free(dbiTags);
	dbiTags = NULL;
	dbiTagsMax = 0;
    }

    /* Always allocate package index */
    dbiTagsMax = 1;
    dbiTags = xcalloc(1, dbiTagsMax * sizeof(*dbiTags));

    for (o = dbiTagStr; o && *o; o = oe) {
	while (*o && isspace(*o))
	    o++;
	if (*o == '\0')
	    break;
	for (oe = o; oe && *oe; oe++) {
	    if (isspace(*oe))
		break;
	    if (oe[0] == ':' && !(oe[1] == '/' && oe[2] == '/'))
		break;
	}
	if (oe && *oe)
	    *oe++ = '\0';
	rpmtag = tagValue(o);
	if (rpmtag < 0) {

	    fprintf(stderr, _("dbiTagsInit: unrecognized tag name: \"%s\" ignored\n"), o);
	    continue;
	}
	if (dbiTagToDbix(rpmtag) >= 0)
	    continue;

	dbiTags = xrealloc(dbiTags, (dbiTagsMax + 1) * sizeof(*dbiTags));
	dbiTags[dbiTagsMax++] = rpmtag;
    }

    free(dbiTagStr);
}

#if USE_DB0
extern struct _dbiVec db0vec;
#define	DB0vec		&db0vec
#else
#define	DB0vec		NULL
#endif

#define	DB1vec		NULL

#if USE_DB2
extern struct _dbiVec db2vec;
#define	DB2vec		&db2vec
#else
#define	DB2vec		NULL
#endif

#if USE_DB3
extern struct _dbiVec db3vec;
#define	DB3vec		&db3vec
#else
#define	DB3vec		NULL
#endif

static struct _dbiVec *mydbvecs[] = {
    DB0vec, DB1vec, DB2vec, DB3vec, NULL
};

inline int dbiSync(dbiIndex dbi, unsigned int flags) {
    return (*dbi->dbi_vec->sync) (dbi, flags);
}

inline int dbiByteSwapped(dbiIndex dbi) {
    return (*dbi->dbi_vec->byteswapped) (dbi);
}

inline int dbiCopen(dbiIndex dbi, DBC ** dbcp, unsigned int flags) {
    return (*dbi->dbi_vec->copen) (dbi, dbcp, flags);
}

inline int dbiCclose(dbiIndex dbi, DBC * dbcursor, unsigned int flags) {
    return (*dbi->dbi_vec->cclose) (dbi, dbcursor, flags);
}

inline int dbiDel(dbiIndex dbi, const void * keyp, size_t keylen, unsigned int flags) {
    return (*dbi->dbi_vec->cdel) (dbi, keyp, keylen, flags);
}

inline int dbiGet(dbiIndex dbi, void ** keypp, size_t * keylenp,
	void ** datapp, size_t * datalenp, unsigned int flags) {
    return (*dbi->dbi_vec->cget) (dbi, keypp, keylenp, datapp, datalenp, flags);
}

inline int dbiPut(dbiIndex dbi, const void * keyp, size_t keylen,
	const void * datap, size_t datalen, unsigned int flags) {
    return (*dbi->dbi_vec->cput) (dbi, keyp, keylen, datap, datalen, flags);
}

inline int dbiClose(dbiIndex dbi, unsigned int flags) {
    return (*dbi->dbi_vec->close) (dbi, flags);
}

dbiIndex dbiOpen(rpmdb rpmdb, int rpmtag, unsigned int flags)
{
    int dbix;
    dbiIndex dbi = NULL;
    int major;
    int rc = 0;

    dbix = dbiTagToDbix(rpmtag);
    if (dbix < 0 || dbix >= dbiTagsMax)
	return NULL;

    /* Is this index already open ? */
    if ((dbi = rpmdb->_dbi[dbix]) != NULL)
	return dbi;

    major = rpmdb->db_major;

    switch (major) {
    case 3:
    case 2:
    case 1:
    case 0:
	if (mydbvecs[major] == NULL) {
	   rc = 1;
	   break;
	}
	errno = 0;
	rc = (*mydbvecs[major]->open) (rpmdb, rpmtag, &dbi);
	if (rc == 0 && dbi)
	    dbi->dbi_vec = mydbvecs[major];
	break;
    case -1:
	major = 4;
	while (major-- > 0) {
	    if (mydbvecs[major] == NULL)
		continue;
	    errno = 0;
	    rc = (*mydbvecs[major]->open) (rpmdb, rpmtag, &dbi);
	    if (rc == 0 && dbi) {
		dbi->dbi_vec = mydbvecs[major];
		break;
	    }
	    if (rc == 1 && major == 3) {
		fprintf(stderr, "*** FIXME: <message about how to convert db>\n");
		fprintf(stderr, _("\n\
--> Please run \"rpm --rebuilddb\" as root to convert your database from\n\
    db1 to db3 on-disk format.\n\
\n\
"));
	    }
	}
	if (rpmdb->db_major == -1)
	    rpmdb->db_major = major;
    	break;
    }

    if (rc == 0) {
	rpmdb->_dbi[dbix] = dbi;
    } else if (dbi) {
        rpmError(RPMERR_DBOPEN, _("dbiOpen: cannot open %s index"),
		tagName(rpmtag));
	db3Free(dbi);
	dbi = NULL;
     }

    return dbi;
}

/**
 * Create and initialize element of index database set.
 * @param recOffset	byte offset of header in db
 * @param fileNumber	file array index
 * @return	new element
 */
static inline dbiIndexRecord dbiReturnIndexRecordInstance(unsigned int recOffset, unsigned int fileNumber) {
    dbiIndexRecord rec = xcalloc(1, sizeof(*rec));
    rec->recOffset = recOffset;
    rec->fileNumber = fileNumber;
    return rec;
}

union _dbswap {
    unsigned int ui;
    unsigned char uc[4];
};

#define	_DBSWAP(_a) \
  { unsigned char _b, *_c = (_a).uc; \
    _b = _c[3]; _c[3] = _c[0]; _c[0] = _b; \
    _b = _c[2]; _c[2] = _c[1]; _c[1] = _b; \
  }

/**
 * Return items that match criteria.
 * @param dbi		index database handle
 * @param keyp		search key
 * @param keylen	search key length (0 will use strlen(key))
 * @param setp		address of items retrieved from index database
 * @return		-1 error, 0 success, 1 not found
 */
static int dbiSearchIndex(dbiIndex dbi, const char * keyp, size_t keylen,
		dbiIndexSet * setp)
{
    int rc;
    void * datap;
    size_t datalen;

    if (setp) *setp = NULL;
    if (keylen == 0) keylen = strlen(keyp);

    rc = dbiGet(dbi, (void **)&keyp, &keylen, &datap, &datalen, 0);

    if (rc < 0) {
	rpmError(RPMERR_DBGETINDEX, _("error getting \"%s\" records from %s index"),
		keyp, tagName(dbi->dbi_rpmtag));
    } else if (rc == 0 && setp) {
	int _dbbyteswapped = dbiByteSwapped(dbi);
	const char * sdbir = datap;
	dbiIndexSet set;
	int i;

	set = xmalloc(sizeof(*set));

	/* Convert to database internal format */
	switch (dbi->dbi_jlen) {
	case 2*sizeof(int_32):
	    set->count = datalen / (2*sizeof(int_32));
	    set->recs = xmalloc(set->count * sizeof(*(set->recs)));
	    for (i = 0; i < set->count; i++) {
		union _dbswap recOffset, fileNumber;

		memcpy(&recOffset.ui, sdbir, sizeof(recOffset.ui));
		sdbir += sizeof(recOffset.ui);
		memcpy(&fileNumber.ui, sdbir, sizeof(fileNumber.ui));
		sdbir += sizeof(fileNumber.ui);
		if (_dbbyteswapped) {
		    _DBSWAP(recOffset);
		    _DBSWAP(fileNumber);
		}
		set->recs[i].recOffset = recOffset.ui;
		set->recs[i].fileNumber = fileNumber.ui;
		set->recs[i].fpNum = 0;
		set->recs[i].dbNum = 0;
	    }
	    break;
	default:
	case 1*sizeof(int_32):
	    set->count = datalen / (1*sizeof(int_32));
	    set->recs = xmalloc(set->count * sizeof(*(set->recs)));
	    for (i = 0; i < set->count; i++) {
		union _dbswap recOffset;

		memcpy(&recOffset.ui, sdbir, sizeof(recOffset.ui));
		sdbir += sizeof(recOffset.ui);
		if (_dbbyteswapped) {
		    _DBSWAP(recOffset);
		}
		set->recs[i].recOffset = recOffset.ui;
		set->recs[i].fileNumber = 0;
		set->recs[i].fpNum = 0;
		set->recs[i].dbNum = 0;
	    }
	    break;
	}
	*setp = set;
    }
    return rc;
}

/**
 * Change/delete items that match criteria.
 * @param dbi	index database handle
 * @param keyp	update key
 * @param set	items to update in index database
 * @return	0 success, 1 not found
 */
/*@-compmempass@*/
static int dbiUpdateIndex(dbiIndex dbi, const char * keyp, dbiIndexSet set)
{
    size_t keylen = strlen(keyp);
    void * datap;
    size_t datalen;
    int rc;

    if (set->count) {
	char * tdbir;
	int i;
	int _dbbyteswapped = dbiByteSwapped(dbi);

	/* Convert to database internal format */

	switch (dbi->dbi_jlen) {
	case 2*sizeof(int_32):
	    datalen = set->count * (2 * sizeof(int_32));
	    datap = tdbir = alloca(datalen);
	    for (i = 0; i < set->count; i++) {
		union _dbswap recOffset, fileNumber;

		recOffset.ui = set->recs[i].recOffset;
		fileNumber.ui = set->recs[i].fileNumber;
		if (_dbbyteswapped) {
		    _DBSWAP(recOffset);
		    _DBSWAP(fileNumber);
		}
		memcpy(tdbir, &recOffset.ui, sizeof(recOffset.ui));
		tdbir += sizeof(recOffset.ui);
		memcpy(tdbir, &fileNumber.ui, sizeof(fileNumber.ui));
		tdbir += sizeof(fileNumber.ui);
	    }
	    break;
	default:
	case 1*sizeof(int_32):
	    datalen = set->count * (1 * sizeof(int_32));
	    datap = tdbir = alloca(datalen);
	    for (i = 0; i < set->count; i++) {
		union _dbswap recOffset;

		recOffset.ui = set->recs[i].recOffset;
		if (_dbbyteswapped) {
		    _DBSWAP(recOffset);
		}
		memcpy(tdbir, &recOffset.ui, sizeof(recOffset.ui));
		tdbir += sizeof(recOffset.ui);
	    }
	    break;
	}

	rc = dbiPut(dbi, keyp, keylen, datap, datalen, 0);

	if (rc) {
	    rpmError(RPMERR_DBPUTINDEX, _("error storing record %s into %s"),
		keyp, tagName(dbi->dbi_rpmtag));
	}

    } else {

	rc = dbiDel(dbi, keyp, keylen, 0);

	if (rc) {
	    rpmError(RPMERR_DBPUTINDEX, _("error removing record %s from %s"),
		keyp, tagName(dbi->dbi_rpmtag));
	}

    }

    return rc;
}
/*@=compmempass@*/

/**
 * Append element to set of index database items.
 * @param set	set of index database items
 * @param rec	item to append to set
 * @return	0 success (always)
 */
static inline int dbiAppendIndexRecord(dbiIndexSet set, dbiIndexRecord rec)
{
    set->count++;

    if (set->count == 1) {
	set->recs = xmalloc(set->count * sizeof(*(set->recs)));
    } else {
	set->recs = xrealloc(set->recs, set->count * sizeof(*(set->recs)));
    }
    set->recs[set->count - 1].recOffset = rec->recOffset;
    set->recs[set->count - 1].fileNumber = rec->fileNumber;

    return 0;
}

/* returns 1 on failure */
/**
 * Remove element from set of index database items.
 * @param set	set of index database items
 * @param rec	item to remove from set
 * @return	0 success, 1 failure
 */
static inline int dbiRemoveIndexRecord(dbiIndexSet set, dbiIndexRecord rec) {
    int from;
    int to = 0;
    int num = set->count;
    int numCopied = 0;

    for (from = 0; from < num; from++) {
	if (rec->recOffset != set->recs[from].recOffset ||
	    rec->fileNumber != set->recs[from].fileNumber) {
	    /* structure assignment */
	    if (from != to) set->recs[to] = set->recs[from];
	    to++;
	    numCopied++;
	} else {
	    set->count--;
	}
    }

    return (numCopied == num);
}

/* XXX rpminstall.c, transaction.c */
unsigned int dbiIndexSetCount(dbiIndexSet set) {
    return set->count;
}

/* XXX rpminstall.c, transaction.c */
unsigned int dbiIndexRecordOffset(dbiIndexSet set, int recno) {
    return set->recs[recno].recOffset;
}

/* XXX transaction.c */
unsigned int dbiIndexRecordFileNumber(dbiIndexSet set, int recno) {
    return set->recs[recno].fileNumber;
}

/**
 * Change record offset of header within element in index database set.
 * @param set	set of index database items
 * @param recno	index of item in set
 * @param recoff new record offset
 */
static inline void dbiIndexRecordOffsetSave(dbiIndexSet set, int recno, unsigned int recoff) {
    set->recs[recno].recOffset = recoff;
}

/* XXX depends.c, install.c, query.c, rpminstall.c, transaction.c */
void dbiFreeIndexSet(dbiIndexSet set) {
    if (set) {
	if (set->recs) free(set->recs);
	free(set);
    }
}

/* XXX the signal handling in here is not thread safe */

static sigset_t signalMask;

static void blockSignals(rpmdb rpmdb)
{
    sigset_t newMask;

    if (!(rpmdb && rpmdb->db_major == 3)) {
	sigfillset(&newMask);		/* block all signals */
	sigprocmask(SIG_BLOCK, &newMask, &signalMask);
    }
}

static void unblockSignals(rpmdb rpmdb)
{
    if (!(rpmdb && rpmdb->db_major == 3)) {
	sigprocmask(SIG_SETMASK, &signalMask, NULL);
    }
}

#define	_DB_ROOT	"/"
#define	_DB_HOME	"%{_dbpath}"
#define	_DB_FLAGS	0
#define _DB_MODE	0
#define _DB_PERMS	0644

#define _DB_MAJOR	-1
#define	_DB_REMOVE_ENV	0
#define	_DB_FILTER_DUPS	0
#define	_DB_ERRPFX	"rpmdb"

static struct rpmdb_s dbTemplate = {
    _DB_ROOT,	_DB_HOME, _DB_FLAGS, _DB_MODE, _DB_PERMS,
    _DB_MAJOR,	_DB_REMOVE_ENV, _DB_FILTER_DUPS, _DB_ERRPFX
};

/* XXX query.c, rpminstall.c, verify.c */
void rpmdbClose (rpmdb rpmdb)
{
    int dbix;

    for (dbix = rpmdb->db_ndbi; --dbix >= 0; ) {
	if (rpmdb->_dbi[dbix] == NULL)
	    continue;
    	dbiClose(rpmdb->_dbi[dbix], 0);
    	rpmdb->_dbi[dbix] = NULL;
    }
    if (rpmdb->db_errpfx) {
	xfree(rpmdb->db_errpfx);
	rpmdb->db_errpfx = NULL;
    }
    if (rpmdb->db_root) {
	xfree(rpmdb->db_root);
	rpmdb->db_root = NULL;
    }
    if (rpmdb->db_home) {
	xfree(rpmdb->db_home);
	rpmdb->db_home = NULL;
    }
    if (rpmdb->_dbi) {
	xfree(rpmdb->_dbi);
	rpmdb->_dbi = NULL;
    }
    free(rpmdb);
}

static /*@only@*/ rpmdb newRpmdb(const char * root, const char * home,
		int mode, int perms, int flags)
{
    rpmdb rpmdb = xcalloc(sizeof(*rpmdb), 1);
    static int _initialized = 0;

    if (!_initialized) {
	_useDbiMajor = rpmExpandNumeric("%{_dbapi}");
	_filterDbDups = rpmExpandNumeric("%{_filterdbdups}");
	_initialized = 1;
    }

    *rpmdb = dbTemplate;	/* structure assignment */

    if (!(perms & 0600)) perms = 0644;	/* XXX sanity */

    if (root)
	rpmdb->db_root = (*root ? root : _DB_ROOT);
    if (home)
	rpmdb->db_home = (*home ? home : _DB_HOME);
    if (mode >= 0)	rpmdb->db_mode = mode;
    if (perms >= 0)	rpmdb->db_perms = perms;
    if (flags >= 0)	rpmdb->db_flags = flags;

    if (rpmdb->db_root)
	rpmdb->db_root = rpmGetPath(rpmdb->db_root, NULL);
    if (rpmdb->db_home) {
	rpmdb->db_home = rpmGetPath(rpmdb->db_home, NULL);
	if (!(rpmdb->db_home && rpmdb->db_home[0] != '%')) {
	    rpmError(RPMERR_DBOPEN, _("no dbpath has been set"));
	   goto errxit;
	}
    }
    if (rpmdb->db_errpfx)
	rpmdb->db_errpfx = xstrdup(rpmdb->db_errpfx);
    rpmdb->db_major = _useDbiMajor;
    rpmdb->db_remove_env = 0;
    rpmdb->db_filter_dups = _filterDbDups;
    rpmdb->db_ndbi = dbiTagsMax;
    rpmdb->_dbi = xcalloc(rpmdb->db_ndbi, sizeof(*rpmdb->_dbi));
    return rpmdb;

errxit:
    if (rpmdb)
	rpmdbClose(rpmdb);
    return NULL;
}

static int openDatabase(const char * prefix, const char * dbpath, rpmdb *dbp,
		int mode, int perms, int flags)
{
    rpmdb rpmdb;
    int rc;
    static int _initialized = 0;
    int justCheck = flags & RPMDB_FLAG_JUSTCHECK;
    int minimal = flags & RPMDB_FLAG_MINIMAL;

    if (!_initialized || dbiTagsMax == 0) {
	dbiTagsInit();
	_initialized++;
    }

    if (dbp)
	*dbp = NULL;
    if (mode & O_WRONLY) 
	return 1;

    rpmdb = newRpmdb(prefix, dbpath, mode, perms, flags);

    {	int dbix;

	rc = 0;
	for (dbix = 0; rc == 0 && dbix < dbiTagsMax; dbix++) {
	    dbiIndex dbi;
	    int rpmtag;

	    rpmtag = dbiTags[dbix];
	    dbi = dbiOpen(rpmdb, rpmtag, 0);
	    if (dbi == NULL)
		continue;

	    switch (dbix) {
	    case 0:
		if (rpmdb->db_major == 3)
		    goto exit;
		break;
	    case 1:
		if (minimal)
		    goto exit;
		break;
	    case 2:
	    {	void * keyp = NULL;
		int xx;

    /* We used to store the fileindexes as complete paths, rather then
       plain basenames. Let's see which version we are... */
    /*
     * XXX FIXME: db->fileindex can be NULL under pathological (e.g. mixed
     * XXX db1/db2 linkage) conditions.
     */
		if (justCheck)
		    break;
		xx = dbiCopen(dbi, NULL, 0);
		xx = dbiGet(dbi, &keyp, NULL, NULL, NULL, 0);
		if (xx == 0) {
		    const char * akey = keyp;
		    if (strchr(akey, '/')) {
			rpmError(RPMERR_OLDDB, _("old format database is present; "
				"use --rebuilddb to generate a new format database"));
			rc |= 1;
		    }
		}
		xx = dbiCclose(dbi, NULL, 0);
	    }	break;
	    default:
		break;
	    }
	}
    }

exit:
    if (rc || justCheck || dbp == NULL)
	rpmdbClose(rpmdb);
    else
	*dbp = rpmdb;

    return rc;
}

/* XXX python/upgrade.c */
int rpmdbOpenForTraversal(const char * prefix, rpmdb * dbp)
{
    return openDatabase(prefix, NULL, dbp, O_RDONLY, 0644, RPMDB_FLAG_MINIMAL);
}

/* XXX python/rpmmodule.c */
int rpmdbOpen (const char * prefix, rpmdb *dbp, int mode, int perms)
{
    return openDatabase(prefix, NULL, dbp, mode, perms, 0);
}

int rpmdbInit (const char * prefix, int perms)
{
    rpmdb rpmdb = NULL;
    int rc;

    rc = openDatabase(prefix, NULL, &rpmdb, (O_CREAT | O_RDWR), perms, RPMDB_FLAG_JUSTCHECK);
    if (rpmdb) {
	rpmdbClose(rpmdb);
	rpmdb = NULL;
    }
    return rc;
}

/* XXX depends.c, install.c, query.c, transaction.c, uninstall.c */
Header rpmdbGetRecord(rpmdb rpmdb, unsigned int offset)
{
    int rpmtag;
    dbiIndex dbi;
    void * uh;
    size_t uhlen;
    void * keyp = &offset;
    size_t keylen = sizeof(offset);
    int rc;

    rpmtag = 0;	/* RPMDBI_PACKAGES */
    dbi = dbiOpen(rpmdb, rpmtag, 0);
    if (dbi == NULL)
	return NULL;
    rc = dbiGet(dbi, &keyp, &keylen, &uh, &uhlen, 0);
    if (rc)
	return NULL;
    return headerLoad(uh);
}

static int rpmdbFindByFile(rpmdb rpmdb, const char * filespec,
			/*@out@*/ dbiIndexSet * matches)
{
    const char * dirName;
    const char * baseName;
    fingerPrintCache fpc;
    fingerPrint fp1;
    dbiIndex dbi = NULL;
    dbiIndexSet allMatches = NULL;
    dbiIndexRecord rec = NULL;
    int i;
    int rc;

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

    dbi = dbiOpen(rpmdb, RPMTAG_BASENAMES, 0);
    rc = dbiSearchIndex(dbi, baseName, 0, &allMatches);
    if (rc) {
	dbiFreeIndexSet(allMatches);
	allMatches = NULL;
	fpCacheFree(fpc);
	return rc;
    }

    *matches = xcalloc(1, sizeof(**matches));
    rec = dbiReturnIndexRecordInstance(0, 0);
    i = 0;
    while (i < allMatches->count) {
	const char ** baseNames, ** dirNames;
	int_32 * dirIndexes;
	unsigned int offset = dbiIndexRecordOffset(allMatches, i);
	unsigned int prevoff;
	Header h;

	if ((h = rpmdbGetRecord(rpmdb, offset)) == NULL) {
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
	    fingerPrint fp2;
	    int num = dbiIndexRecordFileNumber(allMatches, i);

	    fp2 = fpLookup(fpc, dirNames[dirIndexes[num]], baseNames[num], 1);
	    if (FP_EQUAL(fp1, fp2)) {
		rec->recOffset = dbiIndexRecordOffset(allMatches, i);
		rec->fileNumber = dbiIndexRecordFileNumber(allMatches, i);
		dbiAppendIndexRecord(*matches, rec);
	    }

	    prevoff = offset;
	    i++;
	    offset = dbiIndexRecordOffset(allMatches, i);
	} while (i < allMatches->count && 
		(i == 0 || offset == prevoff));

	free(baseNames);
	free(dirNames);
	headerFree(h);
    }

    if (rec) {
	free(rec);
	rec = NULL;
    }
    if (allMatches) {
	dbiFreeIndexSet(allMatches);
	allMatches = NULL;
    }

    fpCacheFree(fpc);

    if ((*matches)->count == 0) {
	dbiFreeIndexSet(*matches);
	*matches = NULL; 
	return 1;
    }

    return 0;
}

/* XXX python/upgrade.c, install.c, uninstall.c */
int rpmdbCountPackages(rpmdb rpmdb, const char * name)
{
    dbiIndexSet matches = NULL;
    int dbix;
    int rc;

    dbix = dbiTagToDbix(RPMTAG_NAME);
    rc = dbiSearchIndex(rpmdb->_dbi[dbix], name, 0, &matches);

    switch (rc) {
    default:
    case -1:		/* error */
	rpmError(RPMERR_DBCORRUPT, _("cannot retrieve package \"%s\" from db"),
                name);
	rc = -1;
	break;
    case 1:		/* not found */
	rc = 0;
	break;
    case 0:		/* success */
	rc = dbiIndexSetCount(matches);
	break;
    }

    if (matches)
	dbiFreeIndexSet(matches);

    return rc;
}

struct _rpmdbMatchIterator {
    const void *	mi_key;
    size_t		mi_keylen;
    rpmdb		mi_rpmdb;
    dbiIndex		mi_dbi;
    int			mi_dbix;
    dbiIndexSet		mi_set;
    int			mi_setx;
    Header		mi_h;
    unsigned int	mi_prevoffset;
    unsigned int	mi_offset;
    unsigned int	mi_filenum;
    const char *	mi_version;
    const char *	mi_release;
};

void rpmdbFreeIterator(rpmdbMatchIterator mi)
{
    if (mi == NULL)
	return;

    if (mi->mi_release) {
	xfree(mi->mi_release);
	mi->mi_release = NULL;
    }
    if (mi->mi_version) {
	xfree(mi->mi_version);
	mi->mi_version = NULL;
    }
    if (mi->mi_h) {
	headerFree(mi->mi_h);
	mi->mi_h = NULL;
    }
    if (mi->mi_set) {
	dbiFreeIndexSet(mi->mi_set);
	mi->mi_set = NULL;
    } else {
	int dbix = 0;	/* RPMDBI_PACKAGES */
	dbiIndex dbi = mi->mi_rpmdb->_dbi[dbix];
	if (dbi)
	    (void) dbiCclose(dbi, NULL, 0);
    }
    if (mi->mi_key) {
	xfree(mi->mi_key);
	mi->mi_key = NULL;
    }
    free(mi);
}

unsigned int rpmdbGetIteratorOffset(rpmdbMatchIterator mi) {
    if (mi == NULL)
	return 0;
    return mi->mi_offset;
}

unsigned int rpmdbGetIteratorFileNum(rpmdbMatchIterator mi) {
    if (mi == NULL)
	return 0;
    return mi->mi_filenum;
}

int rpmdbGetIteratorCount(rpmdbMatchIterator mi) {
    if (!(mi && mi->mi_set))
	return 0;	/* XXX W2DO? */
    return mi->mi_set->count;
}

void rpmdbSetIteratorRelease(rpmdbMatchIterator mi, const char * release) {
    if (mi == NULL)
	return;
    if (mi->mi_release) {
	xfree(mi->mi_release);
	mi->mi_release = NULL;
    }
    mi->mi_release = (release ? xstrdup(release) : NULL);
}

void rpmdbSetIteratorVersion(rpmdbMatchIterator mi, const char * version) {
    if (mi == NULL)
	return;
    if (mi->mi_version) {
	xfree(mi->mi_version);
	mi->mi_version = NULL;
    }
    mi->mi_version = (version ? xstrdup(version) : NULL);
}

Header rpmdbNextIterator(rpmdbMatchIterator mi)
{
    dbiIndex dbi;
    int rpmtag;
    void * uh = NULL;
    size_t uhlen = 0;
    void * keyp;
    size_t keylen;
    int rc;

    if (mi == NULL)
	return NULL;

    rpmtag = 0;	/* RPMDBI_PACKAGES */
    dbi = dbiOpen(mi->mi_rpmdb, rpmtag, 0);
    if (dbi == NULL)
	return NULL;

top:
    /* XXX skip over instances with 0 join key */
    do {
	if (mi->mi_set) {
	    keyp = &mi->mi_offset;
	    keylen = sizeof(mi->mi_offset);
	    if (!(mi->mi_setx < mi->mi_set->count))
		return NULL;
	    if (mi->mi_dbix != 0) {	/* RPMDBI_PACKAGES */
		mi->mi_offset = dbiIndexRecordOffset(mi->mi_set, mi->mi_setx);
		mi->mi_filenum = dbiIndexRecordFileNumber(mi->mi_set, mi->mi_setx);
	    }
	} else {
	    keyp = NULL;
	    keylen = 0;

	    rc = dbiGet(dbi, &keyp, &keylen, &uh, &uhlen, 0);

	    if (rc == 0 && keyp && mi->mi_setx)
		memcpy(&mi->mi_offset, keyp, sizeof(mi->mi_offset));

	    /* Terminate on error or end of keys */
	    if (rc || (mi->mi_setx && mi->mi_offset == 0))
		return NULL;
	}
	mi->mi_setx++;
    } while (mi->mi_offset == 0);

    if (mi->mi_prevoffset && mi->mi_offset == mi->mi_prevoffset)
	return mi->mi_h;

    /* Retrieve header */
    if (uh == NULL) {
	rc = dbiGet(dbi, &keyp, &keylen, &uh, &uhlen, 0);
	if (rc)
	    return NULL;
    }

    if (mi->mi_h) {
	headerFree(mi->mi_h);
	mi->mi_h = NULL;
    }

    mi->mi_h = headerLoad(uh);

    if (mi->mi_release) {
	const char *release;
	headerNVR(mi->mi_h, NULL, NULL, &release);
	if (strcmp(mi->mi_release, release))
	    goto top;
    }

    if (mi->mi_version) {
	const char *version;
	headerNVR(mi->mi_h, NULL, &version, NULL);
	if (strcmp(mi->mi_version, version))
	    goto top;
    }

    mi->mi_prevoffset = mi->mi_offset;
    return mi->mi_h;
}

static int intMatchCmp(const void * one, const void * two) {
    const struct _dbiIndexRecord * a = one,  * b = two;
    return (a->recOffset - b->recOffset);
}

static void rpmdbSortIterator(rpmdbMatchIterator mi) {
    if (mi && mi->mi_set && mi->mi_set->recs && mi->mi_set->count > 0)
	qsort(mi->mi_set->recs, mi->mi_set->count, sizeof(*mi->mi_set->recs),
		intMatchCmp);
}

static int rpmdbGrowIterator(rpmdbMatchIterator mi,
	const void * keyp, size_t keylen, int fpNum)
{
    dbiIndex dbi = NULL;
    dbiIndexSet set = NULL;
    int i;
    int rc;

    if (!(mi && keyp))
	return 1;

    dbi = mi->mi_rpmdb->_dbi[mi->mi_dbix];

    if (keylen == 0)
	keylen = strlen(keyp);

    rc = dbiSearchIndex(dbi, keyp, keylen, &set);

    switch (rc) {
    default:
    case -1:		/* error */
    case 1:		/* not found */
	break;
    case 0:		/* success */
	for (i = 0; i < set->count; i++)
	    set->recs[i].fpNum = fpNum;

	if (mi->mi_set == NULL) {
	    mi->mi_set = set;
	    set = NULL;
	} else {
	    mi->mi_set->recs = xrealloc(mi->mi_set->recs,
		(mi->mi_set->count + set->count) * sizeof(*(mi->mi_set->recs)));
	    memcpy(mi->mi_set->recs + mi->mi_set->count, set->recs,
		set->count * sizeof(*(mi->mi_set->recs)));
	    mi->mi_set->count += set->count;
	}
	break;
    }

    if (set)
	dbiFreeIndexSet(set);
    return rc;
}

rpmdbMatchIterator rpmdbInitIterator(rpmdb rpmdb, int rpmtag,
	const void * keyp, size_t keylen)
{
    rpmdbMatchIterator mi = NULL;
    dbiIndexSet set = NULL;
    dbiIndex dbi;
    int dbix;

    dbix = dbiTagToDbix(rpmtag);
    if (dbix < 0)
	return NULL;
    dbi = dbiOpen(rpmdb, rpmtag, 0);
    if (dbi == NULL)
	return NULL;

    if (keyp) {
	int rc;
	if (rpmtag == RPMTAG_BASENAMES) {
	    rc = rpmdbFindByFile(rpmdb, keyp, &set);
	} else {
	    rc = dbiSearchIndex(dbi, keyp, keylen, &set);
	}
	switch (rc) {
	default:
	case -1:	/* error */
	case 1:		/* not found */
	    if (set)
		dbiFreeIndexSet(set);
	    return NULL;
	    /*@notreached@*/ break;
	case 0:		/* success */
	    break;
	}
    }

    mi = xcalloc(sizeof(*mi), 1);
    if (keyp) {
	if (keylen == 0)
	    keylen = strlen(keyp);

	{   char * k = xmalloc(keylen + 1);
	    memcpy(k, keyp, keylen);
	    k[keylen] = '\0';	/* XXX for strings */
	    mi->mi_key = k;
	}

	mi->mi_keylen = keylen;
    } else {
	mi->mi_key = NULL;
	mi->mi_keylen = 0;
	if (dbi)
	    dbi->dbi_lastoffset = 0;	/* db0: rewind to beginning */
    }
    mi->mi_rpmdb = rpmdb;
    mi->mi_dbi = dbi;

    mi->mi_dbix = dbix;
    mi->mi_set = set;
    mi->mi_setx = 0;
    mi->mi_h = NULL;
    mi->mi_prevoffset = 0;
    mi->mi_offset = 0;
    mi->mi_filenum = 0;
    mi->mi_version = NULL;
    mi->mi_release = NULL;
    return mi;
}

static inline int removeIndexEntry(dbiIndex dbi, const char * keyp, dbiIndexRecord rec,
		             int tolerant)
{
    dbiIndexSet set = NULL;
    int rc;
    
    rc = dbiSearchIndex(dbi, keyp, 0, &set);

    switch (rc) {
    case -1:			/* error */
	rc = 1;
	break;   /* error message already generated from dbindex.c */
    case 1:			/* not found */
	rc = 0;
	if (!tolerant) {
	    rpmError(RPMERR_DBCORRUPT, _("key \"%s\" not found in %s"), 
		keyp, tagName(dbi->dbi_rpmtag));
	    rc = 1;
	}
	break;
    case 0:			/* success */
	if (dbiRemoveIndexRecord(set, rec)) {
	    if (!tolerant) {
		rpmError(RPMERR_DBCORRUPT, _("key \"%s\" not removed from %s"),
		    keyp, tagName(dbi->dbi_rpmtag));
		rc = 1;
	    }
	    break;
	}
	if (dbiUpdateIndex(dbi, keyp, set))
	    rc = 1;
	break;
    }

    if (set) {
	dbiFreeIndexSet(set);
	set = NULL;
    }

    return rc;
}

/* XXX uninstall.c */
int rpmdbRemove(rpmdb rpmdb, unsigned int offset, int tolerant)
{
    Header h;

    h = rpmdbGetRecord(rpmdb, offset);
    if (h == NULL) {
	rpmError(RPMERR_DBCORRUPT, _("rpmdbRemove: cannot read header at 0x%x"),
	      offset);
	return 1;
    }

    {	const char *n, *v, *r;
	headerNVR(h, &n, &v, &r);
	rpmMessage(RPMMESS_VERBOSE, "  --- %s-%s-%s\n", n, v, r);
    }

    blockSignals(rpmdb);

    {	int dbix;
	dbiIndexRecord rec = dbiReturnIndexRecordInstance(offset, 0);

	for (dbix = 0; dbix < dbiTagsMax; dbix++) {
	    dbiIndex dbi;
	    const char **rpmvals = NULL;
	    int rpmtype = 0;
	    int rpmcnt = 0;
	    int rpmtag;

	    /* XXX FIXME: this forces all indices open */
	    rpmtag = dbiTags[dbix];
	    dbi = dbiOpen(rpmdb, rpmtag, 0);

	    if (dbi->dbi_rpmtag == 0) {
		(void) dbiDel(dbi, &offset, sizeof(offset), 0);
		continue;
	    }
	
	    if (!headerGetEntry(h, dbi->dbi_rpmtag, &rpmtype,
		(void **) &rpmvals, &rpmcnt)) {
#if 0
		rpmMessage(RPMMESS_DEBUG, _("removing 0 %s entries.\n"),
			tagName(dbi->dbi_rpmtag));
#endif
		continue;
	    }

	    if (rpmtype == RPM_STRING_TYPE) {
		rpmMessage(RPMMESS_DEBUG, _("removing \"%s\" from %s index.\n"), 
			(const char *)rpmvals, tagName(dbi->dbi_rpmtag));

		(void) removeIndexEntry(dbi, (const char *)rpmvals,
			rec, tolerant);
	    } else {
		int i, mytolerant;

		rpmMessage(RPMMESS_DEBUG, _("removing %d entries in %s index:\n"), 
			rpmcnt, tagName(dbi->dbi_rpmtag));

		for (i = 0; i < rpmcnt; i++) {
		    rpmMessage(RPMMESS_DEBUG, _("\t%6d %s\n"),
			i, rpmvals[i]);

		    mytolerant = tolerant;
		    rec->fileNumber = 0;

		    switch (dbi->dbi_rpmtag) {
		    case RPMTAG_BASENAMES:
			rec->fileNumber = i;
			break;
		    /*
		     * There could be dups in the sorted list. Rather then
		     * sort the list, be tolerant of missing entries as they
		     * should just indicate duplicated entries.
		     */
		    case RPMTAG_REQUIRENAME:
		    case RPMTAG_TRIGGERNAME:
			mytolerant = 1;
			break;
		    }

		    (void) removeIndexEntry(dbi, rpmvals[i],
			rec, mytolerant);
		}
	    }

	    dbiSync(dbi, 0);

	    switch (rpmtype) {
	    case RPM_STRING_ARRAY_TYPE:
	    case RPM_I18NSTRING_TYPE:
		xfree(rpmvals);
		rpmvals = NULL;
		break;
	    }
	    rpmtype = 0;
	    rpmcnt = 0;
	}

	if (rec) {
	    free(rec);
	    rec = NULL;
	}
    }

    unblockSignals(rpmdb);

    headerFree(h);

    return 0;
}

static inline int addIndexEntry(dbiIndex dbi, const char *index, dbiIndexRecord rec)
{
    dbiIndexSet set = NULL;
    int rc;

    rc = dbiSearchIndex(dbi, index, 0, &set);

    switch (rc) {
    default:
    case -1:			/* error */
	rc = 1;
	break;
    case 1:			/* not found */
	rc = 0;
	set = xcalloc(1, sizeof(*set));
	/*@fallthrough@*/
    case 0:			/* success */
	dbiAppendIndexRecord(set, rec);
	if (dbiUpdateIndex(dbi, index, set))
	    rc = 1;
	break;
    }

    if (set) {
	dbiFreeIndexSet(set);
	set = NULL;
    }

    return 0;
}

/* XXX install.c */
int rpmdbAdd(rpmdb rpmdb, Header h)
{
    const char ** baseNames;
    int count = 0;
    int type;
    dbiIndex dbi;
    int rpmtag;
    int dbix;
    unsigned int offset;
    int rc = 0;

    /*
     * If old style filename tags is requested, the basenames need to be
     * retrieved early, and the header needs to be converted before
     * being written to the package header database.
     */

    headerGetEntry(h, RPMTAG_BASENAMES, &type, (void **) &baseNames, &count);

    if (_noDirTokens)
	expandFilelist(h);

    blockSignals(rpmdb);

    {
	unsigned int firstkey = 0;
	void * keyp = &firstkey;
	size_t keylen = sizeof(firstkey);
	void * datap = NULL;
	size_t datalen = 0;
	int rc;

	rpmtag = 0;	/* RPMDBI_PACKAGES */
	dbi = dbiOpen(rpmdb, rpmtag, 0);

	/* XXX db0: hack to pass sizeof header to fadAlloc */
	datap = h;
	datalen = headerSizeof(h, HEADER_MAGIC_NO);

	(void) dbiCopen(dbi, NULL, 0);

	/* Retrieve join key for next header instance. */

	rc = dbiGet(dbi, &keyp, &keylen, &datap, &datalen, 0);

	offset = 0;
	if (rc == 0 && datap)
	    memcpy(&offset, datap, sizeof(offset));
	++offset;
	if (rc == 0 && datap) {
	    memcpy(datap, &offset, sizeof(offset));
	} else {
	    datap = &offset;
	    datalen = sizeof(offset);
	}

	rc = dbiPut(dbi, keyp, keylen, datap, datalen, 0);

	(void) dbiCclose(dbi, NULL, 0);

    }

    if (rc) {
	rpmError(RPMERR_DBCORRUPT, _("cannot allocate new instance in database"));
	goto exit;
    }

    /* Now update the indexes */

    {	dbiIndexRecord rec = dbiReturnIndexRecordInstance(offset, 0);

	for (dbix = 0; dbix < dbiTagsMax; dbix++) {
	    const char **rpmvals = NULL;
	    int rpmtype = 0;
	    int rpmcnt = 0;
	    int rpmtag;

	    /* XXX FIXME: this forces all indices open */
	    rpmtag = dbiTags[dbix];
	    dbi = dbiOpen(rpmdb, rpmtag, 0);

	    if (dbi->dbi_rpmtag == 0) {
		size_t uhlen = headerSizeof(h, HEADER_MAGIC_NO);
		void * uh = headerUnload(h);

		(void) dbiPut(dbi, &offset, sizeof(offset), uh, uhlen, 0);
		free(uh);

		{   const char *n, *v, *r;
		    headerNVR(h, &n, &v, &r);
		    rpmMessage(RPMMESS_VERBOSE, "  +++ %8d %s-%s-%s\n", offset, n, v, r);
		}

		continue;
	    }
	
	    /* XXX preserve legacy behavior */
	    switch (dbi->dbi_rpmtag) {
	    case RPMTAG_BASENAMES:
		rpmtype = type;
		rpmvals = baseNames;
		rpmcnt = count;
		break;
	    default:
		headerGetEntry(h, dbi->dbi_rpmtag, &rpmtype,
			(void **) &rpmvals, &rpmcnt);
		break;
	    }

	    if (rpmcnt <= 0) {
		if (dbi->dbi_rpmtag != RPMTAG_GROUP) {
#if 0
		    rpmMessage(RPMMESS_DEBUG, _("adding 0 %s entries.\n"),
			tagName(dbi->dbi_rpmtag));
#endif
		    continue;
		}

		/* XXX preserve legacy behavior */
		rpmtype = RPM_STRING_TYPE;
		rpmvals = (const char **) "Unknown";
		rpmcnt = 1;
	    }

	    if (rpmtype == RPM_STRING_TYPE) {
		rpmMessage(RPMMESS_DEBUG, _("adding \"%s\" to %s index.\n"), 
			(const char *)rpmvals, tagName(dbi->dbi_rpmtag));

		rc += addIndexEntry(dbi, (const char *)rpmvals, rec);
	    } else {
		int i, j;

		rpmMessage(RPMMESS_DEBUG, _("adding %d entries to %s index:\n"), 
			rpmcnt, tagName(dbi->dbi_rpmtag));

		for (i = 0; i < rpmcnt; i++) {
		    rpmMessage(RPMMESS_DEBUG, _("%6d %s\n"),
			i, rpmvals[i]);

		    rec->fileNumber = 0;

		    switch (dbi->dbi_rpmtag) {
		    case RPMTAG_BASENAMES:
			rec->fileNumber = i;
			break;
		    case RPMTAG_TRIGGERNAME:	/* don't add duplicates */
			if (i == 0)
			   break;
			for (j = 0; j < i; j++) {
			    if (!strcmp(rpmvals[i], rpmvals[j]))
				break;
			}
			if (j < i)
			    continue;
			break;
		    }

		    rc += addIndexEntry(dbi, rpmvals[i], rec);
		}
	    }

	    dbiSync(dbi, 0);

	    switch (rpmtype) {
	    case RPM_STRING_ARRAY_TYPE:
	    case RPM_I18NSTRING_TYPE:
		xfree(rpmvals);
		rpmvals = NULL;
		break;
	    }
	    rpmtype = 0;
	    rpmcnt = 0;
	}

	if (rec) {
	    free(rec);
	    rec = NULL;
	}
    }

exit:
    unblockSignals(rpmdb);

    return rc;
}

/* XXX install.c */
int rpmdbUpdateRecord(rpmdb rpmdb, int offset, Header newHeader)
{
    int rc = 0;

    if (rpmdbRemove(rpmdb, offset, 1))
	return 1;

    if (rpmdbAdd(rpmdb, newHeader)) 
	return 1;

    return rc;
}

static void rpmdbRemoveDatabase(const char * rootdir, const char * dbpath)
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

    {	int i;

	for (i = 0; i < dbiTagsMax; i++) {
	    const char * base = db0basename(dbiTags[i]);
	    sprintf(filename, "%s/%s/%s", rootdir, dbpath, base);
	    unlink(filename);
	    xfree(base);
	}
        for (i = 0; i < 16; i++) {
	    sprintf(filename, "%s/%s/__db.%03d", rootdir, dbpath, i);
	    unlink(filename);
	}
        for (i = 0; i < 4; i++) {
	    sprintf(filename, "%s/%s/packages.db%d", rootdir, dbpath, i);
	    unlink(filename);
	}
    }

    sprintf(filename, "%s/%s", rootdir, dbpath);
    rmdir(filename);

}

static int rpmdbMoveDatabase(const char * rootdir, const char * olddbpath,
	const char * newdbpath)
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

    switch(_useDbiMajor) {
    case 3:
      {	int i;
	    sprintf(ofilename, "%s/%s/%s", rootdir, olddbpath, "packages.db3");
	    sprintf(nfilename, "%s/%s/%s", rootdir, newdbpath, "packages.db3");
	    (void)rpmCleanPath(ofilename);
	    (void)rpmCleanPath(nfilename);
	    if (Rename(ofilename, nfilename)) rc = 1;
        for (i = 0; i < 16; i++) {
	    sprintf(ofilename, "%s/%s/__db.%03d", rootdir, olddbpath, i);
	    (void)rpmCleanPath(ofilename);
	    if (!rpmfileexists(ofilename))
		continue;
	    sprintf(nfilename, "%s/%s/__db.%03d", rootdir, newdbpath, i);
	    (void)rpmCleanPath(nfilename);
	    if (Rename(ofilename, nfilename)) rc = 1;
	}
        for (i = 0; i < 4; i++) {
	    sprintf(ofilename, "%s/%s/packages.db%d", rootdir, olddbpath, i);
	    (void)rpmCleanPath(ofilename);
	    if (!rpmfileexists(ofilename))
		continue;
	    sprintf(nfilename, "%s/%s/packages.db%d", rootdir, newdbpath, i);
	    (void)rpmCleanPath(nfilename);
	    if (Rename(ofilename, nfilename)) rc = 1;
	}
      }	break;
    case 2:
    case 1:
    case 0:
      {	int i;
	for (i = 0; i < dbiTagsMax; i++) {
	    const char * base = db0basename(dbiTags[i]);
	    sprintf(ofilename, "%s/%s/%s", rootdir, olddbpath, base);
	    sprintf(nfilename, "%s/%s/%s", rootdir, newdbpath, base);
	    (void)rpmCleanPath(ofilename);
	    (void)rpmCleanPath(nfilename);
	    if (Rename(ofilename, nfilename))
		rc = 1;
	    xfree(base);
	}
        for (i = 0; i < 16; i++) {
	    sprintf(ofilename, "%s/%s/__db.%03d", rootdir, olddbpath, i);
	    (void)rpmCleanPath(ofilename);
	    if (rpmfileexists(ofilename))
		unlink(ofilename);
	    sprintf(nfilename, "%s/%s/__db.%03d", rootdir, newdbpath, i);
	    (void)rpmCleanPath(nfilename);
	    if (rpmfileexists(nfilename))
		unlink(nfilename);
	}
      }	break;
    }

    return rc;
}

/* XXX transaction.c */
int rpmdbFindFpList(rpmdb rpmdb, fingerPrint * fpList, dbiIndexSet * matchList, 
		    int numItems)
{
    rpmdbMatchIterator mi;
    fingerPrintCache fpc;
    Header h;
    int i;

    mi = rpmdbInitIterator(rpmdb, RPMTAG_BASENAMES, NULL, 0);

    /* Gather all matches from the database */
    for (i = 0; i < numItems; i++) {
	rpmdbGrowIterator(mi, fpList[i].baseName, 0, i);
	matchList[i] = xcalloc(1, sizeof(*(matchList[i])));
    }

    if ((i = rpmdbGetIteratorCount(mi)) == 0) {
	rpmdbFreeIterator(mi);
	return 0;
    }
    fpc = fpCacheCreate(i);

    rpmdbSortIterator(mi);
    /* iterator is now sorted by (recnum, filenum) */

    /* For each set of files matched in a package ... */
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	const char ** dirNames;
	const char ** baseNames;
	const char ** fullBaseNames;
	int_32 * dirIndexes;
	int_32 * fullDirIndexes;
	fingerPrint * fps;
	dbiIndexRecord im;
	int start;
	int num;
	int end;

	start = mi->mi_setx - 1;
	im = mi->mi_set->recs + start;

	/* Find the end of the set of matched files in this package. */
	for (end = start + 1; end < mi->mi_set->count; end++) {
	    if (im->recOffset != mi->mi_set->recs[end].recOffset)
		break;
	}
	num = end - start;

	/* Compute fingerprints for this header's matches */
	headerGetEntryMinMemory(h, RPMTAG_DIRNAMES, NULL, 
			    (void **) &dirNames, NULL);
	headerGetEntryMinMemory(h, RPMTAG_BASENAMES, NULL, 
			    (void **) &fullBaseNames, NULL);
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

	/* Add db (recnum,filenum) to list for fingerprint matches. */
	for (i = 0; i < num; i++, im++) {
	    if (FP_EQUAL_DIFFERENT_CACHE(fps[i], fpList[im->fpNum]))
		dbiAppendIndexRecord(matchList[im->fpNum], im);
	}

	free(fps);
	free(dirNames);
	free(fullBaseNames);
	free(baseNames);
	free(dirIndexes);

	mi->mi_setx = end;
    }

    rpmdbFreeIterator(mi);

    fpCacheFree(fpc);

    return 0;

}

/* XXX transaction.c */
/* 0 found matches */
/* 1 no matches */
/* 2 error */
int findMatches(rpmdb rpmdb, const char * name, const char * version,
			const char * release, dbiIndexSet * matches)
{
    dbiIndex dbi;
    int gotMatches;
    int rc;
    int i;

    dbi = dbiOpen(rpmdb, RPMTAG_NAME, 0);
    rc = dbiSearchIndex(dbi, name, 0, matches);

    if (rc != 0) {
	rc = ((rc == -1) ? 2 : 1);
	goto exit;
    }

    if (!version && !release) {
	rc = 0;
	goto exit;
    }

    gotMatches = 0;

    /* make sure the version and releases match */
    for (i = 0; i < dbiIndexSetCount(*matches); i++) {
	unsigned int recoff = dbiIndexRecordOffset(*matches, i);
	int goodRelease, goodVersion;
	const char * pkgVersion;
	const char * pkgRelease;
	Header h;

	if (recoff == 0)
	    continue;

	h = rpmdbGetRecord(rpmdb, recoff);
	if (h == NULL) {
	    rpmError(RPMERR_DBCORRUPT,_("cannot read header at %d for lookup"), 
		recoff);
	    rc = 2;
	    goto exit;
	}

	headerNVR(h, NULL, &pkgVersion, &pkgRelease);
	    
	goodRelease = goodVersion = 1;

	if (release && strcmp(release, pkgRelease)) goodRelease = 0;
	if (version && strcmp(version, pkgVersion)) goodVersion = 0;

	if (goodRelease && goodVersion) 
	    gotMatches = 1;
	else 
	    dbiIndexRecordOffsetSave(*matches, i, 0);

	headerFree(h);
    }

    if (!gotMatches) {
	rc = 1;
	goto exit;
    }
    rc = 0;

exit:
    if (rc && matches && *matches) {
	dbiFreeIndexSet(*matches);
	*matches = NULL;
    }
    return rc;
}

/* XXX query.c, rpminstall.c */
/* 0 found matches */
/* 1 no matches */
/* 2 error */
int rpmdbFindByLabel(rpmdb rpmdb, const char * arg, dbiIndexSet * matches)
{
    char * localarg, * chptr;
    char * release;
    int rc;
 
    if (!strlen(arg)) return 1;

    /* did they give us just a name? */
    rc = findMatches(rpmdb, arg, NULL, NULL, matches);
    if (rc != 1) return rc;

    /* maybe a name and a release */
    localarg = alloca(strlen(arg) + 1);
    strcpy(localarg, arg);

    chptr = (localarg + strlen(localarg)) - 1;
    while (chptr > localarg && *chptr != '-') chptr--;
    if (chptr == localarg) return 1;

    *chptr = '\0';
    rc = findMatches(rpmdb, localarg, chptr + 1, NULL, matches);
    if (rc != 1) return rc;
    
    /* how about name-version-release? */

    release = chptr + 1;
    while (chptr > localarg && *chptr != '-') chptr--;
    if (chptr == localarg) return 1;

    *chptr = '\0';
    return findMatches(rpmdb, localarg, chptr + 1, release, matches);
}

/** */
int rpmdbRebuild(const char * rootdir)
{
    rpmdb olddb;
    const char * dbpath = NULL;
    const char * rootdbpath = NULL;
    rpmdb newdb;
    const char * newdbpath = NULL;
    const char * newrootdbpath = NULL;
    const char * tfn;
    int nocleanup = 1;
    int failed = 0;
    int rc = 0;
    int _old_db_api;
    int _new_db_api;

    _old_db_api = rpmExpandNumeric("%{_dbapi}");
    _new_db_api = rpmExpandNumeric("%{_dbapi_rebuild}");

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
	(void)stpcpy(stpcpy(t, dbpath), pidbuf);
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

    _useDbiMajor = ((_old_db_api >= 0) ? (_old_db_api & 0x03) : -1);
    rpmMessage(RPMMESS_DEBUG, _("opening old database with dbi_major %d\n"),
		_useDbiMajor);
    if (openDatabase(rootdir, dbpath, &olddb, O_RDONLY, 0644, 
		     RPMDB_FLAG_MINIMAL)) {
	rc = 1;
	goto exit;
    }

    _useDbiMajor = ((_new_db_api >= 0) ? (_new_db_api & 0x03) : -1);
    rpmMessage(RPMMESS_DEBUG, _("opening new database with dbi_major %d\n"),
		_useDbiMajor);
    if (openDatabase(rootdir, newdbpath, &newdb, O_RDWR | O_CREAT, 0644, 0)) {
	rc = 1;
	goto exit;
    }

    {	Header h = NULL;
	rpmdbMatchIterator mi;
#define	_RECNUM	rpmdbGetIteratorOffset(mi)

	/* RPMDBI_PACKAGES */
	mi = rpmdbInitIterator(olddb, 0, NULL, 0);
	while ((h = rpmdbNextIterator(mi)) != NULL) {

	    /* let's sanity check this record a bit, otherwise just skip it */
	    if (!(headerIsEntry(h, RPMTAG_NAME) &&
		headerIsEntry(h, RPMTAG_VERSION) &&
		headerIsEntry(h, RPMTAG_RELEASE) &&
		headerIsEntry(h, RPMTAG_BUILDTIME)))
	    {
		rpmError(RPMERR_INTERNAL,
			_("record number %d in database is bad -- skipping."), 
			_RECNUM);
		continue;
	    }

	    /* Filter duplicate entries ? (bug in pre rpm-3.0.4) */
	    if (newdb->db_filter_dups) {
		const char * name, * version, * release;
		int skip = 0;

		headerNVR(h, &name, &version, &release);

		{   rpmdbMatchIterator mi;
		    mi = rpmdbInitIterator(newdb, RPMTAG_NAME, name, 0);
		    rpmdbSetIteratorVersion(mi, version);
		    rpmdbSetIteratorRelease(mi, release);
		    while (rpmdbNextIterator(mi)) {
			skip = 1;
			break;
		    }
		    rpmdbFreeIterator(mi);
		}

		if (skip)
		    continue;
	    }

	    /* Retrofit "Provide: name = EVR" for binary packages. */
	    providePackageNVR(h);

	    if (rpmdbAdd(newdb, h)) {
		rpmError(RPMERR_INTERNAL,
			_("cannot add record originally at %d"), _RECNUM);
		failed = 1;
		break;
	    }
	}

	rpmdbFreeIterator(mi);

    }

    if (!nocleanup) {
	olddb->db_remove_env = 1;
	newdb->db_remove_env = 1;
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
	    rpmMessage(RPMMESS_ERROR, _("replace files in %s with files from %s "
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
