#include "system.h"

static int _debug = 1;

#include <db.h>

#include <rpmlib.h>

#include "dbindex.h"
/*@access dbiIndex@*/
/*@access dbiIndexSet@*/

#if DB_VERSION_MAJOR == 2
#define	__USE_DB2	1
#define	_mymemset(_a, _b, _c)	memset((_a), (_b), (_c))

static inline DBTYPE dbi_to_dbtype(DBI_TYPE dbitype)
{
    switch(dbitype) {
    case DBI_BTREE:	return DB_BTREE;
    case DBI_HASH:	return DB_HASH;
    case DBI_RECNO:	return DB_RECNO;
    }
    /*@notreached@*/ return DB_HASH;
}

static inline /*@observer@*/ /*@null@*/ DB * GetDB(dbiIndex dbi) {
    return ((DB *)dbi->dbi_db);
}

#if defined(__USE_DB2) || defined(__USE_DB3)
#if defined(__USE_DB2)
static /*@observer@*/ const char * db_strerror(int error)
{
    if (error == 0)
	return ("Successful return: 0");
    if (error > 0)
	return (strerror(error));

    switch (error) {
    case DB_INCOMPLETE:
	return ("DB_INCOMPLETE: Cache flush was unable to complete");
    case DB_KEYEMPTY:
	return ("DB_KEYEMPTY: Non-existent key/data pair");
    case DB_KEYEXIST:
	return ("DB_KEYEXIST: Key/data pair already exists");
    case DB_LOCK_DEADLOCK:
	return ("DB_LOCK_DEADLOCK: Locker killed to resolve a deadlock");
    case DB_LOCK_NOTGRANTED:
	return ("DB_LOCK_NOTGRANTED: Lock not granted");
    case DB_NOTFOUND:
	return ("DB_NOTFOUND: No matching key/data pair found");
#if defined(__USE_DB3)
    case DB_OLD_VERSION:
	return ("DB_OLDVERSION: Database requires a version upgrade");
    case DB_RUNRECOVERY:
	return ("DB_RUNRECOVERY: Fatal error, run database recovery");
#else
    case DB_LOCK_NOTHELD:
	return ("DB_LOCK_NOTHELD:");
    case DB_REGISTERED:
	return ("DB_REGISTERED:");
#endif
    default:
      {
	/*
	 * !!!
	 * Room for a 64-bit number + slop.  This buffer is only used
	 * if we're given an unknown error, which should never happen.
	 * Note, however, we're no longer thread-safe if it does.
	 */
	static char ebuf[40];

	(void)snprintf(ebuf, sizeof(ebuf), "Unknown error: %d", error);
	return(ebuf);
      }
    }
    /*@notreached@*/
}

static int db_env_create(DB_ENV **dbenvp, int foo)
{
    DB_ENV *dbenv;

    if (dbenvp == NULL)
	return 1;
    dbenv = xcalloc(1, sizeof(*dbenv));

    *dbenvp = dbenv;
    return 0;
}
#endif	/* __USE_DB2 */

static int cvtdberr(dbiIndex dbi, const char * msg, int error, int printit) {
    int rc = 0;

    if (error == 0)
	rc = 0;
    else if (error < 0)
	rc = -1;
    else if (error > 0)
	rc = 1;

    if (printit && rc) {
	fprintf(stderr, "*** db%d %s rc %d %s\n", dbi->dbi_major, msg,
		rc, db_strerror(error));
    }

    return rc;
}

