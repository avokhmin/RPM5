/*@-mustmod@*/
/*@-paramuse@*/
/*@-globuse@*/
/*@-moduncon@*/
/*@-noeffectuncon@*/
/*@-compdef@*/
/*@-compmempass@*/
/*@-modfilesystem@*/
/*@-evalorderuncon@*/

/*
 * sqlite.c
 * sqlite interface for rpmdb
 *
 * Author: Mark Hatle <mhatle@mvista.com> or <fray@kernel.crashing.org>
 * Copyright (c) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * or GNU Library General Public License, at your option,
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and GNU Library Public License along with this program;
 * if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include "system.h"

#include <rpmio.h>
#include <rpmlog.h>
#include <rpmmacro.h>
#include <rpmurl.h>     /* XXX urlPath proto */

#include <rpmtag.h>
#define	_RPMDB_INTERNAL
#include <rpmdb.h>

#include <sqlite3.h>

#include "debug.h"

#if defined(__LCLINT__)
#define	UINT32_T	u_int32_t
#else
#define	UINT32_T	rpmuint32_t
#endif

/*@access rpmdb @*/
/*@access dbiIndex @*/

/* XXX retrofit the *BSD typedef for the deprived. */
#if defined(__QNXNTO__)
typedef rpmuint32_t       u_int32_t;
#endif

/*@unchecked@*/
static int _debug = 0;

/* Define the things normally in a header... */
struct _sql_db_s;	typedef struct _sql_db_s	SQL_DB;
struct _sql_dbcursor_s;	typedef struct _sql_dbcursor_s *SCP_t;

struct _sql_db_s {
    sqlite3 * db;		/* Database pointer */
    int transaction;		/* Do we have a transaction open? */
};

struct _sql_dbcursor_s {
/*@shared@*/
    DB *dbp;

/*@only@*/ /*@relnull@*/
    char * cmd;			/* SQL command string */
/*@only@*/ /*@relnull@*/
    sqlite3_stmt *pStmt;	/* SQL byte code */
    const char * pzErrmsg;	/* SQL error msg */

  /* Table -- result of query */
/*@only@*/ /*@relnull@*/
    char ** av;			/* item ptrs */
/*@only@*/ /*@relnull@*/
    size_t * avlen;		/* item sizes */
    int nalloc;
    int ac;			/* no. of items */
    int rx;			/* Which row are we on? 1, 2, 3 ... */
    int nr;			/* no. of rows */
    int nc;			/* no. of columns */

    int all;			/* sequential iteration cursor */
/*@relnull@*/
    DBT ** keys;		/* array of package keys */
    int nkeys;

    int count;

/*@null@*/
    void * lkey;		/* Last key returned */
/*@null@*/
    void * ldata;		/* Last data returned */

    int used;
};

/*@-redef@*/
union _dbswap {
    rpmuint32_t ui;
    unsigned char uc[4];
};
/*@=redef@*/

#define _DBSWAP(_a) \
  { unsigned char _b, *_c = (_a).uc; \
    _b = _c[3]; _c[3] = _c[0]; _c[0] = _b; \
    _b = _c[2]; _c[2] = _c[1]; _c[1] = _b; \
  }

/*@unchecked@*/
static unsigned int endian = 0x11223344;

/*@unchecked@*/ /*@only@*/ /*@null@*/
static const char * sqlCwd = NULL;
/*@unchecked@*/
static int sqlInRoot = 0;

static void enterChroot(dbiIndex dbi)
	/*@globals sqlCwd, sqlInRoot, internalState @*/
	/*@modifies sqlCwd, sqlInRoot, internalState @*/
{
    char * currDir = NULL;
    int xx;

    if ((dbi->dbi_root[0] == '/' && dbi->dbi_root[1] == '\0') || dbi->dbi_rpmdb->db_chrootDone || sqlInRoot)
       /* Nothing to do, was not already in chroot */
       return;

if (_debug)
fprintf(stderr, "sql:chroot(%s)\n", dbi->dbi_root);

    {
      int currDirLen = 0;

      do {
        currDirLen += 128;
        currDir = xrealloc(currDir, currDirLen);
        memset(currDir, 0, currDirLen);
      } while (getcwd(currDir, currDirLen) == NULL && errno == ERANGE);
    }

    sqlCwd = currDir;
/*@-globs@*/
    xx = Chdir("/");
/*@=globs@*/
/*@-modobserver@*/
    xx = Chroot(dbi->dbi_root);
/*@=modobserver@*/
assert(xx == 0);
    sqlInRoot=1;
}

static void leaveChroot(dbiIndex dbi)
	/*@globals sqlCwd, sqlInRoot, internalState @*/
	/*@modifies sqlCwd, sqlInRoot, internalState @*/
{
    int xx;

    if ((dbi->dbi_root[0] == '/' && dbi->dbi_root[1] == '\0') || dbi->dbi_rpmdb->db_chrootDone || !sqlInRoot)
       /* Nothing to do, not in chroot */
       return;

if (_debug)
fprintf(stderr, "sql:chroot(.)\n");

/*@-modobserver@*/
    xx = Chroot(".");
/*@=modobserver@*/
assert(xx == 0);
    if (sqlCwd != NULL) {
/*@-globs@*/
	xx = Chdir(sqlCwd);
/*@=globs@*/
	sqlCwd = _free(sqlCwd);
    }

    sqlInRoot = 0;
}

static void dbg_scp(void *ptr)
	/*@globals stderr, fileSystem @*/
	/*@modifies stderr, fileSystem @*/
{
    SCP_t scp = ptr;

if (_debug)
fprintf(stderr, "\tscp %p [%d:%d] av %p avlen %p nr [%d:%d] nc %d all %d\n", scp, scp->ac, scp->nalloc, scp->av, scp->avlen, scp->rx, scp->nr, scp->nc, scp->all);

}

static void dbg_keyval(const char * msg, dbiIndex dbi, /*@null@*/ DBC * dbcursor,
		DBT * key, DBT * data, unsigned int flags)
	/*@globals stderr, fileSystem @*/
	/*@modifies stderr, fileSystem @*/
{

if (!_debug) return;

    fprintf(stderr, "%s on %s (%p,%p,%p,0x%x)", msg, dbi->dbi_subfile, dbcursor, key, data, flags);

    /* XXX FIXME: ptr alignment is fubar here. */
    if (key != NULL && key->data != NULL) {
	fprintf(stderr, "  key 0x%x[%d]", *(unsigned int *)key->data, key->size);
	if (dbi->dbi_rpmtag == RPMTAG_NAME)
	    fprintf(stderr, " \"%s\"", (const char *)key->data);
    }
    if (data != NULL && data->data != NULL)
	fprintf(stderr, " data 0x%x[%d]", *(unsigned int *)data->data, data->size);

    fprintf(stderr, "\n");
    if (dbcursor != NULL)
	dbg_scp(dbcursor);
}

