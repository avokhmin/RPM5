/** \ingroup db3
 * \file rpmdb/db3.c
 */

static int _debug = 1;	/* XXX if < 0 debugging, > 0 unusual error returns */

#ifdef	__LCLINT__
typedef	unsigned int u_int32_t;
typedef	unsigned short u_int16_t;
typedef	unsigned char u_int8_t;
typedef	int int32_t;
#endif

#include "system.h"

#if defined(HAVE_FTOK) && defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
#endif

#include <db3/db.h>

#include <rpmlib.h>
#include <rpmmacro.h>
#include <rpmurl.h>	/* XXX urlPath proto */

#include "rpmdb.h"
#include "debug.h"

/*@access rpmdb@*/
/*@access dbiIndex@*/
/*@access dbiIndexSet@*/

#if DB_VERSION_MAJOR == 3
#define	__USE_DB3	1

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
#else	/* __USE_DB3 */
    case DB_LOCK_NOTHELD:
	return ("DB_LOCK_NOTHELD:");
    case DB_REGISTERED:
	return ("DB_REGISTERED:");
#endif	/* __USE_DB3 */
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

    rc = error;

    if (printit && rc) {
	if (msg)
	    rpmError(RPMERR_DBERR, _("db%d error(%d) from %s: %s\n"),
		dbi->dbi_api, rc, msg, db_strerror(error));
	else
	    rpmError(RPMERR_DBERR, _("db%d error(%d): %s\n"),
		dbi->dbi_api, rc, db_strerror(error));
    }

    return rc;
}

static int db_fini(dbiIndex dbi, const char * dbhome,
		/*@null@*/ const char * dbfile,
		/*@unused@*/ /*@null@*/ const char * dbsubfile)
{
    rpmdb rpmdb = dbi->dbi_rpmdb;
    DB_ENV * dbenv = dbi->dbi_dbenv;

#if defined(__USE_DB3)
    int rc;

    if (dbenv == NULL) {
	dbi->dbi_dbenv = NULL;
	return 0;
    }

    rc = dbenv->close(dbenv, 0);
    rc = cvtdberr(dbi, "dbenv->close", rc, _debug);

    if (dbfile)
	rpmMessage(RPMMESS_DEBUG, _("closed  db environment %s/%s\n"),
			dbhome, dbfile);

    if (rpmdb->db_remove_env || dbi->dbi_tear_down) {
	int xx;

	xx = db_env_create(&dbenv, 0);
	xx = cvtdberr(dbi, "db_env_create", rc, _debug);
#if DB_VERSION_MAJOR == 3 && (DB_VERSION_MINOR == 1 || DB_VERSION_MINOR == 2)
	xx = dbenv->remove(dbenv, dbhome, 0);
#else
	xx = dbenv->remove(dbenv, dbhome, NULL, 0);
#endif
	xx = cvtdberr(dbi, "dbenv->remove", rc, _debug);

	if (dbfile)
	    rpmMessage(RPMMESS_DEBUG, _("removed db environment %s/%s\n"),
			dbhome, dbfile);

    }
	
#else	/* __USE_DB3 */
    rc = db_appexit(dbenv);
    rc = cvtdberr(dbi, "db_appexit", rc, _debug);
    free(dbenv);
#endif	/* __USE_DB3 */
    dbi->dbi_dbenv = NULL;
    return rc;
}

static int db3_fsync_disable(/*@unused@*/ int fd) {
    return 0;
}