static int db_init(dbiIndex dbi, const char *home, int dbflags,
			DB_ENV **dbenvp, void **dbinfop)
{
    DB_ENV *dbenv = NULL;
    FILE * dberrfile = stderr;
    const char * dberrpfx = "rpmdb";
    int dbcachesize = 1024 * 1024;
    int dbpagesize = 32 * 1024;			/* 0 - 64K */
    int rc;

    if (dbenvp == NULL || dbinfop == NULL)
	return 1;

    rc = db_env_create(&dbenv, 0);
    rc = cvtdberr(dbi, "db_env_create", rc, _debug);
    if (rc)
	goto errxit;

#if defined(__USE_DB3)
  { int xx;
 /* dbenv->set_errcall(???) */
    xx = dbenv->set_errfile(dberrfile);
    xx = cvtdberr(dbi, "dbenv->set_errfile", xx, _debug);
    xx = dbenv->set_errpfx(dberrpfx);
    xx = cvtdberr(dbi, "dbenv->set_errpfx", xx, _debug);
 /* dbenv->set_paniccall(???) */
 /* dbenv->set_verbose(???) */
 /* dbenv->set_lg_max(???) */
 /* dbenv->set_lk_conflicts(???) */
 /* dbenv->set_lk_detect(???) */
 /* dbenv->set_lk_max(???) */
 /* dbenv->set_mp_mmapsize(???) */
    xx = dbenv->set_cachesize(dbcachesize);
    xx = cvtdberr(dbi, "dbenv->set_cachesize", xx, _debug);
 /* dbenv->set_tx_max(???) */
 /* dbenv->set_tx_recover(???) */
  }
#else
    dbenv->db_errfile = dberrfile;
    dbenv->db_errpfx = dberrpfx;
    dbenv->mp_size = dbcachesize;
#endif

#define _DBFMASK	(DB_CREATE|DB_NOMMAP|DB_THREAD)

#if defined(__USE_DB3)
    rc = dbenv->open(dbenv, home, NULL, (dbflags & _DBFMASK), 0);
    rc = cvtdberr(dbi, "dbenv->open", rc, _debug);
    if (rc)
	goto errxit;
    xx = db->set_pagesize(db, dbpagesize)
    xx = cvtdberr(dbi, "db->set_pagesize", xx, _debug);
    *dbinfop = NULL;
#else
    rc = db_appinit(home, NULL, dbenv, (dbflags & _DBFMASK));
    rc = cvtdberr(dbi, "db_appinit", rc, _debug);
    if (rc)
	goto errxit;
    {	DB_INFO * dbinfo = xcalloc(1, sizeof(*dbinfo));
	/* XXX W2DO? */
	dbinfo->db_pagesize = dbpagesize;
	*dbinfop = dbinfo;
     }
#endif

    *dbenvp = dbenv;

    return 0;

errxit:

#if defined(__USE_DB3)
    if (dbenv) {
	xx = dbenv->close(dbenv);
	xx = cvtdberr(dbi, "dbenv->close", xx, _debug);
    }
#else
    if (dbenv)	free(dbenv);
#endif
    return rc;
}
#endif	/* __USE_DB2 || __USE_DB3 */

static int db2close(dbiIndex dbi, unsigned int flags)
{
    DB * db = GetDB(dbi);
    int rc = 0, xx;

#if defined(__USE_DB2) || defined(__USE_DB3)

    if (dbi->dbi_dbcursor) {
	DBC * dbcursor = (DBC *)dbi->dbi_dbcursor;
	xx = dbcursor->c_close(dbcursor);
	xx = cvtdberr(dbi, "dbcursor->c_close", xx, _debug);
	dbi->dbi_dbcursor = NULL;
    }

    if (db) {
	rc = db->close(db, 0);
	rc = cvtdberr(dbi, "db->close", rc, _debug);
	db = dbi->dbi_db = NULL;
    }

    if (dbi->dbi_dbinfo) {
	free(dbi->dbi_dbinfo);
	dbi->dbi_dbinfo = NULL;
    }
    if (dbi->dbi_dbenv) {
	DB_ENV * dbenv = (DB_ENV *)dbi->dbi_dbenv;
#if defined(__USE_DB3)
	xx = dbenv->close(dbenv);
	xx = cvtdberr(dbi, "dbenv->close", xx, _debug);
#else
	xx = db_appexit(dbenv);
	xx = cvtdberr(dbi, "db_appexit", xx, _debug);
#endif
	free(dbi->dbi_dbenv);
	dbi->dbi_dbenv = NULL;
    }
#else
    rc = db->close(db);
#endif

    return rc;
}

static int db2sync(dbiIndex dbi, unsigned int flags)
{
    DB * db = GetDB(dbi);
    int rc;

#if defined(__USE_DB2) || defined(__USE_DB3)
    rc = db->sync(db, flags);
    rc = cvtdberr(dbi, "db->sync", rc, _debug);
#else
    rc = db->sync(db, flags);
#endif

    return rc;
}

static int db2GetFirstKey(dbiIndex dbi, const char ** keyp)
{
    DBT key, data;
    DB * db;
    int rc, xx;

    if (dbi == NULL || dbi->dbi_db == NULL)
	return 1;

    db = GetDB(dbi);
    _mymemset(&key, 0, sizeof(key));
    _mymemset(&data, 0, sizeof(data));

    key.data = NULL;
    key.size = 0;

#if defined(__USE_DB2) || defined(__USE_DB3)
    {	DBC * dbcursor = NULL;
	rc = db->cursor(db, NULL, &dbcursor);
	rc = cvtdberr(dbi, "db->cursor", rc, _debug);
	if (rc == 0) {
	    rc = dbcursor->c_get(dbcursor, &key, &data, DB_FIRST);
	    rc = cvtdberr(dbi, "dbcursor->c_get", rc, _debug);
	}
	if (dbcursor) {
	    xx = dbcursor->c_close(dbcursor);
	    xx = cvtdberr(dbi, "dbcursor->c_close", xx, _debug);
	}
    }
#else
    rc = db->seq(db, &key, &data, R_FIRST);
#endif

    if (rc == 0 && keyp) {
    	char *k = xmalloc(key.size + 1);
	memcpy(k, key.data, key.size);
	k[key.size] = '\0';
	*keyp = k;
    }

    return rc;
}