/*@only@*/
static SCP_t scpResetKeys(/*@only@*/ SCP_t scp)
	/*@modifies scp @*/
{
    int ix;

if (_debug)
fprintf(stderr, "*** scpResetKeys(%p)\n", scp);
dbg_scp(scp);

    for ( ix =0 ; ix < scp->nkeys ; ix++ ) {
      scp->keys[ix]->data = _free(scp->keys[ix]->data);
/*@-unqualifiedtrans@*/
      scp->keys[ix] = _free(scp->keys[ix]);
/*@=unqualifiedtrans@*/
    }
    scp->keys = _free(scp->keys);
    scp->nkeys = 0;

    return scp;
}

/*@only@*/
static SCP_t scpResetAv(/*@only@*/ SCP_t scp)
	/*@modifies scp @*/
{
    int xx;

if (_debug)
fprintf(stderr, "*** scpResetAv(%p)\n", scp);
dbg_scp(scp);

    if (scp->av != NULL) {
	if (scp->nalloc <= 0) {
	    /* Clean up SCP_t used by sqlite3_get_table(). */
	    sqlite3_free_table(scp->av);
	    scp->av = NULL;
	    scp->nalloc = 0;
	} else {
	    /* Clean up SCP_t used by sql_step(). */
/*@-unqualifiedtrans@*/
	    for (xx = 0; xx < scp->ac; xx++)
		scp->av[xx] = _free(scp->av[xx]);
/*@=unqualifiedtrans@*/
	    if (scp->av != NULL)
		memset(scp->av, 0, scp->nalloc * sizeof(*scp->av));
	    if (scp->avlen != NULL)
		memset(scp->avlen, 0, scp->nalloc * sizeof(*scp->avlen));
	    scp->av = _free(scp->av);
	    scp->avlen = _free(scp->avlen);
	    scp->nalloc = 0;
	}
    } else
	scp->nalloc = 0;
    scp->ac = 0;
    scp->nr = 0;
    scp->nc = 0;

    return scp;
}

/*@only@*/
static SCP_t scpReset(/*@only@*/ SCP_t scp)
	/*@modifies scp @*/
{
    int xx;

if (_debug)
fprintf(stderr, "*** scpReset(%p)\n", scp);
dbg_scp(scp);

    if (scp->cmd) {
	sqlite3_free(scp->cmd);
	scp->cmd = NULL;
    }
    if (scp->pStmt) {
	xx = sqlite3_reset(scp->pStmt);
	if (xx) rpmlog(RPMLOG_WARNING, "reset %d\n", xx);
	xx = sqlite3_finalize(scp->pStmt);
	if (xx) rpmlog(RPMLOG_WARNING, "finalize %d\n", xx);
	scp->pStmt = NULL;
    }

    scp = scpResetAv(scp);

    scp->rx = 0;
    return scp;
}

/*@null@*/
static SCP_t scpFree(/*@only@*/ SCP_t scp)
	/*@modifies scp @*/
{
    scp = scpReset(scp);
    scp = scpResetKeys(scp);
    scp->av = _free(scp->av);
    scp->avlen = _free(scp->avlen);

if (_debug)
fprintf(stderr, "*** scpFree(%p)\n", scp);
    scp = _free(scp);
    return NULL;
}

static SCP_t scpNew(DB * dbp)
	/*@*/
{
    SCP_t scp = xcalloc(1, sizeof(*scp));
/*@-temptrans@*/
    scp->dbp = dbp;
/*@=temptrans@*/

    scp->used = 0;

    scp->lkey = NULL;
    scp->ldata = NULL;

if (_debug)
fprintf(stderr, "*** scpNew(%p)\n", scp);
    return scp;
}

static int sql_step(dbiIndex dbi, SCP_t scp)
	/*@modifies dbi, scp @*/
{
    int swapped = dbiByteSwapped(dbi);
    const char * cname;
    const char * vtype;
    size_t nb;
    int loop;
    int need;
    int rc;
    int i;

    scp->nc = sqlite3_column_count(scp->pStmt);

    if (scp->nr == 0 && scp->av != NULL)
	need = 2 * scp->nc;
    else
	need = scp->nc;

    /* XXX scp->nc = need = scp->nalloc = 0 case forces + 1 here */
    if (!scp->ac && !need && !scp->nalloc)
	need++;

    if (scp->ac + need >= scp->nalloc) {
	/* XXX +4 is bogus, was +1 */
	scp->nalloc = 2 * scp->nalloc + need + 4;
	scp->av = xrealloc(scp->av, scp->nalloc * sizeof(*scp->av));
	scp->avlen = xrealloc(scp->avlen, scp->nalloc * sizeof(*scp->avlen));
    }

    if (scp->av != NULL && scp->nr == 0) {
	for (i = 0; i < scp->nc; i++) {
	    scp->av[scp->ac] = xstrdup(sqlite3_column_name(scp->pStmt, i));
	    if (scp->avlen) scp->avlen[scp->ac] = strlen(scp->av[scp->ac]) + 1;
	    scp->ac++;
assert(scp->ac <= scp->nalloc);
	}
    }

/*@-infloopsuncon@*/
    loop = 1;
    while (loop) {
	rc = sqlite3_step(scp->pStmt);
	switch (rc) {
	case SQLITE_DONE:
if (_debug)
fprintf(stderr, "sqlite3_step: DONE scp %p [%d:%d] av %p avlen %p\n", scp, scp->ac, scp->nalloc, scp->av, scp->avlen);
	    loop = 0;
	    /*@switchbreak@*/ break;
	case SQLITE_ROW:
	    if (scp->av != NULL)
	    for (i = 0; i < scp->nc; i++) {
		/* Expand the row array for new elements */
    		if (scp->ac + need >= scp->nalloc) {
		    /* XXX +4 is bogus, was +1 */
		    scp->nalloc = 2 * scp->nalloc + need + 4;
		    scp->av = xrealloc(scp->av, scp->nalloc * sizeof(*scp->av));
		    scp->avlen = xrealloc(scp->avlen, scp->nalloc * sizeof(*scp->avlen));
	        }
assert(scp->av != NULL);
assert(scp->avlen != NULL);

		cname = sqlite3_column_name(scp->pStmt, i);
		vtype = sqlite3_column_decltype(scp->pStmt, i);
		nb = 0;

		if (!strcmp(vtype, "blob")) {
		    const void * v = sqlite3_column_blob(scp->pStmt, i);
		    nb = sqlite3_column_bytes(scp->pStmt, i);
if (_debug)
fprintf(stderr, "\t%d %s %s %p[%d]\n", i, cname, vtype, v, (int)nb);
		    if (nb > 0) {
			void * t = xmalloc(nb);
			scp->av[scp->ac] = memcpy(t, v, nb);
			scp->avlen[scp->ac] = nb;
			scp->ac++;
		    }
		} else
		if (!strcmp(vtype, "double")) {
		    double v = sqlite3_column_double(scp->pStmt, i);
		    nb = sizeof(v);
if (_debug)
fprintf(stderr, "\t%d %s %s %g\n", i, cname, vtype, v);
		    if (nb > 0) {
			scp->av[scp->ac] = memcpy(xmalloc(nb), &v, nb);
			scp->avlen[scp->ac] = nb;
assert(swapped == 0); /* Byte swap?! */
			scp->ac++;
		    }
		} else
		if (!strcmp(vtype, "int")) {
		    rpmint32_t v = sqlite3_column_int(scp->pStmt, i);
		    nb = sizeof(v);
if (_debug)
fprintf(stderr, "\t%d %s %s %d\n", i, cname, vtype, (int) v);
		    if (nb > 0) {
			scp->av[scp->ac] = memcpy(xmalloc(nb), &v, nb);
			scp->avlen[scp->ac] = nb;
if (swapped == 1) {
  union _dbswap dbswap;
  memcpy(&dbswap.ui, scp->av[scp->ac], sizeof(dbswap.ui));
  _DBSWAP(dbswap);
  memcpy(scp->av[scp->ac], &dbswap.ui, sizeof(dbswap.ui));
}
			scp->ac++;
		    }
		} else
		if (!strcmp(vtype, "int64")) {
		    int64_t v = sqlite3_column_int64(scp->pStmt, i);
		    nb = sizeof(v);
if (_debug)
fprintf(stderr, "\t%d %s %s %ld\n", i, cname, vtype, (long)v);
		    if (nb > 0) {
			scp->av[scp->ac] = memcpy(xmalloc(nb), &v, nb);
			scp->avlen[scp->ac] = nb;
assert(swapped == 0); /* Byte swap?! */
			scp->ac++;
		    }
		} else
		if (!strcmp(vtype, "text")) {
		    const char * v = (const char *)sqlite3_column_text(scp->pStmt, i);
		    nb = strlen(v) + 1;
if (_debug)
fprintf(stderr, "\t%d %s %s \"%s\"\n", i, cname, vtype, v);
		    if (nb > 0) {
			scp->av[scp->ac] = memcpy(xmalloc(nb), v, nb);
			scp->avlen[scp->ac] = nb;
			scp->ac++;
		    }
		}
assert(scp->ac <= scp->nalloc);
	    }
	    scp->nr++;
	    /*@switchbreak@*/ break;
	case SQLITE_BUSY:
	    fprintf(stderr, "sqlite3_step: BUSY %d\n", rc);
	    /*@switchbreak@*/ break;
	case SQLITE_ERROR:
	    fprintf(stderr, "sqlite3_step: ERROR %d -- %s\n", rc, scp->cmd);
	    fprintf(stderr, "              %s (%d)\n",
			sqlite3_errmsg(((SQL_DB*)dbi->dbi_db)->db), sqlite3_errcode(((SQL_DB*)dbi->dbi_db)->db));
/*@-nullpass@*/
	    fprintf(stderr, "              cwd '%s'\n", getcwd(NULL,0));
/*@=nullpass@*/
	    loop = 0;
	    /*@switchbreak@*/ break;
	case SQLITE_MISUSE:
	    fprintf(stderr, "sqlite3_step: MISUSE %d\n", rc);
	    loop = 0;
	    /*@switchbreak@*/ break;
	default:
	    fprintf(stderr, "sqlite3_step: rc %d\n", rc);
	    loop = 0;
	    /*@switchbreak@*/ break;
	}
    }
/*@=infloopsuncon@*/

    if (rc == SQLITE_DONE)
	rc = SQLITE_OK;

    return rc;
}