static int db_init(dbiIndex dbi, const char *dbhome,
		/*@null@*/ const char *dbfile,
		/*@unused@*/ /*@null@*/ const char * dbsubfile,
		/*@out@*/ DB_ENV **dbenvp)
{
    rpmdb rpmdb = dbi->dbi_rpmdb;
    DB_ENV *dbenv = NULL;
    int eflags;
    int rc;

    if (dbenvp == NULL)
	return 1;

    /* XXX HACK */
    if (rpmdb->db_errfile == NULL)
	rpmdb->db_errfile = stderr;

    eflags = (dbi->dbi_oeflags | dbi->dbi_eflags);
    if (eflags & DB_JOINENV) eflags &= DB_JOINENV;

    if (dbfile)
	rpmMessage(RPMMESS_DEBUG, _("opening db environment %s/%s %s\n"),
		dbhome, dbfile, prDbiOpenFlags(eflags, 1));

    /* XXX Can't do RPC w/o host. */
    if (dbi->dbi_host == NULL)
	dbi->dbi_ecflags &= ~DB_CLIENT;

    /* XXX Set a default shm_key. */
    if ((dbi->dbi_eflags & DB_SYSTEM_MEM) && dbi->dbi_shmkey == 0) {
#if defined(HAVE_FTOK)
	dbi->dbi_shmkey = ftok(dbhome, 0);
#else
	dbi->dbi_shmkey = 0x44631380;
#endif
    }

    rc = db_env_create(&dbenv, dbi->dbi_ecflags);
    rc = cvtdberr(dbi, "db_env_create", rc, _debug);
    if (rc)
	goto errxit;

#if defined(__USE_DB3)
    if (dbenv == NULL)
	return 1;

  { int xx;
    dbenv->set_errcall(dbenv, rpmdb->db_errcall);
    dbenv->set_errfile(dbenv, rpmdb->db_errfile);
    dbenv->set_errpfx(dbenv, rpmdb->db_errpfx);
 /* dbenv->set_paniccall(???) */
    (void) dbenv->set_verbose(dbenv, DB_VERB_CHKPOINT,
		(dbi->dbi_verbose & DB_VERB_CHKPOINT));
    (void) dbenv->set_verbose(dbenv, DB_VERB_DEADLOCK,
		(dbi->dbi_verbose & DB_VERB_DEADLOCK));
    (void) dbenv->set_verbose(dbenv, DB_VERB_RECOVERY,
		(dbi->dbi_verbose & DB_VERB_RECOVERY));
    (void) dbenv->set_verbose(dbenv, DB_VERB_WAITSFOR,
		(dbi->dbi_verbose & DB_VERB_WAITSFOR));
 /* dbenv->set_lg_max(???) */
 /* dbenv->set_lk_conflicts(???) */
 /* dbenv->set_lk_detect(???) */
 /* dbenv->set_lk_max(???) */
    xx = dbenv->set_mp_mmapsize(dbenv, dbi->dbi_mp_mmapsize);
    xx = cvtdberr(dbi, "dbenv->set_mp_mmapsize", xx, _debug);
    xx = dbenv->set_cachesize(dbenv, 0, dbi->dbi_mp_size, 0);
    xx = cvtdberr(dbi, "dbenv->set_cachesize", xx, _debug);
 /* dbenv->set_tx_max(???) */
 /* dbenv->set_tx_recover(???) */
    if (dbi->dbi_no_fsync) {
#if DB_VERSION_MAJOR == 3 && (DB_VERSION_MINOR == 1 || DB_VERSION_MINOR == 2)
	xx = db_env_set_func_fsync(db3_fsync_disable);
#else
	xx = dbenv->set_func_fsync(dbenv, db3_fsync_disable);
#endif
	xx = cvtdberr(dbi, "db_env_set_func_fsync", xx, _debug);
    }

    if ((dbi->dbi_ecflags & DB_CLIENT) && dbi->dbi_host) {
	xx = dbenv->set_server(dbenv, dbi->dbi_host,
		dbi->dbi_cl_timeout, dbi->dbi_sv_timeout, 0);
	xx = cvtdberr(dbi, "dbenv->set_server", xx, _debug);
    }
    if (dbi->dbi_shmkey) {
	xx = dbenv->set_shm_key(dbenv, dbi->dbi_shmkey);
	xx = cvtdberr(dbi, "dbenv->set_shm_key", xx, _debug);
    }
  }
#else	/* __USE_DB3 */
    dbenv->db_errcall = rpmdb->db_errcall;
    dbenv->db_errfile = rpmdb->db_errfile;
    dbenv->db_errpfx = rpmdb->db_errpfx;
    dbenv->db_verbose = dbi->dbi_verbose;
    dbenv->mp_mmapsize = dbi->dbi_mp_mmapsize;	/* XXX default is 10 Mb */
    dbenv->mp_size = dbi->dbi_mp_size;		/* XXX default is 128 Kb */
#endif	/* __USE_DB3 */

#if defined(__USE_DB3)
#if DB_VERSION_MAJOR == 3 && (DB_VERSION_MINOR == 1 || DB_VERSION_MINOR == 2)
    rc = dbenv->open(dbenv, dbhome, eflags, dbi->dbi_perms);
#else
    rc = dbenv->open(dbenv, dbhome, NULL, eflags, dbi->dbi_perms);
#endif
    rc = cvtdberr(dbi, "dbenv->open", rc, _debug);
    if (rc)
	goto errxit;
#else	/* __USE_DB3 */
    rc = db_appinit(dbhome, NULL, dbenv, eflags);
    rc = cvtdberr(dbi, "db_appinit", rc, _debug);
    if (rc)
	goto errxit;
#endif	/* __USE_DB3 */

    *dbenvp = dbenv;

    return 0;

errxit:

#if defined(__USE_DB3)
    if (dbenv) {
	int xx;
	xx = dbenv->close(dbenv, 0);
	xx = cvtdberr(dbi, "dbenv->close", xx, _debug);
    }
#else	/* __USE_DB3 */
    if (dbenv)	free(dbenv);
#endif	/* __USE_DB3 */
    return rc;
}