static int db2SearchIndex(dbiIndex dbi, const char * str, dbiIndexSet * set)
{
    DBT key, data;
    DB * db = GetDB(dbi);
    int rc;

    if (set) *set = NULL;
    _mymemset(&key, 0, sizeof(key));
    _mymemset(&data, 0, sizeof(data));

    key.data = (void *)str;
    key.size = strlen(str);
    data.data = NULL;
    data.size = 0;

#if defined(__USE_DB2) || defined(__USE_DB3)
    rc = db->get(db, NULL, &key, &data, 0);
    rc = cvtdberr(dbi, "db->get", rc, _debug);
#else
    rc = db->get(db, &key, &data, 0);
#endif

    if (rc == 0 && set) {
	*set = dbiCreateIndexSet();
	(*set)->recs = xmalloc(data.size);
	memcpy((*set)->recs, data.data, data.size);
	(*set)->count = data.size / sizeof(*(*set)->recs);
    }
    return rc;
}

/*@-compmempass@*/
static int db2UpdateIndex(dbiIndex dbi, const char * str, dbiIndexSet set)
{
    DBT key;
    DB * db = GetDB(dbi);
    int rc;

    _mymemset(&key, 0, sizeof(key));
    key.data = (void *)str;
    key.size = strlen(str);

    if (set->count) {
	DBT data;

	_mymemset(&data, 0, sizeof(data));
	data.data = set->recs;
	data.size = set->count * sizeof(*(set->recs));

#if defined(__USE_DB2) || defined(__USE_DB3)
	rc = db->put(db, NULL, &key, &data, 0);
	rc = cvtdberr(dbi, "db->put", rc, _debug);
#else
	rc = db->put(db, &key, &data, 0);
#endif

    } else {

#if defined(__USE_DB2) || defined(__USE_DB3)
	rc = db->del(db, NULL, &key, 0);
	rc = cvtdberr(dbi, "db->del", rc, _debug);
#else
	rc = db->del(db, &key, 0);
#endif

    }

    return rc;
}
/*@=compmempass@*/

static int db2open(dbiIndex dbi)
{
    int rc = 0;

#if defined(__USE_DB2) || defined(__USE_DB3)
    char * dbhome = NULL;
    DB * db = NULL;
    DB_ENV * dbenv = NULL;
    void * dbinfo = NULL;
    u_int32_t dbflags;

    dbflags = (	!(dbi->dbi_flags & O_RDWR) ? DB_RDONLY :
		((dbi->dbi_flags & O_CREAT) ? DB_CREATE : 0));

    rc = db_init(dbi, dbhome, dbflags, &dbenv, &dbinfo);

    if (rc == 0) {
#if defined(__USE_DB3)
	rc = db_create(&db, dbenv, 0);
	rc = cvtdberr(dbi, "db_create", rc, _debug);
	if (rc == 0) {
	    rc = db->open(db, dbi->dbi_file, NULL, dbi_to_dbtype(dbi->dbi_type),
			dbflags, dbi->dbi_perms);
	    rc = cvtdberr(dbi, "db->open", rc, _debug);
	}
#else
	rc = db_open(dbi->dbi_file, dbi_to_dbtype(dbi->dbi_type), dbflags,
			dbi->dbi_perms, dbenv, dbinfo, &db);
	rc = cvtdberr(dbi, "db_open", rc, _debug);
#endif	/* __USE_DB3 */
    }

    dbi->dbi_db = db;
    dbi->dbi_dbenv = dbenv;
    dbi->dbi_dbinfo = dbinfo;

#else
    dbi->dbi_db = dbopen(dbi->dbi_file, dbi->dbi_flags, dbi->dbi_perms,
		dbi_to_dbtype(dbi->dbi_type), dbi->dbi_openinfo);
#endif	/* __USE_DB2 || __USE_DB3 */

    if (rc == 0 && dbi->dbi_db != NULL)
	rc = 0;
    else
	rc = 1;

    return rc;
}

struct _dbiVec db2vec = {
    DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH,
    db2open, db2close, db2sync, db2GetFirstKey, db2SearchIndex, db2UpdateIndex
};

#endif	/* DB_VERSION_MAJOR == 2 */