static int sql_bind_key(dbiIndex dbi, SCP_t scp, int pos, DBT * key)
	/*@modifies dbi, scp @*/
{
    int swapped = dbiByteSwapped(dbi);
    int rc = 0;
    union _dbswap dbswap;

assert(key->data != NULL);
    switch (dbi->dbi_rpmtag) {
    case RPMDBI_PACKAGES:
	{   unsigned int hnum;
/*@i@*/ assert(key->size == sizeof(rpmuint32_t));
	    memcpy(&hnum, key->data, sizeof(hnum));

if (swapped == 1) {
  memcpy(&dbswap.ui, &hnum, sizeof(dbswap.ui));
  _DBSWAP(dbswap);
  memcpy(&hnum, &dbswap.ui, sizeof(dbswap.ui));
}
	    rc = sqlite3_bind_int(scp->pStmt, pos, hnum);
	} break;
    default:
	switch (tagType(dbi->dbi_rpmtag) & RPM_MASK_TYPE) {
	case RPM_BIN_TYPE:
/*@-castfcnptr -nullpass@*/ /* FIX: annotate sqlite. */
	    rc = sqlite3_bind_blob(scp->pStmt, pos, key->data, key->size, SQLITE_STATIC);
/*@=castfcnptr =nullpass@*/
	    /*@innerbreak@*/ break;
	case RPM_UINT8_TYPE:
	{   unsigned char i;
/*@i@*/ assert(key->size == sizeof(unsigned char));
assert(swapped == 0); /* Byte swap?! */
	    memcpy(&i, key->data, sizeof(i));
	    rc = sqlite3_bind_int(scp->pStmt, pos, (int) i);
	} /*@innerbreak@*/ break;
	case RPM_UINT16_TYPE:
	{	unsigned short i;
/*@i@*/ assert(key->size == sizeof(rpmuint16_t));
assert(swapped == 0); /* Byte swap?! */
	    memcpy(&i, key->data, sizeof(i));
	    rc = sqlite3_bind_int(scp->pStmt, pos, (int) i);
	} /*@innerbreak@*/ break;
	case RPM_UINT64_TYPE:
assert(0);	/* borken */
	/*@innerbreak@*/ break;
        case RPM_UINT32_TYPE:
	default:
	{   unsigned int i;
/*@i@*/ assert(key->size == sizeof(rpmuint32_t));
	    memcpy(&i, key->data, sizeof(i));

if (swapped == 1)
{
  memcpy(&dbswap.ui, &i, sizeof(dbswap.ui));
  _DBSWAP(dbswap);
  memcpy(&i, &dbswap.ui, sizeof(dbswap.ui));
}
	    rc = sqlite3_bind_int(scp->pStmt, pos, i);
	}   /*@innerbreak@*/ break;
        case RPM_STRING_TYPE:
        case RPM_STRING_ARRAY_TYPE:
        case RPM_I18NSTRING_TYPE:
/*@-castfcnptr -nullpass@*/ /* FIX: annotate sqlite. */
	    rc = sqlite3_bind_text(scp->pStmt, pos, key->data, key->size, SQLITE_STATIC);
/*@=castfcnptr =nullpass@*/
	    /*@innerbreak@*/ break;
        }
    }

    return rc;
}

static int sql_bind_data(/*@unused@*/ dbiIndex dbi, SCP_t scp,
		int pos, DBT * data)
	/*@modifies scp @*/
{
    int rc;

assert(data->data != NULL);
/*@-castfcnptr -nullpass@*/ /* FIX: annotate sqlite */
    rc = sqlite3_bind_blob(scp->pStmt, pos, data->data, data->size, SQLITE_STATIC);
/*@=castfcnptr =nullpass@*/

    return rc;
}

/*===================================================================*/
/*
 * Transaction support
 */