#endif	/* __USE_DB2 || __USE_DB3 */

static int db3sync(dbiIndex dbi, unsigned int flags)
{
    DB * db = dbi->dbi_db;
    int rc = 0;

#if defined(__USE_DB2) || defined(__USE_DB3)
    int _printit;

    if (db != NULL)
	rc = db->sync(db, flags);
    /* XXX DB_INCOMPLETE is returned occaisionally with multiple access. */
    _printit = (rc == DB_INCOMPLETE ? 0 : _debug);
    rc = cvtdberr(dbi, "db->sync", rc, _printit);
#else	/* __USE_DB2 || __USE_DB3 */
    rc = db->sync(db, flags);
#endif	/* __USE_DB2 || __USE_DB3 */

    return rc;
}

static int db3c_del(dbiIndex dbi, DBC * dbcursor, u_int32_t flags)
{
    int rc;

    rc = dbcursor->c_del(dbcursor, flags);
    rc = cvtdberr(dbi, "dbcursor->c_del", rc, _debug);
    return rc;
}

/*@unused@*/ static int db3c_dup(dbiIndex dbi, DBC * dbcursor, DBC ** dbcp,
		u_int32_t flags)
{
    int rc;

    rc = dbcursor->c_dup(dbcursor, dbcp, flags);
    rc = cvtdberr(dbi, "dbcursor->c_dup", rc, _debug);
    return rc;
}

static int db3c_get(dbiIndex dbi, DBC * dbcursor,
	DBT * key, DBT * data, u_int32_t flags)
{
    int _printit;
    int rc;
    int rmw;

#ifdef	NOTYET
    if ((dbi->dbi_eflags & DB_INIT_CDB) && !(dbi->dbi_oflags & DB_RDONLY))
	rmw = DB_RMW;
    else
#endif
	rmw = 0;

    rc = dbcursor->c_get(dbcursor, key, data, rmw | flags);

    /* XXX DB_NOTFOUND can be returned */
    _printit = (rc == DB_NOTFOUND ? 0 : _debug);
    rc = cvtdberr(dbi, "dbcursor->c_get", rc, _printit);
    return rc;
}

static int db3c_put(dbiIndex dbi, DBC * dbcursor,
	DBT * key, DBT * data, u_int32_t flags)
{
    int rc;

    rc = dbcursor->c_put(dbcursor, key, data, flags);

    rc = cvtdberr(dbi, "dbcursor->c_put", rc, _debug);
    return rc;
}

static inline int db3c_close(dbiIndex dbi, /*@only@*/ /*@null@*/ DBC * dbcursor)
{
    int rc;

    if (dbcursor == NULL) return -2;

    rc = dbcursor->c_close(dbcursor);
    rc = cvtdberr(dbi, "dbcursor->c_close", rc, _debug);
    return rc;
}

static inline int db3c_open(dbiIndex dbi, /*@out@*/ DBC ** dbcp, int dbiflags)
{
    DB * db = dbi->dbi_db;
    DB_TXN * txnid = NULL;
    int flags;
    int rc;

    if (db == NULL) return -2;
#if defined(__USE_DB3)
    if ((dbiflags & DBI_WRITECURSOR) &&
	(dbi->dbi_eflags & DB_INIT_CDB) && !(dbi->dbi_oflags & DB_RDONLY))
    {
	flags = DB_WRITECURSOR;
    } else
	flags = 0;
    rc = db->cursor(db, txnid, dbcp, flags);
#else	/* __USE_DB3 */
    rc = db->cursor(db, txnid, dbcp);
#endif	/* __USE_DB3 */
    rc = cvtdberr(dbi, "db3c_open", rc, _debug);

    return rc;
}

static int db3cclose(dbiIndex dbi, /*@only@*/ /*@null@*/ DBC * dbcursor,
		unsigned int flags)
{
    int rc = 0;

    /* XXX per-iterator cursors */
    if (flags & DBI_ITERATOR)
	return db3c_close(dbi, dbcursor);

    if (!dbi->dbi_use_cursors)
	return 0;

    if (dbcursor == NULL)
	dbcursor = dbi->dbi_rmw;
    if (dbcursor) {
	if (dbcursor == dbi->dbi_rmw)
	    dbi->dbi_rmw = NULL;
	rc = db3c_close(dbi, dbcursor);
    }
    /*@-usereleased -compdef@*/ return rc; /*@=usereleased =compdef@*/
}

static int db3copen(dbiIndex dbi, /*@out@*/ DBC ** dbcp, unsigned int flags)
{
    DBC * dbcursor;
    int rc = 0;

    /* XXX per-iterator cursors */
    if (flags & DBI_ITERATOR)
	return db3c_open(dbi, dbcp, flags);

    if (!dbi->dbi_use_cursors) {
	if (dbcp) *dbcp = NULL;
	return 0;
    }

    if ((dbcursor = dbi->dbi_rmw) == NULL) {
	if ((rc = db3c_open(dbi, &dbcursor, flags)) == 0)
	    dbi->dbi_rmw = dbcursor;
    }

    if (dbcp)
	/*@-onlytrans@*/ *dbcp = dbi->dbi_rmw; /*@=onlytrans@*/

    return rc;
}

static int db3cput(dbiIndex dbi, DBC * dbcursor,
		const void * keyp, size_t keylen,
		const void * datap, size_t datalen,
		/*@unused@*/ unsigned int flags)
{
    DB * db = dbi->dbi_db;
    DB_TXN * txnid = NULL;
    DBT key, data;
    int rc;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    key.data = (void *)keyp;
    key.size = keylen;
    data.data = (void *)datap;
    data.size = datalen;

    if (dbcursor == NULL) {
	if (db == NULL) return -2;
	rc = db->put(db, txnid, &key, &data, 0);
	rc = cvtdberr(dbi, "db->put", rc, _debug);
    } else {

	rc = db3c_put(dbi, dbcursor, &key, &data, DB_KEYLAST);

    }

    return rc;
}

static int db3cdel(dbiIndex dbi, DBC * dbcursor,
		const void * keyp, size_t keylen,
		/*@unused@*/ unsigned int flags)
{
    DB * db = dbi->dbi_db;
    DB_TXN * txnid = NULL;
    DBT key, data;
    int rc;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = (void *)keyp;
    key.size = keylen;

    if (dbcursor == NULL) {
	if (db == NULL) return -2;
	rc = db->del(db, txnid, &key, 0);
	rc = cvtdberr(dbi, "db->del", rc, _debug);
    } else {

	rc = db3c_get(dbi, dbcursor, &key, &data, DB_SET);

	if (rc == 0) {
	    /* XXX TODO: loop over duplicates */
	    rc = db3c_del(dbi, dbcursor, 0);
	}

    }

    return rc;
}

static int db3cget(dbiIndex dbi, DBC * dbcursor,
		void ** keyp, size_t * keylen,
		void ** datap, size_t * datalen,
		/*@unused@*/ unsigned int flags)
{
    DB * db = dbi->dbi_db;
    DB_TXN * txnid = NULL;
    DBT key, data;
    int rc;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    /*@-unqualifiedtrans@*/
    if (keyp)		key.data = *keyp;
    if (keylen)		key.size = *keylen;
    if (datap)		data.data = *datap;
    if (datalen)	data.size = *datalen;
    /*@=unqualifiedtrans@*/

    if (dbcursor == NULL) {
	int _printit;
	/*@-compmempass@*/
	if (db == NULL) return -2;
	/*@=compmempass@*/
	rc = db->get(db, txnid, &key, &data, 0);
	/* XXX DB_NOTFOUND can be returned */
	_printit = (rc == DB_NOTFOUND ? 0 : _debug);
	rc = cvtdberr(dbi, "db->get", rc, _printit);
    } else {

	/* XXX db3 does DB_FIRST on uninitialized cursor */
	rc = db3c_get(dbi, dbcursor, &key, &data,
		key.data == NULL ? DB_NEXT : DB_SET);

    }

    if (rc == 0) {
	/*@-onlytrans@*/
	if (keyp)	*keyp = key.data;
	if (keylen)	*keylen = key.size;
	if (datap)	*datap = data.data;
	if (datalen)	*datalen = data.size;
	/*@=onlytrans@*/
    }

    /*@-compmempass -nullstate@*/
    return rc;
    /*@=compmempass =nullstate@*/
}

static int db3byteswapped(dbiIndex dbi)
{
    DB * db = dbi->dbi_db;
    int rc = 0;

#if defined(__USE_DB3)
    if (db != NULL)
	rc = db->get_byteswapped(db);
#endif	/* __USE_DB3 */

    return rc;
}