static int sql_startTransaction(dbiIndex dbi)
	/*@*/
{
    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    int rc = 0;

    /* XXX:  Transaction Support */
    if (!sqldb->transaction) {
      char * pzErrmsg;
      rc = sqlite3_exec(sqldb->db, "BEGIN TRANSACTION;", NULL, NULL, &pzErrmsg);

if (_debug)
fprintf(stderr, "Begin %s SQL transaction %s (%d)\n",
		dbi->dbi_subfile, pzErrmsg, rc);

      if (rc == 0)
	sqldb->transaction = 1;
    }

    return rc;
}

static int sql_endTransaction(dbiIndex dbi)
	/*@*/
{
    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    int rc=0;

    /* XXX:  Transaction Support */
    if (sqldb->transaction) {
      char * pzErrmsg;
      rc = sqlite3_exec(sqldb->db, "END TRANSACTION;", NULL, NULL, &pzErrmsg);

if (_debug)
fprintf(stderr, "End %s SQL transaction %s (%d)\n",
		dbi->dbi_subfile, pzErrmsg, rc);

      if (rc == 0)
	sqldb->transaction = 0;
    }

    return rc;
}

static int sql_commitTransaction(dbiIndex dbi, int flag)
	/*@*/
{
    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    int rc = 0;

    /* XXX:  Transactions */
    if ( sqldb->transaction ) {
      char * pzErrmsg;
      rc = sqlite3_exec(sqldb->db, "COMMIT;", NULL, NULL, &pzErrmsg);

if (_debug)
fprintf(stderr, "Commit %s SQL transaction(s) %s (%d)\n",
		dbi->dbi_subfile, pzErrmsg, rc);

      sqldb->transaction=0;

      /* Start a new transaction if we were in the middle of one */
      if ( flag == 0 )
	rc = sql_startTransaction(dbi);
    }

    return rc;
}

static int sql_busy_handler(void * dbi_void, int time)
	/*@*/
{
/*@-castexpose@*/
    dbiIndex dbi = (dbiIndex) dbi_void;
/*@=castexpose@*/

    rpmlog(RPMLOG_WARNING, _("Unable to get lock on db %s, retrying... (%d)\n"),
		dbi->dbi_file, time);

    (void) sleep(1);

    return 1;
}

/*===================================================================*/

/**
 * Verify the DB is setup.. if not initialize it
 *
 * Create the table.. create the db_info
 */
static int sql_initDB(dbiIndex dbi)
	/*@globals rpmGlobalMacroContext, h_errno, internalState @*/
	/*@modifies internalState @*/
{
    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    SCP_t scp = scpNew(dbi->dbi_db);
    char cmd[BUFSIZ];
    int rc = 0;

    if (dbi->dbi_tmpdir) {
        const char *root;
        const char *tmpdir;
        int xx;
        root = (dbi->dbi_root ? dbi->dbi_root : dbi->dbi_rpmdb->db_root);
        if ((root[0] == '/' && root[1] == '\0') || dbi->dbi_rpmdb->db_chrootDone)
            root = NULL;
        /*@-mods@*/
        tmpdir = rpmGenPath(root, dbi->dbi_tmpdir, NULL);
        /*@=mods@*/
        sprintf(cmd, "PRAGMA temp_store_directory = '%s';", tmpdir);
        xx = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
        tmpdir = _free(tmpdir);
    }
    if (dbi->dbi_eflags & DB_EXCL) {
        int xx;
        sprintf(cmd, "PRAGMA locking_mode = EXCLUSIVE;");
        xx = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
    }
#ifdef	DYING
    if (dbi->dbi_pagesize > 0) {
        int xx;
        sprintf(cmd, "PRAGMA cache_size = %d;", dbi->dbi_cachesize);
        xx = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
    }
    if (dbi->dbi_cachesize > 0) {
        int xx;
        sprintf(cmd, "PRAGMA page_size = %d;", dbi->dbi_pagesize);
        xx = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
    }
#endif

    /* Check if the table exists... */
    sprintf(cmd,
	"SELECT name FROM 'sqlite_master' WHERE type='table' and name='%s';",
		dbi->dbi_subfile);
/*@-nullstate@*/
    rc = sqlite3_get_table(sqldb->db, cmd,
	&scp->av, &scp->nr, &scp->nc, (char **)&scp->pzErrmsg);
/*@=nullstate@*/
    if (rc)
	goto exit;

    if (scp->nr < 1) {
	const char * valtype = "blob";
	const char * keytype;

	switch (dbi->dbi_rpmtag) {
	case RPMDBI_PACKAGES:
	    keytype = "int UNIQUE PRIMARY KEY";
	    valtype = "blob";
	    break;
	default:
	    switch (tagType(dbi->dbi_rpmtag) & RPM_MASK_TYPE) {
	    case RPM_BIN_TYPE:
	    default:
		keytype = "blob UNIQUE";
		/*@innerbreak@*/ break;
	    case RPM_UINT8_TYPE:
	    case RPM_UINT16_TYPE:
	    case RPM_UINT32_TYPE:
	    case RPM_UINT64_TYPE:
		keytype = "int UNIQUE";
		/*@innerbreak@*/ break;
	    case RPM_STRING_TYPE:
	    case RPM_STRING_ARRAY_TYPE:
	    case RPM_I18NSTRING_TYPE:
		keytype = "text UNIQUE";
		/*@innerbreak@*/ break;
	    }
	}
if (_debug)
fprintf(stderr, "\t%s(%d) type(%d) keytype %s\n", tagName(dbi->dbi_rpmtag), dbi->dbi_rpmtag, (tagType(dbi->dbi_rpmtag) & RPM_MASK_TYPE), keytype);
	sprintf(cmd, "CREATE %sTABLE '%s' (key %s, value %s)",
			dbi->dbi_temporary ? "TEMPORARY " : "",
			dbi->dbi_subfile, keytype, valtype);
	rc = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
	if (rc)
	    goto exit;

	sprintf(cmd, "CREATE %sTABLE 'db_info' (endian TEXT)",
			dbi->dbi_temporary ? "TEMPORARY " : "");
	rc = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
	if (rc)
	    goto exit;

	sprintf(cmd, "INSERT INTO 'db_info' values('%u')", (unsigned)((union _dbswap *)&endian)->uc[0]);
	rc = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
	if (rc)
	    goto exit;
    }

    if (dbi->dbi_no_fsync) {
	int xx;
        sprintf(cmd, "PRAGMA synchronous = OFF;");
        xx = sqlite3_exec(sqldb->db, cmd, NULL, NULL, (char **)&scp->pzErrmsg);
    }

exit:
    if (rc)
	rpmlog(RPMLOG_WARNING, "Unable to initDB %s (%d)\n",
		scp->pzErrmsg, rc);

    scp = scpFree(scp);

    return rc;
}

/**
 * Close database cursor.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_cclose (dbiIndex dbi, /*@only@*/ DBC * dbcursor,
		unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, *dbcursor, fileSystem, internalState @*/
{
    SCP_t scp = (SCP_t)dbcursor;
    int rc;

if (_debug)
fprintf(stderr, "==> sql_cclose(%p)\n", scp);

    if (scp->lkey)
	scp->lkey = _free(scp->lkey);

    if (scp->ldata)
	scp->ldata = _free(scp->ldata);

enterChroot(dbi);

    if (flags == DB_WRITECURSOR)
	rc = sql_commitTransaction(dbi, 1);
    else
	rc = sql_endTransaction(dbi);

/*@-kepttrans -nullstate@*/
    scp = scpFree(scp);
/*@=kepttrans =nullstate@*/

leaveChroot(dbi);

    return rc;
}