static int db3close(/*@only@*/ dbiIndex dbi, /*@unused@*/ unsigned int flags)
{
    rpmdb rpmdb = dbi->dbi_rpmdb;
    const char * urlfn = NULL;
    const char * dbhome;
    const char * dbfile;
    const char * dbsubfile;
    DB * db = dbi->dbi_db;
    int rc = 0, xx;

    urlfn = rpmGenPath(
	(dbi->dbi_root ? dbi->dbi_root : rpmdb->db_root),
	(dbi->dbi_home ? dbi->dbi_home : rpmdb->db_home),
	NULL);
    (void) urlPath(urlfn, &dbhome);
    if (dbi->dbi_temporary) {
	dbfile = NULL;
	dbsubfile = NULL;
    } else {
#ifdef	HACK
	dbfile = (dbi->dbi_file ? dbi->dbi_file : db3basename);
	dbsubfile = (dbi->dbi_subfile ? dbi->dbi_subfile : tagName(dbi->dbi_rpmtag));
#else
	dbfile = (dbi->dbi_file ? dbi->dbi_file : tagName(dbi->dbi_rpmtag));
	dbsubfile = NULL;
#endif
    }

#if defined(__USE_DB2) || defined(__USE_DB3)

    if (dbi->dbi_rmw)
	rc = db3cclose(dbi, NULL, 0);

    if (db) {
	rc = db->close(db, 0);
	rc = cvtdberr(dbi, "db->close", rc, _debug);
	db = dbi->dbi_db = NULL;

	rpmMessage(RPMMESS_DEBUG, _("closed  db index       %s/%s\n"),
		dbhome, (dbfile ? dbfile : tagName(dbi->dbi_rpmtag)));

    }

    if (dbi->dbi_dbinfo) {
	free(dbi->dbi_dbinfo);
	dbi->dbi_dbinfo = NULL;
    }

    if (dbi->dbi_use_dbenv)
	/*@-nullstate@*/
	xx = db_fini(dbi, (dbhome ? dbhome : ""), dbfile, dbsubfile);
	/*@=nullstate@*/

#else	/* __USE_DB2 || __USE_DB3 */

    rc = db->close(db);

#endif	/* __USE_DB2 || __USE_DB3 */
    dbi->dbi_db = NULL;

    urlfn = _free(urlfn);

    db3Free(dbi);

    return rc;
}