/**
 * Close index database, and destroy database handle.
 * @param dbi           index database handle
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_close(/*@only@*/ dbiIndex dbi, unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, fileSystem, internalState @*/
{
    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    int rc = 0;

    if (sqldb) {
enterChroot(dbi);

	/* Commit, don't open a new one */
	rc = sql_commitTransaction(dbi, 1);

	(void) sqlite3_close(sqldb->db);

	rpmlog(RPMLOG_DEBUG, D_("closed   sql db         %s\n"),
		dbi->dbi_subfile);

#if defined(MAYBE) /* XXX should SQLite and BDB have different semantics? */
	if (dbi->dbi_temporary && !(dbi->dbi_eflags & DB_PRIVATE)) {
	    const char * dbhome = NULL;
	    urltype ut = urlPath(dbi->dbi_home, &dbhome);
	    const char * dbfname = rpmGenPath(dbhome, dbi->dbi_file, NULL);
	    int xx = (dbfname ? Unlink(dbfname) : 0);
	    ut = ut; xx = xx;	/* XXX tell gcc to be quiet. */
	    dbfname = _free(dbfname);
	}
#endif

	dbi->dbi_stats = _free(dbi->dbi_stats);
	dbi->dbi_file = _free(dbi->dbi_file);
	dbi->dbi_db = _free(dbi->dbi_db);

leaveChroot(dbi);
    }

    dbi = _free(dbi);

    return rc;
}

/**
 * Return handle for an index database.
 * @param rpmdb         rpm database
 * @param rpmtag        rpm tag
 * @retval *dbip	index database handle
 * @return              0 on success
 */
static int sql_open(rpmdb rpmdb, rpmTag rpmtag, /*@out@*/ dbiIndex * dbip)
	/*@globals rpmGlobalMacroContext, h_errno, fileSystem, internalState @*/
	/*@modifies *dbip, rpmGlobalMacroContext, fileSystem, internalState @*/
{
/*@-nestedextern -shadow @*/
    extern struct _dbiVec sqlitevec;
/*@=nestedextern -shadow @*/

    const char * urlfn = NULL;
    const char * root;
    const char * home;
    const char * dbhome;
    const char * dbfile;
    const char * dbfname;
    const char * sql_errcode;
    mode_t umask_safed = 0002;
    dbiIndex dbi;
    SQL_DB * sqldb;
    size_t len;
    int rc = 0;
    int xx;

    if (dbip)
	*dbip = NULL;

    /*
     * Parse db configuration parameters.
     */
    /*@-mods@*/
    if ((dbi = db3New(rpmdb, rpmtag)) == NULL)
	/*@-nullstate@*/
	return 1;
	/*@=nullstate@*/
    /*@=mods@*/

   /*
     * Get the prefix/root component and directory path
     */
    root = rpmdb->db_root;
    home = rpmdb->db_home;

    dbi->dbi_root = root;
    dbi->dbi_home = home;

    dbfile = tagName(dbi->dbi_rpmtag);

enterChroot(dbi);

    /*
     * Make a copy of the tagName result..
     * use this for the filename and table name
     */
    {	
      char * t;
      len = strlen(dbfile);
      t = xcalloc(len + 1, sizeof(*t));
      (void) stpcpy( t, dbfile );
      dbi->dbi_file = t;
/*@-kepttrans@*/ /* WRONG */
      dbi->dbi_subfile = t;
/*@=kepttrans@*/
    }

    dbi->dbi_mode = O_RDWR;

    /*
     * Either the root or directory components may be a URL. Concatenate,
     * convert the URL to a path, and add the name of the file.
     */
    /*@-mods@*/
    urlfn = rpmGenPath(NULL, home, NULL);
    /*@=mods@*/
    (void) urlPath(urlfn, &dbhome);

    /*
     * Create the /var/lib/rpm directory if it doesn't exist (root only).
     */
    (void) rpmioMkpath(dbhome, 0755, getuid(), getgid());

    if (dbi->dbi_eflags & DB_PRIVATE)
        dbfname = xstrdup(":memory:");
    else
        dbfname = rpmGenPath(dbhome, dbi->dbi_file, NULL);

    rpmlog(RPMLOG_DEBUG, D_("opening  sql db         %s (%s) mode=0x%x\n"),
		dbfname, dbi->dbi_subfile, dbi->dbi_mode);

    /* Open the Database */
    sqldb = xcalloc(1, sizeof(*sqldb));

    sql_errcode = NULL;
/*@+longunsignedintegral@*/
    if (dbi->dbi_perms)
        /* mask-out permission bits which are not requested (security) */
        umask_safed = umask(~((mode_t)(dbi->dbi_perms)));
/*@=longunsignedintegral@*/
    xx = sqlite3_open(dbfname, &sqldb->db);
    if (dbi->dbi_perms) {
        if ((0644 /* = SQLite hard-coded default */ & dbi->dbi_perms) != dbi->dbi_perms) {
            /* add requested permission bits which are still missing (semantic) */
            (void) Chmod(dbfname, dbi->dbi_perms);
        }
/*@+longunsignedintegral@*/
        (void) umask(umask_safed);
/*@=longunsignedintegral@*/
    }
    if (xx != SQLITE_OK)
	sql_errcode = sqlite3_errmsg(sqldb->db);

    if (sqldb->db)
	(void) sqlite3_busy_handler(sqldb->db, &sql_busy_handler, dbi);

    sqldb->transaction = 0;	/* Initialize no current transactions */

    dbi->dbi_db = (DB *)sqldb;

    if (sql_errcode != NULL) {
      rpmlog(RPMLOG_DEBUG, D_("Unable to open database: %s\n"), sql_errcode);
      rc = EINVAL;
    }

    /* initialize table */
    if (rc == 0)
	rc = sql_initDB(dbi);

    if (rc == 0 && dbi->dbi_db != NULL && dbip != NULL) {
	dbi->dbi_vec = &sqlitevec;
	*dbip = dbi;
    } else {
	(void) sql_close(dbi, 0);
    }

    urlfn = _free(urlfn);
    dbfname = _free(dbfname);

/*@-usereleased@*/
leaveChroot(dbi);
/*@=usereleased@*/

    return rc;
}

/**
 * Flush pending operations to disk.
 * @param dbi           index database handle
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_sync (dbiIndex dbi, /*@unused@*/ unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    int rc = 0;

enterChroot(dbi);
    rc = sql_commitTransaction(dbi, 0);
leaveChroot(dbi);

    return rc;
}

/** \ingroup dbi
 * Return whether key exists in a database.
 * @param dbi		index database handle
 * @param key		retrieve key value/length/flags
 * @param flags		usually 0
 * @return		0 if key exists, DB_NOTFOUND if not, else error
 */
static int sql_exists(dbiIndex dbi, DBT * key, unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
if (_debug)
fprintf(stderr, "*** sql_exists:\n");
    return EINVAL;
}