static int db3open(/*@keep@*/ rpmdb rpmdb, int rpmtag, dbiIndex * dbip)
{
    /*@-nestedextern@*/
    extern struct _dbiVec db3vec;
    /*@=nestedextern@*/
    const char * urlfn = NULL;
    const char * dbhome;
    const char * dbfile;
    const char * dbsubfile;
    dbiIndex dbi = NULL;
    int rc = 0;
    int xx;

#if defined(__USE_DB2) || defined(__USE_DB3)
    DB * db = NULL;
    DB_ENV * dbenv = NULL;
    DB_TXN * txnid = NULL;
    u_int32_t oflags;
    int _printit;

    if (dbip)
	*dbip = NULL;
    if ((dbi = db3New(rpmdb, rpmtag)) == NULL)
	/*@-nullstate@*/
	return 1;
	/*@=nullstate@*/
    dbi->dbi_api = DB_VERSION_MAJOR;

    urlfn = rpmGenPath(
	(dbi->dbi_root ? dbi->dbi_root : rpmdb->db_root),
	(dbi->dbi_home ? dbi->dbi_home : rpmdb->db_home),
	NULL);
    (void) urlPath(urlfn, &dbhome);
    if (dbi->dbi_temporary) {
	dbfile = NULL;
	dbsubfile = NULL;
    } else {
#ifdef	HACK
	dbfile = (dbi->dbi_file ? dbi->dbi_file : db3basename);
	dbsubfile = (dbi->dbi_subfile ? dbi->dbi_subfile : tagName(dbi->dbi_rpmtag));
#else
	dbfile = (dbi->dbi_file ? dbi->dbi_file : tagName(dbi->dbi_rpmtag));
	dbsubfile = NULL;
#endif
    }

    oflags = (dbi->dbi_oeflags | dbi->dbi_oflags);
    oflags &= ~DB_TRUNCATE;	/* XXX this is dangerous */

#if 0	/* XXX rpmdb: illegal flag combination specified to DB->open */
    if ( dbi->dbi_mode & O_EXCL) oflags |= DB_EXCL;
#endif

    /*
     * Map open mode flags onto configured database/environment flags.
     */
    if (dbi->dbi_temporary) {
	oflags |= DB_CREATE;
	dbi->dbi_oeflags |= DB_CREATE;
	oflags &= ~DB_RDONLY;
	dbi->dbi_oflags &= ~DB_RDONLY;
    } else {
	if (!(dbi->dbi_mode & (O_RDWR|O_WRONLY))) oflags |= DB_RDONLY;
	if (dbi->dbi_mode & O_CREAT) {
	    oflags |= DB_CREATE;
	    dbi->dbi_oeflags |= DB_CREATE;
	}
#ifdef	DANGEROUS
	if ( dbi->dbi_mode & O_TRUNC) oflags |= DB_TRUNCATE;
#endif
    }

    /*
     * Avoid incompatible DB_CREATE/DB_RDONLY flags on DBENV->open.
     */
    if (dbi->dbi_use_dbenv) {
	if (access(dbhome, W_OK) == -1) {

	    /* dbhome is unwritable, don't attempt DB_CREATE on DB->open ... */
	    oflags &= ~DB_CREATE;

	    /* ... but DBENV->open might still need DB_CREATE ... */
	    if (dbi->dbi_eflags & DB_PRIVATE) {
		dbi->dbi_eflags &= ~DB_JOINENV;
	    } else {
		dbi->dbi_eflags |= DB_JOINENV;
		dbi->dbi_oeflags &= ~DB_CREATE;
		dbi->dbi_oeflags &= ~DB_THREAD;
		/* ... but, unless DB_PRIVATE is used, skip DBENV. */
		dbi->dbi_use_dbenv = 0;
	    }

	    /* ... DB_RDONLY maps dphome perms across files ...  */
	    oflags |= DB_RDONLY;

	    /* ... and DB_WRITECURSOR won't be needed ...  */
	    dbi->dbi_oflags |= DB_RDONLY;
	} else {	/* dbhome is writable, check for persistent dbenv. */
	    const char * dbf = rpmGetPath(dbhome, "/__db.001", NULL);

	    if (access(dbf, F_OK) == -1) {
		/* ... non-existent (or unwritable) DBENV, will create ... */
		dbi->dbi_oeflags |= DB_CREATE;
		dbi->dbi_eflags &= ~DB_JOINENV;
	    } else {
		/* ... pre-existent (or bogus) DBENV, will join ... */
		dbi->dbi_oeflags &= ~DB_CREATE;
		dbi->dbi_eflags |= DB_JOINENV;
	    }
	    dbf = _free(dbf);
	}
    }

    /*
     * Avoid incompatible DB_CREATE/DB_RDONLY flags on DB->open.
     */
    if ((oflags & DB_CREATE) && (oflags & DB_RDONLY)) {
	/* dbhome is writable, and DB->open flags may conflict. */
	const char * dbfn = (dbfile ? dbfile : tagName(dbi->dbi_rpmtag));
	const char * dbf = rpmGetPath(dbhome, "/", dbfn, NULL);

	if (access(dbf, F_OK) == -1) {
	    /* File does not exist, DB->open might create ... */
	    oflags &= ~DB_RDONLY;
	} else {
	    /* File exists, DB->open need not create ... */
	    oflags &= ~DB_CREATE;
	}

	/* Only writers need DB_WRITECURSOR ... */
	if (!(oflags & DB_RDONLY) && access(dbf, W_OK) == 0) {
	    dbi->dbi_oflags &= ~DB_RDONLY;
	} else {
	    dbi->dbi_oflags |= DB_RDONLY;
	}
	dbf = _free(dbf);
    }

    dbi->dbi_dbinfo = NULL;

    if (dbi->dbi_use_dbenv)
	rc = db_init(dbi, dbhome, dbfile, dbsubfile, &dbenv);

    rpmMessage(RPMMESS_DEBUG, _("opening db index       %s/%s %s mode=0x%x\n"),
		dbhome, (dbfile ? dbfile : tagName(dbi->dbi_rpmtag)),
		prDbiOpenFlags(oflags, 0), dbi->dbi_mode);

    if (rc == 0) {
#if defined(__USE_DB3)
	static int _lockdbfd = 0;

	rc = db_create(&db, dbenv, dbi->dbi_cflags);
	rc = cvtdberr(dbi, "db_create", rc, _debug);
	if (rc == 0 && db != NULL) {
	    if (rc == 0 && dbi->dbi_lorder) {
		rc = db->set_lorder(db, dbi->dbi_lorder);
		rc = cvtdberr(dbi, "db->set_lorder", rc, _debug);
	    }
	    if (rc == 0 && dbi->dbi_cachesize) {
		rc = db->set_cachesize(db, 0, dbi->dbi_cachesize, 0);
		rc = cvtdberr(dbi, "db->set_cachesize", rc, _debug);
	    }
	    if (rc == 0 && dbi->dbi_pagesize) {
		rc = db->set_pagesize(db, dbi->dbi_pagesize);
		rc = cvtdberr(dbi, "db->set_pagesize", rc, _debug);
	    }
	    if (rc == 0 && rpmdb->db_malloc) {
		rc = db->set_malloc(db, rpmdb->db_malloc);
		rc = cvtdberr(dbi, "db->set_malloc", rc, _debug);
	    }
	    if (rc == 0 && oflags & DB_CREATE) {
		switch(dbi->dbi_type) {
		default:
		case DB_HASH:
		    if (dbi->dbi_h_ffactor) {
			rc = db->set_h_ffactor(db, dbi->dbi_h_ffactor);
			rc = cvtdberr(dbi, "db->set_h_ffactor", rc, _debug);
			if (rc) break;
		    }
#if DB_VERSION_MAJOR == 3 && DB_VERSION_MINOR == 1
		    if (dbi->dbi_h_hash_fcn) {
			rc = db->set_h_hash(db, dbi->dbi_h_hash_fcn);
			rc = cvtdberr(dbi, "db->set_h_hash", rc, _debug);
			if (rc) break;
		    }
#endif
		    if (dbi->dbi_h_nelem) {
			rc = db->set_h_nelem(db, dbi->dbi_h_nelem);
			rc = cvtdberr(dbi, "db->set_h_nelem", rc, _debug);
			if (rc) break;
		    }
		    if (dbi->dbi_h_flags) {
			rc = db->set_flags(db, dbi->dbi_h_flags);
			rc = cvtdberr(dbi, "db->set_h_flags", rc, _debug);
			if (rc) break;
		    }
/* XXX db-3.2.9 has added a DB arg to the callback. */
#if DB_VERSION_MAJOR == 3 && DB_VERSION_MINOR == 1
		    if (dbi->dbi_h_dup_compare_fcn) {
			rc = db->set_dup_compare(db, dbi->dbi_h_dup_compare_fcn);
			rc = cvtdberr(dbi, "db->set_dup_compare", rc, _debug);
			if (rc) break;
		    }
#endif
		    break;
		case DB_BTREE:
		    break;
		case DB_RECNO:
		    if (dbi->dbi_re_delim) {
			rc = db->set_re_delim(db, dbi->dbi_re_delim);
			rc = cvtdberr(dbi, "db->set_re_selim", rc, _debug);
			if (rc) break;
		    }
		    if (dbi->dbi_re_len) {
			rc = db->set_re_len(db, dbi->dbi_re_len);
			rc = cvtdberr(dbi, "db->set_re_len", rc, _debug);
			if (rc) break;
		    }
		    if (dbi->dbi_re_pad) {
			rc = db->set_re_pad(db, dbi->dbi_re_pad);
			rc = cvtdberr(dbi, "db->set_re_pad", rc, _debug);
			if (rc) break;
		    }
		    if (dbi->dbi_re_source) {
			rc = db->set_re_source(db, dbi->dbi_re_source);
			rc = cvtdberr(dbi, "db->set_re_source", rc, _debug);
			if (rc) break;
		    }
		    break;
		case DB_QUEUE:
		    if (dbi->dbi_q_extentsize) {
			rc = db->set_q_extentsize(db, dbi->dbi_q_extentsize);
			rc = cvtdberr(dbi, "db->set_q_extentsize", rc, _debug);
			if (rc) break;
		    }
		    break;
		}
	    }
	    dbi->dbi_dbinfo = NULL;

	    if (rc == 0) {
		const char * dbfullpath;
		const char * dbpath;
		char * t;
		int nb;

		nb = strlen(dbhome);
		if (dbfile)	nb += 1 + strlen(dbfile);
		dbfullpath = t = alloca(nb + 1);

		t = stpcpy(t, dbhome);
		if (dbfile)
		    t = stpcpy( stpcpy( t, "/"), dbfile);
		dbpath = (!dbi->dbi_use_dbenv && !dbi->dbi_temporary)
			? dbfullpath : dbfile;

		rc = db->open(db, dbpath, dbsubfile,
		    dbi->dbi_type, oflags, dbi->dbi_perms);

		if (rc == 0 && dbi->dbi_type == DB_UNKNOWN)
		    dbi->dbi_type = db->get_type(db);
	    }

	    /* XXX return rc == errno without printing */
	    _printit = (rc > 0 ? 0 : _debug);
	    xx = cvtdberr(dbi, "db->open", rc, _printit);

	    if (rc == 0 && dbi->dbi_use_dbenv && (dbi->dbi_eflags & DB_INIT_CDB) && dbi->dbi_get_rmw_cursor) {
		DBC * dbcursor = NULL;
		xx = db->cursor(db, txnid, &dbcursor,
			((oflags & DB_RDONLY) ? 0 : DB_WRITECURSOR));
		xx = cvtdberr(dbi, "db->cursor", xx, _debug);
		dbi->dbi_rmw = dbcursor;
	    } else
		dbi->dbi_rmw = NULL;

	    /*
	     * Lock a file using fcntl(2). Traditionally this is Packages,
	     * the file used * to store metadata of installed header(s),
	     * as Packages is always opened, and should be opened first,
	     * for any rpmdb access.
	     *
	     * If no DBENV is used, then access is protected with a
	     * shared/exclusive locking scheme, as always.
	     *
	     * With a DBENV, the fcntl(2) lock is necessary only to keep
	     * the riff-raff from playing where they don't belong, as
	     * the DBENV should provide it's own locking scheme. So try to
	     * acquire a lock, but permit failures, as some other
	     * DBENV player may already have acquired the lock.
	     */
	    if (rc == 0 && dbi->dbi_lockdbfd &&
		(!dbi->dbi_use_dbenv || _lockdbfd++ == 0))
	    {
		int fdno = -1;

		if (!(db->fd(db, &fdno) == 0 && fdno >= 0)) {
		    rc = 1;
		} else {
		    struct flock l;
		    memset(&l, 0, sizeof(l));
		    l.l_whence = 0;
		    l.l_start = 0;
		    l.l_len = 0;
		    l.l_type = (dbi->dbi_mode & (O_RDWR|O_WRONLY))
				? F_WRLCK : F_RDLCK;
		    l.l_pid = 0;

		    rc = fcntl(fdno, F_SETLK, (void *) &l);
		    if (rc) {
			rpmError(
			    (!dbi->dbi_use_dbenv ? RPMERR_FLOCK : RPMWARN_FLOCK),
				_("cannot get %s lock on %s/%s\n"),
				((dbi->dbi_mode & (O_RDWR|O_WRONLY))
					? _("exclusive") : _("shared")),
				dbhome, (dbfile ? dbfile : ""));
			rc = (!dbi->dbi_use_dbenv ? 1 : 0);
		    } else if (dbfile) {
			rpmMessage(RPMMESS_DEBUG,
				_("locked  db index       %s/%s\n"),
				dbhome, dbfile);
		    }
		}
	    }
	}
#else	/* __USE_DB3 */
      {	DB_INFO * dbinfo = xcalloc(1, sizeof(*dbinfo));
	dbinfo->db_cachesize = dbi->dbi_cachesize;
	dbinfo->db_lorder = dbi->dbi_lorder;
	dbinfo->db_pagesize = dbi->dbi_pagesize;
	dbinfo->db_malloc = rpmdb->db_malloc;
	if (oflags & DB_CREATE) {
	    switch(dbi->dbi_type) {
	    default:
	    case DB_HASH:
		dbinfo->h_ffactor = dbi->dbi_h_ffactor;
		dbinfo->h_hash = dbi->dbi_h_hash_fcn;
		dbinfo->h_nelem = dbi->dbi_h_nelem;
		dbinfo->flags = dbi->dbi_h_flags;
		break;
	    }
	}
	dbi->dbi_dbinfo = dbinfo;
	rc = db_open(dbfile, dbi->dbi_type, oflags,
			dbi->dbi_perms, dbenv, dbinfo, &db);
	/* XXX return rc == errno without printing */
	_printit = (rc > 0 ? 0 : _debug);
	xx = cvtdberr(dbi, "db->open", rc, _printit);
      }
#endif	/* __USE_DB3 */
    }

    dbi->dbi_db = db;
    dbi->dbi_dbenv = dbenv;

#else	/* __USE_DB2 || __USE_DB3 */
    void * dbopeninfo = NULL;

    if (dbip)
	*dbip = NULL;
    if ((dbi = db3New(rpmdb, rpmtag)) == NULL)
	return 1;

    dbi->dbi_db = dbopen(dbfile, dbi->dbi_mode, dbi->dbi_perms,
		dbi->dbi_type, dbopeninfo);
    /* XXX return rc == errno without printing */
    if (dbi->dbi_db == NULL) rc = errno;
#endif	/* __USE_DB2 || __USE_DB3 */

    if (rc == 0 && dbi->dbi_db != NULL && dbip != NULL) {
	dbi->dbi_vec = &db3vec;
	*dbip = dbi;
    } else
	(void) db3close(dbi, 0);

    urlfn = _free(urlfn);

    /*@-nullstate@*/
    return rc;
    /*@=nullstate@*/
}

/** \ingroup db3
 */
struct _dbiVec db3vec = {
    DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH,
    db3open, db3close, db3sync, db3copen, db3cclose, db3cdel, db3cget, db3cput,
    db3byteswapped
};

#endif	/* DB_VERSION_MAJOR == 3 */