/** \ingroup dbi
 * Return next sequence number.
 * @param dbi		index database handle (with attached sequence)
 * @retval *seqnop	IN: delta (0 does seqno++) OUT: returned 64bit seqno
 * @param flags		usually 0
 * @return		0 on success
 */
static int sql_seqno(dbiIndex dbi, int64_t * seqnop, unsigned int flags)
{
if (_debug)
fprintf(stderr, "*** sql_seqno:\n");
    return EINVAL;
}

/**
 * Open database cursor.
 * @param dbi		index database handle
 * @param txnid		database transaction handle
 * @retval dbcp		address of new database cursor
 * @param flags		DB_WRITECURSOR or 0
 * @return		0 on success
 */
static int sql_copen (dbiIndex dbi,
		/*@unused@*/ /*@null@*/ DB_TXN * txnid,
		/*@out@*/ DBC ** dbcp, unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, *txnid, *dbcp, fileSystem, internalState @*/
{
    SCP_t scp = scpNew(dbi->dbi_db);
    DBC * dbcursor = (DBC *)scp;
    int rc = 0;

if (_debug)
fprintf(stderr, "==> sql_copen(%s) tag %d type %d scp %p\n", tagName(dbi->dbi_rpmtag), dbi->dbi_rpmtag, (tagType(dbi->dbi_rpmtag) & RPM_MASK_TYPE), scp);

enterChroot(dbi);

    /* If we're going to write, start a transaction (lock the DB) */
    if (flags == DB_WRITECURSOR)
	rc = sql_startTransaction(dbi);

    if (dbcp)
	/*@-onlytrans@*/ *dbcp = dbcursor; /*@=onlytrans@*/
    else
	/*@-kepttrans -nullstate @*/ (void) sql_cclose(dbi, dbcursor, 0); /*@=kepttrans =nullstate @*/

leaveChroot(dbi);

    return rc;
}

/**
 * Delete (key,data) pair(s) using db->del or dbcursor->c_del.
 * @param dbi           index database handle
 * @param dbcursor      database cursor (NULL will use db->del)
 * @param key           delete key value/length/flags
 * @param data          delete data value/length/flags
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_cdel (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key,
		DBT * data, unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, *dbcursor, fileSystem, internalState @*/
{
/*@i@*/    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    SCP_t scp = scpNew(dbi->dbi_db);
    int rc = 0;

dbg_keyval("sql_cdel", dbi, dbcursor, key, data, flags);
enterChroot(dbi);

    scp->cmd = sqlite3_mprintf("DELETE FROM '%q' WHERE key=? AND value=?;",
	dbi->dbi_subfile);

    rc = sqlite3_prepare(sqldb->db, scp->cmd, (int)strlen(scp->cmd), &scp->pStmt, &scp->pzErrmsg);
    if (rc) rpmlog(RPMLOG_WARNING, "cdel(%s) prepare %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);
    rc = sql_bind_key(dbi, scp, 1, key);
    if (rc) rpmlog(RPMLOG_WARNING, "cdel(%s) bind key %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);
    rc = sql_bind_data(dbi, scp, 2, data);
    if (rc) rpmlog(RPMLOG_WARNING, "cdel(%s) bind data %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);

    rc = sql_step(dbi, scp);
    if (rc) rpmlog(RPMLOG_WARNING, "cdel(%s) sql_step rc %d\n", dbi->dbi_subfile, rc);

    scp = scpFree(scp);

leaveChroot(dbi);

    return rc;
}

/**
 * Retrieve (key,data) pair using db->get or dbcursor->c_get.
 * @param dbi           index database handle
 * @param dbcursor      database cursor (NULL will use db->get)
 * @param key           retrieve key value/length/flags
 * @param data          retrieve data value/length/flags
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_cget (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key,
		DBT * data, unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, dbcursor, *key, *data, fileSystem, internalState @*/
{
/*@i@*/    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    SCP_t scp = (SCP_t)dbcursor;
    int rc = 0;
    int ix;

assert(dbcursor != NULL);
dbg_keyval("sql_cget", dbi, dbcursor, key, data, flags);

enterChroot(dbi);

    /*
     * First determine if this is a new scan or existing scan
     */

if (_debug)
fprintf(stderr, "\tcget(%s) scp %p rc %d flags %d av %p\n",
		dbi->dbi_subfile, scp, rc, flags, scp->av);
    if ( flags == DB_SET || scp->used == 0 ) {
        scp->used = 1; /* Signal this scp as now in use... */
/*@i@*/	scp = scpReset(scp);	/* Free av and avlen, reset counters.*/

/* XXX: Should we also reset the key table here?  Can you re-use a cursor? */

        /*
         * If we're scanning everything, load the iterator key table
         */
        if ( key->size == 0) {
	    scp->all = 1;

/*
 * The only condition not dealt with is if there are multiple identical keys.  This can lead
 * to later iteration confusion.  (It may return the same value for the multiple keys.)
 */

#ifdef	DYING
/* Only RPMDBI_PACKAGES is supposed to be iterating, and this is guarenteed to be unique */
assert(dbi->dbi_rpmtag == RPMDBI_PACKAGES);
#endif

	    switch (dbi->dbi_rpmtag) {
	    case RPMDBI_PACKAGES:
	        scp->cmd = sqlite3_mprintf("SELECT key FROM '%q' ORDER BY key;",
		    dbi->dbi_subfile);
	        break;
	    default:
	        scp->cmd = sqlite3_mprintf("SELECT key FROM '%q';",
		    dbi->dbi_subfile);
	        break;
	    }
	    rc = sqlite3_prepare(sqldb->db, scp->cmd, (int)strlen(scp->cmd), &scp->pStmt, &scp->pzErrmsg);
	    if (rc) rpmlog(RPMLOG_WARNING, "cget(%s) sequential prepare %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);

	    rc = sql_step(dbi, scp);
	    if (rc) rpmlog(RPMLOG_WARNING, "cget(%s) sequential sql_step rc %d\n", dbi->dbi_subfile, rc);

	    scp = scpResetKeys(scp);
	    scp->nkeys = scp->nr;
	    scp->keys = xcalloc(scp->nkeys, sizeof(*scp->keys));
	    for (ix = 0; ix < scp->nkeys; ix++) {
		scp->keys[ix] = xmalloc(sizeof(*scp->keys[0]));
		scp->keys[ix]->size = (UINT32_T) scp->avlen[ix+1];
		scp->keys[ix]->data = xmalloc(scp->keys[ix]->size);
		memcpy(scp->keys[ix]->data, scp->av[ix+1], scp->avlen[ix+1]);
	    }
        } else {
	    /*
	     * We're only scanning ONE element
	     */
	    scp = scpResetKeys(scp);
	    scp->nkeys = 1;
	    scp->keys = xcalloc(scp->nkeys, sizeof(*scp->keys));
	    scp->keys[0] = xmalloc(sizeof(*scp->keys[0]));
	    scp->keys[0]->size = key->size;
	    scp->keys[0]->data = xmalloc(scp->keys[0]->size);
	    memcpy(scp->keys[0]->data, key->data, key->size);
	}

/*@i@*/	scp = scpReset(scp);	/* reset */

        /* Prepare SQL statement to retrieve the value for the current key */
        scp->cmd = sqlite3_mprintf("SELECT value FROM '%q' WHERE key=?;", dbi->dbi_subfile);
        rc = sqlite3_prepare(sqldb->db, scp->cmd, (int)strlen(scp->cmd), &scp->pStmt, &scp->pzErrmsg);

        if (rc) rpmlog(RPMLOG_WARNING, "cget(%s) prepare %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);
    }

/*@i@*/ scp = scpResetAv(scp);	/* Free av and avlen, reset counters.*/

    /* Now continue with a normal retrive based on key */
    if ((scp->rx + 1) > scp->nkeys )
	rc = DB_NOTFOUND; /* At the end of the list */

    if (rc != 0)
	goto exit;

    /* Bind key to prepared statement */
    rc = sql_bind_key(dbi, scp, 1, scp->keys[scp->rx]);
    if (rc) rpmlog(RPMLOG_WARNING, "cget(%s)  key bind %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);

    rc = sql_step(dbi, scp);
    if (rc) rpmlog(RPMLOG_WARNING, "cget(%s) sql_step rc %d\n", dbi->dbi_subfile, rc);

    rc = sqlite3_reset(scp->pStmt);
    if (rc) rpmlog(RPMLOG_WARNING, "reset %d\n", rc);

/* 1 key should return 0 or 1 row/value */
assert(scp->nr < 2);

    if (scp->nr == 0 && scp->all == 0)
        rc = DB_NOTFOUND; /* No data for that key found! */

    if (rc != 0)
	goto exit;

    /* If we're looking at the whole db, return the key */
    if (scp->all) {

/* To get this far there has to be _1_ key returned! (protect against dup keys) */
assert(scp->nr == 1);

	if ( scp->lkey ) {
	    scp->lkey = _free(scp->lkey);
	}

	key->size = scp->keys[scp->rx]->size;
	key->data = xmalloc(key->size);
	if (! (key->flags & DB_DBT_MALLOC))
	    scp->lkey = key->data;

	(void) memcpy(key->data, scp->keys[scp->rx]->data, key->size);
    }

    /* Construct and return the data element (element 0 is "value", 1 is _THE_ value)*/
    switch (dbi->dbi_rpmtag) {
    default:
	if ( scp->ldata ) {
	    scp->ldata = _free(scp->ldata);
	}

	data->size = (UINT32_T) scp->avlen[1];
        data->data = xmalloc(data->size);
	if (! (data->flags & DB_DBT_MALLOC) )
	    scp->ldata = data->data;

	(void) memcpy(data->data, scp->av[1], data->size);
    }

    scp->rx++;

    /* XXX FIXME: ptr alignment is fubar here. */
if (_debug)
fprintf(stderr, "\tcget(%s) found  key 0x%x (%d)\n", dbi->dbi_subfile,
		key->data == NULL ? 0 : *(unsigned int *)key->data, key->size);
if (_debug)
fprintf(stderr, "\tcget(%s) found data 0x%x (%d)\n", dbi->dbi_subfile,
		key->data == NULL ? 0 : *(unsigned int *)data->data, data->size);

exit:
    if (rc == DB_NOTFOUND) {
if (_debug)
fprintf(stderr, "\tcget(%s) not found\n", dbi->dbi_subfile);
    }

leaveChroot(dbi);

    return rc;
}

/**
 * Store (key,data) pair using db->put or dbcursor->c_put.
 * @param dbi           index database handle
 * @param dbcursor      database cursor (NULL will use db->put)
 * @param key           store key value/length/flags
 * @param data          store data value/length/flags
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_cput (dbiIndex dbi, /*@null@*/ DBC * dbcursor, DBT * key,
			DBT * data, unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, *dbcursor, fileSystem, internalState @*/
{
/*@i@*/    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    SCP_t scp = scpNew(dbi->dbi_db);
    int rc = 0;

dbg_keyval("sql_cput", dbi, dbcursor, key, data, flags);

enterChroot(dbi);

    switch (dbi->dbi_rpmtag) {
    default:
	scp->cmd = sqlite3_mprintf("INSERT OR REPLACE INTO '%q' VALUES(?, ?);",
		dbi->dbi_subfile);
	rc = sqlite3_prepare(sqldb->db, scp->cmd, (int)strlen(scp->cmd), &scp->pStmt, &scp->pzErrmsg);
	if (rc) rpmlog(RPMLOG_WARNING, "cput(%s) prepare %s (%d)\n",dbi->dbi_subfile,  sqlite3_errmsg(sqldb->db), rc);
	rc = sql_bind_key(dbi, scp, 1, key);
	if (rc) rpmlog(RPMLOG_WARNING, "cput(%s)  key bind %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);
	rc = sql_bind_data(dbi, scp, 2, data);
	if (rc) rpmlog(RPMLOG_WARNING, "cput(%s) data bind %s (%d)\n", dbi->dbi_subfile, sqlite3_errmsg(sqldb->db), rc);

	rc = sql_step(dbi, scp);
	if (rc) rpmlog(RPMLOG_WARNING, "cput(%s) sql_step rc %d\n", dbi->dbi_subfile, rc);

	break;
    }

    scp = scpFree(scp);

leaveChroot(dbi);

    return rc;
}

/**
 * Is database byte swapped?
 * @param dbi           index database handle
 * @return              0 no
 */
static int sql_byteswapped (dbiIndex dbi)
	/*@globals fileSystem, internalState @*/
	/*@modifies fileSystem, internalState @*/
{
    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    SCP_t scp = scpNew(dbi->dbi_db);
    int sql_rc, rc = 0;
    union _dbswap db_endian;

enterChroot(dbi);

/*@-nullstate@*/
    sql_rc = sqlite3_get_table(sqldb->db, "SELECT endian FROM 'db_info';",
	&scp->av, &scp->nr, &scp->nc, (char **)&scp->pzErrmsg);
/*@=nullstate@*/

    if (sql_rc == 0 && scp->nr > 0) {
assert(scp->av != NULL);
	db_endian.uc[0] = (unsigned char) strtol(scp->av[1], NULL, 10);

	if ( db_endian.uc[0] == ((union _dbswap *)&endian)->uc[0] )
	    rc = 0; /* Native endian */
	else
	    rc = 1; /* swapped */

    } else {
	if ( sql_rc ) {
	    rpmlog(RPMLOG_DEBUG, D_("db_info failed %s (%d)\n"),
		scp->pzErrmsg, sql_rc);
	}
	rpmlog(RPMLOG_WARNING, "Unable to determine DB endian.\n");
    }

    scp = scpFree(scp);

leaveChroot(dbi);

    return rc;
}

/**************************************************
 *
 *  All of the following are not implemented!
 *  they are not used by the rest of the system
 *
 **************************************************/

/**
 * Associate secondary database with primary.
 * @param dbi           index database handle
 * @param dbisecondary  secondary index database handle
 * @param callback      create secondary key from primary (NULL if DB_RDONLY)
 * @param flags         DB_CREATE or 0
 * @return              0 on success
 */
static int sql_associate (/*@unused@*/ dbiIndex dbi,
		/*@unused@*/ dbiIndex dbisecondary,
    /*@unused@*/int (*callback) (DB *, const DBT *, const DBT *, DBT *),
		/*@unused@*/ unsigned int flags)
	/*@*/
{
if (_debug)
fprintf(stderr, "*** sql_associate:\n");
    return EINVAL;
}

/**
 * Associate secondary database with primary.
 * @param dbi           index database handle
 * @param dbisecondary  secondary index database handle
 * @param callback      create secondary key from primary (NULL if DB_RDONLY)
 * @param flags         DB_CREATE or 0
 * @return              0 on success
 */
static int sql_associate_foreign (/*@unused@*/ dbiIndex dbi,
		/*@unused@*/ dbiIndex dbisecondary,
    /*@unused@*/int (*callback) (DB *, const DBT *, DBT *, const DBT *, int *),
		/*@unused@*/ unsigned int flags)
	/*@*/
{
if (_debug)
fprintf(stderr, "*** sql_associate_foreign:\n");
    return EINVAL;
}

/**
 * Return join cursor for list of cursors.
 * @param dbi           index database handle
 * @param curslist      NULL terminated list of database cursors
 * @retval dbcp         address of join database cursor
 * @param flags         DB_JOIN_NOSORT or 0
 * @return              0 on success
 */
static int sql_join (/*@unused@*/ dbiIndex dbi,
		/*@unused@*/ DBC ** curslist,
		/*@unused@*/ /*@out@*/ DBC ** dbcp,
		/*@unused@*/ unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, *dbcp, fileSystem @*/
{
if (_debug)
fprintf(stderr, "*** sql_join:\n");
    return EINVAL;
}

/**
 * Duplicate a database cursor.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @retval dbcp         address of new database cursor
 * @param flags         DB_POSITION for same position, 0 for uninitialized
 * @return              0 on success
 */
static int sql_cdup (/*@unused@*/ dbiIndex dbi,
		/*@unused@*/ DBC * dbcursor,
		/*@unused@*/ /*@out@*/ DBC ** dbcp,
		/*@unused@*/ unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies dbi, *dbcp, fileSystem @*/
{
if (_debug)
fprintf(stderr, "*** sql_cdup:\n");
    return EINVAL;
}

/**
 * Retrieve (key,data) pair using dbcursor->c_pget.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @param key           secondary retrieve key value/length/flags
 * @param pkey          primary retrieve key value/length/flags
 * @param data          primary retrieve data value/length/flags
 * @param flags         DB_NEXT, DB_SET, or 0
 * @return              0 on success
 */
static int sql_cpget (/*@unused@*/ dbiIndex dbi,
		/*@unused@*/ /*@null@*/ DBC * dbcursor,
		/*@unused@*/ DBT * key,
		/*@unused@*/ DBT * pkey,
		/*@unused@*/ DBT * data,
		/*@unused@*/ unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, *key, *pkey, *data, fileSystem @*/
{
if (_debug)
fprintf(stderr, "*** sql_cpget:\n");
    return EINVAL;
}

/**
 * Retrieve count of (possible) duplicate items using dbcursor->c_count.
 * @param dbi           index database handle
 * @param dbcursor      database cursor
 * @param countp        address of count
 * @param flags         (unused)
 * @return              0 on success
 */
static int sql_ccount (/*@unused@*/ dbiIndex dbi,
		/*@unused@*/ DBC * dbcursor,
		/*@unused@*/ /*@out@*/ unsigned int * countp,
		/*@unused@*/ unsigned int flags)
	/*@globals fileSystem @*/
	/*@modifies *dbcursor, fileSystem @*/
{
if (_debug)
fprintf(stderr, "*** sql_ccount:\n");
    return EINVAL;
}

/** \ingroup dbi
 * Save statistics in database handle.
 * @param dbi           index database handle
 * @param flags         retrieve statistics that don't require traversal?
 * @return              0 on success
 */
static int sql_stat (dbiIndex dbi, /*@unused@*/ unsigned int flags)
	/*@globals fileSystem, internalState @*/
	/*@modifies dbi, fileSystem, internalState @*/
{
/*@i@*/    SQL_DB * sqldb = (SQL_DB *) dbi->dbi_db;
    SCP_t scp = scpNew(dbi->dbi_db);
    int rc = 0;
    long nkeys = -1;

enterChroot(dbi);

    dbi->dbi_stats = _free(dbi->dbi_stats);

/*@-sizeoftype@*/
    dbi->dbi_stats = xcalloc(1, sizeof(DB_HASH_STAT));
/*@=sizeoftype@*/

    scp->cmd = sqlite3_mprintf("SELECT COUNT('key') FROM '%q';", dbi->dbi_subfile);
/*@-nullstate@*/
    rc = sqlite3_get_table(sqldb->db, scp->cmd,
		&scp->av, &scp->nr, &scp->nc, (char **)&scp->pzErrmsg);
/*@=nullstate@*/

    if ( rc == 0 && scp->nr > 0) {
assert(scp->av != NULL);
	nkeys = strtol(scp->av[1], NULL, 10);

	rpmlog(RPMLOG_DEBUG, D_("  stat on %s nkeys %ld\n"),
		dbi->dbi_subfile, nkeys);
    } else {
	if ( rc ) {
	    rpmlog(RPMLOG_DEBUG, D_("stat failed %s (%d)\n"),
		scp->pzErrmsg, rc);
	}
    }

    if (nkeys < 0)
	nkeys = 4096;  /* Good high value */

    ((DB_HASH_STAT *)(dbi->dbi_stats))->hash_nkeys = nkeys;

    scp = scpFree(scp);

leaveChroot(dbi);

    return rc;
}

/* Major, minor, patch version of DB.. we're not using db.. so set to 0 */
/* open, close, sync, associate, asociate_foreign, join */
/* cursor_open, cursor_close, cursor_dup, cursor_delete, cursor_get, */
/* cursor_pget?, cursor_put, cursor_count */
/* db_bytewapped, stat */
/*@observer@*/ /*@unchecked@*/
struct _dbiVec sqlitevec = {
    "Sqlite " SQLITE_VERSION,
    ((SQLITE_VERSION_NUMBER / (1000 * 1000)) % 1000),
    ((SQLITE_VERSION_NUMBER / (       1000)) % 1000),
    ((SQLITE_VERSION_NUMBER                ) % 1000),
    sql_open,
    sql_close,
    sql_sync,
    sql_associate,
    sql_associate_foreign,
    sql_join,
    sql_exists,
    sql_seqno,
    sql_copen,
    sql_cclose,
    sql_cdup,
    sql_cdel,
    sql_cget,
    sql_cpget,
    sql_cput,
    sql_ccount,
    sql_byteswapped,
    sql_stat
};

/*@=evalorderuncon@*/
/*@=modfilesystem@*/
/*@=compmempass@*/
/*@=compdef@*/
/*@=moduncon@*/
/*@=noeffectuncon@*/
/*@=globuse@*/
/*@=paramuse@*/
/*@=mustmod@*/
